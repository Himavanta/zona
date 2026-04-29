# Zona

一个自定义的 Forth 方言。

## 编译

```
cc zona.c -o zona -lm
```

## 使用

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
:use './std.zona'

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
- `examples/` — 示例程序
- `std.zona` — 标准库
