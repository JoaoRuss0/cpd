#!/usr/bin/env bash
set -u

SRC_DIR="../g3/omp/src"
BINARY="$SRC_DIR/docs-omp"

echo "Compiling using Makefile..."
make -C "$SRC_DIR"

if [ $? -ne 0 ]; then
    echo "Compilation failed."
    exit 1
fi

echo
echo "Running tests..."
echo

total=0
passed=0
failed=0

for test_dir in T*; do
    [ -d "$test_dir" ] || continue

    total=$((total + 1))

    input=$(find "$test_dir" -maxdepth 1 -name "*.in")
    expected=$(find "$test_dir" -maxdepth 1 -name "*.out" ! -name "result.out")

    produced="$test_dir/result.out"
    timefile="$test_dir/time.txt"

    echo "Running $test_dir"

    "$BINARY" "$input" > "$produced" 2> "$timefile"

    if diff "$produced" "$expected" > /dev/null; then
        echo "PASS"
        passed=$((passed + 1))
    else
        echo "FAIL"
        diff "$produced" "$expected"
        failed=$((failed + 1))
    fi

    echo "Execution time:"
    cat "$timefile"
    echo
done

echo "Summary"
echo "Total: $total"
echo "Passed: $passed"
echo "Failed: $failed"