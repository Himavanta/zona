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

## 已实现

### `:bind` 声明式绑定

编译器遇到 `:bind` 声明时记录到 extern 表。遇到调用时：

1. 按参数类型从右到左 pop（`s` 类型 pop 两个值：len 和 addr，转成 C 字符串）
2. 按类型生成 QBE 转换指令（`dtosi`/`extsw`/`truncd` 等）
3. 生成 `call $cName(类型 参数, ...)`
4. 返回类型非 `v` 时，转为 double 压栈

解释器忽略 `:bind`（跳过同行所有词元）。

### 已支持的类型

| 字符 | C 类型 | 方向 | 状态 |
|------|--------|------|------|
| `i` | int | 参数+返回 | ✅ |
| `l` | long/size_t | 参数+返回 | ✅ |
| `d` | double | 参数+返回 | ✅ |
| `f` | float | 参数+返回 | ✅ |
| `s` | const char* | 参数+返回 | ✅ 参数：zona→C，返回：C→zona，自动双向转换 |
| `p` | 指针 | 参数+返回 | ✅ 数值传递，语义同 `l` |
| `v` | void | 返回 | ✅ |

### C 内存读写

`:peek8/32/64/d` 和 `:poke8/32/64/d` 操作真实 C 内存地址，用于读写 C 指针指向的内存。解锁了指针操作和手动构造结构体的能力。

## 实现路径

```
第一步（已完成）：:bind 基本类型
第二步（已完成）：C 内存读写原语（:peek8/32/64/d :poke8/32/64/d）
第三步（已完成）：C 字符串返回（s 返回类型自动双向转换）
第四步（待定）：结构体
第五步（远期）：回调
```

当前 FFI 能力与 Gforth（最主流的 Forth 实现）的 FFI 处于同一水平。

## 待定：结构体

### 现状

有了 peek/poke，结构体**已经能用**，只是手动操作繁琐：

```
:bind cmalloc 'malloc' l l
:bind cfree 'free' v l

_ 手动构造 Color { r=255, g=0, b=0, a=255 }
4 cmalloc
255 :over :poke8         _ r at offset 0
0 :over 1 + :poke8       _ g at offset 1
0 :over 2 + :poke8       _ b at offset 2
255 :over 3 + :poke8     _ a at offset 3
_ 栈上现在是指向 Color 数据的指针
```

这和传统 Forth（Gforth、SwiftForth）处理结构体的方式一致——手动算偏移量 + 内存读写。

### 两层问题

**能力层（已有）**：malloc → poke 写字段 → 传指针给 C → peek 读返回的结构体字段。能做任何事。

**便利层（缺）**：用户需要自己算偏移量、知道字段大小。比如 `Rectangle { float x, y, w, h }` 用户得知道 x 在偏移 0、y 在偏移 4、每个 float 占 4 字节。

### 按值传递 vs 按指针传递

- **按指针传**（简单）：zona 分配内存 → poke 写字段 → 传指针。现在就能做。
- **按值传**（难）：C 调用约定要求把结构体字节放到寄存器或栈上。QBE 能处理，但需要 `type :Name = { ... }` 类型定义，且 `:bind` 签名中需要引用结构体类型。

按值传的实际流程也得先构造到内存里，QBE 帮做最后的拆包。

### 曾考虑的 `:struct` 语法

```
:struct Color 'Color' bbbb          _ 4 个 byte
:struct Vec2 'Vector2' ff           _ 2 个 float
:struct Rect 'Rectangle' ffff       _ 4 个 float
```

编译器据此生成 QBE `type` 定义。但**未解决的问题**：

1. **`:bind` 签名怎么引用结构体？** 签名是一串类型字符，塞结构体信息会变丑：
   - `v iif.Color` — 用点号引用？
   - `v iif{bbbb}` — 内联布局？
   - `v iif1` — 用索引？
   都不够好。

2. **zona 栈上怎么表示结构体？** 是 4 个独立值（r g b a 分别压栈），还是一个指针？如果是多个值，参数个数和签名字符数就对不上了。

3. **字段类型字符需要扩展**：`b`（byte）不在现有类型表里，需要新增。

### 替代方案：zona 标准库封装

不加新语法，用 zona 字封装偏移量计算：

```
_ 在 zona 标准库中定义
@ mkcolor        _ ( r g b a -- ptr )
    4 cmalloc
    :over 3 + :poke8
    :over 2 + :poke8
    :over 1 + :poke8
    :over :poke8
;

_ 使用
255 0 0 255 mkcolor    _ 栈上得到 Color 指针
```

优点：零语法扩展。缺点：不支持按值传递，只能传指针。

### 结论

`:struct` 的语法设计还需要想清楚，特别是签名引用和栈表示问题。当前用 peek/poke 手动方式可以覆盖所有场景，建议先用起来，遇到真正的痛点再设计语法糖。

### 其他语言的参考

- **Gforth**：结构体就是手动偏移量 + 内存读写，和 zona 现在一样
- **SwiftForth**：类似 Gforth，手动布局
- **Factor**（Forth 的现代后继者）：有完整的结构体声明和按值传递，但 Factor 有完整的类型系统和垃圾回收器，和传统 Forth 差距很大

## 待定：回调函数

### 现状

zona 无法生成函数指针传给 C。

### 问题

zona 的字（`@ cmp ... ;`）编译后是 QBE 函数，理论上有地址，但：
1. 没有机制把函数地址取出来传给 C
2. zona 函数签名（无参数、通过栈通信）和 C 期望的签名（如 `int(const void*, const void*)`）完全不匹配

要实现需要编译器自动生成 C 签名的"包装函数"，内部把 C 参数转成 zona 栈操作，调用 zona 字，再把结果转回 C 返回值。非常复杂。

### 实际影响

有限。需要回调的场景和不需要回调的替代方案：

| 需要回调的 | 替代方案 |
|-----------|---------|
| qsort 比较函数 | 自己写排序 |
| 信号处理 signal | sigwait 轮询 |
| GUI 事件回调（GTK、Qt）| 用轮询模式的库（raylib、SDL） |
| 异步 IO（libuv）| 同步 IO |

大部分游戏/图形/系统库的核心路径都不依赖回调。raylib 是轮询模式。

真遇到必须回调的场景，可以写一个小 C 文件做桥接，和 zona 编译产物一起链接——这本身就是原生编译 FFI 的优势。

### 结论

暂不设计。优先级最低。

## 设计讨论记录

### 曾考虑的 FFI 方案

- **通用 FFI（dlopen/dlsym + libffi）** — 能调用任意 C 库，但需要外部依赖，实现复杂
- **简化 FFI（只支持数字参数）** — 覆盖面太窄
- **直接链接 raylib 加图形原语** — 简单但不通用

最终决定：QBE 编译后端 + `:bind` 声明式 FFI。零额外依赖。

### 现有原语与 FFI 的关系

`:time` `:rand` `:fopen` 等是 C 标准库的薄封装，理论上有 FFI 后可替代。但保留为内置原语更好：性能更高、使用更简单、无外部依赖。FFI 是补充而非替代。

### `p` 和 `l` 的关系

两者在 QBE 层面都是 `l`（64 位）。`p` 语义是"指针"，`l` 是"大整数"。当前实现无区别。`p` 返回的值可以传给 `:peek`/`:poke`。
