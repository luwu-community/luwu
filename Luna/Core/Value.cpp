// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Value.h"
#include "Core/Record.h"

#include <cmath>
#include <sstream>

#include "Core/CompileError.h"

namespace Luwu::Dsl
{

static GcCell* s_gcHead = nullptr;
static size_t s_gcCount = 0;
static size_t s_gcThreshold = 4096;

GcCell*& gcHead()
{
    return s_gcHead;
}

size_t& gcCount()
{
    return s_gcCount;
}

size_t& gcThreshold()
{
    return s_gcThreshold;
}

Value Value::boolean(bool b)
{
    Value v;
    v.kind = ValueKind::Bool;
    v.boolValue = b;
    return v;
}

Value Value::integer(int64_t i)
{
    Value v;
    v.kind = ValueKind::Int;
    v.intValue = i;
    return v;
}

Value Value::real(double d)
{
    Value v;
    v.kind = ValueKind::Double;
    v.doubleValue = d;
    return v;
}

Value Value::string(std::string s)
{
    Value v;
    v.kind = ValueKind::String;
    v.text = std::move(s);
    return v;
}

Value Value::symbol(std::string s)
{
    Value v;
    v.kind = ValueKind::Symbol;
    v.text = std::move(s);
    return v;
}

Value Value::keyword(std::string s)
{
    Value v;
    v.kind = ValueKind::Keyword;
    v.text = std::move(s);
    return v;
}

Value Value::cons(Value car, Value cdr)
{
    Value v;
    v.kind = ValueKind::Pair;
    v.pair = gcAlloc<Pair>(std::move(car), std::move(cdr));
    return v;
}

Value Value::of(const Record* record)
{
    Value v;
    v.kind = ValueKind::Record;
    v.record = record;
    return v;
}

Value Value::list(const std::vector<Value>& items)
{
    Value v;
    for (size_t i = items.size(); i-- > 0;)
        v = cons(items[i], std::move(v));
    return v;
}

bool listToVector(const Value& v, std::vector<Value>& out)
{
    const Value* cur = &v;
    while (cur->kind == ValueKind::Pair)
    {
        out.push_back(cur->pair->car);
        cur = &cur->pair->cdr;
    }
    return cur->kind == ValueKind::Nil;
}

static std::string quoteString(const std::string& s)
{
    std::string out = "\"";
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c == '\n')
        {
            out += "\\n";
            continue;
        }
        out += c;
    }
    return out + "\"";
}

std::string toString(const Value& v)
{
    switch (v.kind)
    {
    case ValueKind::Nil:
        return "()";
    case ValueKind::Bool:
        return v.boolValue ? "#t" : "#f";
    case ValueKind::Int:
        return std::to_string(v.intValue);
    case ValueKind::Double:
    {
        std::ostringstream os;
        os.precision(17);
        os << v.doubleValue;
        std::string s = os.str();
        if (std::isfinite(v.doubleValue) && s.find_first_of(".e") == std::string::npos)
            s += ".0";
        return s;
    }
    case ValueKind::String:
        return quoteString(v.text);
    case ValueKind::Symbol:
    case ValueKind::Keyword:
        return v.text;
    case ValueKind::Pair:
    {
        std::string s = "(";
        const Value* cur = &v;
        bool firstItem = true;
        while (cur->kind == ValueKind::Pair)
        {
            if (!firstItem)
                s += " ";
            s += toString(cur->pair->car);
            firstItem = false;
            cur = &cur->pair->cdr;
        }
        if (cur->kind != ValueKind::Nil)
            s += " . " + toString(*cur); // an improper list
        return s + ")";
    }
    case ValueKind::Closure:
        return v.closure->name.empty() ? "#<procedure>" : "#<procedure " + v.closure->name + ">";
    case ValueKind::Builtin:
        return "#<builtin " + v.builtin->name + ">";
    case ValueKind::Macro:
        return "#<macro " + v.closure->name + ">";
    case ValueKind::Record:
        return v.record->show();
    case ValueKind::Unassigned:
        return "#<unassigned>";
    }
    return "?";
}

std::string describeKind(const Value& v)
{
    switch (v.kind)
    {
    case ValueKind::Nil:
        return "the empty list";
    case ValueKind::Bool:
        return "a boolean";
    case ValueKind::Int:
        return "an int";
    case ValueKind::Double:
        return "a double";
    case ValueKind::String:
        return "a string";
    case ValueKind::Symbol:
        return "a symbol";
    case ValueKind::Keyword:
        return "a keyword";
    case ValueKind::Pair:
        return "a list";
    case ValueKind::Closure:
    case ValueKind::Builtin:
        return "a procedure";
    case ValueKind::Macro:
        return "a macro";
    case ValueKind::Record:
        return v.record->kindName();
    case ValueKind::Unassigned:
        return "an unassigned variable";
    }
    return "a value";
}

bool valuesEqual(const Value& a, const Value& b)
{
    if (a.kind != b.kind)
    {
        if (a.isNumber() && b.isNumber())
            return (a.kind == ValueKind::Int ? double(a.intValue) : a.doubleValue) == (b.kind == ValueKind::Int ? double(b.intValue) : b.doubleValue);
        return false;
    }
    switch (a.kind)
    {
    case ValueKind::Nil:
        return true;
    case ValueKind::Bool:
        return a.boolValue == b.boolValue;
    case ValueKind::Int:
        return a.intValue == b.intValue;
    case ValueKind::Double:
        return a.doubleValue == b.doubleValue;
    case ValueKind::String:
    case ValueKind::Symbol:
    case ValueKind::Keyword:
        return a.text == b.text;
    case ValueKind::Pair:
    {
        const Value* x = &a;
        const Value* y = &b;
        while (x->kind == ValueKind::Pair && y->kind == ValueKind::Pair)
        {
            if (!valuesEqual(x->pair->car, y->pair->car))
                return false;
            x = &x->pair->cdr;
            y = &y->pair->cdr;
        }
        return valuesEqual(*x, *y);
    }
    case ValueKind::Closure:
    case ValueKind::Macro:
        return a.closure == b.closure;
    case ValueKind::Builtin:
        return a.builtin == b.builtin;
    case ValueKind::Record:
        return a.record == b.record;
    case ValueKind::Unassigned:
        return true;
    }
    return false;
}

Value datumFromSExpr(const SExpr& e, DatumOrigins* origins)
{
    switch (e.kind)
    {
    case SExprKind::Symbol:
        if (e.text == "#t" || e.text == "#f")
            return Value::boolean(e.text == "#t");
        return Value::symbol(e.text);
    case SExprKind::Keyword:
        return Value::keyword(e.text);
    case SExprKind::Number:
        if (e.text.find_first_of(".") != std::string::npos)
            return Value::real(std::stod(e.text));
        return Value::integer(e.intValue);
    case SExprKind::String:
        return Value::string(e.text);
    case SExprKind::List:
    case SExprKind::BracketList:
    {
        // (a b . c) is an improper list: c is the last pair's rest
        size_t n = e.elements.size();
        bool dotted = n >= 3 && e.elements[n - 2].isSymbol(".");
        Value list = dotted ? datumFromSExpr(e.elements[n - 1], origins) : Value::nil();
        for (size_t i = dotted ? n - 2 : n; i-- > 0;)
        {
            if (e.elements[i].isSymbol("."))
                throw CompileError(e.elements[i], "A . goes before the last element of a list: (a b . c)");
            list = Value::cons(datumFromSExpr(e.elements[i], origins), std::move(list));
        }
        if (origins && list.isPair())
            (*origins)[list.pair] = &e;
        return list;
    }
    }
    return Value::nil();
}

SExpr sexprFromDatum(const Value& v, const DatumOrigins& origins, const SourceLocation& loc)
{
    switch (v.kind)
    {
    case ValueKind::Nil:
    {
        SExpr e; // the empty list, as in (lambda () ...)
        e.kind = SExprKind::List;
        e.loc = loc;
        e.endLine = loc.line;
        return e;
    }
    case ValueKind::Bool:
        return SExpr::atom(SExprKind::Symbol, v.boolValue ? "#t" : "#f", loc);
    case ValueKind::Int:
    {
        SExpr e = SExpr::atom(SExprKind::Number, std::to_string(v.intValue), loc);
        e.intValue = v.intValue;
        return e;
    }
    case ValueKind::Double:
        return SExpr::atom(SExprKind::Number, toString(v), loc);
    case ValueKind::String:
        return SExpr::atom(SExprKind::String, v.text, loc);
    case ValueKind::Symbol:
        return SExpr::atom(SExprKind::Symbol, v.text, loc);
    case ValueKind::Keyword:
        return SExpr::atom(SExprKind::Keyword, v.text, loc);
    case ValueKind::Pair:
    {
        auto it = origins.find(v.pair);
        if (it != origins.end())
            return *it->second; // an unchanged list from the source: the form itself
        SExpr e;
        e.kind = SExprKind::List;
        e.loc = loc;
        e.endLine = loc.line;
        const Value* cur = &v;
        while (cur->kind == ValueKind::Pair)
        {
            e.elements.push_back(sexprFromDatum(cur->pair->car, origins, loc));
            cur = &cur->pair->cdr;
        }
        if (cur->kind != ValueKind::Nil) // an improper list: (a b . c)
        {
            e.elements.push_back(SExpr::atom(SExprKind::Symbol, ".", loc));
            e.elements.push_back(sexprFromDatum(*cur, origins, loc));
        }
        return e;
    }
    case ValueKind::Closure:
    case ValueKind::Builtin:
    case ValueKind::Macro:
    case ValueKind::Record:
    case ValueKind::Unassigned:
        break;
    }
    throw CompileError(loc, "A macro expansion can't contain " + describeKind(v) + " (" + toString(v) + "); expand to code that refers to it by name");
}

} // namespace Luwu::Dsl
