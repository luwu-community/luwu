// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Evaluator.h"

#include "Core/CompileError.h"

#include <cctype>
#include <cmath>

namespace Luwu::Dsl
{

using Args = std::vector<Value>;

void defineBuiltin(Env& env, const std::string& name, int minArgs, int maxArgs, std::function<Value(const Args&, const SExpr&)> fn,
    const std::string& syntax, const std::string& desc)
{
    auto b = gcAlloc<Builtin>();
    b->name = name;
    b->syntax = syntax;
    b->desc = desc;
    b->minArgs = minArgs;
    b->maxArgs = maxArgs;
    b->fn = std::move(fn);
    Value v;
    v.kind = ValueKind::Builtin;
    v.builtin = b;
    env.define(name, std::move(v));
}

static void add(Env& env, const std::string& name, int minArgs, int maxArgs, std::function<Value(const Args&, const SExpr&)> fn)
{
    defineBuiltin(env, name, minArgs, maxArgs, std::move(fn));
}

static const Value& need(const Args& args, size_t i, ValueKind kind, const char* what, const std::string& fn, const SExpr& call)
{
    if (args[i].kind != kind)
        throw CompileError(call, fn + " expects " + what + " as argument " + std::to_string(i + 1) + ", got " + toString(args[i]) + " (" + describeKind(args[i]) + ")");
    return args[i];
}

static void needNumber(const Args& args, size_t i, const std::string& fn, const SExpr& call)
{
    if (!args[i].isNumber())
        throw CompileError(call, fn + " expects a number as argument " + std::to_string(i + 1) + ", got " + toString(args[i]) + " (" + describeKind(args[i]) + ")");
}

static double asDouble(const Value& v)
{
    return v.kind == ValueKind::Int ? double(v.intValue) : v.doubleValue;
}

// Performs arithmetic (+, -, *): integer operations wrap on overflow, and any floating-point operand promotes the result to double.
static Value arith(const Args& args, const std::string& op, const SExpr& call)
{
    bool real = false;
    for (size_t i = 0; i < args.size(); ++i)
    {
        needNumber(args, i, op, call);
        real |= args[i].kind == ValueKind::Double;
    }
    if (args.empty())
        return Value::integer(op == "*" ? 1 : 0);
    if (op == "-" && args.size() == 1)
        return real ? Value::real(-args[0].doubleValue) : Value::integer(int64_t(0 - uint64_t(args[0].intValue)));

    if (real)
    {
        double acc = asDouble(args[0]);
        for (size_t i = 1; i < args.size(); ++i)
            acc = op == "+" ? acc + asDouble(args[i]) : op == "-" ? acc - asDouble(args[i]) : acc * asDouble(args[i]);
        return Value::real(acc);
    }
    uint64_t acc = uint64_t(args[0].intValue);
    for (size_t i = 1; i < args.size(); ++i)
    {
        uint64_t x = uint64_t(args[i].intValue);
        acc = op == "+" ? acc + x : op == "-" ? acc - x : acc * x;
    }
    return Value::integer(int64_t(acc));
}

static Value compare(const Args& args, const std::string& op, const SExpr& call)
{
    for (size_t i = 0; i < args.size(); ++i)
        needNumber(args, i, op, call);
    for (size_t i = 0; i + 1 < args.size(); ++i)
    {
        const Value& a = args[i];
        const Value& b = args[i + 1];
        bool ok;
        if (a.kind == ValueKind::Int && b.kind == ValueKind::Int)
        {
            int64_t x = a.intValue, y = b.intValue;
            ok = op == "=" ? x == y : op == "<" ? x < y : op == ">" ? x > y : op == "<=" ? x <= y : x >= y;
        }
        else
        {
            double x = asDouble(a), y = asDouble(b);
            ok = op == "=" ? x == y : op == "<" ? x < y : op == ">" ? x > y : op == "<=" ? x <= y : x >= y;
        }
        if (!ok)
            return Value::boolean(false);
    }
    return Value::boolean(true);
}

static Args listArg(const Args& args, size_t i, const std::string& fn, const SExpr& call)
{
    Args items;
    if (!listToVector(args[i], items))
        throw CompileError(call, fn + " expects a list as argument " + std::to_string(i + 1) + ", got " + toString(args[i]) + " (" + describeKind(args[i]) + ")");
    return items;
}

// Formats a value for display in (str ...) and error messages without string quotes.
static std::string display(const Value& v)
{
    return v.kind == ValueKind::String ? v.text : toString(v);
}

void installBuiltins(Env& env, int64_t& gensymCounter)
{
    for (const char* op : {"+", "-", "*"})
        add(env, op, std::string(op) == "-" ? 1 : 0, -1, [op = std::string(op)](const Args& a, const SExpr& c) { return arith(a, op, c); });
    for (const char* op : {"=", "<", ">", "<=", ">="})
        add(env, op, 1, -1, [op = std::string(op)](const Args& a, const SExpr& c) { return compare(a, op, c); });

    // Performs division: integer division truncates toward zero, and any floating-point operand produces a double.
    add(env, "/", 2, 2, [](const Args& a, const SExpr& c) {
        needNumber(a, 0, "/", c);
        needNumber(a, 1, "/", c);
        if (a[0].kind == ValueKind::Int && a[1].kind == ValueKind::Int)
        {
            if (a[1].intValue == 0)
                throw CompileError(c, "Division by zero");
            if (a[1].intValue == -1)
                return Value::integer(int64_t(0 - uint64_t(a[0].intValue)));
            return Value::integer(a[0].intValue / a[1].intValue);
        }
        return Value::real(asDouble(a[0]) / asDouble(a[1]));
    });
    add(env, "remainder", 2, 2, [](const Args& a, const SExpr& c) {
        int64_t x = need(a, 0, ValueKind::Int, "an int", "remainder", c).intValue;
        int64_t y = need(a, 1, ValueKind::Int, "an int", "remainder", c).intValue;
        if (y == 0)
            throw CompileError(c, "Division by zero");
        return Value::integer(y == -1 ? 0 : x % y);
    });
    add(env, "min", 1, -1, [](const Args& a, const SExpr& c) {
        size_t best = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            needNumber(a, i, "min", c);
            if (asDouble(a[i]) < asDouble(a[best]))
                best = i;
        }
        return a[best];
    });
    add(env, "max", 1, -1, [](const Args& a, const SExpr& c) {
        size_t best = 0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            needNumber(a, i, "max", c);
            if (asDouble(a[i]) > asDouble(a[best]))
                best = i;
        }
        return a[best];
    });
    add(env, "abs", 1, 1, [](const Args& a, const SExpr& c) {
        needNumber(a, 0, "abs", c);
        if (a[0].kind == ValueKind::Double)
            return Value::real(std::fabs(a[0].doubleValue));
        return Value::integer(a[0].intValue < 0 ? int64_t(0 - uint64_t(a[0].intValue)) : a[0].intValue);
    });

    // Bitwise operations on 64-bit two's-complement integers.
    add(env, "bit-and", 1, -1, [](const Args& a, const SExpr& c) {
        uint64_t acc = ~uint64_t(0);
        for (size_t i = 0; i < a.size(); ++i)
            acc &= uint64_t(need(a, i, ValueKind::Int, "an int", "bit-and", c).intValue);
        return Value::integer(int64_t(acc));
    });
    add(env, "bit-or", 1, -1, [](const Args& a, const SExpr& c) {
        uint64_t acc = 0;
        for (size_t i = 0; i < a.size(); ++i)
            acc |= uint64_t(need(a, i, ValueKind::Int, "an int", "bit-or", c).intValue);
        return Value::integer(int64_t(acc));
    });
    add(env, "bit-xor", 1, -1, [](const Args& a, const SExpr& c) {
        uint64_t acc = 0;
        for (size_t i = 0; i < a.size(); ++i)
            acc ^= uint64_t(need(a, i, ValueKind::Int, "an int", "bit-xor", c).intValue);
        return Value::integer(int64_t(acc));
    });
    add(env, "bit-not", 1, 1, [](const Args& a, const SExpr& c) {
        return Value::integer(int64_t(~uint64_t(need(a, 0, ValueKind::Int, "an int", "bit-not", c).intValue)));
    });
    add(env, "shl", 2, 2, [](const Args& a, const SExpr& c) {
        int64_t n = need(a, 1, ValueKind::Int, "an int", "shl", c).intValue;
        if (n < 0 || n > 63)
            throw CompileError(c, "shl takes a shift of 0 to 63, got " + std::to_string(n));
        return Value::integer(int64_t(uint64_t(need(a, 0, ValueKind::Int, "an int", "shl", c).intValue) << n));
    });
    add(env, "shr", 2, 2, [](const Args& a, const SExpr& c) { // Arithmetic right shift.
        int64_t n = need(a, 1, ValueKind::Int, "an int", "shr", c).intValue;
        if (n < 0 || n > 63)
            throw CompileError(c, "shr takes a shift of 0 to 63, got " + std::to_string(n));
        return Value::integer(need(a, 0, ValueKind::Int, "an int", "shr", c).intValue >> n);
    });

    // Type predicate and equality functions.
    auto kindTest = [&](const char* name, std::function<bool(const Value&)> test) {
        add(env, name, 1, 1, [test](const Args& a, const SExpr&) { return Value::boolean(test(a[0])); });
    };
    kindTest("null?", [](const Value& v) { return v.isNil(); });
    kindTest("pair?", [](const Value& v) { return v.isPair(); });
    kindTest("list?", [](const Value& v) {
        Args items;
        return listToVector(v, items);
    });
    kindTest("number?", [](const Value& v) { return v.isNumber(); });
    kindTest("int?", [](const Value& v) { return v.kind == ValueKind::Int; });
    kindTest("double?", [](const Value& v) { return v.kind == ValueKind::Double; });
    kindTest("bool?", [](const Value& v) { return v.kind == ValueKind::Bool; });
    kindTest("string?", [](const Value& v) { return v.kind == ValueKind::String; });
    kindTest("symbol?", [](const Value& v) { return v.kind == ValueKind::Symbol; });
    kindTest("keyword?", [](const Value& v) { return v.kind == ValueKind::Keyword; });
    kindTest("procedure?", [](const Value& v) { return v.isProcedure(); });
    add(env, "not", 1, 1, [](const Args& a, const SExpr&) { return Value::boolean(!a[0].isTruthy()); });
    add(env, "equal?", 2, 2, [](const Args& a, const SExpr&) { return Value::boolean(valuesEqual(a[0], a[1])); });
    // Compares atomic values by equality and lists or procedures by pointer identity.
    add(env, "eq?", 2, 2, [](const Args& a, const SExpr&) {
        if (a[0].kind == ValueKind::Pair && a[1].kind == ValueKind::Pair)
            return Value::boolean(a[0].pair == a[1].pair);
        return Value::boolean(valuesEqual(a[0], a[1]));
    });

    // List manipulation functions.
    add(env, "cons", 2, 2, [](const Args& a, const SExpr&) { return Value::cons(a[0], a[1]); });
    add(env, "list", 0, -1, [](const Args& a, const SExpr&) { return Value::list(a); });
    for (const char* name : {"first", "car"})
        add(env, name, 1, 1, [name = std::string(name)](const Args& a, const SExpr& c) {
            return need(a, 0, ValueKind::Pair, "a non-empty list", name, c).pair->car;
        });
    for (const char* name : {"rest", "cdr"})
        add(env, name, 1, 1, [name = std::string(name)](const Args& a, const SExpr& c) {
            return need(a, 0, ValueKind::Pair, "a non-empty list", name, c).pair->cdr;
        });
    add(env, "length", 1, 1, [](const Args& a, const SExpr& c) { return Value::integer(int64_t(listArg(a, 0, "length", c).size())); });
    add(env, "nth", 2, 2, [](const Args& a, const SExpr& c) {
        Args items = listArg(a, 0, "nth", c);
        int64_t i = need(a, 1, ValueKind::Int, "an int", "nth", c).intValue;
        if (i < 0 || i >= int64_t(items.size()))
            throw CompileError(c, "nth: index " + std::to_string(i) + " is out of range for a list of " + std::to_string(items.size()));
        return items[size_t(i)];
    });
    add(env, "last", 1, 1, [](const Args& a, const SExpr& c) {
        Args items = listArg(a, 0, "last", c);
        if (items.empty())
            throw CompileError(c, "last expects a non-empty list");
        return items.back();
    });
    add(env, "reverse", 1, 1, [](const Args& a, const SExpr& c) {
        Args items = listArg(a, 0, "reverse", c);
        Value out;
        for (const auto& v : items)
            out = Value::cons(v, std::move(out));
        return out;
    });
    add(env, "append", 0, -1, [](const Args& a, const SExpr& c) {
        if (a.empty())
            return Value::nil();
        Value out = a.back(); // Shares the tail list without copying, following Scheme semantics.
        for (size_t i = a.size() - 1; i-- > 0;)
        {
            Args items = listArg(a, i, "append", c);
            for (size_t j = items.size(); j-- > 0;)
                out = Value::cons(items[j], std::move(out));
        }
        return out;
    });
    // apply is implemented directly by the evaluator; this registration provides its name and arity.
    add(env, "apply", 2, -1, [](const Args&, const SExpr& c) -> Value { throw CompileError(c, "apply reached its placeholder"); });

    // String, symbol, and keyword conversion and manipulation functions.
    add(env, "str", 0, -1, [](const Args& a, const SExpr&) {
        std::string s;
        for (const auto& v : a)
            s += display(v);
        return Value::string(s);
    });
    add(env, "string-length", 1, 1, [](const Args& a, const SExpr& c) {
        return Value::integer(int64_t(need(a, 0, ValueKind::String, "a string", "string-length", c).text.size()));
    });
    add(env, "substring", 2, 3, [](const Args& a, const SExpr& c) {
        const std::string& s = need(a, 0, ValueKind::String, "a string", "substring", c).text;
        int64_t from = need(a, 1, ValueKind::Int, "an int", "substring", c).intValue;
        int64_t to = a.size() == 3 ? need(a, 2, ValueKind::Int, "an int", "substring", c).intValue : int64_t(s.size());
        if (from < 0 || to < from || to > int64_t(s.size()))
            throw CompileError(c, "substring: " + std::to_string(from) + ".." + std::to_string(to) + " is out of range for a string of " + std::to_string(s.size()));
        return Value::string(s.substr(size_t(from), size_t(to - from)));
    });
    add(env, "string-suffix?", 2, 2, [](const Args& a, const SExpr& c) {
        const std::string& s = need(a, 0, ValueKind::String, "a string", "string-suffix?", c).text;
        const std::string& suffix = need(a, 1, ValueKind::String, "a string", "string-suffix?", c).text;
        return Value::boolean(s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0);
    });
    add(env, "string-upcase", 1, 1, [](const Args& a, const SExpr& c) {
        std::string s = need(a, 0, ValueKind::String, "a string", "string-upcase", c).text;
        for (char& ch : s)
            ch = char(std::toupper(static_cast<unsigned char>(ch)));
        return Value::string(s);
    });
    add(env, "string<?", 2, 2, [](const Args& a, const SExpr& c) {
        return Value::boolean(need(a, 0, ValueKind::String, "a string", "string<?", c).text < need(a, 1, ValueKind::String, "a string", "string<?", c).text);
    });
    add(env, "string-search", 2, 3, [](const Args& a, const SExpr& c) {
        const std::string& s = need(a, 0, ValueKind::String, "a string", "string-search", c).text;
        const std::string& part = need(a, 1, ValueKind::String, "a string", "string-search", c).text;
        int64_t from = a.size() == 3 ? need(a, 2, ValueKind::Int, "an int", "string-search", c).intValue : 0;
        if (from < 0 || from > int64_t(s.size()))
            throw CompileError(c, "string-search: " + std::to_string(from) + " is out of range for a string of " + std::to_string(s.size()));
        size_t at = s.find(part, size_t(from));
        return at == std::string::npos ? Value::boolean(false) : Value::integer(int64_t(at));
    });
    add(env, "string-split", 2, 2, [](const Args& a, const SExpr& c) {
        const std::string& s = need(a, 0, ValueKind::String, "a string", "string-split", c).text;
        const std::string& separator = need(a, 1, ValueKind::String, "a string", "string-split", c).text;
        if (separator.empty())
            throw CompileError(c, "string-split needs a separator that isn't empty");
        std::vector<Value> parts;
        size_t start = 0;
        while (true)
        {
            size_t end = s.find(separator, start);
            parts.push_back(Value::string(s.substr(start, end == std::string::npos ? std::string::npos : end - start)));
            if (end == std::string::npos)
                break;
            start = end + separator.size();
        }
        return Value::list(parts);
    });
    add(env, "string-join", 2, 2, [](const Args& a, const SExpr& c) {
        std::vector<Value> items;
        if (!listToVector(a[0], items))
            throw CompileError(c, "string-join takes a list of strings, got " + toString(a[0]));
        const std::string& separator = need(a, 1, ValueKind::String, "a string", "string-join", c).text;
        std::string s;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].kind != ValueKind::String)
                throw CompileError(c, "string-join takes a list of strings, got " + toString(items[i]) + " in it");
            s += (i == 0 ? "" : separator) + items[i].text;
        }
        return Value::string(s);
    });
    add(env, "string-downcase", 1, 1, [](const Args& a, const SExpr& c) {
        std::string s = need(a, 0, ValueKind::String, "a string", "string-downcase", c).text;
        for (char& ch : s)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        return Value::string(s);
    });
    add(env, "symbol->string", 1, 1, [](const Args& a, const SExpr& c) { return Value::string(need(a, 0, ValueKind::Symbol, "a symbol", "symbol->string", c).text); });
    add(env, "string->symbol", 1, 1, [](const Args& a, const SExpr& c) { return Value::symbol(need(a, 0, ValueKind::String, "a string", "string->symbol", c).text); });
    // Keyword values retain their leading colon; keyword->string strips it.
    add(env, "keyword->string", 1, 1, [](const Args& a, const SExpr& c) {
        return Value::string(need(a, 0, ValueKind::Keyword, "a keyword", "keyword->string", c).text.substr(1));
    });
    add(env, "string->keyword", 1, 1, [](const Args& a, const SExpr& c) {
        return Value::keyword(":" + need(a, 0, ValueKind::String, "a string", "string->keyword", c).text);
    });
    add(env, "number->string", 1, 1, [](const Args& a, const SExpr& c) {
        needNumber(a, 0, "number->string", c);
        return Value::string(toString(a[0]));
    });
    add(env, "string-escape", 1, 1, [](const Args& a, const SExpr& c) {
        const std::string& text = need(a, 0, ValueKind::String, "a string", "string-escape", c).text;
        std::string s = "\"";
        for (char ch : text)
        {
            if (ch == '"' || ch == '\\')
                s += '\\';
            if (ch == '\n')
            {
                s += "\\n";
                continue;
            }
            s += ch;
        }
        return Value::string(s + "\"");
    });

    // Generates an uninterned unique symbol (#:prefixN) to prevent hygiene violations in macro expansions.
    add(env, "gensym", 0, 1, [&gensymCounter](const Args& a, const SExpr& c) {
        std::string prefix = a.empty() ? "g" : need(a, 0, ValueKind::String, "a string", "gensym", c).text;
        return Value::symbol("#:" + prefix + std::to_string(++gensymCounter));
    });

    // Terminates evaluation by raising a compile error with the formatted message.
    add(env, "error", 1, -1, [](const Args& a, const SExpr& c) -> Value {
        std::string msg;
        for (size_t i = 0; i < a.size(); ++i)
            msg += (i == 0 ? "" : " ") + display(a[i]);
        throw CompileError(c, msg);
    });
}

} // namespace Luwu::Dsl
