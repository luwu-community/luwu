// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"
#include "Lowering/Emitter.h"

#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace Luwu::Dsl
{

// (lowering (<rule> <args>...)): the rule that lowers a command, with every rule parameter given explicitly
struct IrCmdLowering
{
    std::string rule;
    std::vector<SExpr> args;
    SourceLocation loc;
};

// [name :kind|:kind...]: an operand and the operand kinds it may have
struct IrOperandDef
{
    std::string name;
    std::vector<std::string> kinds; // e.g. {"Rn", "Kn"}
    SourceLocation loc;
};

// One [:key [operand :kinds]...] group of target-specific or flag-specific extra operand kinds
struct IrOperandExtras
{
    std::string key; // target ("x64") or flag ("DebugLuauAbortingChecks")
    SourceLocation loc;
    std::vector<IrOperandDef> operands;
};

// An IR command declared with define-ircmd
struct IrCmdDef
{
    std::string name; // e.g. "ADD_INT"
    SourceLocation loc;
    std::vector<std::string> docs;     // doc comments (~) before the form: the enum entry's comment
    std::vector<IrOperandDef> operands; // positional (A..G)
    // (target-specific [:x64 [a :Rn] ...] ...) and (flag-specific [:DebugLuauAbortingChecks [a :Rn] ...] ...): extra kinds
    // an operand may have when compiling for that target, or when that flag is enabled
    std::vector<IrOperandExtras> targetSpecific;
    std::vector<IrOperandExtras> flagSpecific;
    std::string result;                // IrValueKind enumerator ("Int", "Double", ...), empty for None
    std::string cls;                   // "value", "effect", "guard", "terminator" or "pseudo"
    std::set<std::string> flags;       // e.g. "deprecated/has-side-effects"
    std::set<std::string> heapEffects; // e.g. "invalidates-safe-env"
    std::optional<IrCmdLowering> lowering;
};

// Parses the define-ircmd forms of a program (other top-level forms are ignored) and checks them
std::vector<IrCmdDef> parseIrCmds(const std::vector<SExpr>& program);

// Pairs each command with the rule its lowering clause names, binding the rule's parameters to the arguments.
// `rules` are the (expanded) define-rules; the result follows rule order, then command order.
std::vector<LoweringInstance> linkLowerings(const std::vector<IrCmdDef>& cmds, const std::vector<Rule>& rules);

// Every command in declaration order, for the generated lowering dispatch
std::vector<DispatchEntry> loweringDispatch(const std::vector<IrCmdDef>& cmds);

// CodeGen/include/Luau/generated/IrCmdInfo.h: per-command information (names, result kinds, classification)
void emitIrCmdInfo(std::ostream& os, const std::vector<IrCmdDef>& cmds);

// CodeGen/include/Luau/generated/IrCmd.h: the IrCmd enum with its doc comments
void emitIrCmdEnum(std::ostream& os, const std::vector<IrCmdDef>& cmds);

} // namespace Luwu::Dsl
