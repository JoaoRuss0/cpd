#!/usr/bin/env bash
set -e

echo "Compiling..."

g++-15 -fopenmp g3/serial/src/docs.cpp -o docs

echo "Running tests..."

for input in inputs/*.in; do
  base="${input%.in}"
  echo "==> ${base##*/}"

  ./docs "$input" > "$base-new.out"
  diff -u "$base.out" "$base-new.out"
done

echo "All tests passed."