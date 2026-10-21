#!/bin/bash
# Generates the IR command headers (IrCmd.h, IrCmdInfo.h, IrCmdEffects.h) and compares them byte for byte with the
# committed ones in CodeGen/include/Luau/generated. Run from anywhere.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/out"
(cd "$R/Luna" && "$R/build/debug/luwu-dsl-compiler" defs/luwu.luna "$W/out" > /dev/null)
status=0
for header in IrCmd.h IrCmdInfo.h IrCmdEffects.h; do
  if cmp -s "$W/out/$header" "$R/CodeGen/include/Luau/generated/$header"; then
    echo "$header: identical"
  else
    diff "$W/out/$header" "$R/CodeGen/include/Luau/generated/$header" | head -20
    status=1
  fi
done
exit $status
