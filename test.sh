#!/usr/bin/env bash
set -e

echo "Running tests..."

binary=$1
shift

if [ "$#" -eq 0 ]; then
    for dir in tests/T*/; do
        infile=$(echo "$dir"*.in)
        outfile=$(echo "$dir"*.out)

        echo "==> $infile"
        "$binary" "$infile" | diff -u "$outfile" -
    done
else
    for n in "$@"; do
        dir="tests/T$(printf "%02d" "$n")/"
        infile=$(echo "$dir"*.in)
        outfile=$(echo "$dir"*.out)

        echo "==> $infile"
        "$binary" "$infile" | diff -u "$outfile" -
    done
fi

echo "All tests passed."