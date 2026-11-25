// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Describe.h"
#include "Core/Evaluator.h"

#include <iostream>
#include <vector>
#include <algorithm>

namespace Luwu::Dsl
{

// A table cell: | would end it
static std::string escapeCell(const std::string& text)
{
    std::string out;
    for (char c : text)
    {
        if (c == '|')
            out += '\\';
        out += c;
    }
    return out;
}

static void printSection(std::ostream& os, const std::string& title, std::vector<FormDoc>& items)
{
    if (items.empty())
        return;

    std::sort(items.begin(), items.end(), [](const FormDoc& a, const FormDoc& b) {
        return a.name < b.name;
    });

    os << "## " << title << "\n\n";
    os << "| Name | Syntax | Description |\n";
    os << "| :--- | :--- | :--- |\n";
    for (const auto& item : items)
    {
        os << "| `" << escapeCell(item.name) << "` | `" << escapeCell(item.syntax) << "` | " << escapeCell(item.desc) << " |\n";
    }
    os << "\n";
}

// The source syntax that isn't a form
static void getSyntaxDocs(std::vector<FormDoc>& out)
{
    out.push_back({"~", "~ <text>", "Doc comment: to the end of the line, kept and written into the generated C++ as a // comment (; comments stay Luna-only). Leading ones go on their own lines before a statement, a trailing one after code on the same line (it moves up to the statement when it lands on an argument of a statement starting on that line, e.g. (if (constant? a) ~ ...)."});
    out.push_back({"include", "(include \"<path>.luna\")", "Top-level only: splice in another file's definitions at this point. The path is relative to the including file and may not leave the root file's directory; each file is included once, and include cycles are errors"});
}

void printDocs(std::ostream& os, const Evaluator& evaluator)
{
    os << "# Luna DSL Forms Reference\n\n";

    std::vector<FormDoc> forms;
    evaluator.getFormDocs(forms);
    printSection(os, "Forms", forms);

    std::vector<FormDoc> syntax;
    getSyntaxDocs(syntax);
    printSection(os, "Source Syntax", syntax);
}

} // namespace Luwu::Dsl
