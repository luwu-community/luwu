// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/ParseResult.h"

#include <string>
#include <vector>

namespace Luau
{

// Luwu object notation: `.luwon` data files are a static subset of Luwu. A file is `const` and `type` declarations
// ending in `return <value>`, and every value can be computed without running code.

// The calls a data file can make. They're resolved by name: a binding named `vector` doesn't change them.
enum class LuwonConstructor
{
    None,
    VectorCreate,
    BufferFromString,
    // A class named by a global, `Point(1, 2)`. Whoever builds runtime values from the file decides which classes it may
    // construct.
    Class,
};

// A class built into Luwu that a data file constructs by its library's name, `library.name(...)`, without the reader
// allowing it. Its constructor takes `fields` in order, and writing an instance reads them back.
struct LuwonBuiltinClass
{
    const char* library;
    const char* name;
    std::vector<const char*> fields;
};

// The builtin classes the enabled features provide.
std::vector<LuwonBuiltinClass> luwonBuiltinClasses();

LuwonConstructor luwonConstructorOf(const AstExprCall* call);

// The name a `Class` call constructs, as the file writes it.
std::string luwonClassNameOf(const AstExprCall* call);

// Reports everything in a parsed data file that's outside the notation, one error per construct.
void checkLuwonSyntax(AstStatBlock* root, std::vector<ParseError>& errors);

} // namespace Luau
