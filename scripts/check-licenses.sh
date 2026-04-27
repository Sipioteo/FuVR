#!/usr/bin/env bash
# Fail if any source file is missing the SPDX Apache-2.0 token in its first 5 lines.
set -euo pipefail

cd "$(dirname "$0")/.."

TOKEN='SPDX-License-Identifier: Apache-2.0'

# Source extensions we enforce. Keep in sync with the repo's language stack.
exts=(cpp hpp h mm swift kt rs capnp)

# Path patterns to skip (generated bindings, build dirs, vendored deps).
should_skip() {
  case "$1" in
    */proto/gen/*) return 0 ;;
    */gen/*)       return 0 ;;
    *.build/*)     return 0 ;;
    */build/*)     return 0 ;;
    */target/*)    return 0 ;;
    */.git/*)      return 0 ;;
    */node_modules/*) return 0 ;;
    */DerivedData/*) return 0 ;;
  esac
  return 1
}

violations=0
checked=0

while IFS= read -r -d '' file; do
  if should_skip "$file"; then
    continue
  fi
  checked=$((checked + 1))
  if ! head -n 5 "$file" | grep -qF "$TOKEN"; then
    echo "missing SPDX header: $file"
    violations=$((violations + 1))
  fi
done < <(
  find_args=()
  for ext in "${exts[@]}"; do
    find_args+=( -name "*.${ext}" -o )
  done
  unset 'find_args[${#find_args[@]}-1]'
  find . -type f \( "${find_args[@]}" \) -print0
)

echo "checked $checked file(s)"
if [ "$violations" -gt 0 ]; then
  echo "license check failed: $violations file(s) missing '$TOKEN'" >&2
  exit 1
fi
echo "license check OK"
