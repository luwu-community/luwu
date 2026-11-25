// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/DenseHash.h"
#include "Luau/NotNull.h"

#include <memory>
#include <string>
#include <vector>

namespace Luau
{

struct ModuleResolver;
struct RequireCycle;
struct Scope;
using ModuleName = std::string;

// Luwu never-returning calls: the call statements of a module whose callee is declared to return `never`, found before
// any type is solved. The data flow graph, the constraint generator, the type checker and the linter all ask
// `callNeverReturns`, so they agree on which calls end a code path.
//
// Upstream ends a code path only at `error(...)` and `assert(false)`, which it matches by name (`doesCallError`).
struct NeverReturningCalls
{
    DenseHashSet<const AstExprCall*> calls{nullptr};
};

// Finds the call statements under `root` whose callee resolves, without inference, to a function returning `never`:
// - a global declared in `globalScope` (definitions files, builtins);
// - a field of a declared global or of a required module, through any number of `.` or `:` indexes;
// - a local bound to one of the above, or a local function or function-typed local annotated to return `never`.
// A local or global that the module assigns to after its declaration doesn't count.
NeverReturningCalls findNeverReturningCalls(
    AstStatBlock* root,
    const std::shared_ptr<Scope>& globalScope,
    NotNull<ModuleResolver> moduleResolver,
    const ModuleName& moduleName,
    const std::vector<RequireCycle>& requireCycles
);

// Whether a call never returns: `error(...)`, `assert(false)`, or a call in `known`. Without `known`, only the first two.
bool callNeverReturns(const AstExprCall* call, const NeverReturningCalls* known);

} // namespace Luau
