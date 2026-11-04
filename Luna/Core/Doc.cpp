// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Doc.h"

#include "Core/CompileError.h"
#include "Core/Evaluator.h"
#include "Core/Record.h"

namespace Luwu::Dsl
{

static SExpr quotedStrings(const std::vector<std::string>& lines, const SourceLocation& loc)
{
    std::vector<SExpr> items;
    for (const auto& line : lines)
        items.push_back(SExpr::atom(SExprKind::String, line, loc));
    return SExpr::call("quote", {SExpr::list(std::move(items), loc)}, loc);
}

bool isDocWrapper(const SExpr& e)
{
    return e.isList("doc-comment") && e.elements.size() >= 3 && !e.elements.back().isString();
}

const SExpr& unwrapDocs(const SExpr& e)
{
    const SExpr* cur = &e;
    while (isDocWrapper(*cur))
        cur = &cur->elements.back();
    return *cur;
}

// Represents parsed components of a doc-comment expression.
struct DocParts
{
    bool trailing = false;
    std::vector<std::string> lines;
    const SExpr* expr = nullptr;
};

static DocParts docParts(const SExpr& form)
{
    DocParts parts;
    size_t i = 1;
    if (i < form.elements.size() && form.elements[i].isKeyword(":trailing"))
    {
        parts.trailing = true;
        ++i;
    }
    for (; i < form.elements.size(); ++i)
    {
        const SExpr& el = form.elements[i];
        if (el.isString())
        {
            parts.lines.push_back(el.text);
            continue;
        }
        if (i + 1 != form.elements.size())
            throw CompileError(el, "doc-comment takes comment lines (strings), then the expression they document last");
        parts.expr = &el;
    }
    if (parts.lines.empty())
        throw CompileError(form, "doc-comment needs at least one comment line: (doc-comment \"text\" [expr])");
    if (parts.trailing && !parts.expr)
        throw CompileError(form, "A :trailing comment documents an expression: (doc-comment :trailing \"text\" expr)");
    return parts;
}

static std::vector<std::string> strings(const Value& list)
{
    std::vector<Value> items;
    listToVector(list, items);
    std::vector<std::string> out;
    for (const auto& v : items)
        out.push_back(v.stringValue());
    return out;
}

SExpr expandDocComment(const SExpr& form, const Evaluator& evaluator)
{
    DocParts parts = docParts(form);
    if (!parts.expr)
        return SExpr::call("%doc-statement", {quotedStrings(parts.lines, form.loc)}, form.loc);

    // Flattens comment strings from nested doc-comment wrappers down to the underlying expression.
    std::vector<std::string> leading, trailing;
    const SExpr* inner = &form;
    while (isDocWrapper(*inner))
    {
        DocParts p = docParts(*inner);
        auto& into = p.trailing ? trailing : leading;
        into.insert(into.end(), p.lines.begin(), p.lines.end());
        inner = p.expr;
    }

    // Attaches doc comments directly to definitions or macro forms.
    const SExpr& e = *inner;
    if (e.isList() && !e.elements.empty() && e.elements[0].isSymbol() &&
        (evaluator.isDefinition(e.elements[0].text) || evaluator.isMacro(e.elements[0].text)))
    {
        SExpr documented = e;
        documented.docs.insert(documented.docs.begin(), leading.begin(), leading.end());
        if (!trailing.empty())
            documented.trailingDoc = trailing.front();
        return documented;
    }

    // For other expressions, wrap in %doc to evaluate and pass the value and comments to the handler hook.
    return SExpr::call("%doc", {quotedStrings(leading, form.loc), quotedStrings(trailing, form.loc), e}, e.loc);
}

void installDocComments(Evaluator& evaluator)
{
    defineBuiltin(*evaluator.globals(), "%doc", 3, 3, [](const std::vector<Value>& a, const SExpr& call) {
        if (const Value* old = recordField(a[2], "comments"))
        {
            std::vector<std::string> leading = strings(a[0]);
            std::vector<std::string> trailing = strings(a[1]);
            std::vector<Value> lead, trail, parts;
            if (listToVector(*old, parts) && parts.size() == 2)
            {
                listToVector(parts[0], lead);
                listToVector(parts[1], trail);
            }
            for (const auto& l : leading)
                lead.push_back(Value::string(l));
            for (const auto& t : trailing)
                trail.push_back(Value::string(t));
            return recordWith(a[2], "comments", Value::list({Value::list(lead), Value::list(trail)}));
        }
        throw CompileError(call, "This ~ comment documents " + toString(a[2]) + " (" + describeKind(a[2]) +
            "), which isn't code, so it would go nowhere; ~ comments go into the generated code, ; comments stay in Luna");
    });
    defineBuiltin(*evaluator.globals(), "%doc-statement", 1, 1, [&evaluator](const std::vector<Value>& a, const SExpr& call) {
        if (const Value* fn = evaluator.globals()->lookup("%doc-statement-fn"))
            return evaluator.apply(*fn, {a[0]}, call);
        throw CompileError(call, "This ~ comment documents nothing; put it before what it describes (; comments stay in Luna)");
    });
}

} // namespace Luwu::Dsl
