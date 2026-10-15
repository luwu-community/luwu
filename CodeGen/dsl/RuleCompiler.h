// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "TargetBuilder.h"
#include "Emitter.h"

#include <vector>
#include <ostream>

namespace Luwu::Dsl
{

Operand evalExpr(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent);

std::pair<std::string, std::string> evalMem(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& memExpr, const std::string& destReg, const std::string& indent);

void compileRules(std::ostream& os, TargetBuilder& target, const std::vector<Rule>& rules);

} // namespace Luwu::Dsl
