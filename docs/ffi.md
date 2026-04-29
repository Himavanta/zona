# FFI

## 用法

```
:bind initWindow 'InitWindow' void int int char*
:bind closeWindow 'CloseWindow' void

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

1. 按参数类型从右到左 pop（`char*` 类型 pop 两个值：len 和 addr，转成 C 字符串）
2. 按类型生成 QBE 转换指令（`dtosi`/`extsw`/`truncd` 等）
3. 生成 `call $cName(类型 参数, ...)`
4. 返回类型非 `void` 时，转为 double 压栈

解释器忽略 `:bind`（跳过同行所有词元）。

### 已支持的类型

| zona 类型名 | C 类型 | 方向 | 状态 |
|------------|--------|------|------|
| `int` | int | 参数+返回 | ✅ |
| `long` | long/size_t | 参数+返回 | ✅ |
| `double` | double | 参数+返回 | ✅ |
| `float` | float | 参数+返回 | ✅ |
| `char*` | const char* | 参数+返回 | ✅ 自动双向转换 |
| `void*` | void* | 参数+返回 | ✅ 数值传递 |
| `void` | void | 返回 | ✅ |

### C 内存读写

`:peek8/32/64/d` 和 `:poke8/32/64/d` 操作真实 C 内存地址，用于读写 C 指针指向的内存。解锁了指针操作和手动构造结构体的能力。

### zona 中的指针

zona 栈上一切都是 double。指针就是一个数字（内存地址），没有任何标记区分"这是指针"还是"这是值"。区分发生在 `:bind` 的签名里——签名决定 QBE 怎么传递这个数字。zona 不需要"指针类型"，因为指针和整数在二进制层面就是同一个东西。

## 实现路径

```
第一步（已完成）：:bind 基本类型
第二步（已完成）：C 内存读写原语（:peek8/32/64/d :poke8/32/64/d）
第三步（已完成）：C 字符串返回（char* 返回类型自动双向转换）
第四步（设计中）：结构体（:struct + 构造器）
第五步（远期）：回调
```

当前 FFI 能力与 Gforth（最主流的 Forth 实现）的 FFI 处于同一水平。

## 设计中：结构体

### 本质

C 的结构体就是一块连续内存，按顺序放了几个值。比如 `Color { r, g, b, a }` 就是 4 个字节紧挨着。C 里写 `color.r` 就是"读这块内存的第 0 个字节"。在 ABI 层面，结构体不关心名字，只关心字段类型和顺序——布局一致的结构体可以互相替代。

### 现状

有了 peek/poke，结构体**已经能用**，只是手动操作繁琐：

```
:bind cmalloc 'malloc' void* long
4 cmalloc                  _ 分配 4 字节
255 :over :poke8           _ r at offset 0
0 :over 1 + :poke8         _ g at offset 1
0 :over 2 + :poke8         _ b at offset 2
255 :over 3 + :poke8       _ a at offset 3
```

这和传统 Forth（Gforth、SwiftForth）处理结构体的方式一致——手动算偏移量 + 内存读写。

### 两层问题

**能力层（已有）**：malloc → poke 写字段 → 传指针给 C → peek 读返回的结构体字段。能做任何事。

**便利层（缺）**：用户需要自己算偏移量、知道字段大小。

### `:struct` 语法

```
:struct Color char char char char
:struct Rect float float float float
```

- 只需要一个名字，不需要像 `:bind` 那样两个名字
- 因为结构体在 ABI 层面只是内存布局，C 不关心 zona 这边叫什么
- 字段类型使用 C 类型名，和 `:bind` 保持一致
- 行格式规则同 `:bind`：行首、独占一行、不在 `@ ;` 内部

### zona 结构体 vs C 结构体

`:struct` 同时解决两个需求：

- **绑定 C 的 struct**：告诉编译器 C 结构体的内存布局，用于 FFI
- **声明 zona 的结构体**：zona 程序内部的数据组织

两者本质相同——都是"一块内存 + 按偏移量读写字段"。统一用一个 `:struct`，不区分。zona 的栈上全是 double，结构体本质上就是"一块内存 + 按偏移量读写字段"，和 C 结构体在 zona 里的操作方式完全一样。

### 两种传递方式（都支持，不冲突）

**方案 A：`:bind` 签名里写结构体名，自动打包**

```
:struct Color char char char char
:bind drawCircle 'DrawCircle' void int int float Color

255 0 0 255 400 300 50.0 drawCircle
_ 编译器看到签名里的 Color，自动从栈上 pop 4 个值打包
```

- 栈上的值直接消耗，传给 C，结束
- 没有中间产物，不需要释放
- 简洁，适合一次性使用
- 实现较复杂：需要生成 QBE `type` 定义 + 处理调用约定

**方案 B：构造器，显式打包成指针**

```
:struct Color char char char char
:bind drawCircle 'DrawCircle' void int int float void*

255 0 0 255 Color              _ 从栈上取 4 个值，打包，压回指针
400 300 50.0 drawCircle        _ 传指针给 C
```

- `Color` 像字一样调用，是 `:struct` 声明的副产品
- 编译器自动生成构造函数：pop 字段值 → 栈分配内存 → poke → push 指针
- `:bind` 签名里写 `void*`，不需要特殊处理
- 更灵活：指针可以存起来复用、传给多个函数
- 实现简单：只需要生成 alloc + poke 代码

### 构造器的内存管理

构造器默认使用**栈分配**（QBE 的 `alloc4`），不是 malloc：

```
@ draw
    255 0 0 255 Color      _ 栈分配，得到指针
    400 300 50.0 drawCircle _ 传给 C
;                           _ 函数返回，内存自动释放
```

- 不需要手动释放，不需要新的释放原语
- 生命周期：当前 zona 字返回时自动释放
- 适合绝大多数场景（临时构造传给 C）

如果需要长期持有结构体（跨函数使用），用现有的 `cmalloc` + `poke` 手动构造，用完 `cfree` 手动释放。这些原语已经有了，不需要新机制。

### 构造器不是原语

`Color` 不是新原语，是编译器根据 `:struct` 声明自动生成的内部函数。编译器遇到 T_WORD 时，先查字典，再查 extern 表，再查 struct 表。命中 struct 时调用生成的构造函数。

### 实现优先级

先做方案 B（构造器），因为：
1. 不涉及 QBE 类型定义
2. `:bind` 签名不需要改（用 `void*`）
3. 栈分配自动释放，零负担
4. 覆盖大部分实际场景

方案 A（自动打包 + 按值传递）后续按需添加。

### 其他语言的参考

- **Gforth**：结构体就是手动偏移量 + 内存读写，和 zona 现在一样
- **SwiftForth**：类似 Gforth，手动布局
- **Factor**（Forth 的现代后继者）：有完整的结构体声明和按值传递，但 Factor 有完整的类型系统和垃圾回收器，和传统 Forth 差距很大

## 远期：回调函数

### 现状

zona 无法生成函数指针传给 C。

### 问题

zona 的字（`@ cmp ... ;`）编译后是 QBE 函数，理论上有地址，但：
1. 没有机制把函数地址取出来传给 C
2. zona 函数签名（无参数、通过栈通信）和 C 期望的签名（如 `int(const void*, const void*)`）完全不匹配

要实现需要编译器自动生成 C 签名的"包装函数"，内部把 C 参数转成 zona 栈操作，调用 zona 字，再把结果转回 C 返回值。非常复杂。

### 实际影响

有限。在 FFI 领域，回调不是核心需求。需要回调的场景和替代方案：

| 需要回调的 | 替代方案 |
|-----------|---------|
| qsort 比较函数 | 自己写排序 |
| 信号处理 signal | sigwait 轮询 |
| GUI 事件回调（GTK、Qt）| 用轮询模式的库（raylib、SDL） |
| 异步 IO（libuv）| 同步 IO |

大部分游戏/图形/系统库的核心路径都不依赖回调。raylib 是轮询模式。

真遇到必须回调的场景，可以写一个小 C 文件做桥接，和 zona 编译产物一起链接——这本身就是原生编译 FFI 的优势。

### 结论

暂不设计，暂不实现。优先级最低。但 `:bind` 的类型系统中预留了函数指针的位置——当前可以用 `void*` 传递函数指针的数值，只是 zona 无法生成有效的函数指针。

## 设计讨论记录

### 曾考虑的 FFI 方案

- **通用 FFI（dlopen/dlsym + libffi）** — 能调用任意 C 库，但需要外部依赖，实现复杂
- **简化 FFI（只支持数字参数）** — 覆盖面太窄
- **直接链接 raylib 加图形原语** — 简单但不通用

最终决定：QBE 编译后端 + `:bind` 声明式 FFI。零额外依赖。

### 类型系统演进

1. 最初设计：单字符类型（`i d f s l p v`），紧凑但需要记忆映射表
2. 改为 C 标准类型名（`int double float char* void*` 等），零学习成本
3. 每个类型一个词元，`*` 在 `:bind` 行内自动和前一个词合并
4. signed/unsigned 在 FFI 层面不影响 ABI，不区分。`const` 也忽略。`size_t` 等同 `long`

### 现有原语与 FFI 的关系

`:time` `:rand` `:fopen` 等是 C 标准库的薄封装，理论上有 FFI 后可替代。但保留为内置原语更好：性能更高、使用更简单、无外部依赖。FFI 是补充而非替代。
