// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <ostream>
#include <string>
#include <vector>

namespace Luwu::Dsl
{

struct FormDoc
{
    std::string name;
    std::string syntax;
    std::string desc;
};

class Evaluator;

// The --describe reference: the forms the evaluator has (with its modules installed) and the source syntax
void printDocs(std::ostream& os, const Evaluator& evaluator);

} // namespace Luwu::Dsl
