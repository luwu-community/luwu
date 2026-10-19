#!/bin/bash
# Instantiates tests/microops-cases.luna for both targets and compiles the C++ inside IrLoweringA64.cpp and
# IrLoweringX64.cpp (with clang, syntax only). Run from anywhere; prints the compiler's errors and warnings.
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
cp -r "$R/Luna/defs" "$W/defs"
cp "$R/Luna/tests/microops-cases.luna" "$W/cases.luna"
cat > "$W/harness.luna" <<'LUNA'
(include "defs/luwu.luna")
(include "cases.luna")
(display "//// a64\n")
(emit-microop-tests :a64 "A64")
(display "//// x64\n")
(emit-microop-tests :x64 "X64")
(display "//// end\n")
LUNA
(cd "$W" && "$R/build/debug/luwu-dsl-compiler" --eval harness.luna > out.txt)
awk -v W="$W" '/^\/\/\/\/ a64/{f=W"/MoA64.inc";next} /^\/\/\/\/ x64/{f=W"/MoX64.inc";next} /^\/\/\/\/ end/{f="";next} f{print > f}' "$W/out.txt"
status=0
for t in A64 X64; do
  python3 - "$R" "$W" "$t" <<'PY'
import sys
R, W, t = sys.argv[1:]
src = open(f'{R}/CodeGen/src/IrLowering{t}.cpp').read()
inc = open(f'{W}/Mo{t}.inc').read()
marker = f'#include "generated/LoweringRules{t}.h"'
src = src.replace(marker, marker + '\nnamespace Luau { namespace CodeGen { namespace ' + t + ' {\n' + inc + '\n} } }\n', 1)
open(f'{W}/IrLowering{t}.cpp', 'w').write(src)
PY
  n=$(grep -c "^inline void mo_" "$W/Mo$t.inc")
  if out=$(clang++ -std=c++17 -fsyntax-only -Wall -I"$R/CodeGen/src" -I"$R/Common/include" -I"$R/CodeGen/include" -I"$R/VM/include" -I"$R/VM/src" "$W/IrLowering$t.cpp" 2>&1) && [ -z "$out" ]; then
    echo "$t: $n test functions compile"
  else
    echo "$out" | grep -E "error|warning" | head -40
    status=1
  fi
done
exit $status
