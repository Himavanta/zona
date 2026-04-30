# Zona 项目上下文

## 项目概述

Zona 是一个自定义 Forth 方言，用纯 C 实现。项目路径：`/Users/seven/Desktop/mine/zona`

## 目录结构

```
zona/
  src/
    zona.h      — 共享前端（Token 定义、tokenizer、Word 结构、read_file、校验函数）（235 行）
    zona.c      — 解释器（471 行）
    zonac.c     — QBE 编译器（1192 行）
  std/          — 标准库（math/logic/stack/io/test.zona + all.zona）
  docs/
    spec.md     — 语言规范（完整）
    roadmap.md  — 路线图
    ffi.md      — FFI 讨论记录
  examples/     — demo.zona
  tests/        — test_all.zona + run.sh 自动化测试
  editors/vscode/ — VS Code 语法高亮扩展
  README.md
```

## 编译

```
cc src/zona.c -o zona -lm -lreadline    # 解释器
cc src/zonac.c -o zonac -lm             # 编译器（依赖 QBE）
```

## 语言核心设计

- 全部单符号语法，无复合符号
- 用户字：字母开头，字母数字组成，无保留字
- 系统原语：`:xxx` 格式，天然与用户字不冲突
- 字符串：单引号包裹，运行时压入地址和长度两个值
- 数字：统一 double（IEEE 754）
- 栈：无类型栈，数字和字符串地址混在一起
- 字可覆盖重定义
## 单符号语法标记

`@ ; $ ? ! ~ & # + - * / % ^ > < = . _ '`

`.` 空格规则：有空格=打印，无空格=成员访问。见 `spec.md` 2.1。

待选符号：`( ) [ ] { } , | " `` `
注意：`[ ]` `( )` `{ }` 预留给错误处理等关键功能

## 系统原语完整列表

栈：`:dup :drop :swap :over :rot`
内存：`:here :allot :alloc :free`
IO：`:type :emit :key :fopen :fread :fwrite :fclose`
系统：`:time :rand :exit :argc :argv :use :bind`
C 内存：`:peek8 :peek32 :peek64 :peekd :poke8 :poke32 :poke64 :poked`
调试：`:stack`

## 控制流

- `?` 只读一个词元，`? X ! Y` 双分支
- `~` 跳回字头部（循环）
- `$` 显式返回
- `? $` 条件退出循环

## QBE 后端实现（已完成）

### 用法

```
./zonac prog.zona -o prog
./prog
```

### 架构

复用 `zona.h` 的前端（tokenizer + Word 解析），`zonac.c` 两遍编译：
1. 收集所有 `:use` 文件、字定义、字符串字面量、松散代码段
2. 生成 QBE IR → qbe 编译为汇编 → cc 链接为原生可执行文件

QBE 查找：直接使用 PATH 中的 `qbe`。

### 栈处理

采用运行时栈方案（非编译时虚拟栈）。全局 `$stack` 数组 + `$sp` 指针，通过 `$zona_push` / `$zona_pop` / `$zona_peek` 函数操作。原因：Zona 的控制流（`? ~ $`）和栈操作原语（`:rot`、`:over`）在编译时无法静态追踪栈深度。

### zona → QBE 映射（实际）

- 数据栈 → 全局 `data $stack`，运行时 push/pop
- `@ name ... ;` → `function $zona_name()`，带 `@Lentry → @Lstart` prelude（QBE 要求首 block 不可被跳转）
- `+ - * /` → QBE `add sub mul div`（`d` 类型）
- `%` → 手动 fmod：`a - floor(a/b)*b`
- `^` → `call $pow`
- `> < =` → QBE `cgtd cltd ceqd`，结果 `swtof` 转回 double
- `?` → `jnz`（条件跳转），`? X ! Y` 双分支
- `~` → `jmp @Lstart`（跳回函数开头）
- `$` → `ret`（后跟 dead block 避免 QBE 报错）
- `.` → `$zona_print()`（整数用 `%ld`，浮点用 `%g`）
- 字符串 → QBE `data` 定义 + `$zona_store_str()` 存入 mem
- `& #` → `$zona_mem_load()` / `$zona_mem_store()`
- `:use` → 编译时递归处理文件

### 已实现的原语

| 类别 | 原语 | 状态 |
|------|------|------|
| 栈 | `:dup :drop :swap :over :rot` | ✅ |
| 内存 | `:here :allot :alloc :free` | ✅ |
| IO | `:type :emit :key` | ✅ |
| IO | `:fopen :fread :fwrite :fclose` | ✅ |
| 系统 | `:time :rand :exit :use :argc :argv` | ✅ |
| FFI | `:bind`（声明式 C 函数绑定） | ✅ |
| 调试 | `:stack` | ✅ |

所有原语与解释器完全对齐（REPL 模式除外，编译器天然不支持）。

### FFI（`:bind`）

通过 `:bind` 声明外部 C 函数，使用 C 标准类型名称，编译后直接链接：

```
:bind myPuts 'puts' int char*
:bind getenv 'getenv' char* char*
:bind cmalloc 'malloc' void* long
'hello' myPuts :drop
```

支持类型：`void` `char` `short` `int` `long` `float` `double` `char*` `void*` 及其他指针类型。
`char*` 参数自动从 zona 字符串转换，`char*` 返回值自动转为 zona 字符串。
解释器忽略 `:bind`。详见 `docs/ffi.md`。

## 设计决策记录

这些是实现者在修改相关代码时需要知道的**理由**，不是语法规范（语法见 `spec.md`）。

- **模块名和字名分开存储**（模块表 vs `dict[]`）：`@ math` 定义同名字不覆盖模块名，因为 `math` 和 `math.abs` 通过 `.` 有无后缀自然区分。
- **`.` 不做 `,` 替代**：打印保持 Forth 传统的 `.`，与成员访问通过空格规则共存——词法层判断，不需要运行时上下文。
- **不需要 `:reexport` 原语**：无别名 `:use` 在模块内部摊平即实现 barrel export。
- **`"` 作为模块分隔符被放弃**：`'` 和 `"` 同屏视觉混淆，且无法与 struct 字段访问统一。
- **构造器（方案 B）优先于自动打包（方案 A）**：不涉及 QBE 类型定义，栈分配自动释放，实现更简单。方案 A 后续按需添加。
