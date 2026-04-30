# 性能优化计划

> QBE IR 参考文档：https://c9x.me/compile/doc/il.html
> 所有优化方案在实现前应参考 QBE 文档确认 IR 层面的最佳做法。

## 已做

### 阶段一：虚栈骨架 ✅

T_NUM、算术符号走虚栈；控制流边界 vsync。fib(35) 0.48s → 0.29s。

### 阶段二：栈原语虚栈化 ✅

`:dup` `:swap` `:drop` `:over` `:rot` 在虚栈上操作，零运行时指令生成。fib(35) 0.29s → 0.26s。

### 阶段三：运行时函数内联展开 ✅

push/pop/peek 的 QBE 指令（各 7 行）直接展开到调用点，消除 call/ret 开销。QBE 自动消除相邻 store-load 冗余对。

- `emit_push(t)`: 展开 store + sp++ 指令
- `emit_pop()`: 展开 sp-- + load 指令，返回 temp
- `emit_peek()`: 展开 sp-1 + load 指令（不修改 sp），返回 temp
- `vsync()`: 循环调用 emit_push
- `vpopr()`: 虚栈空时调用 emit_pop
- `:dup` 虚栈空时调用 emit_peek

fib(35) 0.26s → 0.16s（1.6x），loop(10M) 0.07s → 0.05s。

### 累计效果

| 测试 | 优化前 | 优化后 | 总提升 |
|------|--------|--------|--------|
| fib(35) | 0.48s | 0.16s | **3x** |
| loop(10M) | 0.08s | 0.05s | **1.6x** |

编译器 vs 解释器：fib 从 3.2x → 9.7x 加速。性能从 Ruby 级 → Node.js 和 Python 之间。

---

## 下一步（按优先级）

### 阶段四：超指令 ⏳

识别 Zona 惯用 token 序列，编译时替换为高效版本：

| 序列 | 替换 | 场景 |
|------|------|------|
| `:dup +` | dup-add | `:dup n +` 极常见 |
| `1 - ~` | dec-loop | 倒计数循环 |
| `:dup 0 = ? $` | exit-if-zero | 循环出口 |
| `:swap :drop` | nip | 标准库已有 |
| `:dup 0 < ? neg` | abs | 标准库 abs |

前面阶段已消除 push/pop 调用，超指令进一步压缩多条 QBE 指令为单条。

### 阶段五：小字内联 ⏳

body ≤ 8 token、非递归的字直接在调用点展开。扩大"直线代码区域"。

### 解释器：栈顶缓存 + 首字符跳表

- 栈顶 3 个值放 C 局部变量（硬件寄存器）
- `exec_prim` 的 strcmp 链改成首字符 switch

---

## 更远的方向

- 调用约定优化：静态分析栈效应 `(in → out)`，调用时只同步参数
- 类型特化：整数走 `w` 指令，浮点走 `d`
- 生成 C（release 模式）
- 字节级字符串存储
