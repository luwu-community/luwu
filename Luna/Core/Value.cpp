// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Value.h"
#include "Core/Bytecode.h"
#include "Core/Record.h"

#include <cmath>
#include <sstream>
#include <limits>
#include <set>

#include "Core/CompileError.h"

namespace Luwu::Dsl
{

Value* Env::find(const std::string& name)
{
    for (Env* e = this; e; e = e->parent)
    {
        if (e->layout)
        {
            auto slot = e->layout->localIndices.find(name);
            if (slot != e->layout->localIndices.end())
            {
                uint32_t index = slot->second;
                if (e->captured[index] && e->captured[index]->bound) return &e->captured[index]->value;
                if (!e->captured[index] && e->bound[index]) return &e->slots[index];
            }
            auto capture = e->layout->captureIndices.find(name);
            if (capture != e->layout->captureIndices.end())
                if (Value* value = e->procedure->upvalues[capture->second]->get()) return value;
        }
        auto it = e->vars.find(name);
        if (it != e->vars.end()) return &it->second;
    }
    return nullptr;
}

const Value* Env::lookup(const std::string& name) const
{
    return const_cast<Env*>(this)->find(name);
}

void Env::define(const std::string& name, Value value)
{
    if (layout)
    {
        auto slot = layout->localIndices.find(name);
        if (slot != layout->localIndices.end())
        {
            uint32_t index = slot->second;
            if (captured[index]) { captured[index]->value = std::move(value); captured[index]->bound = true; }
            else { slots[index] = std::move(value); bound[index] = true; }
            return;
        }
    }
    vars[name] = std::move(value);
}

Value* Env::local(uint32_t slot)
{
    if (captured[slot]) return captured[slot]->get();
    if (bound[slot]) return &slots[slot];
    auto inherited = layout->captureIndices.find(layout->localNames[slot]);
    if (inherited != layout->captureIndices.end()) return procedure->upvalues[inherited->second]->get();
    return parent ? parent->find(layout->localNames[slot]) : nullptr;
}

Upvalue* Env::capture(uint32_t slot)
{
    if (!captured[slot])
    {
        Upvalue* cell = gcAlloc<Upvalue>();
        cell->value = std::move(slots[slot]);
        slots[slot] = Value::nil();
        cell->bound = bound[slot];
        cell->fallback = parent;
        cell->name = layout->localNames[slot];
        auto inherited = layout->captureIndices.find(cell->name);
        if (inherited != layout->captureIndices.end()) cell->fallbackCell = procedure->upvalues[inherited->second];
        captured[slot] = cell;
    }
    return captured[slot];
}

Value* Upvalue::get()
{
    return bound ? &value : fallbackCell ? fallbackCell->get() : fallback ? fallback->find(name) : nullptr;
}

void Upvalue::markChildren(std::vector<const GcCell*>& worklist) const
{
    if (const GcCell* cell = value.cell()) worklist.push_back(cell);
    if (!bound && fallback) worklist.push_back(fallback);
    if (!bound && fallbackCell) worklist.push_back(fallbackCell);
}

void Env::markChildren(std::vector<const GcCell*>& worklist) const
{
    if (parent) worklist.push_back(parent);
    if (layout) worklist.push_back(layout);
    if (procedure) worklist.push_back(procedure);
    for (const auto& entry : vars)
        if (const GcCell* cell = entry.second.cell()) worklist.push_back(cell);
    for (const Value& value : slots)
        if (const GcCell* cell = value.cell()) worklist.push_back(cell);
    for (const Upvalue* cell : captured)
        if (cell) worklist.push_back(cell);
}

void Closure::markChildren(std::vector<const GcCell*>& worklist) const
{
    if (env) worklist.push_back(env);
    if (datumTransformer) worklist.push_back(datumTransformer);
    if (bytecode) worklist.push_back(bytecode);
    for (const Upvalue* cell : upvalues) worklist.push_back(cell);
}

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
    v.rope = gcAlloc<Rope>(std::move(s));
    return v;
}

const std::string& Rope::flatten() const
{
    if (!flat)
    {
        std::string result;
        result.reserve(size);
        std::vector<const Rope*> todo{this};
        while (!todo.empty())
        {
            const Rope* node = todo.back();
            todo.pop_back();
            if (node->flat) result += node->bytes;
            else { todo.push_back(node->right); todo.push_back(node->left); }
        }
        bytes = std::move(result);
        flat = true;
    }
    return bytes;
}

Value Value::string(const Rope* rope)
{
    Value v;
    v.kind = ValueKind::String;
    v.rope = rope;
    return v;
}

const std::string& Value::stringValue() const { return rope->flatten(); }
size_t Value::stringSize() const { return rope->size; }

Value Value::concat(const std::vector<Value>& strings)
{
    const Rope* result = nullptr;
    for (const Value& s : strings)
    {
        if (!s.rope->size) continue;
        if (result && s.rope->size > std::numeric_limits<size_t>::max() - result->size)
            throw std::length_error("Luna string is too long");
        result = result ? gcAlloc<Rope>(result, s.rope) : s.rope;
    }
    return result ? Value::string(result) : Value::string(std::string{});
}

void Vector::markChildren(std::vector<const GcCell*>& worklist) const
{
    for (const Value& v : items) if (const GcCell* c = v.cell()) worklist.push_back(c);
}

void Table::markChildren(std::vector<const GcCell*>& worklist) const
{
    for (const Entry& e : entries) if (e.live)
    {
        if (const GcCell* c = e.key.cell()) worklist.push_back(c);
        if (const GcCell* c = e.value.cell()) worklist.push_back(c);
    }
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

Value Value::of(const Syntax* syntax)
{
    Value v;
    v.kind = ValueKind::Syntax;
    v.syntax = syntax;
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
        return quoteString(v.stringValue());
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
    case ValueKind::Vector:
    {
        static thread_local std::set<const Vector*> printing;
        if (!printing.insert(v.vector).second) return "#<cycle>";
        std::string s = "#(";
        for (size_t i = 0; i < v.vector->items.size(); ++i) s += (i ? " " : "") + toString(v.vector->items[i]);
        printing.erase(v.vector);
        return s + ")";
    }
    case ValueKind::Table: return "#<table " + std::to_string(v.table->count) + ">";
    case ValueKind::Set: return "#<set " + std::to_string(v.table->count) + ">";
    case ValueKind::Parameter: return "#<parameter>";
    case ValueKind::Closure:
        return v.closure->name.empty() ? "#<procedure>" : "#<procedure " + v.closure->name + ">";
    case ValueKind::Builtin:
        return "#<builtin " + v.builtin->name + ">";
    case ValueKind::Macro:
        return "#<macro " + v.closure->name + ">";
    case ValueKind::Record:
        return v.record->show();
    case ValueKind::Syntax:
        return v.syntax->isIdentifier() ? "#<syntax " + v.syntax->text + ">" : "#<syntax>";
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
    case ValueKind::Vector: return "a vector";
    case ValueKind::Table: return "a table";
    case ValueKind::Set: return "a set";
    case ValueKind::Parameter: return "a parameter";
    case ValueKind::Record:
        return v.record->kindName();
    case ValueKind::Syntax:
        return "a syntax object";
    case ValueKind::Unassigned:
        return "an unassigned variable";
    }
    return "a value";
}

bool valuesIdentical(const Value& a, const Value& b)
{
    if (a.kind == b.kind && (a.kind == ValueKind::Pair || a.kind == ValueKind::Vector)) return a.cell() == b.cell();
    return valuesEqual(a, b);
}

bool valuesEqual(const Value& a, const Value& b)
{
    // Compiler code compares symbols and small atoms very frequently. Keep the
    // common path allocation-free; only compound comparisons need a worklist.
    if (a.kind != b.kind)
        return a.isNumber() && b.isNumber() &&
            (a.kind == ValueKind::Int ? double(a.intValue) : a.doubleValue) ==
            (b.kind == ValueKind::Int ? double(b.intValue) : b.doubleValue);
    switch (a.kind)
    {
    case ValueKind::Nil: case ValueKind::Unassigned: return true;
    case ValueKind::Bool: return a.boolValue == b.boolValue;
    case ValueKind::Int: return a.intValue == b.intValue;
    case ValueKind::Double: return a.doubleValue == b.doubleValue;
    case ValueKind::String: return a.stringValue() == b.stringValue();
    case ValueKind::Symbol: case ValueKind::Keyword: return a.text == b.text;
    case ValueKind::Pair: case ValueKind::Vector: break;
    default: return a.cell() == b.cell();
    }
    std::vector<std::pair<Value, Value>> todo{{a, b}};
    std::set<std::pair<const GcCell*, const GcCell*>> seen;
    while (!todo.empty())
    {
        auto pair = std::move(todo.back()); todo.pop_back();
        const Value& x = pair.first; const Value& y = pair.second;
        if (x.kind != y.kind)
        {
            if (x.isNumber() && y.isNumber() &&
                (x.kind == ValueKind::Int ? double(x.intValue) : x.doubleValue) ==
                (y.kind == ValueKind::Int ? double(y.intValue) : y.doubleValue)) continue;
            return false;
        }
        switch (x.kind)
        {
        case ValueKind::Nil: case ValueKind::Unassigned: break;
        case ValueKind::Bool: if (x.boolValue != y.boolValue) return false; break;
        case ValueKind::Int: if (x.intValue != y.intValue) return false; break;
        case ValueKind::Double: if (x.doubleValue != y.doubleValue) return false; break;
        case ValueKind::String: if (x.stringValue() != y.stringValue()) return false; break;
        case ValueKind::Symbol: case ValueKind::Keyword: if (x.text != y.text) return false; break;
        case ValueKind::Pair:
            if (seen.emplace(x.cell(), y.cell()).second)
            {
                todo.emplace_back(x.pair->car, y.pair->car);
                todo.emplace_back(x.pair->cdr, y.pair->cdr);
            }
            break;
        case ValueKind::Vector:
            if (x.vector->items.size() != y.vector->items.size()) return false;
            if (seen.emplace(x.cell(), y.cell()).second)
                for (size_t i = 0; i < x.vector->items.size(); ++i) todo.emplace_back(x.vector->items[i], y.vector->items[i]);
            break;
        default: if (x.cell() != y.cell()) return false; break;
        }
    }
    return true;
}

size_t valueHash(const Value& value, bool identity, const SExpr& call)
{
    size_t hash = 0;
    std::vector<Value> todo{value};
    while (!todo.empty())
    {
        Value v = std::move(todo.back()); todo.pop_back();
        if (v.kind == ValueKind::Double && std::isnan(v.doubleValue))
            throw CompileError(call, "NaN cannot be a table/set key");
        size_t part = size_t(v.kind);
        if (v.isNumber()) part = std::hash<double>{}(v.kind == ValueKind::Int ? double(v.intValue) : v.doubleValue);
        else if (v.kind == ValueKind::String) part ^= std::hash<std::string>{}(v.stringValue());
        else if (v.kind == ValueKind::Symbol || v.kind == ValueKind::Keyword) part ^= std::hash<std::string>{}(v.text);
        else if (v.kind == ValueKind::Bool) part ^= size_t(v.boolValue);
        else if (v.kind == ValueKind::Pair && !identity)
        {
            todo.push_back(v.pair->cdr); todo.push_back(v.pair->car);
        }
        else if (v.kind == ValueKind::Vector && !identity)
            throw CompileError(call, "Mutable vectors cannot be structural table/set keys; use :identity");
        else if (const GcCell* c = v.cell()) part ^= std::hash<const void*>{}(c);
        hash ^= part + size_t(0x9e3779b9) + (hash << 6) + (hash >> 2);
    }
    return hash;
}

size_t Table::find(const Value& key, const SExpr& call) const
{
    auto it = buckets.find(valueHash(key, identity, call));
    if (it != buckets.end()) for (size_t i : it->second)
        if (entries[i].live && (identity ? valuesIdentical(entries[i].key, key) : valuesEqual(entries[i].key, key))) return i;
    return size_t(-1);
}

bool Table::put(const Value& key, Value value, const SExpr& call)
{
    size_t i = find(key, call);
    if (i != size_t(-1)) { entries[i].value = std::move(value); return false; }
    size_t hash = valueHash(key, identity, call);
    buckets[hash].push_back(entries.size()); entries.push_back({key, std::move(value), true}); ++count;
    return true;
}

bool Table::erase(const Value& key, const SExpr& call)
{
    size_t i = find(key, call);
    if (i == size_t(-1)) return false;
    entries[i].live = false; entries[i].key = {}; entries[i].value = {}; --count;
    // Worklists repeatedly remove and reinsert keys. Reclaim tombstones without
    // changing the insertion order of surviving keys.
    if (count == 0) { entries.clear(); buckets.clear(); }
    else if (entries.size() > count * 2 + 64)
    {
        std::vector<Entry> compact;
        for (const Entry& e : entries) if (e.live) compact.push_back(e);
        entries = std::move(compact); buckets.clear();
        for (size_t j = 0; j < entries.size(); ++j) buckets[valueHash(entries[j].key, identity, call)].push_back(j);
    }
    return true;
}

Value datumFromSExpr(const SExpr& e, DatumOrigins* origins)
{
    switch (e.kind)
    {
    case SExprKind::Symbol:
        if (e.text == "#t" || e.text == "#f")
            return Value::boolean(e.text == "#t");
        {
            Value v = Value::symbol(e.text);
            v.datumSyntax = e.syntax;
            return v;
        }
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
        return SExpr::atom(SExprKind::String, v.stringValue(), loc);
    case ValueKind::Symbol:
        {
            SExpr e = SExpr::atom(SExprKind::Symbol, v.text, loc);
            e.syntax = v.datumSyntax;
            return e;
        }
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
    case ValueKind::Vector:
    case ValueKind::Table:
    case ValueKind::Set:
    case ValueKind::Parameter:
    case ValueKind::Record:
    case ValueKind::Unassigned:
    case ValueKind::Syntax:
        break;
    }
    throw CompileError(loc, "A macro expansion can't contain " + describeKind(v) + " (" + toString(v) + "); expand to code that refers to it by name");
}

} // namespace Luwu::Dsl
