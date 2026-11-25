// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Evaluator.h"
#include "Core/CompileError.h"

#include <limits>
#include <set>

namespace Luwu::Dsl
{
using Args = std::vector<Value>;

static Vector& vectorArg(const Value& v, const SExpr& call)
{
    if (v.kind != ValueKind::Vector) throw CompileError(call, "Expected a vector, got " + describeKind(v));
    return *v.vector;
}
static Table& tableArg(const Value& v, const SExpr& call, bool set = false)
{
    if (v.kind != (set ? ValueKind::Set : ValueKind::Table)) throw CompileError(call, set ? "Expected a set" : "Expected a table");
    return *v.table;
}
static size_t indexArg(const Value& v, size_t length, const SExpr& call, bool lengthArgument = false)
{
    if (v.kind != ValueKind::Int || v.intValue < 0 || (!lengthArgument && uint64_t(v.intValue) >= length))
        throw CompileError(call, "Expected a nonnegative integer " + std::string(lengthArgument ? "length" : "index in range"));
    return size_t(v.intValue);
}
static Args listArg(const Value& v, const SExpr& call)
{
    Args items;
    if (!listToVector(v, items)) throw CompileError(call, "Expected a proper list");
    return items;
}
static Value makeVector(Args items)
{
    Value v; v.kind = ValueKind::Vector; v.vector = gcAlloc<Vector>(); v.vector->items = std::move(items); return v;
}
static Value makeTable(bool set, bool identity)
{
    Value v; v.kind = set ? ValueKind::Set : ValueKind::Table; v.table = gcAlloc<Table>(); v.table->identity = identity; return v;
}
static bool modeArg(const Args& a, const SExpr& call)
{
    if (a.empty()) return false;
    if (a[0].kind != ValueKind::Keyword || (a[0].text != ":equal" && a[0].text != ":identity"))
        throw CompileError(call, "Comparison mode must be :equal or :identity");
    return a[0].text == ":identity";
}

void installCollections(Evaluator& ev)
{
    auto def = [&](const std::string& name, int min, int max, auto fn, const std::string& syntax, const std::string& docs) {
        defineBuiltin(*ev.globals(), name, min, max, fn, syntax, docs);
    };
    def("vector", 0, -1, [](const Args& a, const SExpr&) { return makeVector(a); }, "(vector value...)", "Creates a mutable vector.");
    def("make-vector", 1, 2, [](const Args& a, const SExpr& c) {
        size_t n = indexArg(a[0], 0, c, true);
        if (n > Args{}.max_size()) throw CompileError(c, "Vector length is too large");
        return makeVector(Args(n, a.size() == 2 ? a[1] : Value::nil()));
    }, "(make-vector length [fill])", "Creates a fixed-length mutable vector, filled with fill or nil.");
    def("vector?", 1, 1, [](const Args& a, const SExpr&) { return Value::boolean(a[0].kind == ValueKind::Vector); }, "(vector? value)", "Tests for a vector.");
    def("vector-length", 1, 1, [](const Args& a, const SExpr& c) { return Value::integer(int64_t(vectorArg(a[0], c).items.size())); }, "(vector-length v)", "Returns the vector length.");
    def("vector-ref", 2, 2, [](const Args& a, const SExpr& c) { const Args& v = vectorArg(a[0], c).items; return v[indexArg(a[1], v.size(), c)]; }, "(vector-ref v index)", "Reads a vector element; invalid indices raise an error.");
    def("vector-set!", 3, 3, [](const Args& a, const SExpr& c) { Args& v = vectorArg(a[0], c).items; v[indexArg(a[1], v.size(), c)] = a[2]; return Value::nil(); }, "(vector-set! v index value)", "Replaces a vector element.");
    def("vector->list", 1, 1, [](const Args& a, const SExpr& c) { return Value::list(vectorArg(a[0], c).items); }, "(vector->list v)", "Copies vector elements into a list.");
    def("list->vector", 1, 1, [](const Args& a, const SExpr& c) { return makeVector(listArg(a[0], c)); }, "(list->vector xs)", "Copies a proper list into a vector.");
    def("vector-copy", 1, 1, [](const Args& a, const SExpr& c) { return makeVector(vectorArg(a[0], c).items); }, "(vector-copy v)", "Makes a shallow vector copy.");

    def("make-parameter", 1, 1, [](const Args& a, const SExpr&) {
        Value v; v.kind = ValueKind::Parameter; v.parameter = gcAlloc<Parameter>(); v.parameter->value = a[0]; return v;
    }, "(make-parameter initial)", "Creates a parameter: (p) reads it and (p value) updates its current binding.");
    def("parameter?", 1, 1, [](const Args& a, const SExpr&) { return Value::boolean(a[0].kind == ValueKind::Parameter); }, "(parameter? value)", "Tests for a parameter.");
    def("%call-with-parameters", 2, 2, [&ev](const Args& a, const SExpr& c) {
        Args bindings = listArg(a[0], c);
        Evaluator::Roots saved(ev);
        std::set<Parameter*> seen;
        Args parameters, values;
        for (const Value& b : bindings)
        {
            Args pair = listArg(b, c);
            if (pair.size() != 2 || pair[0].kind != ValueKind::Parameter) throw CompileError(c, "parameterize bindings require a parameter and a value");
            if (!seen.insert(pair[0].parameter).second) throw CompileError(c, "parameterize binds one parameter twice");
            parameters.push_back(pair[0]); values.push_back(pair[1]); saved.values.push_back(pair[0].parameter->value);
        }
        // Parameters themselves remain rooted in the outer builtin's arguments.
        struct Restore
        {
            Args& parameters; Args& saved;
            ~Restore() { for (size_t i = 0; i < parameters.size(); ++i) parameters[i].parameter->value = saved[i]; }
        } restore{parameters, saved.values};
        for (size_t i = 0; i < parameters.size(); ++i) parameters[i].parameter->value = values[i];
        return ev.apply(a[1], {}, c);
    }, "(%call-with-parameters bindings thunk)", "Internal exception-safe dynamic binding used by parameterize.");

    for (bool set : {false, true})
    {
        std::string prefix = set ? "set" : "table";
        def("make-" + prefix, 0, 1, [set](const Args& a, const SExpr& c) { return makeTable(set, modeArg(a, c)); }, "(make-" + prefix + " [:equal|:identity])", "Creates an insertion-ordered collection with explicit key comparison.");
        def(prefix + "?", 1, 1, [set](const Args& a, const SExpr&) { return Value::boolean(a[0].kind == (set ? ValueKind::Set : ValueKind::Table)); }, "(" + prefix + "? value)", "Tests the collection type.");
        def(prefix + "-size", 1, 1, [set](const Args& a, const SExpr& c) { return Value::integer(int64_t(tableArg(a[0], c, set).count)); }, "(" + prefix + "-size collection)", "Returns the number of entries.");
        def(prefix + "-has?", 2, 2, [set](const Args& a, const SExpr& c) { return Value::boolean(tableArg(a[0], c, set).find(a[1], c) != size_t(-1)); }, "(" + prefix + "-has? collection key)", "Tests membership independently of the stored value.");
        def(prefix + "-delete!", 2, 2, [set](const Args& a, const SExpr& c) { return Value::boolean(tableArg(a[0], c, set).erase(a[1], c)); }, "(" + prefix + "-delete! collection key)", "Deletes a key and reports whether it existed.");
        def(prefix + "-copy", 1, 1, [set](const Args& a, const SExpr& c) {
            Table& source = tableArg(a[0], c, set); Value v = makeTable(set, source.identity);
            for (const auto& e : source.entries) if (e.live) v.table->put(e.key, e.value, c);
            return v;
        }, "(" + prefix + "-copy collection)", "Makes a shallow copy preserving comparison mode and insertion order.");
        def(set ? "set->list" : "table-keys", 1, 1, [set](const Args& a, const SExpr& c) {
            Args out; for (const auto& e : tableArg(a[0], c, set).entries) if (e.live) out.push_back(e.key); return Value::list(out);
        }, set ? "(set->list s)" : "(table-keys t)", "Returns keys in insertion order.");
    }
    def("table-ref", 2, 3, [](const Args& a, const SExpr& c) {
        Table& t = tableArg(a[0], c); size_t i = t.find(a[1], c);
        if (i != size_t(-1)) return t.entries[i].value;
        if (a.size() == 3) return a[2];
        throw CompileError(c, "table-ref: missing key " + toString(a[1]));
    }, "(table-ref t key [default])", "Looks up a value, raising an error on absence unless a default is supplied.");
    def("table-set!", 3, 3, [](const Args& a, const SExpr& c) { tableArg(a[0], c).put(a[1], a[2], c); return Value::nil(); }, "(table-set! t key value)", "Inserts or replaces a value without changing an existing key's order.");
    def("table-update!", 3, 4, [&ev](const Args& a, const SExpr& c) {
        Table& t = tableArg(a[0], c); size_t i = t.find(a[1], c);
        if (i == size_t(-1) && a.size() != 4) throw CompileError(c, "table-update!: missing key");
        Value value = ev.apply(a[2], {i == size_t(-1) ? a[3] : t.entries[i].value}, c);
        t.put(a[1], value, c); return value;
    }, "(table-update! t key procedure [default])", "Updates a value through a procedure; absence requires a default.");
    def("table-values", 1, 1, [](const Args& a, const SExpr& c) {
        Args out; for (const auto& e : tableArg(a[0], c).entries) if (e.live) out.push_back(e.value); return Value::list(out);
    }, "(table-values t)", "Returns values in insertion order.");
    def("set-add!", 2, 2, [](const Args& a, const SExpr& c) { return Value::boolean(tableArg(a[0], c, true).put(a[1], Value::boolean(true), c)); }, "(set-add! s value)", "Adds an element and reports whether it was new.");
    def("list->set", 1, 2, [](const Args& a, const SExpr& c) {
        Value s = makeTable(true, modeArg(a.size() == 2 ? Args{a[1]} : Args{}, c));
        for (const Value& x : listArg(a[0], c)) s.table->put(x, Value::boolean(true), c); return s;
    }, "(list->set xs [:equal|:identity])", "Creates a set in first-occurrence order.");
    for (std::string operation : {"union", "intersect", "difference", "="})
        def(operation == "=" ? "set=?" : "set-" + operation, 2, 2, [operation](const Args& a, const SExpr& c) {
            Table& x = tableArg(a[0], c, true); Table& y = tableArg(a[1], c, true);
            if (x.identity != y.identity) throw CompileError(c, "Set comparison modes must match");
            if (operation == "=")
            {
                if (x.count != y.count) return Value::boolean(false);
                for (const auto& e : x.entries) if (e.live && y.find(e.key, c) == size_t(-1)) return Value::boolean(false);
                return Value::boolean(true);
            }
            Value out = makeTable(true, x.identity);
            for (const auto& e : x.entries) if (e.live)
            {
                bool in = y.find(e.key, c) != size_t(-1);
                if (operation == "union" || (operation == "intersect" ? in : !in)) out.table->put(e.key, e.value, c);
            }
            if (operation == "union") for (const auto& e : y.entries) if (e.live) out.table->put(e.key, e.value, c);
            return out;
        }, operation == "=" ? "(set=? a b)" : "(set-" + operation + " a b)", "Compares or combines sets with matching comparison modes; inputs are unchanged.");
}
} // namespace Luwu::Dsl
