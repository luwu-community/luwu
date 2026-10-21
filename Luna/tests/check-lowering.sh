#!/bin/bash
# Regenerates and syntax-checks both targets. Generation errors must fail the check.
set -euo pipefail
R=$(cd "$(dirname "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
OUT=${1:-$W/generated}
mkdir -p "$OUT"
(cd "$R/Luna" && "$R/build/debug/luwu-dsl-compiler" defs/luwu.luna "$W" --lowering-output "$OUT") | sed '/^luwu-dsl-compiler: compiled/d'
status=0
for t in A64 X64; do
  mkdir -p "$W/src/generated"
  cp "$OUT/LoweringRules$t.h" "$W/src/generated/"
  cp "$R/CodeGen/src/IrLowering$t.cpp" "$W/src/"
  if out=$(clang++ -std=c++17 -fsyntax-only -Wall -I"$W/src" -I"$R/CodeGen/src" -I"$R/Common/include" -I"$R/CodeGen/include" -I"$R/VM/include" -I"$R/VM/src" "$W/src/IrLowering$t.cpp" 2>&1) && [ -z "$out" ]; then
    echo "$t: $(grep -c '^    case IrCmd::' "$OUT/LoweringRules$t.h") IR cases compile"
  else
    echo "$out" | grep -E "error|warning" | head -40
    status=1
  fi
done
exit $status
