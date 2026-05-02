# Zona — 编译器内部架构

> 描述 `zonac.c` 的实际实现，作为语言规范（`spec.md`）的配套文档。

## 一、总体架构

两遍编译：

1. **第一遍（收集）**：`first_pass_tokens()` 递归遍历源码。收集所有 `@ ... ;` 字定义到全局字典 `dict[]`，收集所有 `'...'` 字符串字面量到 `strs[]`，处理 `:use` 加载模块文件。
2. **第二遍（生成）**：遍历 `dict[]`，为每个字调用 `gen_word()` 生成 QBE IR。最后生成 `$main` 函数。

编译管线：zona 源码 → tokenizer → 两遍编译 → QBE IR (.ssa) → `qbe` → 汇编 (.s) → `cc` → 可执行文件。

## 二、虚栈（Virtual Stack）

编译器用 `vstack[]` 和 `vtype[]` 追踪 QBE 临时变量及其类型。绝大多数操作在虚栈上完成，不碰运行时 `$stack`。

```c
#define VSTACK_MAX 256
static int  vstack[VSTACK_MAX];  // QBE temp IDs
static Type vtype[VSTACK_MAX];   // 每个 temp 的类型
static int  vsp = 0;
```

- `vpush(t, ty)` — 将 temp `t`（类型 `ty`）推入虚栈
- `vpop(&ty)` — 从虚栈弹出。虚栈为空时从运行时栈 `$stack` 回退（`emit_pop_typed(TY_D)`）
- `vpeek(&ty)` — 查看栈顶，不回退逻辑同上

**为什么虚栈空时要回退运行时栈？** 控制流边界（`~` 循环、函数调用）会 `vsync()` 将虚栈刷到运行时栈。之后代码生成的第一个操作若需要虚栈数据，`vpop` 发现虚栈为空，就调用 `emit_pop_typed` 从运行时栈读取。

## 三、寄存器传参

栈效应签名直接生成带类型参数的 QBE 函数：

```
@ fib d:d :dup 2.0 < ? $ :dup 1.0 - fib :swap 2.0 - fib + ;
```

生成：

```
function d $zona_fib(d %p0) {
@Lentry
    %t0 =d copy %p0
@Lstart
    ...
```

`gen_word()` 的工作流程：
1. 根据 `sig.n_out` 确定返回类型，`sig.n_in` 确定参数类型
2. 写出函数头 + `@Lentry` 块（QBE 要求首 block 不被跳转）
3. 将参数从 `%p0..%pN` copy 到虚栈 temps
4. 跳转到 `@Lstart` 开始执行 body

**调用侧**（`T_WORD` / `T_MEMBER`）：
1. 从虚栈 `vpop` 取出参数
2. 生成 `call $zona_xxx(type arg, ...)`
3. 将返回值 `vpush` 回虚栈

## 四、vsync() — 虚栈刷到运行时栈

```c
static void vsync(void) {
    for (int i = 0; i < vsp; i++) {
        // 从 $sp 读当前栈指针
        // 算出 $stack[sp] 的地址
        // 如果值是 TY_L → sltof 转为 d 再 stored
        // 如果值是 TY_D/P → 直接 stored
        // $sp += 1
    }
    vsp = 0;
}
```

**触发时机**：
- `~` 循环跳转前——循环体内可能改变了栈，需要将当前虚栈状态写入运行时栈，让下次迭代正确开始
- 函数调用前（在 `gen_token` 的 `T_WORD` 分支内）——被调用函数从运行时栈读取输入参数
- `.` 和 `:print` 打印前——需要从运行时栈 pop 值

运行时栈统一存储为 `d`（double），`TY_L` 整数在 `vsync` 时通过 `sltof` 转换。pop 时 `emit_pop_typed` 通过 `dtosi` 转回整数。

## 五、emit_pop_typed — 从运行时栈弹出

```c
int emit_pop_typed(Type ty) {
    // 1. $sp -= 1
    // 2. 计算 $stack[$sp] 的地址
    // 3. loadd 读取（运行时栈统一为 d）
    // 4. 如果 ty == TY_L → dtosi 转整数
    // 5. 返回 QBE temp
}
```

## 六、控制流

控制流是虚栈追踪最复杂的部分。编译器通过虚栈快照保存/恢复、`vsync()` 刷栈、分支汇合重载等机制，在 QBE IR 层面实现类型化的控制流。

### 6.1 条件 `?`

```
条件 ? 真词元 ! 假词元
```

编译器策略（`gen_word` 内 `?` 分支）：

1. **弹出条件值**：`vpop(&ty)` — 条件值必须是 `l` 类型（比较结果或逻辑值），生成 `jnz %cond, @true, @false`
2. **保存虚栈快照**：将当前虚栈状态（`vsp`, `vstack[]`, `vtype[]`）完整复制到 `save_vstack`/`save_vtype`
3. **判断是否有 `!` else 分支**：检查 `?` 后的下一个 token 是否为 `!`
4. **`out_vsp` / `out_vtype[]` 分支输出追踪**：记录分支执行后的虚栈状态，用于汇合点恢复
5. **真分支**（`@l_true`）：
   - 恢复虚栈快照（与 false 分支从相同起点开始）
   - 若第一个 token 是 `$` → 直接生成 `ret`（返回值取虚栈顶值），**豁免汇合约束**
   - 否则 → 执行一个 token 的 `gen_token()`，记录 `out_vsp`/`out_vtype`，`vsync()` 刷栈，`jmp @l_end`
6. **假分支**（`@l_false`）：
   - 恢复虚栈快照
   - 若有 `!` 分支 → 与真分支相同逻辑处理 `!` 后的 token，也可能是 `$` 豁免
   - 若无 `!` 分支 → 直接取 `save_vsp` 作为输出（分支为空，栈状态不变）
7. **汇合点**（`@l_end`）：
   - `vsp = 0` 清空虚栈
   - 从运行时栈弹出 `out_vsp` 个值（`emit_pop_typed`），按**逆序**推入虚栈
   - 将虚栈**反转**以恢复正确的原始顺序（因为 pop 顺序与 push 相反）

**关键实现细节 — 分支输出的恢复机制**：

```
_ 示例：@ demo ll:l ? + ! - ;
_ 保存时虚栈有 2 个 l（两个参数）
_ 真分支执行 + → 虚栈剩 1 个 l → vsync() 写入运行时栈
_ 假分支执行 - → 虚栈剩 1 个 l → vsync() 写入运行时栈
_ 汇合点：从运行时栈 pop 1 个值（类型为 l）
_ 类型一致则数据正确，类型不一致则运行时数据错乱
```

**分支汇合类型检查（TODO）**：当前编译器记录 `out_vtype[]` 并在汇合点按此类型生成 `emit_pop_typed` 调用，但**不验证两分支的 `out_vtype` 是否一致**。如果两分支产生的栈类型不同，编译器仍会生成代码，但运行时数据会错乱。这是已知的技术债务，需要在编译器中增加分支汇合类型一致性验证。

### 6.2 返回 `$`

`vpop` 弹出返回值（如果字有输出），生成对应类型的 `ret` 指令：

- `n_out == 1` 且值类型为 `l` → `ret %t_x`
- `n_out == 1` 且值类型为 `d`/`p` → `ret %t_x`
- `n_out == 0` → `ret`
- `n_out > 1` → 先 `vsync()` 将所有值刷到运行时栈，再 `ret`

执行 `$` 后，`gen_word()` 直接 `return 1`——**后续 token 不生成代码**。这保证了 `$` 之后的 dead code 不会被编译。

`$` 在 `?` 分支内部时，该分支豁免汇合约束——不参与 `out_vsp` 追踪，直接生成 `ret` 退出函数。

### 6.3 循环 `~`

`~` 是 Zona 唯一的循环原语。编译器策略：

1. 调用 `vsync()` — 将当前虚栈所有值刷到运行时栈
2. 生成 `jmp @Lstart`
3. `return 1` 结束函数体编译（`~` 后不能有代码）

**关键约束**：`~` 处的栈状态**必须**与 `@Lstart` 处的栈状态一致。`@Lstart` 紧跟在参数复制代码之后：

```
function l $zona_countdown(l %p0) {
@Lentry
    %t0 =l copy %p0                _ 参数入虚栈
@Lstart                              _ ~ 跳回此标签
    ... 循环体 ...
    vsync()                         _ 刷虚栈到运行时栈
    jmp @Lstart                     _ 跳回
}
```

如果 `~` 处的虚栈深度/类型与 `@Lstart` 处不一致，循环体后续操作会从运行时栈读到类型错误的数据。编译器当前**不验证** `~` 处的栈类型是否与 `@Lstart` 一致——这也是已知 TODO。

### 6.4 多输出字与控制流

当栈效应声明有多个输出（如 `@ swap ll:ll`）时，控制流处理有特殊之处：

- **`$` 返回**：多输出字的 `$` 先调用 `vsync()` 将所有虚栈值刷到运行时栈，然后生成无返回值的 `ret`。调用方通过运行时栈读取多个返回值。
- **`?` 分支**：多输出字的分支汇合机制与单输出相同——两分支的输出类型和数量必须一致。汇合点从运行时栈恢复所有输出值。
- **`~` 循环**：多输出字的循环点一致性要求更高——`~` 处所有值的类型必须与函数入口严格匹配。

## 七、运行时支持函数

编译器生成三个辅助函数到 QBE IR 中：

| 函数 | 用途 |
|------|------|
| `$zona_pop_l()` | 从运行时栈 pop 一个值，`dtosi` 转为 l 返回 |
| `$zona_pop_d()` | 从运行时栈 pop 一个值，直接 `loadd` 返回 |

`$zona_print()` 已废弃——打印改由 `printf` 直接从虚栈输出。

## 八、数据段

```c
static void emit_data_section(void) {
    // $fmt_int = "%ld\n"    整数打印格式
    // $fmt_flt = "%g\n"     浮点打印格式
    // $fmt_str = "%.*s"     字符串打印格式
    // $str0 = "hello"       字符串字面量（每个去重后唯一）
    // $stack = z 2048       运行时数据栈（256 个 d）
    // $sp = w 0             栈指针
}
```

## 九、浮点常量生成

QBE 要求浮点字面量带 `d_` 前缀且必须有小数点。编译器用 `%.17g` 格式：

```c
fprintf(out, "    %%t%d =d copy d_%.17g\n", v, t->num);
```

已验证 `d_5.0`、`d_2.0`、`d_35.0` 等格式均正确。

## 十、模块系统实现

`Module` 结构体存储模块名和文件路径（用于去重）。`Word.module` 指针指向所属模块（NULL = 全局）。

`:use name 'path'` 解析流程：
1. 读取 `:use` 行，提取别名和路径
2. 在 `modules[]` 中查找/创建 `Module`
3. 调用 `load_file_into_module(path, module)`
4. 内部递归调用 `first_pass_tokens()`，字定义标记 `w->module = owner`

`T_MEMBER`（如 `math.add` 或 `b.c.d.abs`）：逐层解析 `.`，除最后一个段外均为模块名，在 `modules[]` 中查找。最后一个段为词名，在最终模块中通过 `find_word_in_module()` 查找。然后生成带类型参数的 `call`。

路径解析：`current_dir` 全局变量追踪当前源文件所在目录。`:use` 中的相对路径基于此解析。文件去重通过绝对路径比较。
