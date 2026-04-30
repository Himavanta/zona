# 性能基准测试

## 测试环境

- 平台：macOS, Apple Silicon (arm64)
- QBE 目标：arm64_apple
- zona 解释器：`cc src/zona.c -o zona -lm -lreadline`
- zona 编译器：`cc src/zonac.c -o zonac -lm`

## 测试方法

### fib(35) — 递归密集型

测试递归调用开销，fib(35) 产生约 2930 万次函数调用。

```
@ fib :dup 2 < ? $ :dup 1 - fib :swap 2 - fib + ;
35 fib .
```

### loop(10000000) — 循环密集型

测试循环和栈操作开销，1000 万次迭代。

```
@ loop :dup 0 = ? $ 1 - ~ ;
10000000 loop
'done' :type 10 :emit
```

## 测试结果

### fib(35)

| 语言 | 时间 (user) | vs C |
|------|------------|------|
| C (-O2, double) | 0.042s | **1x** |
| **zona 编译器** | **0.105s** | **2.5x** |
| Node.js (V8 JIT) | 0.069s | 1.6x |
| **zona 解释器** | **1.62s** | **39x** |
| Python | 1.05s | 25x |

编译器 vs C 差距从优化前的 **18x** 缩小到 **2.5x**。

### loop(10000000)

| 语言 | 时间 (user) |
|------|------------|
| **zona 编译器** | **0.05s** |
| **zona 解释器** | **0.29s** |

### 编译器 vs 解释器

| 测试 | 解释器 | 编译器 | 加速比 |
|------|--------|--------|--------|
| fib(35) | 1.62s | 0.105s | **15x** |
| loop(10M) | 0.29s | 0.05s | **5.8x** |

## 性能定位

编译器 fib(35) 距 C 仅 2.5x，优于 Node.js 外的所有动态语言。
优化累计：0.48s → 0.29s（虚栈骨架）→ 0.26s（栈原语虚栈化）→ 0.16s（内联展开）→ 0.105s（实测）。

### 阶段一：虚拟栈骨架

fib(35) 0.48s → 0.29s（1.65x）。直线代码消除 push/pop 函数调用。

### 阶段二：栈原语虚栈化

fib(35) 0.29s → 0.26s。`:dup` `:swap` `:drop` `:over` `:rot` 零运行时开销。

### 阶段三：运行时函数内联展开

fib(35) 0.26s → 0.16s（1.6x），loop(10M) 0.07s → 0.05s。push/pop/peek 的 7 条 QBE 指令直接展开到调用点，消除 call/ret 开销，QBE 自动消除相邻 store-load 冗余对。

## 性能瓶颈分析

当前编译器使用**运行时栈**方案：每次栈操作都通过函数调用（`$zona_push`/`$zona_pop`）读写全局内存数组。这意味着：

- 每个 `+` 操作 = 2 次 pop 函数调用 + 1 次 add + 1 次 push 函数调用
- 每次 push/pop = 读写全局 `$sp` + 计算偏移 + 读写 `$stack` 数组
- 函数调用本身有开销（保存/恢复寄存器）

本质上还是在模拟栈机器，只是省掉了解释器的 token 分发（switch/if-else 链）开销。

## 优化方向

### 1. 编译时虚拟栈（预期提升：5-10x）

将 zona 栈操作直接映射到 QBE 临时变量，不经过 push/pop：

```
_ zona: 3 5 +
_ 当前生成：
call $zona_push(d d_3.0)
call $zona_push(d d_5.0)
%t0 =d call $zona_pop()
%t1 =d call $zona_pop()
%t2 =d add %t1, %t0
call $zona_push(d %t2)

_ 优化后：
%t0 =d copy d_3.0
%t1 =d copy d_5.0
%t2 =d add %t0, %t1
```

**难点**：控制流（`? ~ $`）和栈操作原语（`:rot`、`:over`）在编译时无法静态追踪栈深度。需要在基本块边界做栈状态合并，或对简单情况做特殊优化。

### 2. 内联小函数（预期提升：2-3x）

将短小的用户字（如 `@ neg 0 :swap - ;`）内联到调用点，减少函数调用开销。

### 3. 常量折叠（预期提升：小）

编译时计算常量表达式，如 `3 5 +` 直接生成 `8`。

### 4. 尾调用优化（预期提升：递归场景显著）

zona 的 `~` 已经是循环（不消耗返回栈），但递归调用（如 `fib`）仍然消耗调用栈。可以检测尾位置的递归调用并转为跳转。

## 实际影响

当前 3-4x 的提升对实际使用已经够了：
- FFI 调用的 C 库函数（raylib 绑图、数学计算等）本身是原生速度
- zona 代码主要是胶水层和控制逻辑
- 计算密集型任务应该通过 FFI 调用 C 库

## 复现方法

```bash
cd /path/to/zona

# 编译
cc src/zona.c -o zona -lm -lreadline
cc src/zonac.c -o zonac -lm

# fib(35) 测试
cat > /tmp/bench.zona << 'EOF'
@ fib :dup 2 < ? $ :dup 1 - fib :swap 2 - fib + ;
35 fib .
EOF

time ./zona /tmp/bench.zona           # 解释器
./zonac /tmp/bench.zona -o /tmp/bench
time /tmp/bench                        # 编译器

# loop 测试
cat > /tmp/bench2.zona << 'EOF'
@ loop :dup 0 = ? $ 1 - ~ ;
10000000 loop
'done' :type 10 :emit
EOF

time ./zona /tmp/bench2.zona
./zonac /tmp/bench2.zona -o /tmp/bench2
time /tmp/bench2
```
