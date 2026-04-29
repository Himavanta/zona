# FFI

## 用法

```
:bind initWindow 'InitWindow' v iis
:bind closeWindow 'CloseWindow' v

800 600 'hello' initWindow
closeWindow
```

编译并链接 C 库：

```
./zonac game.zona -o game
cc game.s -o game -lraylib -lm
```

语法和校验规则见 `spec.md` 第九章。

## 实现

编译器遇到 `:bind` 声明时记录到 extern 表。遇到调用时：

1. 按参数类型从右到左 pop（`s` 类型 pop 两个值：len 和 addr，转成 C 字符串）
2. 按类型生成 QBE 转换指令（`dtosi`/`extsw`/`truncd` 等）
3. 生成 `call $cName(类型 参数, ...)`
4. 返回类型非 `v` 时，转为 double 压栈

解释器忽略 `:bind`（跳过同行所有词元）。

## 待定问题

### `p` 和 `l` 的关系

两者在 QBE 层面都是 `l`（64 位）。`p` 语义是"指针"，`l` 是"大整数"。当前实现无区别，可能影响未来类型检查。

### 结构体

zona 栈上全是 double，无法直接传递 C 结构体。应对方式：

- raylib `Color`（4 字节）可打包成一个 int
- `Vector2`、`Rectangle` 等小结构体用多个参数模拟
- 大结构体用 `:alloc` 分配内存、逐字段写入、传指针

### 回调函数

zona 无法生成函数指针传给 C。raylib 是轮询模式，不依赖回调，影响有限。

## 设计讨论记录

### 曾考虑的方案

- **通用 FFI（dlopen/dlsym + libffi）** — 能调用任意 C 库，但需要外部依赖，实现复杂
- **简化 FFI（只支持数字参数）** — 覆盖面太窄
- **直接链接 raylib 加图形原语** — 简单但不通用

最终决定：QBE 编译后端 + `:bind` 声明式 FFI。零额外依赖。

### 现有原语与 FFI 的关系

`:time` `:rand` `:fopen` 等是 C 标准库的薄封装，理论上有 FFI 后可替代。但保留为内置原语更好：性能更高、使用更简单、无外部依赖。FFI 是补充而非替代。
