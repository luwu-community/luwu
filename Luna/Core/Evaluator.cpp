// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Evaluator.h"

#include "Core/Record.h"
#include "Core/Doc.h"

#include "Core/CompileError.h"
#include "Core/Lexer.h"
#include "Core/Parser.h"

#include <iostream>

namespace Luwu::Dsl
{

// Forms implemented directly by the expander and the evaluator.
static const FormDoc kCoreForms[] = {
    {"quote", "(quote x) or 'x", "Treats x as literal data; symbols remain symbols and lists are preserved without evaluation."},
    {"quasiquote", "`x with ,e and ,@e inside", "Constructs a template where ,e is evaluated and ,@e is spliced into the enclosing list."},
    {"if", "(if c then [else])", "Evaluates then when condition c is not #f; otherwise evaluates else (or returns nil)."},
    {"lambda", "(lambda (a b . rest) body...) or (lambda args body...)", "Defines a procedure whose remaining arguments are gathered into a list."},
    {"define", "(define name value) or (define (name params...) body...)", "Binds a variable or procedure in the current environment."},
    {"set!", "(set! name value)", "Assigns a new value to an existing variable in the nearest enclosing scope."},
    {"begin", "(begin body...)", "Evaluates expressions sequentially and yields the value of the last expression."},
    {"define-macro", "(define-macro (name params...) body...)", "Defines a syntax transformer that receives AST arguments as data and returns expanded code."},
    {"define-syntax", "(define-syntax name transformer)", "Defines a hygienic macro transformer (typically using syntax-case)."},
    {"syntax-case", "(syntax-case val (literals...) [pattern fender output]...)", "Pattern matches syntax objects with ellipsis (...) and hygiene."},
    {"syntax", "(syntax template) or #'template", "Constructs a syntax object with hygienic lexical scope and ellipsis transcription."},
    {"doc-comment", "~ text before an expression, or after it on its line; (doc-comment [:trailing] \"line\"... [expr])",
        "Attaches documentation to a definition or associates comments with generated code."},
    {"let", "(let ([x v]...) body...) or (let loop ([x v]...) body...)", "Binds local variables in parallel, or defines a named recursive loop."},
    {"let*", "(let* ([x v]...) body...)", "Binds local variables sequentially, allowing subsequent bindings to reference earlier ones."},
    {"letrec", "(letrec ([f v]...) body...)", "Binds mutually recursive local variables visible to all expressions in the body."},
    {"cond", "(cond [test body...]... [else body...])", "Evaluates clauses in sequence and executes the body of the first matching test."},
    {"and", "(and e...)", "Evaluates expressions until one yields #f, returning #f or the value of the last expression."},
    {"or", "(or e...)", "Evaluates expressions until one yields a truthy value, returning that value or #f."},
};

void Evaluator::getFormDocs(std::vector<FormDoc>& out) const
{
    for (const auto& f : kCoreForms)
        out.push_back(f);
    for (const auto& [name, v] : global->vars)
        if (v.kind == ValueKind::Builtin && !v.builtin->syntax.empty())
            out.push_back({name, v.builtin->syntax, v.builtin->desc});

    // Collects top-level macros and procedures documented with doc comments.
    for (const auto& [name, v] : global->vars)
    {
        if ((v.kind != ValueKind::Closure && v.kind != ValueKind::Macro) || v.closure->docs.empty())
            continue;
        const Closure& c = *v.closure;
        const Closure& signatureClosure = c.datumTransformer ? *c.datumTransformer : c;
        std::string signature = "(" + name;
        for (const auto& p : signatureClosure.params)
            signature += " " + p;
        if (!signatureClosure.restParam.empty())
            signature += " . " + signatureClosure.restParam;
        signature += ")";
        std::string desc;
        for (const auto& line : c.docs)
            desc += (desc.empty() ? "" : " ") + line;
        out.push_back({name, signature, (v.kind == ValueKind::Macro ? "Macro. " : "") + desc});
    }
}

const SExpr& Evaluator::keep(SExpr form)
{
    kept.push_back(std::move(form));
    return kept.back();
}

void Evaluator::resetGensym()
{
    gensymCounter = 0;
}

static std::vector<Evaluator*> s_liveEvaluators;

Evaluator::Evaluator()
    : global(gcAlloc<Env>())
{
    s_liveEvaluators.push_back(this);
    try
    {
        installBuiltins(*global, gensymCounter);
        global->define("nil", Value::nil());
        // Internal symbols used by quasiquote and letrec expansions to avoid shadowing.
        global->define("#:list", *global->lookup("list"));
        global->define("#:append", *global->lookup("append"));
        defineBuiltin(*global, "#:unassigned", 0, 0, [](const std::vector<Value>&, const SExpr&) {
            Value v;
            v.kind = ValueKind::Unassigned;
            return v;
        });
        defineBuiltin(*global, "%unassigned", 0, 0, [](const std::vector<Value>&, const SExpr&) {
            Value v;
            v.kind = ValueKind::Unassigned;
            return v;
        });
        // The prelude derives define-macro from define-syntax. These helpers adapt its legacy
        // datum procedure to the same syntax-in/syntax-out protocol as every other transformer.
        defineBuiltin(*global, "%make-datum-transformer", 2, 2, [this](const std::vector<Value>& a, const SExpr& call) {
            if (a[0].kind != ValueKind::Symbol || a[1].kind != ValueKind::Closure)
                throw CompileError(call, "A datum transformer takes a name and a procedure");
            Value procedure = a[1];
            Closure* named = gcAlloc<Closure>(*procedure.closure);
            named->name = a[0].text;
            if (a[0].datumSyntax)
                named->loc = a[0].datumSyntax->loc;
            procedure.closure = named;
            Roots roots(*this, {procedure});
            EnvPtr adapterEnv = gcAlloc<Env>(global);
            adapterEnv->define("#:datum-procedure", procedure);
            SExpr input = SExpr::symbol("#:datum-input", call.loc);
            SExpr adapter = SExpr::call("lambda", {SExpr::list({input}, call.loc),
                SExpr::call("#:invoke-datum-transformer", {SExpr::symbol("#:datum-procedure", call.loc), input}, call.loc)}, call.loc);
            Value wrapper = eval(adapter, adapterEnv);
            const_cast<Closure*>(wrapper.closure)->datumTransformer = named;
            const_cast<Closure*>(wrapper.closure)->loc = named->loc;
            return wrapper;
        });
        defineBuiltin(*global, "#:invoke-datum-transformer", 2, 2, [this](const std::vector<Value>& a, const SExpr& call) {
            if (a[0].kind != ValueKind::Closure || !a[1].isSyntax() || expansions.empty())
                throw CompileError(call, "A datum transformer can only run during macro expansion");
            // Use the original call to retain forwarded syntax context and datum source locations.
            size_t frame = expansions.size() - 1;
            const SExpr& original = *expansions[frame].call;
            DatumOrigins origins;
            Roots args(*this);
            for (size_t i = 1; i < original.elements.size(); ++i)
                args.values.push_back(datumFromSExpr(original.elements[i], &origins));
            const DatumOrigins* previousOrigins = expansions[frame].origins;
            expansions[frame].origins = &origins;
            // Nested expansion may reallocate the vector; retain the index, not a frame reference.
            struct RestoreOrigins
            {
                std::vector<Expansion>& frames;
                size_t index;
                const DatumOrigins* previous;
                ~RestoreOrigins() { frames[index].origins = previous; }
            } restore{expansions, frame, previousOrigins};
            Value result = apply(a[0], args.values, original);
            SExpr expanded = result.isNil() ? SExpr::symbol("nil", original.loc) : sexprFromDatum(result, origins, original.loc);
            // Datum macros intentionally introduce no scope. Cancel the common invoker's flip,
            // leaving any hygienic context on forwarded input untouched.
            flipScope(expanded, currentIntroScope);
            return syntaxFromSExpr(expanded);
        });
        // Built-in to expand macros into core forms as data.
        defineBuiltin(*global, "expand", 1, 1, [this](const std::vector<Value>& a, const SExpr& call) {
            return datumFromSExpr(expand(sexprFromDatum(a[0], {}, call.loc), false));
        });
        installDocComments(*this);
        installRecords(*this);
        installCollections(*this);

        defineBuiltin(*global, "syntax?", 1, 1, [](const std::vector<Value>& a, const SExpr&) {
            return Value::boolean(a[0].isSyntax());
        }, "(syntax? x)", "Returns true if x is a syntax object, otherwise false.");

        defineBuiltin(*global, "identifier?", 1, 1, [](const std::vector<Value>& a, const SExpr&) {
            return Value::boolean(isIdentifier(a[0]));
        }, "(identifier? x)", "Returns true if x is an identifier syntax object, otherwise false.");

        defineBuiltin(*global, "syntax->datum", 1, 1, [](const std::vector<Value>& a, const SExpr&) {
            return syntaxToDatum(a[0]);
        }, "(syntax->datum stx)", "Recursively strips syntax wrappers from stx, yielding raw datums.");

        defineBuiltin(*global, "datum->syntax", 2, 3, [](const std::vector<Value>& a, const SExpr& call) {
            std::optional<SourceLocation> loc;
            if (a.size() > 2)
            {
                if (a[2].kind != ValueKind::String)
                    throw CompileError(call, "datum->syntax optional location must be a string");
                loc = SourceLocation{a[2].stringValue(), 0, 0};
            }
            return datumToSyntax(a[0], a[1], loc);
        }, "(datum->syntax ctxt datum [loc])", "Wraps datum as a syntax object inheriting lexical context from ctxt.");

        defineBuiltin(*global, "bound-identifier=?", 2, 2, [](const std::vector<Value>& a, const SExpr& call) {
            if (!isIdentifier(a[0]) || !isIdentifier(a[1]))
                throw CompileError(call, "bound-identifier=? requires two identifier syntax objects");
            return Value::boolean(boundIdentifierEqual(a[0], a[1]));
        }, "(bound-identifier=? id1 id2)", "Returns true if two identifiers have identical names and scopes.");

        defineBuiltin(*global, "free-identifier=?", 2, 2, [this](const std::vector<Value>& a, const SExpr& call) {
            if (!isIdentifier(a[0]) || !isIdentifier(a[1]))
                throw CompileError(call, "free-identifier=? requires two identifier syntax objects");
            return Value::boolean(syntaxBindingKey(a[0].syntax->text, a[0].syntax->scopes) ==
                                  syntaxBindingKey(a[1].syntax->text, a[1].syntax->scopes));
        }, "(free-identifier=? id1 id2)", "Returns true if two identifiers refer to the same lexical binding.");

        defineBuiltin(*global, "generate-temporaries", 1, 1, [this](const std::vector<Value>& a, const SExpr& call) -> Value {
            size_t count = 0;
            if (a[0].kind == ValueKind::Int)
                count = a[0].intValue >= 0 ? size_t(a[0].intValue) : 0;
            else if (a[0].isSyntax() && a[0].syntax->isList())
                count = a[0].syntax->items.size();
            else if (a[0].isPair())
            {
                std::vector<Value> v;
                if (listToVector(a[0], v)) count = v.size();
            }
            else if (a[0].isNil())
                count = 0;
            else
                throw CompileError(call, "generate-temporaries expects a list or integer");

            std::vector<Value> out;
            out.reserve(count);
            for (size_t i = 0; i < count; ++i)
            {
                Syntax* s = gcAlloc<Syntax>();
                s->formKind = SyntaxFormKind::Identifier;
                s->text = "#:t" + std::to_string(++gensymCounter);
                s->loc = call.loc;
                if (currentIntroScope != 0)
                    s->scopes.add(currentIntroScope);
                out.push_back(Value::of(s));
            }
            return Value::list(out);
        }, "(generate-temporaries l)", "Generates a list of fresh unique syntax identifiers.");

        // Raises a syntax error pointing to the source location of the provided AST datum or macro invocation.
        defineBuiltin(*global, "syntax-error", 2, -1, [this](const std::vector<Value>& a, const SExpr& call) -> Value {
            std::string msg;
            for (size_t i = 1; i < a.size(); ++i)
                msg += (i == 1 ? "" : " ") + (a[i].kind == ValueKind::String ? a[i].stringValue() : toString(a[i]));
            if (a[0].isSyntax())
                throw CompileError(a[0].syntax->loc, msg);
            if (expansions.empty())
                throw CompileError(call, msg);
            const Expansion& x = expansions.back();
            // When datum is an integer index, point to the corresponding element of the macro call.
            if (a[0].kind == ValueKind::Int && a[0].intValue >= 0 && size_t(a[0].intValue) < x.call->elements.size())
                throw CompileError(x.call->elements[size_t(a[0].intValue)], msg);
            if (a[0].kind == ValueKind::Pair)
            {
                auto it = x.origins->find(a[0].pair);
                if (it != x.origins->end())
                    throw CompileError(*it->second, msg);
            }
            throw CompileError(*x.call, msg);
        }, "(syntax-error datum \"message\"...)",
           "In a macro: raises an error at the source of datum or at the macro invocation.");
        defineBuiltin(*global, "macro-argument-location", 1, 1, [this](const std::vector<Value>& a, const SExpr& call) -> Value {
            if (expansions.empty() || a[0].kind != ValueKind::Int || a[0].intValue < 0 || size_t(a[0].intValue) >= expansions.back().call->elements.size())
                throw CompileError(call, "macro-argument-location takes the position of an element of the macro call being expanded");
            return Value::string(expansions.back().call->elements[size_t(a[0].intValue)].loc.toString());
        }, "(macro-argument-location n)", "In a macro: returns the source location of the nth macro call element as a string.");
        defineBuiltin(*global, "macro-at-top-level?", 0, 0, [this](const std::vector<Value>&, const SExpr& call) -> Value {
            if (expansions.empty())
                throw CompileError(call, "macro-at-top-level? is only known while a macro expands");
            return Value::boolean(expansions.back().topLevel);
        }, "(macro-at-top-level?)", "In a macro: returns true if the call being expanded is a top-level form.");
        // Evaluates thunk, appending the specified context description to any raised error.
        defineBuiltin(*global, "with-error-context", 2, 2, [this](const std::vector<Value>& a, const SExpr& call) -> Value {
            if (a[0].kind != ValueKind::String)
                throw CompileError(call, "with-error-context takes a string and a procedure");
            try
            {
                return apply(a[1], {}, call);
            }
            catch (const CompileError& e)
            {
                throw CompileError(e.loc(), e.rawMessage() + " (" + a[0].stringValue() + ")");
            }
        }, "(with-error-context \"context\" thunk)", "Evaluates thunk, appending context description to any raised error.");
        out = &std::cout;
        // Writes values to the configured output stream.
        defineBuiltin(*global, "display", 0, -1, [this](const std::vector<Value>& a, const SExpr&) {
            for (const auto& v : a)
                *out << (v.kind == ValueKind::String ? v.stringValue() : toString(v));
            return Value::nil();
        });

        Lexer lexer(lunaPrelude(), "<prelude>");
        Parser parser(lexer);
        for (auto& form : parser.parseProgram())
            eval(keep(std::move(form)));
        resetGensym();
    }
    catch (...)
    {
        auto it = std::find(s_liveEvaluators.begin(), s_liveEvaluators.end(), this);
        if (it != s_liveEvaluators.end())
            s_liveEvaluators.erase(it);
        throw;
    }
}

Evaluator::~Evaluator()
{
    auto it = std::find(s_liveEvaluators.begin(), s_liveEvaluators.end(), this);
    if (it != s_liveEvaluators.end())
        s_liveEvaluators.erase(it);

    if (s_liveEvaluators.empty())
    {
        while (gcHead())
        {
            GcCell* next = gcHead()->next;
            delete gcHead();
            gcHead() = next;
        }
        gcCount() = 0;
        gcThreshold() = 4096;
    }
    else
    {
        collectGarbage();
    }
}

Value Evaluator::evalProgram(const std::vector<SExpr>& program)
{
    Value last;
    for (const auto& form : program)
        last = eval(form, global);
    return last;
}

Value Evaluator::eval(const SExpr& expr)
{
    return eval(expr, global);
}

Value Evaluator::eval(const SExpr& expr, const EnvPtr& env)
{
    const SExpr& expanded = keep(expand(expr, env == global));
    return run(&expanded, env, nullptr, nullptr, nullptr);
}

Value Evaluator::apply(const Value& fn, std::vector<Value> args, const SExpr& call)
{
    return run(nullptr, nullptr, &fn, &args, &call);
}

void Evaluator::collectGarbage()
{
    std::vector<const GcCell*> worklist;

    for (Evaluator* ev : s_liveEvaluators)
    {
        if (ev->global)
            worklist.push_back(ev->global);
        for (const auto* roots : ev->nativeRoots)
            for (const Value& v : *roots)
                if (const GcCell* c = v.cell()) worklist.push_back(c);
        std::function<void(const PatternTree&)> markPattern = [&](const PatternTree& tree) {
            if (const GcCell* c = tree.leaf.cell()) worklist.push_back(c);
            for (const PatternTree& child : tree.children) markPattern(child);
        };
        for (const PatternEnv& patterns : ev->patternEnvStack)
            for (const auto& entry : patterns) markPattern(entry.second);

        for (const Expansion& exp : ev->expansions)
        {
            if (exp.args)
            {
                for (const Value& v : *exp.args)
                {
                    if (const GcCell* c = v.cell())
                        worklist.push_back(c);
                }
            }
        }

        ev->markBytecode(worklist);
    }

    // Mark
    while (!worklist.empty())
    {
        const GcCell* c = worklist.back();
        worklist.pop_back();
        if (!c || c->marked)
            continue;
        c->marked = true;
        c->markChildren(worklist);
    }

    // Sweep
    size_t live = 0;
    GcCell** cur = &gcHead();
    while (*cur)
    {
        if (!(*cur)->marked)
        {
            GcCell* dead = *cur;
            *cur = dead->next;
            delete dead;
        }
        else
        {
            (*cur)->marked = false;
            cur = &(*cur)->next;
            ++live;
        }
    }
    gcCount() = live;
    gcThreshold() = std::max<size_t>(4096, live * 2);
}

// Parses parameter lists of the form (params ...), [params ...], (params ... . rest), or a bare rest symbol.
void parseParams(const SExpr& list, std::vector<std::string>& params, std::string& rest)
{
    if (list.isSymbol())
    {
        rest = list.text;
        return;
    }
    if (!list.isList())
        throw CompileError(list, "Expected a parameter list, like (a b), (a . rest) or args");
    for (size_t i = 0; i < list.elements.size(); ++i)
    {
        const SExpr& p = list.elements[i];
        if (!p.isSymbol() || p.text == "#t" || p.text == "#f")
            throw CompileError(p, "Parameters are symbols");
        if (p.text == ".")
        {
            if (i + 2 != list.elements.size() || !list.elements[i + 1].isSymbol())
                throw CompileError(p, ". takes exactly one rest parameter, last: (a b . rest)");
            rest = list.elements[i + 1].text;
            return;
        }
        for (const auto& prev : params)
            if (prev == p.text)
                throw CompileError(p, "Duplicate parameter '" + p.text + "'");
        params.push_back(p.text);
    }
}

} // namespace Luwu::Dsl
