#!/usr/bin/env bash
set -e

echo "Running tests..."

for input in inputs/*.in; do
  base="${input%.in}"
  echo "==> ${base##*/}"

  ./$1 "$input" > "$base-new.out"
  diff -u "$base.out" "$base-new.out"
  rm "$base-new.out"
done

echo "All tests passed."