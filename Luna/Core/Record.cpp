// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Record.h"

#include "Core/CompileError.h"
#include "Core/Evaluator.h"

namespace Luwu::Dsl
{

std::string Record::show() const
{
    std::string s = "#<" + type;
    for (size_t i = 0; i < fields.size(); ++i)
        s += " " + fields[i] + ": " + toString(values[i]);
    return s + ">";
}

bool isRecord(const Value& v, const std::string& type)
{
    return v.kind == ValueKind::Record && (type.empty() || v.record->type == type);
}

const Record& recordOf(const Value& v)
{
    return *v.record;
}

const Value* recordField(const Value& v, const std::string& field)
{
    if (!isRecord(v))
        return nullptr;
    const Record& r = recordOf(v);
    for (size_t i = 0; i < r.fields.size(); ++i)
        if (r.fields[i] == field)
            return &r.values[i];
    return nullptr;
}

Value recordWith(const Value& v, const std::string& field, const Value& value)
{
    auto copy = gcAlloc<Record>(recordOf(v));
    for (size_t i = 0; i < copy->fields.size(); ++i)
        if (copy->fields[i] == field)
            copy->values[i] = value;
    return Value::of(copy);
}

void installRecords(Evaluator& evaluator)
{
    using Args = std::vector<Value>;
    Env& env = *evaluator.globals();
    auto symbolArg = [](const Value& v, const std::string& fn, const SExpr& c) {
        if (v.kind != ValueKind::Symbol)
            throw CompileError(c, fn + " takes a symbol, got " + toString(v));
        return v.text;
    };
    defineBuiltin(env, "%make-record", 2, -1, [symbolArg](const Args& a, const SExpr& c) {
        auto r = gcAlloc<Record>();
        r->type = symbolArg(a[0], "%make-record", c);
        std::vector<Value> fields;
        listToVector(a[1], fields);
        if (fields.size() != a.size() - 2)
            throw CompileError(c, "A " + r->type + " record has " + std::to_string(fields.size()) + " fields, got " + std::to_string(a.size() - 2) + " values");
        for (size_t i = 0; i < fields.size(); ++i)
        {
            r->fields.push_back(symbolArg(fields[i], "%make-record", c));
            r->values.push_back(a[i + 2]);
        }
        return Value::of(r);
    });
    defineBuiltin(env, "record?", 1, 2, [symbolArg](const Args& a, const SExpr& c) {
        return Value::boolean(isRecord(a[0], a.size() == 2 ? symbolArg(a[1], "record?", c) : ""));
    }, "(record? x ['type])", "Returns true if x is a record, optionally matching the specified record type symbol.");
    defineBuiltin(env, "record-type", 1, 1, [](const Args& a, const SExpr& c) {
        if (!isRecord(a[0]))
            throw CompileError(c, "record-type takes a record, got " + toString(a[0]));
        return Value::symbol(recordOf(a[0]).type);
    }, "(record-type r)", "Returns the type symbol of record r.");
    defineBuiltin(env, "record-ref", 2, 2, [symbolArg](const Args& a, const SExpr& c) {
        const Value* v = recordField(a[0], symbolArg(a[1], "record-ref", c));
        if (!v)
            throw CompileError(c, "record-ref: " + toString(a[0]) + " has no field " + a[1].text);
        return *v;
    }, "(record-ref r 'field)", "Returns the value of the specified field in record r.");
    defineBuiltin(env, "record-with", 3, 3, [symbolArg](const Args& a, const SExpr& c) {
        if (!recordField(a[0], symbolArg(a[1], "record-with", c)))
            throw CompileError(c, "record-with: " + toString(a[0]) + " has no field " + a[1].text);
        return recordWith(a[0], a[1].text, a[2]);
    }, "(record-with r 'field v)", "Returns a copy of record r with the specified field updated to value v.");
}

} // namespace Luwu::Dsl
