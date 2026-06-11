#!/usr/bin/env bash
set -e

MAKE_DIR="./omp/src"
MAKE_TARGET=""
DEFAULT_BINARY="./omp/src/docs-omp"

if [ $# -eq 0 ]; then
    echo "No binary provided. Building with make..."
    if [ -n "$MAKE_TARGET" ]; then
        make -C "$MAKE_DIR" "$MAKE_TARGET"
    else
        make -C "$MAKE_DIR"
    fi
    binary="$DEFAULT_BINARY"
    test_args=()
elif [ "$1" = "mpirun" ] || [ "$1" = "mpiexec" ]; then
    # Consume everything up to and including the actual binary
    # e.g. mpirun -n 4 ./docs-mpi
    cmd=()
    while [ $# -gt 0 ]; do
        cmd+=("$1")
        # Stop after we hit something that looks like the binary (starts with ./ or /)
        if [[ "$1" == ./* || "$1" == /* ]] && [ "${#cmd[@]}" -gt 1 ]; then
            shift
            break
        fi
        shift
    done
    binary=("${cmd[@]}")
    test_args=("$@")
else
    binary=("$1")
    shift
    test_args=("$@")
fi

echo "Running tests..."

if [ "${#test_args[@]}" -eq 0 ]; then
    for dir in tests/T*/; do
        infile=$(echo "$dir"*.in)
        outfile=$(echo "$dir"*.out)

        echo "==> $infile"
        "${binary[@]}" "$infile" | diff -u "$outfile" -
    done
else
    for n in "${test_args[@]}"; do
        dir="tests/T$(printf "%02d" "$n")/"
        infile=$(echo "$dir"*.in)
        outfile=$(echo "$dir"*.out)

        echo "==> $infile"
        "${binary[@]}" "$infile" | diff -u "$outfile" -
    done
fi

echo "All tests passed."
