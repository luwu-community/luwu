// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/ToString.h"
#include "Luau/Variant.h"

#include <optional>
#include <string>
#include <string_view>

namespace Luau
{

// Luwu: how a type is printed, by `types.format` (`tostring(type)` is `types.format` with these defaults) and by
// luwu-lsp's hovers. Set from a table of the same keys -- `types.format`'s options, or the JSON in luwu-lsp's
// settings -- through setTypeFormatOption, so the two can't drift apart.
struct TypeFormatOptions
{
    enum class Expand
    {
        // every alias prints as its name
        None,
        // the type itself prints as its structure even when it's an alias, the aliases inside it by name (a hover)
        Root,
        // every alias prints as its structure (a recursive one still by name where it refers to itself)
        All,
    };

    Expand expand = Expand::Root;
    // tables one property per line, long unions and function signatures broken up
    bool multiline = false;
    // spaces per indentation level, when multiline
    size_t indent = 4;
    // `(path: string) -> ()` rather than `(string) -> ()`
    bool parameterNames = true;
    // when multiline, each table property's doc comment's first line above it, cut to this many characters; 0 for none
    size_t commentWidth = 0;
    // past this many characters the type is cut short; 0 for the default limit
    size_t maxLength = 0;
};

using TypeFormatValue = Variant<bool, double, std::string>;

// The comment width `comments = true` means
constexpr size_t kDefaultTypeFormatCommentWidth = 80;

// Sets the option `key` (`expand`, `multiline`, `indent`, `parameternames`, `comments`, `maxlength`) to `value`, or says
// why it can't
std::optional<std::string> setTypeFormatOption(TypeFormatOptions& options, std::string_view key, const TypeFormatValue& value);

void applyTypeFormatOptions(const TypeFormatOptions& format, ToStringOptions& options);

std::string formatType(TypeId ty, const TypeFormatOptions& format);

} // namespace Luau
