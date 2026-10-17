// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"
#include "Types.h"
#include "Describe.h"

#include <string>
#include <vector>
#include <ostream>

namespace Luwu::Dsl
{

struct RuleParam
{
    std::string name; // e.g. "pc-offset"
    std::string type; // e.g. "uint", "int", "reg", "label", "tag"
    SourceLocation loc;
};

struct Rule
{
    std::string name; // e.g. "set-savedpc"
    std::vector<RuleParam> params;
    std::vector<SExpr> body;
    SourceLocation loc;
    bool isSubrule = false;
    std::vector<std::string> docs; // doc comments (~) on the definition, written above the generated function
};

// A generated lower_<CMD> function: a define-rule instantiated for one IR command, each rule parameter bound to one of
// the command's operand slots (OP_A..OP_G) or to a literal
struct LoweringInstance
{
    std::string cmdName; // e.g. "ADD_INT"
    const Rule* rule = nullptr;
    std::vector<int> operandIndex; // per rule parameter: the operand slot, or -1 when bound to `literals[i]`
    std::vector<SExpr> literals;
};

// One case of the generated lowering dispatch, in IrCmd declaration order
struct DispatchEntry
{
    std::string cmdName;
    bool lowered = true; // false for pseudo-instructions, which are never lowered
};

std::vector<Rule> parseRules(const std::vector<SExpr>& program);
std::vector<Rule> expandRules(const std::vector<Rule>& rules);

void emitA64(std::ostream& os, const std::vector<LoweringInstance>& lowerings, const std::vector<DispatchEntry>& dispatch);
void emitX64(std::ostream& os, const std::vector<LoweringInstance>& lowerings, const std::vector<DispatchEntry>& dispatch);

void getA64Docs(std::vector<FormDoc>& ops, std::vector<FormDoc>& preds);
void getX64Docs(std::vector<FormDoc>& ops, std::vector<FormDoc>& preds);

// Targets Luna generates lowering for, as written in target-case branches and target-specific groups (:a64, :x64)
inline const std::vector<std::string>& knownTargets()
{
    static const std::vector<std::string> targets = {"a64", "x64"};
    return targets;
}

std::string toCppIdent(const std::string& name);
std::string toUpperIdent(const std::string& name);
std::string toCppVarName(const std::string& name);

} // namespace Luwu::Dsl
