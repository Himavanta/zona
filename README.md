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
zona> 'hello' :type
hello
```

文件执行：

```
./zona test.zona
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
@ abs :dup 0 < ? neg ;
@ neg 0 :swap - ;
-7 abs .         _ 7

_ 循环（~ 跳回字头部）
@ countdown :dup 0 = ? $ :dup . 1 - ~ ;
5 countdown :drop

_ 字符串（压入地址和长度，:type 打印）
'hello zona' :type

_ 内存
42 :here #       _ 写
:here & .        _ 读：42
```

## 文档

- `spec.md` — 语言规范
- `roadmap.md` — 实现路线图
