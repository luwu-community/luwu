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
    auto isDefine = [](const SExpr& e) { return e.isList() && !e.elements.empty() && (e.elements[0].isSymbol("define") || e.elements[0].isSymbol("define-macro") || e.elements[0].isSymbol("define-syntax")); };
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
    struct Binding
    {
        std::string name;
        ScopeSet scopes;
        std::string runtimeName;
    };
    std::vector<std::vector<Binding>> frames;

    const Binding* resolve(const SExpr& id) const
    {
        ScopeSet scopes = id.syntax ? id.syntax->scopes : ScopeSet{};
        const Binding* best = nullptr;
        for (auto frame = frames.rbegin(); frame != frames.rend(); ++frame)
            for (const Binding& b : *frame)
                if (b.name == id.text && b.scopes.isSubsetOf(scopes) && (!best || b.scopes.ids.size() > best->scopes.ids.size()))
                    best = &b;
        return best;
    }
};

// Scope metadata is persistent: adding a lexical scope to a copied form must
// not mutate the source form or a template retained by another macro.
static void detachSyntax(SExpr& e)
{
    e.syntax = e.syntax ? std::make_shared<SyntaxData>(*e.syntax) : std::make_shared<SyntaxData>();
}

static void patternVariables(const SExpr& pattern, const SExpr& literals, std::vector<SExpr>& names)
{
    if (pattern.isSymbol())
    {
        if (pattern.text == "_" || pattern.text == "..." || pattern.text == "." || pattern.text == "#t" || pattern.text == "#f") return;
        for (const SExpr& literal : literals.elements) if (literal.text == pattern.text) return;
        names.push_back(pattern);
    }
    else if (pattern.isList())
        for (const SExpr& part : pattern.elements) patternVariables(part, literals, names);
}

std::string Evaluator::syntaxBindingKey(const std::string& name, const ScopeSet& scopes) const
{
    const SyntaxBinding* best = nullptr;
    for (const SyntaxBinding& binding : syntaxBindings)
        if (binding.name == name && binding.scopes.isSubsetOf(scopes) &&
            (!best || binding.scopes.ids.size() > best->scopes.ids.size()))
            best = &binding;
    if (!best) return "global:" + name;
    std::string key = "local:" + name;
    for (uint32_t id : best->scopes.ids) key += ":" + std::to_string(id);
    return key;
}

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
        {
            if (!e.isSymbol())
                return e;
            SExpr out = e;
            if (const auto* binding = scope.resolve(e))
            {
                out.text = binding->runtimeName;
                if (out.syntax && out.syntax->globalBinding)
                {
                    detachSyntax(out);
                    out.syntax->globalBinding = false;
                }
            }
            else if (e.syntax && global->lookup(e.text))
            {
                for (uint32_t context : e.syntax->scopes.ids)
                    if (macroIntroScopes.count(context))
                    {
                        detachSyntax(out);
                        out.syntax->globalBinding = true;
                        break;
                    }
            }
            return out;
        }
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

    if (h == "syntax")
    {
        if (n != 2)
            throw CompileError(e, "syntax takes one template: (syntax template)");
        return e; // Templates contain identifiers and patterns, not applications.
    }
    if (h == "syntax-case")
    {
        if (n < 3 || !e.elements[2].isList())
            throw CompileError(e, "syntax-case is (syntax-case value (literals...) clauses...)");
        SExpr out = e;
        out.elements[1] = expandIn(e.elements[1], scope, false);
        for (const SExpr& lit : e.elements[2].elements)
            if (!lit.isSymbol()) throw CompileError(lit, "syntax-case literals must be identifiers");
        for (size_t i = 3; i < n; ++i)
        {
            if (!e.elements[i].isList() || e.elements[i].elements.size() < 2 || e.elements[i].elements.size() > 3)
                throw CompileError(e.elements[i], "A syntax-case clause is [pattern output] or [pattern fender output]");
            std::vector<SExpr> names;
            patternVariables(e.elements[i].elements[0], e.elements[2], names);
            scope.frames.emplace_back();
            for (const SExpr& name : names)
                scope.frames.back().push_back({name.text, name.syntax ? name.syntax->scopes : ScopeSet{}, name.text});
            for (size_t j = 1; j < e.elements[i].elements.size(); ++j)
                out.elements[i].elements[j] = expandIn(e.elements[i].elements[j], scope, false);
            scope.frames.pop_back();
        }
        return out;
    }
    if (h == "with-syntax")
    {
        if (n < 3 || !e.elements[1].isList())
            throw CompileError(e, "with-syntax is (with-syntax ([pattern expression]...) body...)");
        std::vector<SExpr> patterns, values;
        for (const SExpr& binding : e.elements[1].elements)
        {
            if (!binding.isList() || binding.elements.size() != 2)
                throw CompileError(binding, "A with-syntax binding is [pattern expression]");
            patterns.push_back(binding.elements[0]);
            values.push_back(binding.elements[1]);
        }
        SExpr clause = SExpr::list({SExpr::list(std::move(patterns), loc), SExpr::call("begin", rest(2), loc)}, loc);
        return expandIn(SExpr::call("syntax-case", {SExpr::call("#:list", std::move(values), loc),
                        SExpr::list({}, loc), clause}, loc), scope, false);
    }
    if (h == "syntax-rules")
    {
        if (n < 3 || !e.elements[1].isList())
            throw CompileError(e, "syntax-rules is (syntax-rules (literals...) [pattern template]...)");
        SExpr arg = SExpr::symbol("#:syntax-rules" + std::to_string(++gensymCounter), loc);
        std::vector<SExpr> clauses{arg, e.elements[1]};
        for (size_t i = 2; i < n; ++i)
        {
            const SExpr& clause = e.elements[i];
            if (!clause.isList() || clause.elements.size() != 2)
                throw CompileError(clause, "A syntax-rules clause is [pattern template]");
            clauses.push_back(SExpr::list({clause.elements[0], SExpr::call("syntax", {clause.elements[1]}, loc)}, clause.loc));
        }
        return expandIn(SExpr::call("lambda", {SExpr::list({arg}, loc),
                        SExpr::call("syntax-case", std::move(clauses), loc)}, loc), scope, false);
    }
    if (h == "define-syntax")
    {
        if (!topLevel)
            throw CompileError(e, "define-syntax is only allowed at the top level");
        if (n != 3 || !e.elements[1].isSymbol())
            throw CompileError(e, "define-syntax is (define-syntax name transformer)");
        const SExpr& transformer = keep(expandIn(e.elements[2], scope, false));
        Value fn = run(&transformer, global, nullptr, nullptr, nullptr);
        if (fn.kind != ValueKind::Closure)
            throw CompileError(e, "define-syntax requires a procedure transformer");
        Closure* named = gcAlloc<Closure>(*fn.closure);
        named->name = e.elements[1].text;
        named->docs = e.docs;
        if (e.trailingDoc) named->docs.push_back(*e.trailingDoc);
        fn.closure = named;
        fn.kind = ValueKind::Macro;
        global->define(e.elements[1].text, fn);
        return SExpr::call("begin", {}, loc);
    }

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
        SExpr copy = e;
        uint32_t lexical = allocScope();
        addScope(copy.elements[1], lexical);
        for (size_t i = 2; i < n; ++i)
            addScope(copy.elements[i], lexical);
        scope.frames.emplace_back();
        auto bind = [&](SExpr& id) {
            if (id.isSymbol(".")) return;
            std::string runtimeName = id.text;
            for (uint32_t context : id.syntax->scopes.ids)
                if (macroIntroScopes.count(context))
                {
                    runtimeName = "#:h" + std::to_string(lexical) + ":" + id.text;
                    break;
                }
            scope.frames.back().push_back({id.text, id.syntax->scopes, runtimeName});
            syntaxBindings.push_back({id.text, id.syntax->scopes});
            id.text = runtimeName;
        };
        if (copy.elements[1].isSymbol()) bind(copy.elements[1]);
        else for (SExpr& id : copy.elements[1].elements) bind(id);
        std::vector<SExpr> out = {copy.elements[0], copy.elements[1]};
        for (auto& b : expandBody(copy, 2, scope))
            out.push_back(std::move(b));
        scope.frames.pop_back();
        copy.elements = std::move(out);
        return copy;
    }
    if (h == "define")
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
            {
                // Detach the child before assignment replaces its containing vector.
                SExpr rest = params.elements[1];
                params = std::move(rest); // Collects all arguments into a single rest parameter.
            }
            std::vector<SExpr> lambda = {SExpr::symbol("lambda", loc), params};
            for (auto& b : rest(2))
                lambda.push_back(std::move(b));
            SExpr value = SExpr::list(std::move(lambda), loc);
            result = SExpr::list({SExpr::symbol("define", loc), target.elements[0], value}, loc);
        }
        else
        {
            if (!target.isSymbol() || n != 3)
                throw CompileError(e, "define takes a name and a value: (define name value)");
            result = e;
            if (!scope.frames.empty())
            {
                ScopeSet context = target.syntax ? target.syntax->scopes : ScopeSet{};
                std::string name = target.text;
                for (uint32_t id : context.ids)
                    if (macroIntroScopes.count(id))
                    {
                        name = "#:d" + std::to_string(allocScope()) + ":" + target.text;
                        break;
                    }
                scope.frames.back().push_back({target.text, context, name});
                syntaxBindings.push_back({target.text, context});
                result.elements[1].text = name;
            }
            result.elements[2] = expandIn(e.elements[2], scope, false);
            return result;
        }
        result.docs = e.docs;
        result.trailingDoc = e.trailingDoc;
        return expandIn(result, scope, topLevel);
    }
    if (h == "set!")
    {
        if (n != 3 || !e.elements[1].isSymbol())
            throw CompileError(e, "set! takes a name and a value: (set! name value)");
        SExpr copy = e;
        copy.elements[1] = expandIn(e.elements[1], scope, false);
        copy.elements[2] = expandIn(e.elements[2], scope, false);
        return copy;
    }
    if (h == "doc-comment")
        return expandIn(expandDocComment(e, *this), scope, topLevel);

    // Macro expansion for identifiers not shadowed by local bindings.
    if (!scope.resolve(e.elements[0]))
    {
        const Value* v = global->lookup(h);
        if (v && v->isMacro())
        {
            uint32_t intro = allocScope();
            macroIntroScopes.insert(intro);
            Value input = syntaxFromSExpr(e);
            addScope(const_cast<Syntax*>(input.syntax), intro);
            Roots roots(*this, {input});
            Value transformer = *v;
            transformer.kind = ValueKind::Closure;
            DatumOrigins origins;
            expansions.push_back({&e, &origins, topLevel, &roots.values});
            uint32_t previous = currentIntroScope;
            currentIntroScope = intro;
            Value result;
            try { result = apply(transformer, {input}, e); }
            catch (...) { currentIntroScope = previous; expansions.pop_back(); throw; }
            currentIntroScope = previous;
            expansions.pop_back();
            if (!result.isSyntax())
                throw CompileError(e, "A define-syntax transformer must return a syntax object");
            SExpr expanded = sexprFromSyntax(result);
            flipScope(expanded, intro);
            return expandIn(carryDocs(e, std::move(expanded)), scope, topLevel);
        }
    }

    // Procedure or builtin invocation.
    SExpr copy = e;
    for (size_t i = 0; i < n; ++i)
        copy.elements[i] = expandIn(e.elements[i], scope, false);
    return copy;
}

} // namespace Luwu::Dsl
