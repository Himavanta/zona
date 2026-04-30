#!/bin/bash
cd "$(dirname "$0")/../.."

echo "=== Zona Benchmark ==="
echo ""

cc src/zona.c -o zona -lm -lreadline 2>/dev/null
cc src/zonac.c -o zonac -lm 2>/dev/null

bench() {
    local name="$1" file="$2" expected="$3"
    echo "--- $name ---"

    # interpreter
    local t1
    t1=$( (time ./zona "$file" >/dev/null 2>&1) 2>&1 | awk '/user/ {print $1}' | tr -d 's')
    echo "  int:  ${t1:-?}s"

    # compiler
    ./zonac "$file" -o /tmp/_bench 2>/dev/null
    local t2
    t2=$( (time /tmp/_bench >/dev/null 2>&1) 2>&1 | awk '/user/ {print $1}' | tr -d 's')
    echo "  comp: ${t2:-?}s"
    rm -f /tmp/_bench /tmp/_bench.s

    # verify
    local actual
    actual=$(./zona "$file" 2>/dev/null)
    if [ "$actual" != "$expected" ]; then
        echo "  WARN: expected '$expected' got '$actual'"
    fi
    echo ""
}

bench "fib(35)"     tests/bench/fib.zona  "9227465"
bench "loop(10M)"   tests/bench/loop.zona "done"

echo "=== 参考 ==="
echo ""

t1=$( (time ./zona tests/bench/fib.zona >/dev/null 2>&1) 2>&1 | awk '/user/ {print $1}' | tr -d 's')
t2=$( (time ./zona tests/bench/loop.zona >/dev/null 2>&1) 2>&1 | awk '/user/ {print $1}' | tr -d 's')
echo "  Zona int fib(35):   ${t1}s"
echo "  Zona int loop(10M): ${t2}s"

cat > /tmp/_cfib.c << 'EOF'
#include <stdio.h>
double fib(double n) { return n < 2 ? n : fib(n-1)+fib(n-2); }
int main() { printf("%.0f\n", fib(35)); return 0; }
EOF
cc -O2 /tmp/_cfib.c -o /tmp/_cfib 2>/dev/null
t=$( (time /tmp/_cfib >/dev/null) 2>&1 | awk '/user/ {print $1}' | tr -d 's')
echo "  C -O2 fib(35):      ${t}s"

t=$( (time python3 -c 'def f(n):return n if n<2 else f(n-1)+f(n-2);print(f(35))' >/dev/null) 2>&1 | awk '/user/ {print $1}' | tr -d 's')
echo "  Python fib(35):     ${t}s"
