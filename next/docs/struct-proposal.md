# 结构体方案

> **已过时** — 本文档为讨论过程中的方案对比，最终决策见 `plan.md` 待决 0（统一类型词汇表）和待决 2（结构体语法）。选定方案为：统一 QBE 类型词汇表 + `p*` 字符串指针扩展，`:struct` 字段使用 QBE 类型名。

## 背景

V2 已决定：
- 栈效应声明用 `i`/`d`/`p`（Zona 语义层）
- `:struct` 顶层声明，独占一行，无结束符号
- 字段访问 `Point.x` 与模块成员访问统一为 `T_MEMBER`
- 自动生成构造器/析构器/字段读写

核心未决问题：**`:struct` 和 `:bind` 中用什么类型词？**

---

## 类型词的三层现实

| 层 | 类型词 | 职责 |
|---|---|---|
| Zona 栈效应 | `i` `d` `p` | 编译期追踪栈值的语义族 |
| FFI/结构体声明 | ??? | 精确描述 C ABI 类型，决定字节大小、对齐、调用约定 |
| QBE IL | `w` `l` `s` `d` `b` `h` | 编译器后端代码生成 |

栈效应的 `i`/`d`/`p` 是 Zona 内部概念，不需要改。问题是 FFI/结构体声明这一层用什么。

---

## 方案 A：C 类型名（V1 现状）

```
:bind initWindow 'InitWindow' void int int char*
:struct Color r char g char b char a char
:struct Point x double y double
```

V1 编译器已有完整的 C 类型 → QBE 类型映射（`zonac.c:912-921`）：

| C 类型名 | QBE 类型 | 转换指令（栈→C） | 转换指令（C→栈） |
|----------|---------|----------------|---------------|
| `double` | `d` | 直接 | 直接 |
| `float` | `s` | `truncd` | `exts` |
| `long` / `size_t` / 指针 | `l` | `dtosi` | `sltof` |
| `int` / `char` / `short` | `w` | `dtosi` | `swtof` |
| `char*` | `l` + 特殊处理 | 自动双向字符串转换 | |
| `void*` | `l` | 直接传数值 | |

优点：
- 零学习成本 — 就是 C 的类型名
- `char*` 和 `void*` 能区分 — 对 FFI 至关重要（`char*` 需要自动字符串转换）
- 结构体偏移量精确 — `char` 是 1 字节，`int` 是 4 字节，和 C 一致

缺点：
- 两套类型词 — 栈效应用 `i`/`d`/`p`，声明用 C 类型名
- C 类型词较长 — `double` 比 `d` 多 5 个字符，`char*` 更长

---

## 方案 B：QBE 类型名

```
:bind initWindow 'InitWindow' void w w l    _ int int char*???
:struct Color r b g b b b a b
:struct Point x d y d
```

QBE 类型体系：

| QBE 类型 | 大小 | 含义 |
|----------|------|------|
| `b` | 1 字节 | 字节（仅 aggregate 内） |
| `h` | 2 字节 | 半字（仅 aggregate 内） |
| `w` | 4 字节 | 字（32 位整数） |
| `l` | 8 字节 | 长字（64 位整数 / 指针） |
| `s` | 4 字节 | 单精度浮点 |
| `d` | 8 字节 | 双精度浮点 |

优点：
- 单字符，和 Zona 风格一致
- `:struct` 布局精确 — `b`/`h` 直接对应 QBE aggregate 定义
- 减少一层间接 — 不需要 C 类型名 → QBE 类型的映射

缺点：

### 关键问题：`char*` 无法表达

QBE 中指针就是 `l`，没有指针类型。`char*` 和 `void*` 和 `long` 都是 `l`。

但 V1 的 FFI 对 `char*` 有**特殊处理**：
- 参数方向：从 Zona 栈弹出地址+长度 → 分配 C 字符串 → 传 `l` 给 C → 调用后释放
- 返回方向：从 C 拿到 `l` → 复制到 Zona 内存 → 压入地址+长度

这意味着编译器**必须知道**某个 `l` 参数是 `char*` 还是 `void*`，否则：
- 传 `char*` 当 `void*` → 编译器不转字符串，C 收到 Zona 内部地址（崩溃）
- 传 `void*` 当 `char*` → 编译器做字符串转换，多余分配+语义错误

用纯 QBE 类型无法区分，必须额外标注。

### 可能的修补

1. **加指针标记** — `l*` 或 `lp` 表示指针，`ls` 表示字符串指针：
   ```
   :bind initWindow 'InitWindow' void w w ls
   ```
   但这不再是纯 QBE 类型了，是自定义扩展。

2. **用 `p` 做指针，`s` 做字符串** — 重载 Zona 栈效应标记：
   ```
   :bind initWindow 'InitWindow' void w w s
   ```
   但 `s` 在 QBE 里是 float，语义冲突。

3. **`char*` 单独处理** — 保留 `char*` 作为唯一复合类型词：
   ```
   :bind initWindow 'InitWindow' void w w char*
   ```
   混合方案，部分 QBE 部分 C 类型名，不统一。

### 结构体中的 QBE 类型

`:struct` 用 QBE 类型没有 `char*` 问题（结构体字段不存在自动字符串转换），布局也很精确：

```
:struct Color r b g b b b a b          _ 4 字节，和 C 的 Color 一致
:struct Rect x s y s w s h s           _ 16 字节，4 个 float
```

直接生成 QBE aggregate：
```
type :Color = { b, b, b, b }
type :Rect = { s, s, s, s }
```

这部分确实比 C 类型名更优雅。

---

## 方案 C：分区使用

- `:struct` 用 QBE 类型 — 没有字符串转换问题，布局精确，单字符简洁
- `:bind` 用 C 类型名 — 必须区分 `char*`/`void*`/`long`，C 类型名是唯一无歧义的表达

```
:struct Color r b g b b b a b
:struct Point x d y d
:struct Rect x s y s w s h s

:bind initWindow 'InitWindow' void int int char*
:bind drawCircle 'DrawCircle' void int int float void*
```

优点：各取所长
缺点：两套语法，用户要记两种规则

---

## 方案 D：统一 QBE 类型 + 指针修饰

以 QBE 类型为基础，用 `*` 后缀标记指针：

```
:struct Color r b g b b b a b
:struct Point x d y d

:bind initWindow 'InitWindow' void w w l*     _ l* = char* (字符串指针)
:bind drawCircle 'DrawCircle' void w w s l    _ l = void* (裸指针)
```

规则：
- 基础类型：`b` `h` `w` `l` `s` `d` — 与 QBE 一一对应
- `l*` — 字符串指针，触发自动双向字符串转换
- `l` — 裸指针（`void*`、函数指针等），直接传数值

优点：
- 单一套类型词体系
- `:struct` 和 `:bind` 语法统一
- 指针语义精确 — `l*` vs `l` 区分字符串指针和裸指针
- 直接映射到 QBE — 无中间层

缺点：
- `l*` 是自定义扩展，QBE 本身没有这个概念
- 相比 C 类型名不够直观 — `w` 不如 `int` 易读

### QBE 类型 → Zona 栈效应映射

| QBE 类型 | Zona 栈效应 | 说明 |
|----------|-----------|------|
| `b` `h` `w` | `i` | 整数族 |
| `l` | `i` | 64 位整数 |
| `l*` | `p` | 字符串指针（压入 `p i`：地址+长度） |
| `s` `d` | `d` | 浮点族 |

### C ABI 完整覆盖验证

| C 类型 | 方案 D 表示 | QBE 类型 | 覆盖？ |
|--------|-----------|---------|-------|
| `void` | `void`（保留） | — | ✅ 仅用于返回类型 |
| `char` | `b` | `b`（aggregate内）/ `w`（参数） | ✅ |
| `short` | `h` | `h`（aggregate内）/ `w`（参数） | ✅ |
| `int` | `w` | `w` | ✅ |
| `unsigned int` | `w` | `w` | ✅ ABI 层面 unsigned 与 signed 相同 |
| `long` | `l` | `l` | ✅ |
| `size_t` | `l` | `l` | ✅ |
| `float` | `s` | `s` | ✅ |
| `double` | `d` | `d` | ✅ |
| `void*` | `l` | `l` | ✅ |
| `char*` | `l*` | `l` + 字符串转换 | ✅ |
| `int*` | `l` | `l` | ✅ 指针就是 64 位整数 |
| `struct Color*` | `l` | `l` | ✅ 结构体指针就是指针 |
| `int(*)(void)` | `l` | `l` | ✅ 函数指针就是指针 |

unsigned 问题：QBE 的 `w`/`l` 不编码符号，C ABI 层面 unsigned 和 signed 的传参规则相同。返回值时，`unsigned int` 返回的值如果 > INT_MAX，Zona 用 `swtof` 会得到负数。但这是**所有方案共有的问题**，V1 也不处理 unsigned。实际影响极小。

结论：**方案 D 完整覆盖 C ABI**。

---

## 推荐排序

1. **方案 D** — 统一 QBE 类型 + `l*` 指针修饰。最一致，直接对应编译后端，完整覆盖 FFI
2. **方案 C** — 分区使用。实用但两套规则
3. **方案 A** — C 类型名。V1 现状，成熟可靠
4. **方案 B** — 纯 QBE 类型。无法表达 `char*`，不可行
