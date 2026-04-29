# FFI

## 背景

QBE 编译后端已完成，编译产物是原生代码。链接 C 库只需：

```
./zonac game.zona -o game
cc game.s -o game -lraylib -lm
```

不需要 dlopen，不需要 libffi，零额外依赖。

但 zona 语言层面目前无法声明和调用外部 C 函数，需要一个绑定语法。

## `:bind` 语法

```
:bind zonaName 'cName' returnType [paramTypes]
```

- `zonaName` — zona 里的字名，遵守现有词法规则（字母开头，字母数字）
- `'cName'` — C 函数真名，字符串包裹，支持下划线等任意 C 标识符
- `returnType` — 单字符，返回类型
- `paramTypes` — 可选，参数类型字符串，无参数时省略

### 示例

```
:bind closeWindow 'CloseWindow' v               _ 无参无返回
:bind getWidth 'GetScreenWidth' i               _ 无参有返回
:bind drawCircle 'DrawCircle' v iifd            _ 有参无返回
:bind puts 'puts' i s                           _ 字符串参数
:bind createWindow 'create_window' v iis        _ 别名绑定

800 600 'hello' createWindow
getWidth .
closeWindow
```

### 类型字符

| 字符 | C 类型 | QBE 类型 | zona 栈上 | 转换 |
|------|--------|----------|----------|------|
| `i` | int | `w` | double | `dtosi` |
| `l` | long / size_t | `l` | double | `dtosi` + `extsw` |
| `d` | double | `d` | double | 直接 |
| `f` | float | `s` | double | `truncd` |
| `s` | const char* | `l` | addr+len | 从 zona mem 提取成 C 字符串 |
| `p` | 指针 (void* 等) | `l` | double | 待定，语义同 `l` 但更明确 |
| `v` | void | 无 | 不压栈 | 仅用于返回类型 |

### 行格式规则

`:bind` 和 `:use` 遵守相同的行格式规则：

- 必须是行首第一个词元（`_` 注释行除外）
- 独占一行，同一行的所有词元都属于该声明
- 不能出现在 `@ ... ;` 字定义体内部
- 行尾可用 `_` 注释

### 编译期校验

第一遍扫描时对 `:bind` 行做完整校验，不合法直接报错带行号：

**`:use` 行校验：**
- 第二个词元必须是字符串（T_STR）
- 不能有第三个词元

**`:bind` 行校验：**
- 第二个词元必须是用户字（T_WORD）
- 第三个词元必须是字符串（T_STR）
- 第四个词元必须是用户字（T_WORD），且只含合法类型字符 `idfslpv`，且长度为 1（返回类型）
- 第五个词元可选，如果有必须是用户字（T_WORD），且只含合法类型字符 `idfslpv`
- 不能有更多词元

**`@ ... ;` 内部检查：**
- 字定义体收集完成后，扫描 body 中是否有 `:use` 或 `:bind`，有则报错

### 重定义

- 同名 `:bind` 后定义遮盖先定义，与字的重定义规则一致
- `:bind` 的 zona 名可以和已有字同名，后定义遮盖先定义
- C 函数名不存在时，编译能过，链接时由 `cc` 报 undefined symbol

### 编译器行为

遇到 `:bind` 声明时，记录到 extern 表。后续遇到 `zonaName` 调用时：

1. 按 `paramTypes` 从右到左 pop 参数（`s` 类型 pop 两个值：len 和 addr）
2. 按类型生成 QBE 转换指令
3. 生成 `call $cName(类型 参数, ...)`
4. 如果返回类型非 `v`，将返回值转为 double 压栈

### 解释器行为

解释器遇到 `:bind` 时忽略（跳过该行所有词元）。

## 待定问题

### `p` 和 `l` 的关系

两者在 QBE 层面都是 `l`（64 位）。是否需要区分？`p` 的语义是"这是一个指针"，`l` 是"这是一个大整数"。可能影响未来的类型检查，但当前实现上没有区别。

### 结构体

zona 栈上全是 double，无法直接传递 C 结构体。实际影响：

- raylib 的 `Color` 是 4 字节，可以打包成一个 int 传
- `Vector2`、`Rectangle` 等小结构体可以用多个参数模拟
- 大结构体需要在 zona 侧分配内存、逐字段写入、传指针

可能的未来方案：新增结构体打包原语，或在标准库层面封装。暂不设计。

### 回调函数

zona 无法生成函数指针传给 C。实际影响有限——raylib 是轮询模式，不依赖回调。暂不支持。

## 早期讨论记录

### 曾考虑的方案

- **方案 A：通用 FFI（dlopen/dlsym + libffi）** — 能调用任意 C 库，但需要外部依赖，实现复杂
- **方案 B：简化 FFI（只支持数字参数）** — 覆盖面太窄，不推荐
- **方案 C：直接链接 raylib 加图形原语** — 简单但不通用

最终决定：QBE 编译后端 + `:bind` 声明式 FFI。编译产物是原生代码，链接时 `cc game.s -lraylib`，和 C 调 C 一样自然。

### 关于现有原语与 FFI 的关系

`:time` `:rand` `:fopen` 等原语本质上是 C 标准库的薄封装，理论上有 FFI 后都可以替代。但保留为内置原语更好：性能更高、使用更简单、无外部依赖。FFI 的价值是补充而非替代。
