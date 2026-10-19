#!/bin/bash
# Generates the micro-op pipeline's LoweringRules{A64,X64}.h into a directory (default ~/luna-plan/generated) and
# compiles them in place of CodeGen's frozen ones (clang, syntax only). Prints the compiler's errors and warnings.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
OUT=${1:-$HOME/luna-plan/generated}
mkdir -p "$OUT"
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
(cd "$R/Luna" && "$R/build/debug/luwu-dsl-compiler" defs/luwu.luna "$W" --lowering-output "$OUT" | grep -v "^luwu-dsl-compiler: compiled") || true
status=0
for t in A64 X64; do
  mkdir -p "$W/src/generated"
  cp "$OUT/LoweringRules$t.h" "$W/src/generated/"
  cp "$R/CodeGen/src/IrLowering$t.cpp" "$W/src/"
  if out=$(clang++ -std=c++17 -fsyntax-only -Wall -I"$W/src" -I"$R/CodeGen/src" -I"$R/Common/include" -I"$R/CodeGen/include" -I"$R/VM/include" -I"$R/VM/src" "$W/src/IrLowering$t.cpp" 2>&1) && [ -z "$out" ]; then
    echo "$t: $(grep -c '^inline void lower_' "$OUT/LoweringRules$t.h") lowering functions compile"
  else
    echo "$out" | grep -E "error|warning" | head -40
    status=1
  fi
done
exit $status
