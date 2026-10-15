// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "TargetBuilder.h"
#include "Emitter.h"
#include "CodeWriter.h"
#include "CompileError.h"

#include <vector>
#include <ostream>

namespace Luwu::Dsl
{

Operand evalExpr(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg);

std::pair<std::string, std::string> evalMem(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& memExpr, const std::string& destReg);

void compileRules(std::ostream& os, TargetBuilder& target, const std::vector<Rule>& rules);

} // namespace Luwu::Dsl
