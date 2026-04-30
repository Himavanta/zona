#!/bin/bash
set -e
cd "$(dirname "$0")/.."

# --- build -------------------------------------------------
echo "# build"
cc src/zona.c -o zona -lm -lreadline 2>&1 || exit 1
cc src/zonac.c -o zonac -lm 2>&1 || exit 1

PASS=0; FAIL=0

# --- helpers -----------------------------------------------
run() {
    local name="$1" cmd="$2" expected="$3"
    local actual
    actual=$(eval "$cmd" 2>&1)
    if [ "$actual" = "$expected" ]; then
        echo "  PASS $name"
        PASS=$((PASS + 1))
    else
        echo "  FAIL $name"
        echo "    expected: $expected"
        echo "    actual:   $actual"
        FAIL=$((FAIL + 1))
    fi
}

zc() {  # compile zona source, output to project dir, run from there
    local src="$1"; shift
    ./zonac "$src" -o /tmp/_zc_test 2>&1
    /tmp/_zc_test "$@" 2>&1
    rm -f /tmp/_zc_test /tmp/_zc_test.s
}

# compile file.zona specially — needs CWD to be project root for relative paths
zc_pwd() {
    local src="$1"; shift
    ./zonac "$src" -o ./_zc_test 2>&1
    ./_zc_test "$@" 2>&1
    rm -f ./_zc_test ./_zc_test.s
}

file_expect() {
    local file="$1"; shift
    ./zona "$file" "$@" 2>&1
}

# --- core.zona ---------------------------------------------
echo "# core"
CORE=$(cat <<'EOF'
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
)
run "core-int"  "./zona tests/core.zona" "$CORE"
run "core-comp" "zc tests/core.zona" "$CORE"

# --- edge.zona ---------------------------------------------
echo "# edge"
EDGE_PASSES=64
actual=$(./zona tests/edge.zona 2>&1 | grep -c PASS)
[ "$actual" = "$EDGE_PASSES" ] && { echo "  PASS edge-int ($actual/$EDGE_PASSES)"; PASS=$((PASS+1)); } || { echo "  FAIL edge-int ($actual/$EDGE_PASSES)"; FAIL=$((FAIL+1)); }
actual=$(zc tests/edge.zona 2>&1 | grep -c PASS)
[ "$actual" = "$EDGE_PASSES" ] && { echo "  PASS edge-comp ($actual/$EDGE_PASSES)"; PASS=$((PASS+1)); } || { echo "  FAIL edge-comp ($actual/$EDGE_PASSES)"; FAIL=$((FAIL+1)); }

# --- cli.zona ----------------------------------------------
echo "# cli"
CLI=$(printf '2\nfoo\nbar')
run "cli-int"  "./zona tests/cli.zona foo bar" "$CLI"
run "cli-comp" "zc tests/cli.zona foo bar" "$CLI"

# --- file.zona ---------------------------------------------
echo "# file"
FILE_OUT=$(printf '72\n105\n10\n-1')
run "file-int"  "./zona tests/file.zona" "$FILE_OUT"
run "file-comp" "zc_pwd tests/file.zona" "$FILE_OUT"
rm -f tests/tmp.txt

# --- import.zona -------------------------------------------
echo "# import"
IMPORT=$(printf '7\n81\n3\n7\n0\nhello\nworld\n42')
run "import-int"  "./zona tests/import.zona" "$IMPORT"
run "import-comp" "zc tests/import.zona" "$IMPORT"

# --- :bind FFI (compiler only) -----------------------------
echo "# ffi"
cat > /tmp/_bind.zona << 'ZONA'
:bind myPuts 'puts' int char*
:bind myAbs 'abs' int int
'hello FFI' myPuts :drop
-42 myAbs .
ZONA
run "bind" "zc /tmp/_bind.zona" "$(printf 'hello FFI\n42')"

# --- peek/poke (compiler only) -----------------------------
cat > /tmp/_peek.zona << 'ZONA'
:bind cmalloc 'malloc' void* long
:bind cfree 'free' void void*
16 cmalloc
42 :over :poke32
99 :over 4 + :poke8
:dup :peek32 .
:dup 4 + :peek8 .
cfree
ZONA
run "peek" "zc /tmp/_peek.zona" "$(printf '42\n99')"

# --- validation --------------------------------------------
echo "# validation"

cat > /tmp/_v1.zona << 'ZONA'
:use './std/math.zona' extra
ZONA
run "v:use-extra" "./zona /tmp/_v1.zona 2>&1" "line 1: :use line must contain only :use and path"

cat > /tmp/_v2.zona << 'ZONA'
@ foo :bind bar 'bar' v ;
ZONA
run "v:bind-in-word" "./zona /tmp/_v2.zona 2>&1" "line 1: :bind cannot appear inside word 'foo'"

cat > /tmp/_v3.zona << 'ZONA'
@ foo :use './std/math.zona' ;
ZONA
run "v:use-in-word" "./zona /tmp/_v3.zona 2>&1" "line 1: :use cannot appear inside word 'foo'"

cat > /tmp/_v4.zona << 'ZONA'
:bind foo 'foo'
ZONA
run "v:bind-few" "./zonac /tmp/_v4.zona -o /tmp/_v4 2>&1" "line 1: :bind requires: name 'cname' retType [paramTypes...]"

cat > /tmp/_v5.zona << 'ZONA'
:bind foo
ZONA
run "v:bind-missing" "./zonac /tmp/_v5.zona -o /tmp/_v5 2>&1" "line 1: :bind requires: name 'cname' retType [paramTypes...]"

cat > /tmp/_v6.zona << 'ZONA'
:bind myPuts 'puts' int char*
42 .
ZONA
run "v:bind-ignored" "./zona /tmp/_v6.zona" "42"

# --- REPL --------------------------------------------------
echo "# repl"
run "repl" "printf '3 5 + .\n' | ./zona" "8"
run "repl-err" "printf '1 2\n+\nfoo\n' | ./zona" "line 3: unknown word: foo"

# --- cleanup & summary -------------------------------------
rm -f /tmp/_bind.zona /tmp/_peek.zona /tmp/_v*.zona
echo ""
echo "$((PASS + FAIL)) tests, $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && exit 0 || exit 1
