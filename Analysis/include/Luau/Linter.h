// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/LinterConfig.h"
#include "Luau/Location.h"

#include <memory>
#include <string>
#include <vector>

namespace Luau
{

struct AstName;
class AstStat;
class AstNameTable;
struct TypeChecker;
struct Module;
struct Comment;

using ScopePtr = std::shared_ptr<struct Scope>;

struct LintResult
{
    std::vector<LintWarning> errors;
    std::vector<LintWarning> warnings;
};

std::vector<LintWarning> lint(
    AstStat* root,
    const AstNameTable& names,
    const ScopePtr& env,
    const Module* module,
    const std::vector<HotComment>& hotcomments,
    const LintOptions& options,
    // Luwu: the module's comments, for lints that read them (AnyCast takes a comment as the reason for a cast)
    const std::vector<Comment>* comments = nullptr
);

std::vector<AstName> getDeprecatedGlobals(const AstNameTable& names);

} // namespace Luau
