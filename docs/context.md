# Zona 项目上下文

## 项目概述

Zona 是一个自定义 Forth 方言，用纯 C 实现。项目路径：`/Users/seven/Desktop/mine/zona`

## 目录结构

```
zona/
  src/
    zona.h      — 共享前端（Token 定义、tokenizer、Word 结构、read_file）
    zona.c      — 解释器（456 行）
    zonac.c     — QBE 编译器（待实现）
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
cc src/zonac.c -o zonac -lm             # 编译器（待实现）
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

待选符号：`( ) [ ] { } , | " `` `
注意：`[ ]` `( )` 预留给匿名代码块或错误处理

## 系统原语完整列表

栈：`:dup :drop :swap :over :rot`
内存：`:here :allot :alloc :free`
IO：`:type :emit :key :fopen :fread :fwrite :fclose`
系统：`:time :rand :exit :argc :argv :use`
调试：`:stack`

## 控制流

- `?` 只读一个词元，`? X ! Y` 双分支
- `~` 跳回字头部（循环）
- `$` 显式返回
- `? $` 条件退出循环

## QBE 后端实现计划

### 目标

`zonac prog.zona -o prog` 编译为原生可执行文件

### 架构

复用 `zona.h` 的前端（tokenizer + Word 解析），新写 `zonac.c` 生成 QBE IR 文本，然后调用 qbe + cc 链接。

### zona → QBE 映射

- zona 数据栈 → QBE `%` 临时变量（编译时追踪栈深度）
- `@ name ... ;` → `function $name`
- `+ - * /` → QBE `add sub mul div`（类型用 `d` double）
- `> < =` → QBE `cgtd cltd ceqd`
- `?` → QBE `jnz`（条件跳转）
- `~` → QBE `jmp @start`（跳回函数开头）
- `$` → QBE `ret`
- 调用用户字 → QBE `call $name`
- `.` → `call $printf`
- `:type` → 循环 `call $putchar`
- `:emit` → `call $putchar`
- 字符串字面量 → QBE `data` 定义

### QBE 关键点

- 类型：`d`(double) `l`(long/pointer) `w`(word/int)
- 不需要 SSA/phi，QBE 自动修复
- `%` 临时变量，`$` 全局符号，`@` 块标签
- `jnz val, @true, @false` 条件跳转
- `jmp @label` 无条件跳转
- `call $func(type arg, ...)` 函数调用
- QBE 文档：https://c9x.me/compile/doc/il.html

### 实现步骤

1. 最简：`42 .` → 压数字 + 调 printf
2. 算术：`3 5 + .`
3. 字定义和调用
4. 控制流（`? ~ $`）
5. 内存和字符串

### 栈处理（核心难点）

编译时维护一个虚拟栈，记录每个位置对应的 QBE 临时变量名。push 生成新临时变量，pop 返回栈顶变量名。例如：

```
3 5 +
```
编译为：
```
%t0 =d copy d_3.0
%t1 =d copy d_5.0
%t2 =d add %t0, %t1
```

### FFI

QBE 后端完成后 FFI 免费获得——编译产物是原生代码，直接 `cc output.s -lraylib` 链接 C 库。
