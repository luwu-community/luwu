// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <string_view>

namespace Luau
{

// Luwu Batched Buffer Read/Write (rfcs/buffer-batched.md): `buffer.pack*` and `buffer.unpack*` for each number format
inline bool isBatchedBufferMember(std::string_view member)
{
    for (std::string_view format : {"i8", "u8", "i16", "u16", "i32", "u32", "f32", "f64"})
    {
        bool endsWithFormat = member.size() > format.size() && member.substr(member.size() - format.size()) == format;
        if (!endsWithFormat)
            continue;

        std::string_view verb = member.substr(0, member.size() - format.size());
        return verb == "pack" || verb == "unpack";
    }
    return false;
}

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
    if (library == "buffer" && isBatchedBufferMember(member))
        return true;
    if (library == "buffer")
        return member == "isfrozen" || member == "memcmp";
    // Luwu String Extensions (rfcs/string-extensions.md)
    if (library == "string")
        return member == "trim" || member == "trimstart" || member == "trimend" || member == "startswith" || member == "endswith" ||
               member == "contains" || member == "replace";
    return false;
}

// Luwu: whether `name` is a global library or function only Luwu has, such as `collections`. The LuwuOnlyApi lint
// reports it in Luau and Lua modules, and autocomplete doesn't offer it there.
inline bool isLuwuOnlyGlobal(std::string_view name)
{
    return name == "collections";
}

} // namespace Luau
