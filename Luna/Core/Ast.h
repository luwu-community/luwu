// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <iostream>

namespace Luwu::Dsl
{

struct SourceLocation
{
    std::string file;
    int line = 0;
    int col = 0;

    std::string toString() const
    {
        if (file.empty())
        {
            if (line == 0 && col == 0)
                return "";
            return std::to_string(line) + ":" + std::to_string(col);
        }
        if (line == 0 && col == 0)
            return file;
        return file + ":" + std::to_string(line) + ":" + std::to_string(col);
    }
};

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
    SourceLocation loc;
    int endLine = 0; // line of the last token (the closing bracket for lists)

    // Doc comments (~): kept and written into the generated C++. Leading ones are on their own lines before the
    // expression, a trailing one follows it on the line where it ends.
    std::vector<std::string> docs;
    std::optional<std::string> trailingDoc;

    bool hasDocs() const { return !docs.empty() || trailingDoc.has_value(); }

    static SExpr atom(SExprKind kind, std::string text, const SourceLocation& loc)
    {
        SExpr e;
        e.kind = kind;
        e.text = std::move(text);
        e.loc = loc;
        e.endLine = loc.line;
        return e;
    }

    static SExpr symbol(std::string text, const SourceLocation& loc)
    {
        return atom(SExprKind::Symbol, std::move(text), loc);
    }

    static SExpr list(std::vector<SExpr> elements, const SourceLocation& loc)
    {
        SExpr e;
        e.kind = SExprKind::List;
        e.elements = std::move(elements);
        e.loc = loc;
        e.endLine = loc.line;
        return e;
    }

    static SExpr call(std::string name, std::vector<SExpr> args, const SourceLocation& loc)
    {
        args.insert(args.begin(), symbol(std::move(name), loc));
        return list(std::move(args), loc);
    }

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
