# Zona

一个自定义的 Forth 方言，最终目标是成为全能通用语言。

## 编译

```
cc src/zona.c -o zona -lm -lreadline    # 解释器
cc src/zonac.c -o zonac -lm             # 编译器
```

编译器依赖 [QBE](https://c9x.me/compile/)，需将 `qbe` 放在 PATH 中。

## 使用

### 解释器

REPL 交互模式：

```
./zona
zona> 3 5 + .
8
zona> 'hello' :type 10 :emit
hello
```

文件执行：

```
./zona examples/demo.zona
```

### 编译器

编译为原生可执行文件：

```
./zonac examples/demo.zona -o demo
./demo
```

带命令行参数：

```
./zonac tests/test_args.zona -o targs
./targs foo bar
```

编译流程：zona 源码 → QBE IR → 汇编 → 原生可执行文件。

#### FFI：链接 C 库

编译产物是原生代码，可直接链接任意 C 库。用 `:bind` 声明 C 函数：

```
:bind initWindow 'InitWindow' void int int char*
:bind closeWindow 'CloseWindow' void

800 600 'hello' initWindow
closeWindow
```

编译并链接：

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

_ 定义字
@ double :dup + ;
5 double .       _ 10

_ 条件
@ neg 0 :swap - ;
@ abs :dup 0 < ? neg ;
-7 abs .         _ 7

_ 循环（~ 跳回字头部）
@ countdown :dup 0 = ? $ :dup . 1 - ~ ;
5 countdown :drop

_ 字符串
'hello zona' :type 10 :emit

_ 引入文件
:use './std/all.zona'    _ 全部标准库
:use './std/math.zona'   _ 或按需引入

_ 内存
42 :here #       _ 写
:here & .        _ 读：42

_ 动态内存
10 :alloc        _ 分配
42 :over #       _ 写
:dup & .         _ 读：42
:free            _ 释放

_ 调试
1 2 3 :stack     _ <3> 1 2 3
```

## 编辑器支持

### VS Code

语法高亮扩展在 `editors/vscode/` 目录下，安装：

```
ln -s /path/to/zona/editors/vscode ~/.vscode/extensions/zona-lang
```

重启 VS Code 即可。支持语法高亮和 `Cmd+/` 注释快捷键。

## 文档

- `docs/spec.md` — 语言规范
- `docs/roadmap.md` — 实现路线图
- `docs/context.md` — 项目上下文与实现细节
- `docs/ffi.md` — FFI 讨论记录
- `docs/perf.md` — 性能基准测试
- `examples/` — 示例程序
- `std/` — 标准库（`all.zona` 全部引入，或按需引入 `math.zona` `io.zona` `logic.zona` `stack.zona` `test.zona`）
