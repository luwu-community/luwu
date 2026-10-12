// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/DenseHash.h"

#include <vector>

namespace Luau
{

enum class Mode
{
    NoCheck,    // Do not perform any inference
    Nonstrict,  // Unannotated symbols are any
    Strict,     // Unannotated symbols are inferred
    Definition, // Type definition module, has special parsing rules
};

struct FragmentParseResumeSettings
{
    DenseHashMap<AstName, AstLocal*> localMap{AstName()};
    std::vector<AstLocal*> localStack;
    Position resumePosition;
};

struct ParseOptions
{
    bool allowDeclarationSyntax = false;
    bool captureComments = false;
    std::optional<FragmentParseResumeSettings> parseFragment = std::nullopt;
    bool storeCstData = false;
    bool noErrorLimit = false;

    // Luwu: the module is Luau or Lua code rather than Luwu (a `.luau` or `.lua` file), so Luwu syntax in it is an
    // error even when its FFlag is on. Upstream has no such option: every module is Luau.
    bool luauModule = false;
    // Luwu: the errors are shown by static analysis (an editor, `luwu-analyze`), which has room for a 'Help' section,
    // rather than raised by a runtime compiling the module.
    bool analysisErrors = false;
};

} // namespace Luau
