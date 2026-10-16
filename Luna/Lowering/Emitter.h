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
};

std::vector<Rule> parseRules(const std::vector<SExpr>& program);
std::vector<Rule> expandRules(const std::vector<Rule>& rules);

void emitA64(std::ostream& os, const std::vector<Rule>& rules);
void emitX64(std::ostream& os, const std::vector<Rule>& rules);

void getA64Docs(std::vector<FormDoc>& ops, std::vector<FormDoc>& preds);
void getX64Docs(std::vector<FormDoc>& ops, std::vector<FormDoc>& preds);

std::string toCppIdent(const std::string& name);
std::string toUpperIdent(const std::string& name);
std::string toCppVarName(const std::string& name);

} // namespace Luwu::Dsl
