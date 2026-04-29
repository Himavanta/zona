#!/bin/bash
cd "$(dirname "$0")/.."
cc src/zona.c -o zona -lm -lreadline 2>&1 || exit 1
cc src/zonac.c -o zonac -lm 2>&1 || exit 1

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

# helper: compile and run
zonac_run() {
    local src="$1"; shift
    ./zonac "$src" -o /tmp/_zonac_test "$@" 2>&1 && /tmp/_zonac_test "$@" 2>&1
    local rc=$?
    rm -f /tmp/_zonac_test /tmp/_zonac_test.s
    return $rc
}

EXPECTED_ALL="$(cat <<'EOF'
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

EXPECTED_USE="$(cat <<'EOF'
7
81
3
7
0
hello
world
EOF
)"

EXPECTED_DEMO="$(cat <<'EOF'
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

EXPECTED_ARGS="$(cat <<'EOF'
2
foo
bar
EOF
)"

# --- interpreter tests ---
echo "# interpreter"
run "i:test_all" "./zona tests/test_all.zona" "$EXPECTED_ALL"
run "i:test_use" "./zona tests/test_use.zona" "$EXPECTED_USE"
run "i:demo" "./zona examples/demo.zona" "$EXPECTED_DEMO"
run "i:args" "./zona tests/test_args.zona foo bar" "$EXPECTED_ARGS"
run "i:repl" "printf '3 5 + .\n' | ./zona" "8"
run "i:error_line" "printf '1 2\n+\nfoo\n' | ./zona" "line 3: unknown word: foo"

# --- compiler tests (mirror) ---
echo "# compiler"
run "c:test_all" "zonac_run tests/test_all.zona" "$EXPECTED_ALL"
run "c:test_use" "zonac_run tests/test_use.zona" "$EXPECTED_USE"
run "c:demo" "zonac_run examples/demo.zona" "$EXPECTED_DEMO"
run "c:args" "./zonac tests/test_args.zona -o /tmp/_zonac_args 2>&1 && /tmp/_zonac_args foo bar 2>&1; rm -f /tmp/_zonac_args /tmp/_zonac_args.s" "$EXPECTED_ARGS"

# --- :bind test ---
cat > /tmp/_test_bind.zona << 'ZONA'
:bind myPuts 'puts' i s
:bind myAbs 'abs' i i
'hello FFI' myPuts :drop
-42 myAbs .
ZONA
run "c:bind" "zonac_run /tmp/_test_bind.zona" "$(printf 'hello FFI\n42')"

# --- peek/poke test ---
cat > /tmp/_test_peek.zona << 'ZONA'
:bind cmalloc 'malloc' l l
:bind cfree 'free' v l
16 cmalloc
42 :over :poke32
99 :over 4 + :poke8
:dup :peek32 .
:dup 4 + :peek8 .
cfree
ZONA
run "c:peek_poke" "zonac_run /tmp/_test_peek.zona" "$(printf '42\n99')"

# --- S return type test ---
cat > /tmp/_test_sret.zona << 'ZONA'
:bind getenv 'getenv' S s
'USER' getenv :type 10 :emit
ZONA
run "c:S_return" "zonac_run /tmp/_test_sret.zona" "$(whoami)"

# --- validation tests ---
echo "# validation"

# :use extra tokens
cat > /tmp/_test_v1.zona << 'ZONA'
:use './std/math.zona' extra
ZONA
run "v:use_extra" "./zona /tmp/_test_v1.zona 2>&1" "line 1: :use line must contain only :use and path"

# :bind inside word
cat > /tmp/_test_v2.zona << 'ZONA'
@ foo :bind bar 'bar' v ;
ZONA
run "v:bind_in_word" "./zona /tmp/_test_v2.zona 2>&1" "line 1: :bind cannot appear inside word 'foo'"

# :use inside word
cat > /tmp/_test_v3.zona << 'ZONA'
@ foo :use './std/math.zona' ;
ZONA
run "v:use_in_word" "./zona /tmp/_test_v3.zona 2>&1" "line 1: :use cannot appear inside word 'foo'"

# :bind bad return type
cat > /tmp/_test_v4.zona << 'ZONA'
:bind foo 'foo' int
ZONA
run "v:bind_bad_ret" "./zonac /tmp/_test_v4.zona -o /tmp/_v4 2>&1" "line 1: :bind return type must be a single type char (idfslpv)"

# :bind bad param types
cat > /tmp/_test_v5.zona << 'ZONA'
:bind foo 'foo' v xyz
ZONA
run "v:bind_bad_params" "./zonac /tmp/_test_v5.zona -o /tmp/_v5 2>&1" "line 1: :bind param types must only contain type chars (idfslpv)"

# :bind too few tokens
cat > /tmp/_test_v6.zona << 'ZONA'
:bind foo
ZONA
run "v:bind_too_few" "./zonac /tmp/_test_v6.zona -o /tmp/_v6 2>&1" "line 1: :bind requires: name 'cname' retType [paramTypes]"

# interpreter ignores :bind
cat > /tmp/_test_v7.zona << 'ZONA'
:bind myPuts 'puts' i s
42 .
ZONA
run "v:bind_ignored" "./zona /tmp/_test_v7.zona" "42"

# cleanup
rm -f /tmp/_test_bind.zona /tmp/_test_peek.zona /tmp/_test_sret.zona /tmp/_test_v*.zona

echo ""
echo "$((PASS + FAIL)) tests, $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && exit 0 || exit 1
