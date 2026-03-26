#!/usr/bin/env bash
set -e

MAKE_DIR="./g3/omp/src"                   # directory where the Makefile is
MAKE_TARGET=""                            # e.g. "project" or leave empty for default target
DEFAULT_BINARY="./g3/omp/src/docs-omp"    # binary produced by make


if [ $# -eq 0 ]; then
    echo "No binary provided. Building with make..."

    if [ -n "$MAKE_TARGET" ]; then
        make -C "$MAKE_DIR" "$MAKE_TARGET"
    else
        make -C "$MAKE_DIR"
    fi

    binary="$DEFAULT_BINARY"
else
    binary=$1
    shift
fi

echo "Running tests..."

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