# Zona — 实施计划与状态

> 语言规范：[`spec.md`](spec.md) · 决策记录：[`decisions.md`](decisions.md) · 编译器内部：[`compiler.md`](compiler.md) · 废案：[`rejected.md`](rejected.md)

---

## 已完成 ✅

### 编译器核心

**产出**：`zonac.c`

- tokenizer（整数/浮点分离、`: ` 双重语义、`T_MEMBER`、`&` `#`）
- 栈效应签名解析（`d:d` `l:l` `pl:` 等格式）
- 类型化算术/比较（`l`/`d` 分离，比较返回 `l`）
- 控制流 `? ! $ ~`（分支虚栈快照保存/恢复）
- 寄存器调用：`@ fib d:d` → `function d $zona_fib(d %n)`
- 虚栈追踪 + 控制流边界 `vsync()`
- `:use` 模块系统：命名导入 + `T_MEMBER` 成员访问

### 已实现原语

`:dup` `:drop` `:swap` `:over` `:rot` `.` `:print` `:type` `:emit` `:key` `:alloc` `:free` `:time` `:rand` `:exit` `:argc` `:argv`

### 性能 (fib(35), Apple M 系列)

| | 时间 | vs C |
|------|------|------|
| 当前编译器 | **0.04s** | **~1.0x** |
| 前一版本 | 0.106s | 2.5x |
| C (-O2) | 0.043s | 1x |

---

## 待实现 ⬜

| 阶段 | 内容 |
|------|------|
| FFI | `:bind` → QBE extern 调用 |
| 标准库 | math / logic / io .zona 文件 |

## 远期 🔮

| 阶段 | 内容 |
|------|------|
| 结构体 | `:struct` + 字段 `.` 访问 |
| 内联展开 | 短词在 QBE IR 层面展开 |
