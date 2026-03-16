#!/usr/bin/env bash

TEST="T06" # <--Change this value

BINARY="../g3/omp/src/docs-omp"

input=$(find "$TEST" -name "*.in")
expected=$(find "$TEST" -name "*.out")

produced="$TEST/result.out"
timefile="$TEST/time.txt"

echo "Running $TEST..."

"$BINARY" "$input" > "$produced" 2> "$timefile"

echo
echo "Diff:"
diff "$produced" "$expected"

echo
echo "Execution time:"
cat "$timefile"