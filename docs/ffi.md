# FFI 与图形能力讨论

## 动机

希望用 zona 打开窗口、绘制图形（如 raylib）。

## 方案对比

### 方案 A：通用 FFI（dlopen/dlsym + libffi）

- 能调用任意 C 库函数
- 需要安装 `libffi` 外部依赖
- 需要处理参数类型转换（字符串指针、结构体等）
- 实现复杂度高（⭐⭐⭐）
- 好处：一次实现，所有 C 库都能用

### 方案 B：简化 FFI（只支持数字参数）

- 不需要外部依赖
- 只能调用参数为 int/double 的函数
- 大部分实际 C 库需要传字符串或结构体，覆盖面太窄
- 结论：实用性不足，不推荐

### 方案 C：直接链接 raylib（推荐）

- 把 raylib 和 zona.c 一起编译：`cc zona.c -o zona -lm -lreadline -lraylib`
- 添加一组图形原语：`:initwin` `:closewin` `:begindraw` `:enddraw` `:clear` `:circle` `:rect` `:text` 等
- raylib API 大部分是简单数字参数，和 zona 栈模型很搭
- 安装：`brew install raylib`
- 实现简单，每个原语几行 C 代码

## 关于现有原语与 FFI 的关系

现有的 `:time` `:rand` `:fopen` 等原语本质上是 C 标准库的薄封装，理论上有完整 FFI 后都可以替代。但保留为内置原语更好：性能更高、使用更简单、无外部依赖。FFI 的价值是补充而非替代。

## 结论

- ~~短期：方案 C，直接链接 raylib，加图形原语~~
- ~~长期：方案 A，通用 FFI~~
- **最终决定：做 QBE 编译后端，FFI 免费获得**。编译后的 zona 程序生成原生代码，链接时直接 `cc zona_output.s -o prog -lraylib`，和 C 调 C 一样自然。不需要 dlopen，不需要 libffi，零额外依赖。

## 可能的 raylib 原语设计

```
_ 窗口
800 600 'hello' :initwin
:closewin
:winopen                    _ 窗口是否仍打开 ( -- flag )

_ 绘制循环
:begindraw
  255 255 255 255 :clear    _ RGBA
  400 300 50 255 0 0 255 :circle  _ x y r R G B A
:enddraw

_ 输入
:keypressed                 _ ( key -- flag )
```
