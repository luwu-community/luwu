// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <iostream>

namespace Luwu::Dsl
{

enum class SExprKind
{
    Symbol,
    Keyword,     // starts with ':'
    Number,
    String,
    List,        // (...)
    BracketList, // [...]
};

struct SExpr
{
    SExprKind kind = SExprKind::Symbol;
    std::string text;
    int64_t intValue = 0;
    std::vector<SExpr> elements;
    int line = 1;
    int col = 1;

    bool isSymbol() const { return kind == SExprKind::Symbol; }
    bool isKeyword() const { return kind == SExprKind::Keyword; }
    bool isNumber() const { return kind == SExprKind::Number; }
    bool isString() const { return kind == SExprKind::String; }
    bool isList() const { return kind == SExprKind::List || kind == SExprKind::BracketList; }

    bool isSymbol(const std::string& s) const { return isSymbol() && text == s; }
    bool isKeyword(const std::string& k) const { return isKeyword() && text == k; }

    bool isList(const std::string& headSymbol) const
    {
        return isList() && !elements.empty() && elements[0].isSymbol(headSymbol);
    }

    void dump(std::ostream& os) const
    {
        switch (kind)
        {
        case SExprKind::Symbol:
        case SExprKind::Keyword:
            os << text;
            break;
        case SExprKind::Number:
            os << intValue;
            break;
        case SExprKind::String:
            os << '"' << text << '"';
            break;
        case SExprKind::List:
            os << '(';
            for (size_t i = 0; i < elements.size(); ++i)
            {
                if (i > 0) os << ' ';
                elements[i].dump(os);
            }
            os << ')';
            break;
        case SExprKind::BracketList:
            os << '[';
            for (size_t i = 0; i < elements.size(); ++i)
            {
                if (i > 0) os << ' ';
                elements[i].dump(os);
            }
            os << ']';
            break;
        }
    }
};

} // namespace Luwu::Dsl
