// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/TypeFormat.h"

#include <cmath>

namespace Luau
{

// larger than any width or length worth asking for, and small enough for a size_t
constexpr double kMaxTypeFormatCount = 1e6;

static std::optional<size_t> count(const TypeFormatValue& value)
{
    const double* number = get_if<double>(&value);
    if (!number || *number < 0 || *number > kMaxTypeFormatCount || std::floor(*number) != *number)
        return std::nullopt;
    return size_t(*number);
}

std::optional<std::string> setTypeFormatOption(TypeFormatOptions& options, std::string_view key, const TypeFormatValue& value)
{
    auto boolean = [&](bool& field) -> std::optional<std::string>
    {
        const bool* b = get_if<bool>(&value);
        if (!b)
            return "'" + std::string(key) + "' should be a boolean";
        field = *b;
        return std::nullopt;
    };
    auto number = [&](size_t& field) -> std::optional<std::string>
    {
        std::optional<size_t> n = count(value);
        if (!n)
            return "'" + std::string(key) + "' should be a whole number, 0 or more";
        field = *n;
        return std::nullopt;
    };

    if (key == "expand")
    {
        const std::string* s = get_if<std::string>(&value);
        if (s && *s == "none")
            options.expand = TypeFormatOptions::Expand::None;
        else if (s && *s == "root")
            options.expand = TypeFormatOptions::Expand::Root;
        else if (s && *s == "all")
            options.expand = TypeFormatOptions::Expand::All;
        else
            return "'expand' should be \"none\", \"root\" or \"all\"";
        return std::nullopt;
    }
    if (key == "multiline")
        return boolean(options.multiline);
    if (key == "indent")
        return number(options.indent);
    if (key == "parameternames")
        return boolean(options.parameterNames);
    if (key == "comments")
    {
        if (const bool* b = get_if<bool>(&value))
        {
            options.commentWidth = *b ? kDefaultTypeFormatCommentWidth : 0;
            return std::nullopt;
        }
        std::optional<size_t> n = count(value);
        if (!n)
            return "'comments' should be a boolean or a width in characters";
        options.commentWidth = *n;
        return std::nullopt;
    }
    if (key == "maxlength")
        return number(options.maxLength);

    return "'" + std::string(key) + "' isn't an option; the options are expand, multiline, indent, parameternames, comments and maxlength";
}

void applyTypeFormatOptions(const TypeFormatOptions& format, ToStringOptions& options)
{
    options.alwaysExpandRootAlias = format.expand != TypeFormatOptions::Expand::None;
    options.expandAliases = format.expand == TypeFormatOptions::Expand::All;
    options.useLineBreaks = format.multiline;
    options.indentSize = format.indent;
    options.functionTypeArguments = format.parameterNames;
    options.propertyCommentWidth = format.commentWidth;
    if (format.maxLength > 0)
    {
        options.maxTypeLength = format.maxLength;
        options.maxTableLength = format.maxLength;
    }
}

std::string formatType(TypeId ty, const TypeFormatOptions& format)
{
    ToStringOptions options;
    applyTypeFormatOptions(format, options);
    return toString(ty, options);
}

} // namespace Luau
