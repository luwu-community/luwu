// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Syntax.h"

#include "Core/CompileError.h"
#include "Core/Evaluator.h"

#include <algorithm>
#include <set>

namespace Luwu::Dsl
{

static uint32_t gNextScope = 1;

uint32_t allocScope()
{
    return gNextScope++;
}

void addScope(Syntax* stx, uint32_t scope)
{
    if (!stx) return;
    stx->scopes.add(scope);
    for (auto& item : stx->items)
    {
        if (item.isSyntax())
            addScope(const_cast<Syntax*>(item.syntax), scope);
    }
}

void removeScope(Syntax* stx, uint32_t scope)
{
    if (!stx) return;
    stx->scopes.remove(scope);
    for (auto& item : stx->items)
    {
        if (item.isSyntax())
            removeScope(const_cast<Syntax*>(item.syntax), scope);
    }
}

void flipScope(Syntax* stx, uint32_t scope)
{
    if (!stx) return;
    stx->scopes.flip(scope);
    for (auto& item : stx->items)
    {
        if (item.isSyntax())
            flipScope(const_cast<Syntax*>(item.syntax), scope);
    }
}

void addScope(SExpr& expr, uint32_t scope)
{
    if (expr.syntax)
        expr.syntax = std::make_shared<SyntaxData>(*expr.syntax);
    if (!expr.syntax)
    {
        expr.syntax = std::make_shared<SyntaxData>();
        expr.syntax->loc = expr.loc;
        expr.syntax->docs = expr.docs;
        expr.syntax->trailingDoc = expr.trailingDoc;
    }
    expr.syntax->scopes.add(scope);
    for (auto& el : expr.elements)
        addScope(el, scope);
}

void flipScope(SExpr& expr, uint32_t scope)
{
    if (expr.syntax)
        expr.syntax = std::make_shared<SyntaxData>(*expr.syntax);
    if (!expr.syntax)
    {
        expr.syntax = std::make_shared<SyntaxData>();
        expr.syntax->loc = expr.loc;
        expr.syntax->docs = expr.docs;
        expr.syntax->trailingDoc = expr.trailingDoc;
    }
    expr.syntax->scopes.flip(scope);
    for (auto& el : expr.elements)
        flipScope(el, scope);
}

Value syntaxFromSExpr(const SExpr& expr, const ScopeSet& initialScopes)
{
    Syntax* s = gcAlloc<Syntax>();
    s->loc = expr.loc;
    s->docs = expr.docs;
    s->trailingDoc = expr.trailingDoc;
    s->scopes = expr.syntax ? expr.syntax->scopes : initialScopes;

    switch (expr.kind)
    {
    case SExprKind::Symbol:
        if (expr.text == "#t" || expr.text == "#f")
        {
            s->formKind = SyntaxFormKind::Constant;
            s->datum = Value::boolean(expr.text == "#t");
        }
        else
        {
            s->formKind = SyntaxFormKind::Identifier;
            s->text = expr.text;
        }
        return Value::of(s);
    case SExprKind::Keyword:
        s->formKind = SyntaxFormKind::Identifier;
        s->text = expr.text;
        return Value::of(s);
    case SExprKind::Number:
    case SExprKind::String:
        s->formKind = SyntaxFormKind::Constant;
        s->datum = datumFromSExpr(expr);
        return Value::of(s);
    case SExprKind::BracketList:
    case SExprKind::List:
    {
        s->formKind = SyntaxFormKind::List;
        s->isBracket = (expr.kind == SExprKind::BracketList);
        size_t n = expr.elements.size();
        bool dotted = n >= 3 && expr.elements[n - 2].isSymbol(".");
        s->dotted = dotted;

        if (dotted)
        {
            for (size_t i = 0; i < n - 2; ++i)
                s->items.push_back(syntaxFromSExpr(expr.elements[i], s->scopes));
            s->items.push_back(syntaxFromSExpr(expr.elements[n - 1], s->scopes));
        }
        else
        {
            for (size_t i = 0; i < n; ++i)
                s->items.push_back(syntaxFromSExpr(expr.elements[i], s->scopes));
        }
        return Value::of(s);
    }
    }
    return Value::nil();
}

SExpr sexprFromSyntax(const Value& val)
{
    if (!val.isSyntax())
    {
        return sexprFromDatum(val, {}, SourceLocation{});
    }

    const Syntax& s = *val.syntax;
    switch (s.formKind)
    {
    case SyntaxFormKind::Identifier:
    {
        bool kw = s.text.rfind(':', 0) == 0;
        SExpr e = SExpr::atom(kw ? SExprKind::Keyword : SExprKind::Symbol, s.text, s.loc);
        e.docs = s.docs;
        e.trailingDoc = s.trailingDoc;
        e.syntax = std::make_shared<SyntaxData>();
        e.syntax->scopes = s.scopes;
        e.syntax->loc = s.loc;
        e.syntax->docs = s.docs;
        e.syntax->trailingDoc = s.trailingDoc;
        return e;
    }
    case SyntaxFormKind::Constant:
    {
        SExpr e = sexprFromDatum(s.datum, {}, s.loc);
        e.docs = s.docs;
        e.trailingDoc = s.trailingDoc;
        e.syntax = std::make_shared<SyntaxData>();
        e.syntax->scopes = s.scopes;
        e.syntax->loc = s.loc;
        e.syntax->docs = s.docs;
        e.syntax->trailingDoc = s.trailingDoc;
        return e;
    }
    case SyntaxFormKind::Vector:
    case SyntaxFormKind::List:
    {
        SExpr e;
        e.kind = s.isBracket ? SExprKind::BracketList : SExprKind::List;
        e.loc = s.loc;
        e.endLine = s.loc.line;
        e.docs = s.docs;
        e.trailingDoc = s.trailingDoc;
        e.syntax = std::make_shared<SyntaxData>();
        e.syntax->scopes = s.scopes;
        e.syntax->loc = s.loc;
        e.syntax->docs = s.docs;
        e.syntax->trailingDoc = s.trailingDoc;

        if (s.dotted && s.items.size() >= 2)
        {
            for (size_t i = 0; i < s.items.size() - 1; ++i)
                e.elements.push_back(sexprFromSyntax(s.items[i]));
            e.elements.push_back(SExpr::atom(SExprKind::Symbol, ".", s.loc));
            e.elements.push_back(sexprFromSyntax(s.items.back()));
        }
        else
        {
            for (const auto& item : s.items)
                e.elements.push_back(sexprFromSyntax(item));
        }
        return e;
    }
    }
    return SExpr{};
}

Value syntaxToDatum(const Value& stx)
{
    if (!stx.isSyntax())
        return stx;

    const Syntax& s = *stx.syntax;
    switch (s.formKind)
    {
    case SyntaxFormKind::Identifier:
        if (s.text.rfind(':', 0) == 0)
            return Value::keyword(s.text);
        if (s.text == "#t" || s.text == "#f")
            return Value::boolean(s.text == "#t");
        return Value::symbol(s.text);
    case SyntaxFormKind::Constant:
        return s.datum;
    case SyntaxFormKind::List:
    case SyntaxFormKind::Vector:
    {
        std::vector<Value> items;
        items.reserve(s.items.size());
        for (const auto& item : s.items)
            items.push_back(syntaxToDatum(item));

        if (s.dotted && items.size() >= 2)
        {
            Value list = items.back();
            for (size_t i = items.size() - 1; i-- > 0;)
                list = Value::cons(items[i], std::move(list));
            return list;
        }
        return Value::list(items);
    }
    }
    return Value::nil();
}

Value datumToSyntax(const Value& ctxt, const Value& datum, const std::optional<SourceLocation>& loc)
{
    ScopeSet scopes;
    SourceLocation useLoc;
    std::vector<std::string> docs;
    std::optional<std::string> trailingDoc;

    if (ctxt.isSyntax())
    {
        scopes = ctxt.syntax->scopes;
        useLoc = ctxt.syntax->loc;
        docs = ctxt.syntax->docs;
        trailingDoc = ctxt.syntax->trailingDoc;
    }
    if (loc.has_value())
        useLoc = *loc;

    if (datum.isSyntax())
        return datum;

    Syntax* s = gcAlloc<Syntax>();
    s->scopes = scopes;
    s->loc = useLoc;
    s->docs = docs;
    s->trailingDoc = trailingDoc;

    switch (datum.kind)
    {
    case ValueKind::Symbol:
        s->formKind = SyntaxFormKind::Identifier;
        s->text = datum.text;
        return Value::of(s);
    case ValueKind::Keyword:
        s->formKind = SyntaxFormKind::Identifier;
        s->text = datum.text;
        return Value::of(s);
    case ValueKind::Bool:
    case ValueKind::Int:
    case ValueKind::Double:
    case ValueKind::String:
        s->formKind = SyntaxFormKind::Constant;
        s->datum = datum;
        return Value::of(s);
    case ValueKind::Nil:
        s->formKind = SyntaxFormKind::List;
        return Value::of(s);
    case ValueKind::Pair:
    {
        s->formKind = SyntaxFormKind::List;
        const Value* cur = &datum;
        while (cur->isPair())
        {
            s->items.push_back(datumToSyntax(ctxt, cur->pair->car, loc));
            cur = &cur->pair->cdr;
        }
        if (!cur->isNil())
        {
            s->dotted = true;
            s->items.push_back(datumToSyntax(ctxt, *cur, loc));
        }
        return Value::of(s);
    }
    case ValueKind::Vector:
    {
        s->formKind = SyntaxFormKind::Vector;
        for (const auto& item : datum.vector->items)
            s->items.push_back(datumToSyntax(ctxt, item, loc));
        return Value::of(s);
    }
    default:
        break;
    }
    s->formKind = SyntaxFormKind::Constant;
    s->datum = datum;
    return Value::of(s);
}

bool isIdentifier(const Value& val)
{
    return val.isSyntax() && val.syntax->isIdentifier();
}

bool boundIdentifierEqual(const Value& a, const Value& b)
{
    if (!isIdentifier(a) || !isIdentifier(b))
        return false;
    return a.syntax->text == b.syntax->text && a.syntax->scopes == b.syntax->scopes;
}

bool freeIdentifierEqual(const Value& a, const Value& b)
{
    if (!isIdentifier(a) || !isIdentifier(b))
        return false;
    // For free identifier comparison in current scope, if names match and scopes match:
    return a.syntax->text == b.syntax->text && a.syntax->scopes == b.syntax->scopes;
}

static void collectPatVars(const SExpr& pat, const std::vector<SExpr>& literals, std::set<std::string>& out)
{
    if (pat.isSymbol())
    {
        if (pat.text != "_" && pat.text != "..." && pat.text != "#t" && pat.text != "#f" &&
            std::none_of(literals.begin(), literals.end(), [&](const SExpr& lit) { return lit.text == pat.text; }))
        {
            out.insert(pat.text);
        }
    }
    else if (pat.isList())
    {
        for (const auto& el : pat.elements)
            collectPatVars(el, literals, out);
    }
}

bool matchPattern(
    const SExpr& pat,
    const Value& target,
    const std::vector<SExpr>& literals,
    PatternEnv& env,
    int currentDepth,
    Evaluator& eval,
    const SExpr& call)
{
    if (pat.isSymbol())
    {
        if (pat.text == "_")
            return true; // Wildcard matches anything without binding
        if (pat.text == "#t" || pat.text == "#f")
            return valuesEqual(syntaxToDatum(target), Value::boolean(pat.text == "#t"));

        for (const auto& lit : literals)
        {
            if (pat.text == lit.text)
            {
                if (!isIdentifier(target)) return false;
                return target.syntax->text == pat.text &&
                    eval.syntaxBindingKey(lit.text, lit.syntax ? lit.syntax->scopes : ScopeSet{}) ==
                    eval.syntaxBindingKey(target.syntax->text, target.syntax->scopes);
            }
        }

        // Pattern variable: binds target at currentDepth
        PatternTree tree;
        tree.depth = currentDepth;
        tree.leaf = target;
        env[pat.text] = std::move(tree);
        return true;
    }

    if (pat.isList())
    {
        if (!target.isSyntax() || !target.syntax->isList())
            return false;

        const auto& patElems = pat.elements;
        const auto& tgtItems = target.syntax->items;
        bool dotted = patElems.size() >= 3 && patElems[patElems.size() - 2].isSymbol(".");
        size_t patCount = patElems.size() - (dotted ? 2 : 0);
        size_t tgtCount = tgtItems.size() - (target.syntax->dotted ? 1 : 0);
        auto tailAfter = [&](size_t count) {
            if (count == tgtCount && target.syntax->dotted) return tgtItems.back();
            Syntax* tail = gcAlloc<Syntax>(*target.syntax);
            tail->items.assign(tgtItems.begin() + count, tgtItems.end());
            return Value::of(tail);
        };
        if (!dotted && target.syntax->dotted) return false;

        // Check for ellipsis in pattern elements
        size_t ellipIdx = static_cast<size_t>(-1);
        for (size_t i = 1; i < patCount; ++i)
        {
            if (patElems[i].isSymbol("..."))
            {
                ellipIdx = i;
                break;
            }
        }

        if (ellipIdx == static_cast<size_t>(-1))
        {
            // Simple list match without ellipsis
            if (dotted ? patCount > tgtCount : patCount != tgtCount)
                return false;
            for (size_t i = 0; i < patCount; ++i)
            {
                if (!matchPattern(patElems[i], tgtItems[i], literals, env, currentDepth, eval, call))
                    return false;
            }
            return !dotted || matchPattern(patElems.back(), tailAfter(patCount), literals, env, currentDepth, eval, call);
        }

        // Ellipsis matching: patElems[ellipIdx - 1] is repeated
        size_t repIdx = ellipIdx - 1;
        size_t prefixCount = repIdx;
        size_t suffixCount = patCount - 1 - ellipIdx;
        size_t minItems = prefixCount + suffixCount;

        if (tgtCount < minItems)
            return false;

        size_t numRepeats = tgtCount - minItems;

        // Match prefix
        for (size_t i = 0; i < prefixCount; ++i)
        {
            if (!matchPattern(patElems[i], tgtItems[i], literals, env, currentDepth, eval, call))
                return false;
        }

        // Match suffix
        for (size_t i = 0; i < suffixCount; ++i)
        {
            size_t pIdx = ellipIdx + 1 + i;
            size_t tIdx = tgtCount - suffixCount + i;
            if (!matchPattern(patElems[pIdx], tgtItems[tIdx], literals, env, currentDepth, eval, call))
                return false;
        }

        // Match repeated items
        std::vector<PatternEnv> repeatEnvs(numRepeats);
        for (size_t r = 0; r < numRepeats; ++r)
        {
            size_t tIdx = prefixCount + r;
            if (!matchPattern(patElems[repIdx], tgtItems[tIdx], literals, repeatEnvs[r], currentDepth, eval, call))
                return false;
        }

        // Aggregate repeated bindings into PatternTrees with incremented depth
        std::set<std::string> varNames;
        collectPatVars(patElems[repIdx], literals, varNames);
        for (const auto& re : repeatEnvs)
            for (const auto& [name, _] : re)
                varNames.insert(name);

        for (const auto& name : varNames)
        {
            PatternTree combined;
            combined.depth = currentDepth + 1;
            combined.children.resize(numRepeats);
            for (size_t r = 0; r < numRepeats; ++r)
            {
                auto it = repeatEnvs[r].find(name);
                if (it != repeatEnvs[r].end())
                    combined.children[r] = std::move(it->second);
            }
            env[name] = std::move(combined);
        }
        return !dotted || matchPattern(patElems.back(), tailAfter(tgtCount), literals, env, currentDepth, eval, call);
    }

    // Literals/constants (numbers, strings, booleans, keywords)
    if (!target.isSyntax())
        return false;

    if (pat.isKeyword() && target.syntax->isIdentifier())
        return pat.text == target.syntax->text;

    if (target.syntax->isConstant())
        return valuesEqual(target.syntax->datum, datumFromSExpr(pat));

    return false;
}

static void collectPatternVars(const SExpr& expr, const PatternEnv& env, std::vector<std::string>& out)
{
    if (expr.isSymbol())
    {
        if (env.count(expr.text))
        {
            if (std::find(out.begin(), out.end(), expr.text) == out.end())
                out.push_back(expr.text);
        }
    }
    else if (expr.isList())
    {
        for (const auto& el : expr.elements)
            collectPatternVars(el, env, out);
    }
}

Value transcribeTemplate(
    const SExpr& tmpl,
    const PatternEnv& env,
    uint32_t introScope,
    Evaluator& eval,
    const SExpr& call)
{
    if (tmpl.isSymbol())
    {
        if (tmpl.text == "#t" || tmpl.text == "#f")
        {
            Syntax* s = gcAlloc<Syntax>();
            s->formKind = SyntaxFormKind::Constant;
            s->datum = Value::boolean(tmpl.text == "#t");
            s->loc = tmpl.loc;
            return Value::of(s);
        }
        auto it = env.find(tmpl.text);
        if (it != env.end())
        {
            if (it->second.depth == 0)
                return it->second.leaf;
            throw CompileError(tmpl, "Pattern variable '" + tmpl.text + "' used outside ellipsis at depth " + std::to_string(it->second.depth));
        }

        // Identifier introduced by macro template
        Syntax* s = gcAlloc<Syntax>();
        s->formKind = SyntaxFormKind::Identifier;
        s->text = tmpl.text;
        s->loc = tmpl.loc;
        s->docs = tmpl.docs;
        s->trailingDoc = tmpl.trailingDoc;
        if (tmpl.syntax) s->scopes = tmpl.syntax->scopes;
        return Value::of(s);
    }

    if (tmpl.isNumber() || tmpl.isString() || tmpl.isKeyword())
    {
        Syntax* s = gcAlloc<Syntax>();
        if (tmpl.isKeyword())
        {
            s->formKind = SyntaxFormKind::Identifier;
            s->text = tmpl.text;
        }
        else
        {
            s->formKind = SyntaxFormKind::Constant;
            s->datum = datumFromSExpr(tmpl);
        }
        s->loc = tmpl.loc;
        return Value::of(s);
    }

    if (tmpl.isList())
    {
        // Escaped ellipsis: (... tmpl)
        if (tmpl.elements.size() == 2 && tmpl.elements[0].isSymbol("..."))
        {
            return transcribeTemplate(tmpl.elements[1], env, introScope, eval, call);
        }

        std::vector<Value> outItems;
        const auto& elems = tmpl.elements;
        bool dotted = elems.size() >= 3 && elems[elems.size() - 2].isSymbol(".");
        size_t count = elems.size() - (dotted ? 2 : 0);

        for (size_t i = 0; i < count; ++i)
        {
            if (i + 1 < count && elems[i + 1].isSymbol("..."))
            {
                const SExpr& subTmpl = elems[i];
                std::vector<std::string> referencedVars;
                collectPatternVars(subTmpl, env, referencedVars);

                if (referencedVars.empty())
                    throw CompileError(elems[i + 1], "Ellipsis in template does not follow any pattern variable");

                size_t count = 0;
                bool first = true;
                for (const auto& var : referencedVars)
                {
                    const PatternTree& pt = env.at(var);
                    if (pt.depth < 1)
                        throw CompileError(elems[i + 1], "Pattern variable '" + var + "' has insufficient ellipsis depth in template");
                    if (first)
                    {
                        count = pt.children.size();
                        first = false;
                    }
                    else if (pt.children.size() != count)
                    {
                        throw CompileError(elems[i + 1], "Incompatible ellipsis match counts for pattern variables in template");
                    }
                }

                // Splicing repetitions
                for (size_t k = 0; k < count; ++k)
                {
                    PatternEnv childEnv = env;
                    for (const auto& var : referencedVars)
                    {
                        childEnv[var] = env.at(var).children[k];
                    }
                    outItems.push_back(transcribeTemplate(subTmpl, childEnv, introScope, eval, call));
                }
                ++i; // Skip "..."
            }
            else
            {
                outItems.push_back(transcribeTemplate(elems[i], env, introScope, eval, call));
            }
        }

        Syntax* s = gcAlloc<Syntax>();
        s->formKind = SyntaxFormKind::List;
        if (dotted)
        {
            Value tail = transcribeTemplate(elems.back(), env, introScope, eval, call);
            if (tail.isSyntax() && tail.syntax->isList())
            {
                outItems.insert(outItems.end(), tail.syntax->items.begin(), tail.syntax->items.end());
                s->dotted = tail.syntax->dotted;
            }
            else
            {
                outItems.push_back(tail);
                s->dotted = true;
            }
        }
        s->items = std::move(outItems);
        s->isBracket = (tmpl.kind == SExprKind::BracketList);
        s->loc = tmpl.loc;
        s->docs = tmpl.docs;
        s->trailingDoc = tmpl.trailingDoc;
        if (tmpl.syntax) s->scopes = tmpl.syntax->scopes;
        return Value::of(s);
    }

    return Value::nil();
}

} // namespace Luwu::Dsl
