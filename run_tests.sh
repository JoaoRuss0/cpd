#!/usr/bin/env bash
set -e

for input in ../../../inputs/*.in; do
  base="${input%.in}"
  echo "==> ${base##*/}"
  ./docs "$input" > "$base-new.out"
  diff -u "$base.out" "$base-new.out"
done

echo "All tests passed."