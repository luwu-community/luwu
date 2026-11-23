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

// Luwu: the language a module is written in. Luwu code lives in `.luwu` files; Luau and Lua files are checked too. Luwu
// syntax is an error in Luau and Lua modules, and some diagnostics depend on the language (a Luwu-only API in a Luau
// module is a lint). The values match lua_Language in luacode.h.
enum class Language
{
    Luwu = 0,
    Luau = 1,
    Lua = 2,
};

struct ParseOptions
{
    bool allowDeclarationSyntax = false;
    bool captureComments = false;
    std::optional<FragmentParseResumeSettings> parseFragment = std::nullopt;
    bool storeCstData = false;
    bool noErrorLimit = false;

    // Luwu: the module's language. Luwu syntax in a Luau or Lua module is an error, even when its FFlag is on. Upstream
    // has no such option: every module is Luau.
    Language language = Language::Luwu;
    // Luwu: the errors are shown by static analysis (an editor, `luwu-analyze`), which has room for a 'Help' section,
    // rather than raised by a runtime compiling the module.
    bool analysisErrors = false;
};

} // namespace Luau
