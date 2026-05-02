# Zona

一个自定义的 Forth 方言，最终目标是成为全能通用语言。

**Forth 的栈式表达力，C 级别的性能，工程化的类型系统。**

编译器依赖 [QBE](https://c9x.me/compile/)，需将 `qbe` 放在 PATH 中。

## 性能

fib(35)，Apple M 系列：

| | 时间 | vs C |
|------|------|------|
| Zona | **0.04s** | **~1.0x** |
| C (-O2) | 0.043s | 1x |

## 编译

```
cc zonac.c -o zonac -lm
```

## 使用

编译为原生可执行文件：

```
./zonac <file.zona> -o <output>
./<output>
```

编译流程：zona 源码 → tokenizer → 两遍编译 → QBE IR (.ssa) → `qbe` → 汇编 (.s) → `cc` → 可执行文件。

### 带命令行参数

```
./zonac tests/test_args.zona -o targs
./targs foo bar
```

### FFI：链接 C 库

编译产物是原生代码，可直接链接任意 C 库。用 `:bind` 声明 C 函数：

```
:bind initWindow 'InitWindow' void int int char*
:bind closeWindow 'CloseWindow' void

800 600 'hello' initWindow
closeWindow
```

编译并链接（`:bind` 待实现）：

```
./zonac game.zona -o game
cc game.s -o game -lraylib -lm
```

## 快速入门

```
_ 这是注释

_ 算术
3 5 + .          _ 8
2 10 ^ .         _ 1024

_ 栈操作
1 2 :swap . .    _ 1 2
3 :dup + .       _ 6

_ 定义字（必须声明栈效应）
@ double l:l :dup + ;
5 double .       _ 10

_ 条件
@ neg l:l 0 :swap - ;
@ abs l:l :dup 0 < ? neg ;
-7 abs .         _ 7

_ 循环（~ 跳回字头部，递归实现）
@ sum l:l :dup 0 = ? $ :dup 1 - sum + ;
5 sum .          _ 15

_ 字符串
'hello zona' :type 10 :emit

_ 引入模块
:use math './std/math.zona'
3 5 math.min .   _ 3

_ 内存读写
100 :alloc       _ 分配
42 :over #       _ 写
:dup & .         _ 读：42
:free            _ 释放

_ 类型化算术（整数 vs 浮点由小数点区分）
3 5 + .          _ 8 (整数加法)
3.0 5.0 + .      _ 8 (浮点加法)
```

## 标准库

位于 `std/` 目录下：

| 模块 | 文件 | 内容 |
|------|------|------|
| `math` | `std/math.zona` | `neg`, `abs`, `sq`, `min`, `max` |
| `logic` | `std/logic.zona` | `not`, `and`, `or` |
| `io` | `std/io.zona` | `nl`, `space`, `tab` |
| `stack` | `std/stack.zona` | `nip`, `tuck` |
| `test` | `std/test.zona` | `check` 测试框架 |

```
:use math './std/math.zona'
:use io './std/io.zona'
```

或全部引入：

```
:use math './std/math.zona'
:use logic './std/logic.zona'
:use io './std/io.zona'
:use stack './std/stack.zona'
:use test './std/test.zona'
```

## 核心特性

### 栈效应声明（强制类型系统）

每个字必须声明栈效应签名，格式为 `输入类型:输出类型`：

```
@ add ll:l + ;          _ 消费 2 个 l，产生 1 个 l
@ fib d:d :dup 2.0 < ? $ :dup 1.0 - fib :swap 2.0 - fib + ;
@ greet : 'hello' :type ;  _ 0 入 0 出
```

类型标记：`l` = 64 位整数，`d` = 64 位浮点，`p` = 指针。

栈效应让编译器能通过寄存器传递参数，达到 C 级别的性能。

### 类型化操作

数字类型由小数点决定：

- `35` = 整数（l 类型）
- `35.0` = 浮点（d 类型）

算术和比较根据操作数类型选择对应 QBE 指令。类型不匹配编译报错。

### 控制流

| 符号 | 语义 |
|------|------|
| `?` | 条件执行（消费栈顶 l，非零为真）|
| `!` | else 分支标记 |
| `$` | 显式返回 |
| `~` | 跳回当前字头部（循环）|

### 字符串

`'hello'` 压入 `p l` 两个值——C 字符串指针（null 结尾，FFI 兼容）+ 编译时长度。

### 内存读写

| 符号 | 签名 | 语义 |
|------|------|------|
| `&` | `p:l` 或 `p:d` | 读 8 字节 |
| `#` | `lp:` 或 `dp:` | 写 8 字节 |

### 系统原语

栈操作：`:dup` `:drop` `:swap` `:over` `:rot`
I/O：`.` `:print` `:type` `:emit` `:key`
内存：`:alloc` `:free`
系统：`:time` `:rand` `:exit` `:argc` `:argv`

## 项目状态

### 已完成 ✅

- Tokenizer（整数/浮点分离、`:` 双重语义、成员访问）
- 栈效应签名解析
- 类型化算术/比较
- 控制流 `? ! $ ~`
- 寄存器传参
- 所有基础原语
- `:use` 模块系统（命名导入 + 成员访问）
- 性能达 C 水平（fib 0.04s）

### 待实现 ⬜

- FFI（`:bind` → QBE extern 调用）
- 标准库完善

### 远期 🔮

- 结构体（`:struct` + 字段访问）
- 内联展开

## 文档

- `docs/spec.md` — 语言规范
- `docs/plan.md` — 实施计划与状态
- `docs/compiler.md` — 编译器内部架构
- `docs/decisions.md` — 设计决策记录
- `docs/rejected.md` — 废案记录
- `docs/perf-analysis.md` — 性能优化深度分析
- `docs/redesign.md` — 早期设计探索（历史文档）

## 编辑器支持

### VS Code

语法高亮扩展在 `editors/vscode/` 目录下（从项目根目录安装）：

```
ln -s /path/to/zona/editors/vscode ~/.vscode/extensions/zona-lang
```

重启 VS Code 即可。支持语法高亮和 `Cmd+/` 注释快捷键。
