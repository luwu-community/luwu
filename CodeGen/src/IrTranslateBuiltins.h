// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

namespace Luau
{
namespace CodeGen
{

struct IrBuilder;
struct IrOp;

enum class BuiltinImplType
{
    None,
    UsesFallback, // Uses fallback for unsupported cases
    Full,         // Is either implemented in full, or exits to VM
};

struct BuiltinImplResult
{
    BuiltinImplType type;
    int actualResultCount;
};

BuiltinImplResult translateBuiltin(
    IrBuilder& build,
    int bfid,
    int ra,
    int arg,
    IrOp args,
    IrOp arg3,
    int nparams,
    int nresults,
    IrOp fallback,
    int pcpos
);

// The tag a builtin's first argument must have for its fast path to run, or -1 when it takes any first argument
int builtinFirstArgumentTag(int bfid);

} // namespace CodeGen
} // namespace Luau
