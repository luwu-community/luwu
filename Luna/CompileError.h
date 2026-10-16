// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"

#include <stdexcept>
#include <string>

namespace Luwu::Dsl
{

class CompileError : public std::runtime_error
{
public:
    CompileError(const SourceLocation& loc, const std::string& message)
        : std::runtime_error(formatError(loc, message)), loc_(loc), rawMessage_(message)
    {
    }

    CompileError(const SExpr& expr, const std::string& message)
        : CompileError(expr.loc, message)
    {
    }

    const SourceLocation& loc() const { return loc_; }
    const std::string& rawMessage() const { return rawMessage_; }

private:
    static std::string formatError(const SourceLocation& loc, const std::string& message)
    {
        std::string s = loc.toString();
        if (!s.empty())
            s += ": ";
        s += "error: " + message;
        return s;
    }

    SourceLocation loc_;
    std::string rawMessage_;
};

} // namespace Luwu::Dsl
