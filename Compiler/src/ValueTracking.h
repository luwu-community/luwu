// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/DenseHash.h"

namespace Luau
{
class AstNameTable;
}

namespace Luau
{
namespace Compile
{

enum class Global
{
    Default = 0,
    Mutable, // builtin that has contents unknown at compile time, blocks GETIMPORT for chains
    Written, // written in the code which means we can't reason about the value
};

struct Variable
{
    AstExpr* init = nullptr; // initial value of the variable; filled by trackValues
    bool written = false;    // is the variable ever assigned to? filled by trackValues
    bool constant = false;   // is the variable's value a compile-time constant? filled by constantFold

    // Is the variable ever assigned to from a function nested inside the one that declares it?
    // Filled by trackValues.
    //
    // `written` says only that a write exists somewhere in the module. This says where it can run.
    // A write in the declaring function has a location in the source, so a region of that function
    // containing no write to the variable still holds whatever was true when it was entered.
    // A write from a nested function runs whenever that function is called, which no region rules out.
    //
    // Luwu Classes (rfcs/classes.md) uses this to keep a `class.isinstance` proof alive across writes
    // that cannot reach it (see Compiler::matchIsinstanceProvenLocal).
    bool writtenByNestedFunction = false;
};

void assignMutable(DenseHashMap<AstName, Global>& globals, const AstNameTable& names, const char* const* mutableGlobals);
void trackValues(
    DenseHashMap<AstName, Global>& globals,
    DenseHashMap<AstLocal*, Variable>& variables,
    DenseHashMap<AstName, AstLocal*>& classLocals,
    AstNode* root
);

inline Global getGlobalState(const DenseHashMap<AstName, Global>& globals, AstName name)
{
    const Global* it = globals.find(name);

    return it ? *it : Global::Default;
}

} // namespace Compile
} // namespace Luau
