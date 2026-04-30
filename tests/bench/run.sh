#!/bin/bash
cd "$(dirname "$0")/../.."
cc src/zona.c -o zona -lm -lreadline 2>/dev/null
cc src/zonac.c -o zonac -lm 2>/dev/null

echo "=== Zona fib(35) ==="
echo "  解释器:"
time ./zona tests/bench/fib.zona
echo "  编译器:"
./zonac tests/bench/fib.zona -o /tmp/_fib 2>/dev/null
time /tmp/_fib
rm -f /tmp/_fib /tmp/_fib.s

echo ""
echo "=== Zona loop(10M) ==="
echo "  解释器:"
time ./zona tests/bench/loop.zona
echo "  编译器:"
./zonac tests/bench/loop.zona -o /tmp/_loop 2>/dev/null
time /tmp/_loop
rm -f /tmp/_loop /tmp/_loop.s

echo ""
echo "=== 参考 ==="
echo ""

echo "C fib(35):"
cat > /tmp/_cfib.c << 'EOF'
#include <stdio.h>
double fib(double n) { return n<2?n:fib(n-1)+fib(n-2); }
int main() { printf("%.0f\n", fib(35)); return 0; }
EOF
cc -O2 /tmp/_cfib.c -o /tmp/_cfib 2>/dev/null
time /tmp/_cfib
rm -f /tmp/_cfib /tmp/_cfib.c

echo ""
echo "Node.js fib(35):"
time node -e 'function f(n){return n<2?n:f(n-1)+f(n-2)};console.log(f(35))'

echo ""
echo "Python fib(35):"
time python3 -c 'def f(n):return n if n<2 else f(n-1)+f(n-2);print(f(35))'
