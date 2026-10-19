// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Core/Ast.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Luwu::Dsl
{

struct Value;
struct Pair;
struct Closure;
struct Builtin;
struct Record;
struct Env;

struct GcCell
{
    mutable bool marked = false;
    GcCell* next = nullptr;

    GcCell() = default;
    GcCell(const GcCell&) : marked(false), next(nullptr) {}
    GcCell& operator=(const GcCell&) { return *this; }
    virtual ~GcCell() = default;
    virtual void markChildren(std::vector<const GcCell*>& worklist) const {}
};

GcCell*& gcHead();
size_t& gcCount();
size_t& gcThreshold();

template <typename T, typename... Args>
T* gcAlloc(Args&&... args)
{
    T* cell = new T(std::forward<Args>(args)...);
    cell->next = gcHead();
    gcHead() = cell;
    ++gcCount();
    return cell;
}

using EnvPtr = Env*;

enum class ValueKind
{
    Nil, // the empty list
    Bool,
    Int,
    Double,
    String,
    Symbol,
    Keyword, // text includes the leading ':'
    Pair,
    Closure,
    Builtin,
    Macro, // a define-macro: a closure from the call's arguments (as data) to its expansion
    Record, // a record value (define-record)
    Unassigned, // a letrec name before its value is computed
};

// A generation-time value. Lists are chains of pairs ending in Nil, as in Scheme.
struct Value
{
    ValueKind kind = ValueKind::Nil;
    union
    {
        bool boolValue;
        int64_t intValue = 0;
        double doubleValue;
        const Pair* pair;
        const Closure* closure;
        const Builtin* builtin;
        const Record* record;
        const GcCell* gcCell;
    };
    std::string text; // String, Symbol, Keyword

    static Value nil() { return Value{}; }
    static Value boolean(bool b);
    static Value integer(int64_t i);
    static Value real(double d);
    static Value string(std::string s);
    static Value symbol(std::string s);
    static Value keyword(std::string s);
    static Value cons(Value car, Value cdr);
    static Value list(const std::vector<Value>& items);
    static Value of(const Record* record);

    const GcCell* cell() const
    {
        return (kind == ValueKind::Pair || kind == ValueKind::Closure || kind == ValueKind::Macro ||
                kind == ValueKind::Builtin || kind == ValueKind::Record)
            ? gcCell
            : nullptr;
    }

    bool isNil() const { return kind == ValueKind::Nil; }
    bool isPair() const { return kind == ValueKind::Pair; }
    bool isNumber() const { return kind == ValueKind::Int || kind == ValueKind::Double; }
    bool isProcedure() const { return kind == ValueKind::Closure || kind == ValueKind::Builtin; }
    bool isMacro() const { return kind == ValueKind::Macro; }
    // Only #f is false; nil (the empty list) is true, as in Scheme
    bool isTruthy() const { return !(kind == ValueKind::Bool && !boolValue); }
};

struct Pair : GcCell
{
    Value car;
    Value cdr;

    Pair() = default;
    Pair(Value car, Value cdr)
        : car(std::move(car)), cdr(std::move(cdr))
    {
    }

    void markChildren(std::vector<const GcCell*>& worklist) const override
    {
        if (const GcCell* c = car.cell())
            worklist.push_back(c);
        if (const GcCell* c = cdr.cell())
            worklist.push_back(c);
    }
};

// Lexical environment: a frame of bindings with a parent
struct Env : GcCell
{
    std::unordered_map<std::string, Value> vars;
    EnvPtr parent = nullptr;

    explicit Env(EnvPtr parent = nullptr)
        : parent(parent)
    {
    }

    Value* find(const std::string& name)
    {
        for (Env* e = this; e; e = e->parent)
        {
            auto it = e->vars.find(name);
            if (it != e->vars.end())
                return &it->second;
        }
        return nullptr;
    }

    const Value* lookup(const std::string& name) const
    {
        for (const Env* e = this; e; e = e->parent)
        {
            auto it = e->vars.find(name);
            if (it != e->vars.end())
                return &it->second;
        }
        return nullptr;
    }

    void define(const std::string& name, Value v) { vars[name] = std::move(v); }

    void markChildren(std::vector<const GcCell*>& worklist) const override
    {
        if (parent)
            worklist.push_back(parent);
        for (const auto& [k, v] : vars)
        {
            if (const GcCell* c = v.cell())
                worklist.push_back(c);
        }
    }
};

// (lambda (params...) body...): the body is forms[bodyStart..] of the defining form, evaluated in a child of `env`
struct Closure : GcCell
{
    std::string name; // for messages: the define'd name, or empty
    std::vector<std::string> params;
    std::string restParam; // after & in the parameter list, empty when none
    const SExpr* form = nullptr;
    size_t bodyStart = 0;
    EnvPtr env = nullptr;
    SourceLocation loc;
    std::vector<std::string> docs; // ~ comments on its define, for --describe

    void markChildren(std::vector<const GcCell*>& worklist) const override
    {
        if (env)
            worklist.push_back(env);
    }
};

// A primitive. Builtins don't call back into the evaluator; `apply`, which does, is handled by the machine.
struct Builtin : GcCell
{
    std::string name;
    std::string syntax, desc; // its --describe entry, when it has one
    int minArgs = 0;
    int maxArgs = -1; // -1: any number
    std::function<Value(const std::vector<Value>& args, const SExpr& call)> fn;

    void markChildren(std::vector<const GcCell*>& worklist) const override {}
};

// A list as a vector; false when v isn't a proper list
bool listToVector(const Value& v, std::vector<Value>& out);

// The value written as Luna source would read it (strings quoted)
std::string toString(const Value& v);

// What a value is, for messages ("an int", "a list", ...)
std::string describeKind(const Value& v);

bool valuesEqual(const Value& a, const Value& b);

// A quoted datum: symbols stay symbols, lists become lists. With `origins`, each list made records the form it came
// from, so code built from it keeps the source locations.
using DatumOrigins = std::unordered_map<const Pair*, const SExpr*>;
Value datumFromSExpr(const SExpr& e, DatumOrigins* origins = nullptr);

// Code from a datum (a macro's expansion): lists that came from source forms are those forms again (locations, docs),
// everything else is located at `loc`
SExpr sexprFromDatum(const Value& v, const DatumOrigins& origins, const SourceLocation& loc);

} // namespace Luwu::Dsl
