# Zona V2 — 编译器内部架构

> 描述 `next/src/zonac.c` 的实际实现，作为语言规范（`spec.md`）的配套文档。

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

### 条件 `?`

```
条件 ? 真词元 ! 假词元
```

编译器策略（`gen_word` 内）：
1. `vpop` 条件值（l 类型）
2. 保存虚栈快照（`save_vsp`, `save_vstack[]`, `save_vtype[]`）
3. 生成 `jnz → @true, @false`
4. 真分支：执行词元，若 `$` 则 `ret`（返回值为虚栈快照栈顶），否则 `jmp @end`
5. 假分支：恢复虚栈快照，执行词元，同上
6. `@end` 标签

**分支汇合后的虚栈**：两分支都可能修改虚栈，当前未做汇合类型检查（TODO）。

### 返回 `$`

`vpop` 返回值（如果字有输出），生成 `ret`。直接 `return 1` 停止遍历 body——后续 token 不生成代码。

### 循环 `~`

`vsync()` 刷虚栈，生成 `jmp @Lstart` 跳回字头部。

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
