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

struct TypeDoc
{
    std::string name;
    std::string context;
    std::string desc;
};

void printDocs(std::ostream& os, const std::string& archFilter = "");

} // namespace Luwu::Dsl
