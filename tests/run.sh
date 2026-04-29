#!/bin/bash
cd "$(dirname "$0")/.."
cc zona.c -o zona -lm -lreadline 2>&1 || exit 1

PASS=0
FAIL=0

run() {
    local name="$1" cmd="$2" expected="$3"
    actual=$(eval "$cmd" 2>&1)
    if [ "$actual" = "$expected" ]; then
        echo "PASS $name"
        PASS=$((PASS + 1))
    else
        echo "FAIL $name"
        echo "  expected: $expected"
        echo "  actual:   $actual"
        FAIL=$((FAIL + 1))
    fi
}

# zona test suite
run "test_all" "./zona tests/test_all.zona" "$(cat <<'EOF'
PASS add
PASS sub
PASS mul
PASS pow
PASS mod
PASS gt
PASS gt2
PASS eq
PASS abs
PASS sq
PASS min
PASS max
PASS not
PASS not2
PASS cond1
PASS cond0
PASS loop
PASS mem
done
EOF
)"

run "test_use" "./zona tests/test_use.zona" "$(cat <<'EOF'
7
81
3
7
0
hello
world
EOF
)"

run "demo" "./zona examples/demo.zona" "$(cat <<'EOF'
8
1024
42
81
120
40320
3628800
5
4
3
2
1
4
25
1
hello zona!
EOF
)"

run "args" "./zona tests/test_args.zona foo bar" "$(cat <<'EOF'
2
foo
bar
EOF
)"

run "repl" "printf '3 5 + .\n' | ./zona" "8"

run "error_line" "printf '1 2\n+\nfoo\n' | ./zona" "line 3: unknown word: foo"

echo ""
echo "$((PASS + FAIL)) tests, $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && exit 0 || exit 1
