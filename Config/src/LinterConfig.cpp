// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/LinterConfig.h"

#include "Luau/ParseResult.h"

namespace Luau
{

void LintOptions::setDefaults()
{
    // By default, we enable all warnings
    warningMask.set();

    // Luwu: except the ones a module or codebase has to ask for (`--!lint Name`, or the config), because they're about
    // style and would flag most existing code
    warningMask.reset(LintWarning::Code_ConstLocal);
}

const char* LintWarning::getName(Code code)
{
    LUAU_ASSERT(unsigned(code) < Code__Count);

    return kWarningNames[code];
}

LintWarning::Code LintWarning::parseName(const char* name)
{
    for (int code = Code_Unknown; code < Code__Count; ++code)
        if (strcmp(name, getName(Code(code))) == 0)
            return Code(code);

    return Code_Unknown;
}

bool LintWarning::isAllName(const char* name)
{
    return strcmp(name, "All") == 0;
}

LintMask LintWarning::parseEnableMask(const std::vector<HotComment>& hotcomments)
{
    LintMask result;

    for (const HotComment& hc : hotcomments)
    {
        if (!hc.header || hc.content.compare(0, 4, "lint") != 0)
            continue;

        // `--!lint` needs a lint name after a space; the CommentDirective lint reports what's wrong otherwise
        size_t name = hc.content.find_first_not_of(" \t", 4);
        if (name == std::string::npos || name == 4)
            continue;

        LintWarning::Code code = LintWarning::parseName(hc.content.c_str() + name);
        if (code != LintWarning::Code_Unknown)
            result.set(code);
    }

    return result;
}

LintMask LintWarning::parseMask(const std::vector<HotComment>& hotcomments)
{
    LintMask result;
    bool disablesEverything = false;

    for (const HotComment& hc : hotcomments)
    {
        if (!hc.header)
            continue;

        if (hc.content.compare(0, 6, "nolint") != 0)
            continue;

        size_t name = hc.content.find_first_not_of(" \t", 6);

        // --!nolint disables everything
        // Luwu: except BareNolint, which asks whether that was meant; `--!nolint All` turns that off too, in any position,
        // so the loop keeps reading after this one instead of returning
        if (name == std::string::npos)
        {
            disablesEverything = true;
            continue;
        }

        // --!nolint needs to be followed by a whitespace character
        if (name == 6)
            continue;

        // Luwu: every lint, on purpose
        if (LintWarning::isAllName(hc.content.c_str() + name))
        {
            result.set();
            continue;
        }

        // --!nolint name disables the specific lint
        LintWarning::Code code = LintWarning::parseName(hc.content.c_str() + name);

        if (code != LintWarning::Code_Unknown)
            result.set(code);
    }

    if (disablesEverything)
        result |= LintMask().set().reset(LintWarning::Code_BareNolint);

    return result;
}

} // namespace Luau
