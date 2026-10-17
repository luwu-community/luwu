// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "TargetBuilder.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Luwu::Dsl
{

using TemplateOpTable = std::unordered_map<std::string_view, TemplateOpSpec>;

// Number of arguments a template refers to (highest placeholder index + 1)
size_t templateArity(const char* fmt);

// Substitutes the arguments into a template (see TemplateOpSpec)
std::string expandOpTemplate(EvalContext& ctx, const TargetBuilder& target, const SExpr& expr, const char* fmt, const std::vector<Operand>& args);

// Architecture-independent template operations (vm-reg, double-val, set-label!, ...)
const TemplateOpSpec* findCoreTemplateOp(const std::string& name);

void getTemplateDocs(const TemplateOpTable& table, std::vector<FormDoc>& ops, std::vector<FormDoc>& values);
void getCoreTemplateDocs(std::vector<FormDoc>& ops, std::vector<FormDoc>& values);

} // namespace Luwu::Dsl
