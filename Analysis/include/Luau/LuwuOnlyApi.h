// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <string_view>

namespace Luau
{

// Luwu Integers (rfcs/integers.md): the members of the `integer` library upstream Luau 0.730's didn't have
inline bool isLuwuOnlyIntegerMember(std::string_view member)
{
    static const std::string_view kMembers[] = {
        "Mode", "dynamic", "i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64", "suffixes", "cast", "mode", "isbig", "signum", "sadd",
        "ssub", "smul", "cadd", "csub", "cmul", "cdiv", "pow", "spow", "cpow", "sqrt", "log2", "log10", "popcount",
    };
    for (std::string_view name : kMembers)
    {
        if (member == name)
            return true;
    }
    return false;
}

// Luwu: whether `library.member` is a member Luwu adds to a library upstream Luau also has (`table.discard`, `class.of`). A
// Luau or Lua module can't use one: the LuwuOnlyApi lint reports it there, and autocomplete doesn't offer it. A string
// method (`s:startswith`) is a member of `string`. Every Luwu-only API goes here when it's added.
inline bool isLuwuOnlyMember(std::string_view library, std::string_view member)
{
    // Upstream's `class` library has `isinstance` too, and `classof` where Luwu has `of`
    if (library == "class")
        return member == "of" || member == "name" || member == "implements" || member == "fields";
    if (library == "table")
        return member == "discard";
    if (library == "buffer")
        return member == "isfrozen";
    // Luwu String Extensions (rfcs/string-extensions.md)
    if (library == "string")
        return member == "trim" || member == "trimstart" || member == "trimend" || member == "startswith" || member == "endswith" ||
               member == "contains" || member == "replace";
    // Luwu try expressions
    if (library == "types")
        return member == "raises";
    // Luwu Integers (rfcs/integers.md): the members upstream Luau 0.730's `integer` library didn't have
    if (library == "integer")
        return isLuwuOnlyIntegerMember(member);
    return false;
}

// Luwu: whether `name` is a global library, class or value only Luwu has, such as `collections`. The LuwuOnlyApi lint
// reports it in Luau and Lua modules, and autocomplete doesn't offer it there.
inline bool isLuwuOnlyGlobal(std::string_view name)
{
    // Luwu try expressions: options and results
    if (name == "Ok" || name == "Err" || name == "Some" || name == "None")
        return true;
    // Luwu structured runtime errors: the error variants and the globals that raise them
    if (name == "Error" || name == "unreachable" || name == "todo")
        return true;
    return name == "collections" || name == "bundle";
}

} // namespace Luau
