// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <string_view>

namespace Luau
{

// Luwu: whether `library.member` is a member Luwu adds to a library upstream Luau also has (`table.drop`, `class.of`). A
// Luau or Lua module can't use one: the LuwuOnlyApi lint reports it there, and autocomplete doesn't offer it. A string
// method (`s:startswith`) is a member of `string`. Every Luwu-only API goes here when it's added.
inline bool isLuwuOnlyMember(std::string_view library, std::string_view member)
{
    // Upstream's `class` library has `isinstance` too, and `classof` where Luwu has `of`
    if (library == "class")
        return member == "of" || member == "name" || member == "implements" || member == "fields";
    if (library == "table")
        return member == "drop";
    if (library == "buffer")
        return member == "isfrozen";
    // Luwu String Extensions (rfcs/string-extensions.md)
    if (library == "string")
        return member == "trim" || member == "trimstart" || member == "trimend" || member == "startswith" || member == "endswith" ||
               member == "contains" || member == "replace";
    return false;
}

} // namespace Luau
