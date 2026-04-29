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
| `s` | const char* | 参数 | ✅ zona addr+len → C 字符串 |
| `S` | const char* 返回 | 返回 | ✅ C char* → zona 字符串（自动拷贝）|
| `p` | 指针 | 参数+返回 | ✅ 数值传递，语义同 `l` |
| `v` | void | 返回 | ✅ |

## 未实现：完整 C 互操作能力

当前 `:bind` 只覆盖了"zona 调 C 函数传基本类型"这一个场景。要做通用 FFI，还缺四块能力。

### 1. C 内存读写（优先级：高）

**问题**：zona 的 `& #` 只操作 zona 自己的 mem 数组（double 数组），无法读写 C 指针指向的真实内存。C 函数返回的指针、`:alloc` 分配的 C 内存，都没法操作。

**影响**：拿到 C 指针后什么都做不了。比如 C 返回一个 `int*`，zona 无法读取它指向的值。

**可能的方案**：新增一组原语操作真实内存地址：

```
_ 按类型读写 C 内存
:peek8  ( addr -- val )     _ 读 1 字节
:peek32 ( addr -- val )     _ 读 4 字节 int
:peek64 ( addr -- val )     _ 读 8 字节 long/pointer
:peekd  ( addr -- val )     _ 读 8 字节 double
:poke8  ( val addr -- )     _ 写 1 字节
:poke32 ( val addr -- )     _ 写 4 字节
:poke64 ( val addr -- )     _ 写 8 字节
:poked  ( val addr -- )     _ 写 8 字节 double
```

有了这些，就能操作任意 C 内存，包括构造结构体、读取结构体字段。

### 2. C 字符串返回（优先级：高）

**问题**：C 函数返回 `char*` 时，zona 拿到一个指针数字，但没法把它变成 zona 的字符串（addr+len in mem）来用 `:type` 打印。

**影响**：`GetWorkingDirectory()`、`TextFormat()` 等返回字符串的 C 函数无法使用。

**可能的方案**：

- 新增返回类型 `S`（大写）：自动把 C `char*` 拷贝到 zona mem，压入 addr+len
- 或者：有了 C 内存读写后，用户可以自己循环读取 `char*` 内容（但很繁琐）

### 3. 结构体（优先级：中）

**问题**：C 函数参数或返回值是结构体时，QBE 需要知道结构体的类型定义（大小、对齐、字段布局）。zona 目前没有描述结构体的能力。

**影响**：raylib 的 `Color`、`Vector2`、`Rectangle`、`Texture2D` 等都是结构体。

**两层问题**：

1. **按值传递结构体** — QBE 的 `call` 需要用 `:TypeName` 标注参数类型，需要先用 QBE 的 `type` 定义结构体布局
2. **构造/解构结构体** — 需要在 zona 侧分配内存、按偏移量写入字段值

**可能的方案**：新增结构体声明语法：

```
:struct Color 'Color' bbbb          _ 4 个 byte
:struct Vec2 'Vector2' ff           _ 2 个 float
:struct Rect 'Rectangle' ffff       _ 4 个 float
```

编译器据此生成 QBE `type` 定义，并在 `:bind` 签名中支持结构体类型引用。

有了 C 内存读写（第 1 项），结构体的构造/解构可以手动完成。结构体声明主要解决的是 QBE 调用约定问题。

### 4. 回调函数（优先级：低）

**问题**：zona 无法生成函数指针传给 C。

**影响**：`qsort`、信号处理、GUI 事件回调等需要函数指针的 API 无法使用。

**实际影响有限**：raylib 是轮询模式，大部分游戏/图形库不强依赖回调。

**可能的方案**：极其复杂，需要在编译时生成 C 函数包装器。暂不设计。

## 实现路径

```
第一步（已完成）：:bind 基本类型
    ↓
第二步（已完成）：C 内存读写原语（:peek8/32/64/d :poke8/32/64/d）
    ↓  解锁：指针操作、手动构造结构体
第三步（已完成）：C 字符串返回（S 返回类型）
    ↓  解锁：返回 char* 的 C 函数
第四步：结构体声明（:struct）
    ↓  解锁：按值传递/返回结构体的 C 函数
第五步：回调（远期）
```

每一步都独立可用，不需要等后面的步骤。

## 设计讨论记录

### 曾考虑的方案

- **通用 FFI（dlopen/dlsym + libffi）** — 能调用任意 C 库，但需要外部依赖，实现复杂
- **简化 FFI（只支持数字参数）** — 覆盖面太窄
- **直接链接 raylib 加图形原语** — 简单但不通用

最终决定：QBE 编译后端 + `:bind` 声明式 FFI。零额外依赖。

### 现有原语与 FFI 的关系

`:time` `:rand` `:fopen` 等是 C 标准库的薄封装，理论上有 FFI 后可替代。但保留为内置原语更好：性能更高、使用更简单、无外部依赖。FFI 是补充而非替代。

### `p` 和 `l` 的关系

两者在 QBE 层面都是 `l`（64 位）。`p` 语义是"指针"，`l` 是"大整数"。当前实现无区别。有了 C 内存读写后，`p` 的语义会更明确——它返回的值可以传给 `:peek`/`:poke`。
