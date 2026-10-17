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

// An IR command declared with define-ircmd
struct IrCmdDef
{
    std::string name; // e.g. "ADD_INT"
    SourceLocation loc;
    std::vector<std::string> docs;     // doc comments (~) before the form: the enum entry's comment
    std::vector<std::string> operands; // operand names, positional (A..G)
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
