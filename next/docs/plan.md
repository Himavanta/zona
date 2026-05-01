# Zona Next 实施计划

> 本文档是 Next 版本开发的唯一执行依据。`next/docs/` 下的其他文档为参考背景。语言规范以 `next/docs/spec.md` 为准。

## 语言设计锁定

Next 相对 V1 的语言变更，详见 `next/docs/spec.md`。核心变更：

1. **栈效应声明（强制）** — `@ 名称 输入:输出 body ;`，`:` 做签名分隔符
2. **数字类型分离** — `35` 整数(i)，`35.0` 浮点(d)，类型不匹配编译错误
3. **指针真实地址** — `:alloc` `:free` 返回真实指针，`:peek` `:poke` 替代 `&` `#`
4. **默认内联短词** — body ≤ 8 词元且非递归自动内联
5. **`.` 仅做成员访问** — 打印用 `:print` 原语
6. **`&` `#` 释放** — 预留给局部变量等未来特性

符号变更：

| 变更 | V1 | V2 |
|------|----|----|
| 签名分隔 | 无 | `:` |
| `.` 语义 | 打印 / 成员访问 | 仅成员访问 |
| 打印 | `.` | `:print` |
| 内存读 | `&` | `:peek` 原语 |
| 内存写 | `#` | `:poke` 原语 |
| `&` `#` | 占用 | 释放（预留局部变量） |

### 不做的事（现阶段）

- 不加局部变量语法 — 先用纯栈式写，验证语言设计
- 不加结构体 — 后续阶段
- 不加模块系统 — 后续阶段
- 不加错误处理 — 后续阶段
- 不加并发 — 后续阶段
- 不加字嵌套定义 — Forth 传统平级定义，不引入闭包复杂度

---

## 实施阶段

### 阶段 0：语言规范 ✅

**产出**：`v2/spec.md`

已完成。V2 语言规范定义了词法、类型系统、栈效应声明、控制流、原语列表。

---

### 阶段 1：最小解释器

**目标**：能运行 fib，验证栈效应声明 + 类型分离的核心价值。

**产出**：`next/src/zona.c` — Next 解释器

**实现内容**：

1. **tokenizer** — 基于 V1 的 `zona.h`，新增：
   - 浮点字面量识别（含小数点的数字）
   - 栈效应声明的解析 `输入:输出`（`:` 前后的类型字母序列）

2. **分离栈** — 三栈模型：
   ```c
   int64_t istack[1024];  _ 整数栈
   double  fstack[1024];  _ 浮点栈
   void*   pstack[256];   _ 指针栈
   int isp, fsp, psp;
   ```

3. **栈效应验证** — 编译字定义时，模拟执行 body，验证净栈变化与声明一致

4. **类型化执行** — 根据栈效应声明中的类型，操作对应的栈

5. **控制流** — `? ~ $` 与 V1 语义一致，`!` 做 else 分支标记

**必须通过的测试**：

```
_ 整数算术
3 5 + :print          _ 输出 8

_ 浮点算术
3.0 5.0 + :print      _ 输出 8.0

_ 类型错误
3 5.0 +               _ 编译错误：类型不匹配

_ 栈效应验证
@ add1 i:i 1 + ;
3 add1 :print         _ 输出 4

@ bad i:i :drop ;     _ 编译错误：声明产出1个，实际产出0个

_ 空签名
@ hello : 'Hello' :type 10 :emit ;
hello                  _ 输出 Hello

_ fib
@ fib d:d :dup 2.0 < ? $ :dup 1.0 - fib :swap 2.0 - fib + ;
35.0 fib :print       _ 输出 9227465
```

**不做**：字符串、内存操作、文件 I/O、模块、FFI、REPL

---

### 阶段 2：字节码 VM

**目标**：将 token 流解释升级为字节码执行，大幅提升性能。

**产出**：在 `next/src/zona.c` 中新增字节码编译器和 VM

**实现内容**：

1. **字节码指令集定义** — 类型化操作码：

   ```
   _ 栈操作
   OP_PUSH_I    (1 + 8 bytes)   _ 压入整数
   OP_PUSH_D    (1 + 8 bytes)   _ 压入浮点
   OP_DUP_I     (1 byte)        _ 复制栈顶整数
   OP_DUP_D     (1 byte)        _ 复制栈顶浮点
   OP_DROP_I    (1 byte)
   OP_DROP_D    (1 byte)
   OP_SWAP_I    (1 byte)
   OP_SWAP_D    (1 byte)

   _ 算术
   OP_ADD_I     (1 byte)        _ 整数加
   OP_ADD_D     (1 byte)        _ 浮点加
   OP_SUB_I     (1 byte)
   OP_SUB_D     (1 byte)
   OP_MUL_I     (1 byte)
   OP_MUL_D     (1 byte)
   OP_DIV_I     (1 byte)
   OP_DIV_D     (1 byte)
   OP_MOD_I     (1 byte)

   _ 比较（返回整数）
   OP_LT_I      (1 byte)
   OP_LT_D      (1 byte)
   OP_GT_I      (1 byte)
   OP_GT_D      (1 byte)
   OP_EQ_I      (1 byte)
   OP_EQ_D      (1 byte)

   _ 控制流
   OP_JZ        (1 + 4 bytes)   _ 条件跳转（整数 == 0 则跳）
   OP_JMP       (1 + 4 bytes)   _ 无条件跳转
   OP_RET       (1 byte)

   _ 调用
   OP_CALL       (1 + 4 bytes)  _ 调用字（索引）

   _ 打印
   OP_PRINT_I   (1 byte)
   OP_PRINT_D   (1 byte)
   ```

2. **字节码编译器** — 遍历 token 流，输出字节码序列。每个字编译为独立的字节码函数体。

3. **直接线程 VM** — 用 computed goto 分派字节码

4. **内联** — 短词（≤ 8 token，非递归）的字节码直接展开到调用点

**验证**：
- 阶段 1 的所有测试继续通过
- fib(35) 性能基准 — 预期从 V1 的 1.63s 降至 0.3-0.5s

---

### 阶段 3：核心原语

**目标**：补齐编程所需的全部基础原语。

**产出**：在 `next/src/zona.c` 中新增原语

**实现内容**：

1. **字符串** — `'hello'` 压入 `p i`（地址+长度）
2. **内存操作** — `:alloc` `:free`，基于真实 malloc/free
3. **I/O** — `:type` `:emit` `:key` `:print`
4. **文件 I/O** — `:fopen` `:fread` `:fwrite` `:fclose`
5. **系统** — `:time` `:rand` `:exit` `:argc` `:argv`
6. **指针操作** — `:peek` `:poke` 等，操作真实地址
7. **REPL** — 交互模式

**验证**：
- V1 的 `tests/core.zona` 移植后通过
- REPL 可以交互执行

---

### 阶段 4：模块系统

**目标**：支持多文件项目，解决命名冲突。

**产出**：

1. **模块语法**：
   ```
   _ math.zona
   @ abs d:d :dup 0.0 < ? 0.0 :swap - ! ;
   :export abs
   ```

2. **导入语法**：
   ```
   :use math
   math.abs
   ```

3. **独立命名空间** — 模块内定义的词默认不导出，外部只看到 `:export` 的词

**验证**：
- V1 的 `tests/import.zona` 移植后通过
- 两个模块有同名词不冲突

---

### 阶段 5：标准库

**目标**：用 Zona V2 自身重写标准库。

**产出**：`next/std/` 目录

```
next/std/
  math.zona    _ abs, neg, sq, min, max
  logic.zona   _ not, and, or
  stack.zona   _ nip, tuck
  io.zona      _ nl, space, tab
  test.zona    _ pass, fail, check
  all.zona     _ barrel import
```

**验证**：所有标准库模块有测试覆盖

---

### 阶段 6：ARM64 编译器

**目标**：字节码 → ARM64 汇编翻译器，原生性能。

**产出**：`next/src/zonac.c`

**实现内容**：

1. **字节码 → ARM64 指令映射** — 一对一翻译，每条字节码指令对应固定的 ARM64 指令序列
2. **C 调用约定** — 按 AAPCS64，`d:d` 的字编译为 `double func(double n)`
3. **函数序言/尾声** — callee-saved 寄存器保存/恢复
4. **汇编输出** — 生成 `.s` 文件，用 `cc` 链接
5. **FFI** — `:bind` 按 AAPCS64 生成 C 函数调用

**验证**：
- fib(35) 性能 — 预期接近 C（1.0-1.3x）
- 阶段 3 的所有测试编译后通过
- FFI 调用 C 标准库函数成功

---

### 阶段 7：结构体 + 独立编译

**目标**：数据封装 + 增量编译，支撑大型项目。

**产出**：

1. **结构体**：
   ```
   :struct Point
     x d
     y d
   :end
   Point.new :p       _ 构造器
   point.x p:d        _ 字段读取
   point.x! dp:       _ 字段写入
   Point.free p:      _ 析构器
   ```

2. **接口文件** — 编译时自动生成，包含模块导出词的栈效应签名
3. **独立编译** — 每个模块编译为 `.o`，链接为可执行文件

**验证**：
- 结构体创建、字段读写、释放
- 多模块项目增量编译（改一个文件只重编译该文件）

---

### 阶段 8：优化 + 工具

**目标**：打磨性能和开发体验。

**可能内容**：
- 尾调用优化
- 常量折叠
- 调试器（断点、单步、栈查看）
- 性能分析器
- 局部变量语法（使用 `&` `#` 预留符号）
- 错误处理设计

---

## 里程碑总结

| 阶段 | 产出 | 关键验证 |
|------|------|---------|
| 0 | 语言规范 | ✅ 完成 |
| 1 | 最小解释器 | fib 运行成功，类型错误被捕获 |
| 2 | 字节码 VM | fib 性能 0.3-0.5s |
| 3 | 核心原语 | V1 核心测试移植通过 |
| 4 | 模块系统 | 多文件项目、无命名冲突 |
| 5 | 标准库 | 全部模块有测试 |
| 6 | ARM64 编译器 | fib 性能接近 C |
| 7 | 结构体 + 独立编译 | 大型项目可行性验证 |
| 8 | 优化 + 工具 | 性能打磨、开发体验 |

## 文件结构规划

```
next/                   _ Next 版本独立开发目录
  docs/
    spec.md             _ 语言规范
    plan.md             _ 本文档
    perf-analysis.md    _ 参考：性能分析
    redesign.md         _ 参考：重设计讨论
    interpreter.md      _ 参考：解释器设计
  src/
    zona.c              _ Next 解释器（阶段 1-3）
    zonac.c             _ Next 编译器（阶段 6）
  std/                  _ Next 标准库（阶段 5）
  tests/                _ Next 测试
  examples/             _ Next 示例
```
