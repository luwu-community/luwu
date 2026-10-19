// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Evaluator.h"
#include "Core/Doc.h"

#include "Core/CompileError.h"

#include <set>

namespace Luwu::Dsl
{

// Propagates doc comments from a macro call onto its expansion, attaching them to the first inner definition or re-wrapping the expansion.
static SExpr carryDocs(const SExpr& call, SExpr expansion)
{
    if (!call.hasDocs())
        return expansion;
    auto isDefine = [](const SExpr& e) { return e.isList() && !e.elements.empty() && (e.elements[0].isSymbol("define") || e.elements[0].isSymbol("define-macro")); };
    auto document = [&](SExpr& e) {
        e.docs.insert(e.docs.begin(), call.docs.begin(), call.docs.end());
        if (call.trailingDoc)
            e.trailingDoc = call.trailingDoc;
    };
    if (isDefine(expansion))
    {
        document(expansion);
        return expansion;
    }
    if (expansion.isList("begin"))
        for (auto& e : expansion.elements)
            if (isDefine(e))
            {
                document(e);
                return expansion;
            }
    SExpr wrapped;
    wrapped.kind = SExprKind::List;
    wrapped.loc = expansion.loc;
    wrapped.endLine = expansion.endLine;
    wrapped.elements.push_back(SExpr::atom(SExprKind::Symbol, "doc-comment", expansion.loc));
    for (const auto& line : call.docs)
        wrapped.elements.push_back(SExpr::atom(SExprKind::String, line, expansion.loc));
    if (call.trailingDoc)
    {
        if (call.docs.empty())
            wrapped.elements.push_back(SExpr::atom(SExprKind::Keyword, ":trailing", expansion.loc));
        else
        {
            // When both leading and trailing comments exist, wrap the expansion in the trailing comment first.
            SExpr inner = wrapped;
            inner.elements = {SExpr::atom(SExprKind::Symbol, "doc-comment", expansion.loc), SExpr::atom(SExprKind::Keyword, ":trailing", expansion.loc), SExpr::atom(SExprKind::String, *call.trailingDoc, expansion.loc), expansion};
            wrapped.elements.push_back(inner);
            return wrapped;
        }
        wrapped.elements.push_back(SExpr::atom(SExprKind::String, *call.trailingDoc, expansion.loc));
    }
    wrapped.elements.push_back(expansion);
    return wrapped;
}

void parseParams(const SExpr& list, std::vector<std::string>& params, std::string& rest);

// Tracks lexical bindings enclosing an expanded form so local variables can shadow macro identifiers.
struct Evaluator::ExpandScope
{
    std::vector<std::set<std::string>> frames;

    bool bound(const std::string& name) const
    {
        for (const auto& f : frames)
            if (f.count(name))
                return true;
        return false;
    }
};

static bool isPrefixForm(const SExpr& e, const char* name)
{
    return e.kind == SExprKind::List && e.elements.size() == 2 && e.elements[0].isSymbol(name);
}

// Expands a quasiquote template into explicit list construction calls at the specified nesting depth.
static SExpr expandTemplate(const SExpr& t, int depth)
{
    const SourceLocation& loc = t.loc;
    auto quoted = [&](const SExpr& x) { return SExpr::call("quote", {x}, loc); };

    if (isPrefixForm(t, "unquote"))
    {
        if (depth == 1)
            return t.elements[1];
        return SExpr::call("#:list", {quoted(t.elements[0]), expandTemplate(t.elements[1], depth - 1)}, loc);
    }
    if (isPrefixForm(t, "unquote-splicing"))
    {
        if (depth == 1)
            throw CompileError(t, ",@ splices into a list; use it as a list element");
        return SExpr::call("#:list", {quoted(t.elements[0]), expandTemplate(t.elements[1], depth - 1)}, loc);
    }
    if (isPrefixForm(t, "quasiquote"))
        return SExpr::call("#:list", {quoted(t.elements[0]), expandTemplate(t.elements[1], depth + 1)}, loc);

    if (!t.isList())
        return t.isSymbol() ? quoted(t) : t; // numbers, strings and keywords evaluate to themselves
    if (t.elements.empty())
        return SExpr::symbol("nil", loc);

    // Handles improper lists by appending elements to the dotted tail.
    size_t n = t.elements.size();
    if (n >= 3 && t.elements[n - 2].isSymbol("."))
    {
        SExpr head = t;
        head.elements.resize(n - 2);
        return SExpr::call("#:append", {expandTemplate(head, depth), expandTemplate(t.elements[n - 1], depth)}, loc);
    }

    // Groups runs of literal elements into #:list calls and splices unquote-splicing elements directly.
    std::vector<SExpr> segments;
    std::vector<SExpr> run;
    auto flush = [&]() {
        if (!run.empty())
            segments.push_back(SExpr::call("#:list", std::move(run), loc));
        run.clear();
    };
    for (const auto& el : t.elements)
    {
        if (depth == 1 && isPrefixForm(el, "unquote-splicing"))
        {
            flush();
            segments.push_back(el.elements[1]);
        }
        else
        {
            run.push_back(expandTemplate(el, depth));
        }
    }
    flush();
    if (segments.size() == 1 && segments[0].isList("#:list"))
        return segments[0];
    return SExpr::call("#:append", std::move(segments), loc);
}

SExpr Evaluator::expand(const SExpr& form, bool topLevel)
{
    ExpandScope scope;
    return expandIn(form, scope, topLevel);
}

std::vector<SExpr> Evaluator::expandBody(const SExpr& form, size_t start, ExpandScope& scope)
{
    std::vector<SExpr> out;
    for (size_t i = start; i < form.elements.size(); ++i)
        out.push_back(expandIn(form.elements[i], scope, false));
    return out;
}

SExpr Evaluator::expandIn(const SExpr& e, ExpandScope& scope, bool topLevel)
{
    if (!e.isList() || e.elements.empty() || !e.elements[0].isSymbol())
    {
        if (!e.isList() || e.elements.empty())
            return e;
        // Expands higher-order applications where the operator is an expression: ((lambda ...) args).
        SExpr copy = e;
        for (auto& el : copy.elements)
            el = expandIn(el, scope, false);
        return copy;
    }

    const std::string& h = e.elements[0].text;
    const SourceLocation& loc = e.loc;
    size_t n = e.elements.size();
    auto rest = [&](size_t from) { return std::vector<SExpr>(e.elements.begin() + from, e.elements.end()); };

    // ---- Core forms
    if (h == "quote")
    {
        if (n != 2)
            throw CompileError(e, "quote takes one datum: (quote x)");
        return e;
    }
    if (h == "quasiquote")
    {
        if (n != 2)
            throw CompileError(e, "quasiquote takes one template: `x");
        return expandIn(expandTemplate(e.elements[1], 1), scope, false);
    }
    if (h == "unquote" || h == "unquote-splicing")
        throw CompileError(e, std::string(h == "unquote" ? "," : ",@") + " is only allowed inside a quasiquote (`)");
    if (h == "if")
    {
        if (n != 3 && n != 4)
            throw CompileError(e, "if takes a condition, a then branch and an optional else branch");
        SExpr copy = e;
        for (size_t i = 1; i < n; ++i)
            copy.elements[i] = expandIn(e.elements[i], scope, false);
        return copy;
    }
    if (h == "begin")
    {
        SExpr copy = e;
        for (size_t i = 1; i < n; ++i)
            copy.elements[i] = expandIn(e.elements[i], scope, topLevel);
        return copy;
    }
    if (h == "lambda")
    {
        if (n < 2)
            throw CompileError(e, "lambda takes parameters and a body: (lambda (a b) body...)");
        std::vector<std::string> params;
        std::string restParam;
        parseParams(e.elements[1], params, restParam);
        scope.frames.emplace_back(params.begin(), params.end());
        if (!restParam.empty())
            scope.frames.back().insert(restParam);
        std::vector<SExpr> out = {e.elements[0], e.elements[1]};
        for (auto& b : expandBody(e, 2, scope))
            out.push_back(std::move(b));
        scope.frames.pop_back();
        SExpr copy = e;
        copy.elements = std::move(out);
        return copy;
    }
    if (h == "define" || h == "define-macro")
    {
        if (n < 2)
            throw CompileError(e, h + " takes a name and a value, or (" + h + " (name params...) body...)");
        const SExpr& target = e.elements[1];
        SExpr result;
        if (target.isList())
        {
            // Desugars function definition syntax (define (name params...) body...) into lambda binding.
            if (target.elements.empty() || !target.elements[0].isSymbol())
                throw CompileError(target, h + "'s function form is (" + h + " (name params...) body...)");
            SExpr params = target;
            params.elements.erase(params.elements.begin());
            if (params.elements.size() == 2 && params.elements[0].isSymbol("."))
                params = params.elements[1]; // Collects all arguments into a single rest parameter.
            std::vector<SExpr> lambda = {SExpr::symbol("lambda", loc), params};
            for (auto& b : rest(2))
                lambda.push_back(std::move(b));
            SExpr value = SExpr::list(std::move(lambda), loc);
            if (h == "define-macro")
                value = SExpr::call("%make-macro", {value}, loc);
            result = SExpr::list({SExpr::symbol("define", loc), target.elements[0], value}, loc);
        }
        else
        {
            if (h == "define-macro")
                throw CompileError(e, "define-macro is (define-macro (name params...) body...)");
            if (!target.isSymbol() || n != 3)
                throw CompileError(e, "define takes a name and a value: (define name value)");
            result = e;
            result.elements[2] = expandIn(e.elements[2], scope, false);
            if (!scope.frames.empty())
                scope.frames.back().insert(target.text);
            return result;
        }
        if (h == "define-macro" && !topLevel)
            throw CompileError(e, "define-macro is only allowed at the top level (where the expander can see it before its uses)");
        result.docs = e.docs;
        result.trailingDoc = e.trailingDoc;
        return expandIn(result, scope, topLevel);
    }
    if (h == "set!")
    {
        if (n != 3 || !e.elements[1].isSymbol())
            throw CompileError(e, "set! takes a name and a value: (set! name value)");
        SExpr copy = e;
        copy.elements[2] = expandIn(e.elements[2], scope, false);
        return copy;
    }
    if (h == "doc-comment")
        return expandIn(expandDocComment(e, *this), scope, topLevel);

    // Macro expansion for identifiers not shadowed by local bindings.
    if (!scope.bound(h))
    {
        const Value* v = global->lookup(h);
        if (v && v->isMacro())
        {
            DatumOrigins origins;
            std::vector<Value> args;
            for (size_t i = 1; i < n; ++i)
                args.push_back(datumFromSExpr(e.elements[i], &origins));
            Value expander = *v;
            expander.kind = ValueKind::Closure; // Treat macro as a procedure during expansion.
            expansions.push_back({&e, &origins, topLevel, &args});
            Value expansion;
            try
            {
                expansion = apply(expander, args, e);
            }
            catch (...)
            {
                expansions.pop_back();
                throw;
            }
            expansions.pop_back();
            // An empty list expansion denotes nil, as () is not a valid expression.
            if (expansion.isNil())
                return SExpr::symbol("nil", loc);
            return expandIn(carryDocs(e, sexprFromDatum(expansion, origins, loc)), scope, topLevel);
        }
    }

    // Procedure or builtin invocation.
    SExpr copy = e;
    for (size_t i = 0; i < n; ++i)
        copy.elements[i] = expandIn(e.elements[i], scope, false);
    return copy;
}

} // namespace Luwu::Dsl
