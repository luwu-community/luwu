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
struct Rope;
struct Vector;
struct Table;
struct Parameter;
struct Syntax;
struct BytecodeFunction;
struct Upvalue;

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
    Macro, // a define-syntax transformer: call syntax -> expansion syntax
    Record, // a record value (define-record)
    Vector,
    Table,
    Set,
    Parameter,
    Unassigned, // a letrec name before its value is computed
    Syntax,     // a syntax object (syntax-case)
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
        const Rope* rope;
        Vector* vector;
        Table* table;
        Parameter* parameter;
        const Syntax* syntax;
        const GcCell* gcCell;
    };
    std::string text; // Symbol and Keyword; strings use an immutable rope.
    // Legacy datum macros may rearrange identifiers originating in syntax.
    std::shared_ptr<SyntaxData> datumSyntax;

    static Value nil() { return Value{}; }
    static Value boolean(bool b);
    static Value integer(int64_t i);
    static Value real(double d);
    static Value string(std::string s);
    static Value string(const Rope* rope);
    const std::string& stringValue() const;
    size_t stringSize() const;
    static Value concat(const std::vector<Value>& strings);
    static Value symbol(std::string s);
    static Value keyword(std::string s);
    static Value cons(Value car, Value cdr);
    static Value list(const std::vector<Value>& items);
    static Value of(const Record* record);
    static Value of(const Syntax* syntax);

    const GcCell* cell() const
    {
        return (kind == ValueKind::Pair || kind == ValueKind::Closure || kind == ValueKind::Macro ||
                kind == ValueKind::Builtin || kind == ValueKind::Record || kind == ValueKind::String ||
                kind == ValueKind::Vector || kind == ValueKind::Table || kind == ValueKind::Set ||
                kind == ValueKind::Parameter || kind == ValueKind::Syntax)
            ? gcCell
            : nullptr;
    }

    bool isNil() const { return kind == ValueKind::Nil; }
    bool isPair() const { return kind == ValueKind::Pair; }
    bool isNumber() const { return kind == ValueKind::Int || kind == ValueKind::Double; }
    bool isProcedure() const { return kind == ValueKind::Closure || kind == ValueKind::Builtin || kind == ValueKind::Parameter; }
    bool isMacro() const { return kind == ValueKind::Macro; }
    bool isSyntax() const { return kind == ValueKind::Syntax; }
    // Only #f is false; nil (the empty list) is true, as in Scheme
    bool isTruthy() const { return !(kind == ValueKind::Bool && !boolValue); }
};

// Ropes share immutable chunks. Flattening and GC traversal are iterative, so a
// long left-associated concatenation cannot overflow the C++ stack.
struct Rope : GcCell
{
    const Rope* left = nullptr;
    const Rope* right = nullptr;
    size_t size = 0;
    mutable bool flat = true;
    mutable std::string bytes;
    explicit Rope(std::string bytes) : size(bytes.size()), bytes(std::move(bytes)) {}
    Rope(const Rope* left, const Rope* right) : left(left), right(right), size(left->size + right->size), flat(false) {}
    const std::string& flatten() const;
    void markChildren(std::vector<const GcCell*>& worklist) const override
    {
        if (left) worklist.push_back(left);
        if (right) worklist.push_back(right);
    }
};

struct Vector : GcCell
{
    std::vector<Value> items;
    void markChildren(std::vector<const GcCell*>& worklist) const override;
};

struct Parameter : GcCell
{
    Value value;
    void markChildren(std::vector<const GcCell*>& worklist) const override
    {
        if (const GcCell* c = value.cell()) worklist.push_back(c);
    }
};

struct Table : GcCell
{
    struct Entry { Value key; Value value; bool live = true; };
    bool identity = false;
    std::vector<Entry> entries;
    std::unordered_map<size_t, std::vector<size_t>> buckets;
    size_t count = 0;
    void markChildren(std::vector<const GcCell*>& worklist) const override;
    size_t find(const Value& key, const SExpr& call) const;
    bool put(const Value& key, Value value, const SExpr& call);
    bool erase(const Value& key, const SExpr& call);
};

bool valuesIdentical(const Value& a, const Value& b);
size_t valueHash(const Value& value, bool identity, const SExpr& call);

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

enum class SyntaxFormKind
{
    Identifier,
    Constant,
    List,
    Vector,
};

struct Syntax : GcCell
{
    SyntaxFormKind formKind = SyntaxFormKind::Identifier;
    std::string text;         // For Identifier or Keyword
    Value datum;              // For Constant (Nil, Bool, Int, Double, String)
    std::vector<Value> items; // For List or Vector (items are Values, usually ValueKind::Syntax)
    bool isBracket = false;
    bool dotted = false;      // When true, the last item in items is the dotted tail

    ScopeSet scopes;
    SourceLocation loc;
    std::vector<std::string> docs;
    std::optional<std::string> trailingDoc;

    Syntax() = default;

    bool isIdentifier() const { return formKind == SyntaxFormKind::Identifier; }
    bool isConstant() const { return formKind == SyntaxFormKind::Constant; }
    bool isList() const { return formKind == SyntaxFormKind::List; }
    bool isVector() const { return formKind == SyntaxFormKind::Vector; }

    void markChildren(std::vector<const GcCell*>& worklist) const override
    {
        if (const GcCell* c = datum.cell())
            worklist.push_back(c);
        for (const auto& item : items)
        {
            if (const GcCell* c = item.cell())
                worklist.push_back(c);
        }
    }
};

// Lexical environment: a frame of bindings with a parent
struct Env : GcCell
{
    std::unordered_map<std::string, Value> vars;
    EnvPtr parent = nullptr;
    const BytecodeFunction* layout = nullptr;
    const Closure* procedure = nullptr;
    std::vector<Value> slots;
    std::vector<uint8_t> bound;
    std::vector<Upvalue*> captured;

    explicit Env(EnvPtr parent = nullptr)
        : parent(parent)
    {
    }

    Value* find(const std::string& name);
    const Value* lookup(const std::string& name) const;
    void define(const std::string& name, Value v);
    Value* local(uint32_t slot);
    Upvalue* capture(uint32_t slot);
    void markChildren(std::vector<const GcCell*>& worklist) const override;
};

// Captured locals are boxed only on first capture. Sibling closures share the
// cell, so no pointers into movable register arrays or closing pass are needed.
struct Upvalue : GcCell
{
    Value value;
    bool bound = false;
    EnvPtr fallback = nullptr; // Preserves lookup before an internal define runs.
    Upvalue* fallbackCell = nullptr;
    std::string name;
    Value* get();
    void markChildren(std::vector<const GcCell*>& worklist) const override;
};

// A compiled procedure, its captured cells, and its global/external environment.
struct Closure : GcCell
{
    std::string name; // for messages: the define'd name, or empty
    std::vector<std::string> params;
    std::string restParam; // after & in the parameter list, empty when none
    EnvPtr env = nullptr;
    SourceLocation loc;
    std::vector<std::string> docs; // ~ comments on its define, for --describe
    const Closure* datumTransformer = nullptr; // Signature of a define-macro compatibility adapter.
    const BytecodeFunction* bytecode = nullptr;
    std::vector<Upvalue*> upvalues;

    void markChildren(std::vector<const GcCell*>& worklist) const override;
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
