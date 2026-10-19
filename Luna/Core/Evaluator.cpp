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
        std::string signature = "(" + name;
        for (const auto& p : c.params)
            signature += " " + p;
        if (!c.restParam.empty())
            signature += " . " + c.restParam;
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
        defineBuiltin(*global, "%make-macro", 1, 1, [](const std::vector<Value>& a, const SExpr& call) {
            if (a[0].kind != ValueKind::Closure)
                throw CompileError(call, "A macro is made from a procedure");
            Value m = a[0];
            m.kind = ValueKind::Macro;
            return m;
        });
        // Built-in to expand macros into core forms as data.
        defineBuiltin(*global, "expand", 1, 1, [this](const std::vector<Value>& a, const SExpr& call) {
            return datumFromSExpr(expand(sexprFromDatum(a[0], {}, call.loc), false));
        });
        installDocComments(*this);
        installRecords(*this);
        // Raises a syntax error pointing to the source location of the provided AST datum or macro invocation.
        defineBuiltin(*global, "syntax-error", 2, -1, [this](const std::vector<Value>& a, const SExpr& call) -> Value {
            std::string msg;
            for (size_t i = 1; i < a.size(); ++i)
                msg += (i == 1 ? "" : " ") + (a[i].kind == ValueKind::String ? a[i].text : toString(a[i]));
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
                throw CompileError(e.loc(), e.rawMessage() + " (" + a[0].text + ")");
            }
        }, "(with-error-context \"context\" thunk)", "Evaluates thunk, appending context description to any raised error.");
        out = &std::cout;
        // Writes values to the configured output stream.
        defineBuiltin(*global, "display", 0, -1, [this](const std::vector<Value>& a, const SExpr&) {
            for (const auto& v : a)
                *out << (v.kind == ValueKind::String ? v.text : toString(v));
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

// Continuation frame representing pending work after evaluating an expression.
struct Evaluator::Frame
{
    enum class Kind
    {
        Seq,    // Evaluates remaining expressions in a body sequence.
        If,
        Define, // Evaluates value for define.
        Set,    // Evaluates value for set!.
        Call,   // Evaluates head and argument expressions for a call.
    };

    Kind kind;
    const SExpr* form;
    EnvPtr env;
    size_t idx = 0;
    std::vector<Value> vals;
};

struct Evaluator::ActiveRun
{
    EnvPtr& env;
    Value& val;
    Value& applyFn;
    std::vector<Value>& applyArgs;
    std::vector<Frame>& stack;
    ActiveRun* prev = nullptr;
};

void Evaluator::collectGarbage()
{
    std::vector<const GcCell*> worklist;

    for (Evaluator* ev : s_liveEvaluators)
    {
        if (ev->global)
            worklist.push_back(ev->global);

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

        for (ActiveRun* r = ev->activeRun; r; r = r->prev)
        {
            if (r->env)
                worklist.push_back(r->env);
            if (const GcCell* c = r->val.cell())
                worklist.push_back(c);
            if (const GcCell* c = r->applyFn.cell())
                worklist.push_back(c);
            for (const Value& v : r->applyArgs)
            {
                if (const GcCell* c = v.cell())
                    worklist.push_back(c);
            }
            for (const Frame& f : r->stack)
            {
                if (f.env)
                    worklist.push_back(f.env);
                for (const Value& v : f.vals)
                {
                    if (const GcCell* c = v.cell())
                        worklist.push_back(c);
                }
            }
        }
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

static Value numberLiteral(const SExpr& e)
{
    const std::string& t = e.text;
    bool isHex = t.size() > 2 && (t[1] == 'x' || t[1] == 'X' || (t.size() > 3 && (t[2] == 'x' || t[2] == 'X')));
    if (!isHex && (t.find('.') != std::string::npos || t.find('e') != std::string::npos || t.find('E') != std::string::npos))
        return Value::real(std::stod(t));
    return Value::integer(e.intValue);
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

static Value makeClosure(const SExpr& paramList, const SExpr* form, size_t bodyStart, const EnvPtr& env)
{
    auto c = gcAlloc<Closure>();
    parseParams(paramList, c->params, c->restParam);
    c->form = form;
    c->bodyStart = bodyStart;
    c->env = env;
    c->loc = form->loc;
    Value v;
    v.kind = ValueKind::Closure;
    v.closure = c;
    return v;
}

static std::string procName(const Value& fn)
{
    if (fn.kind == ValueKind::Builtin)
        return fn.builtin->name;
    return fn.closure->name.empty() ? "an anonymous lambda" : fn.closure->name;
}

Value Evaluator::run(const SExpr* startExpr, EnvPtr startEnv, const Value* startFn, std::vector<Value>* startArgs, const SExpr* startCall)
{
    using Kind = Frame::Kind;
    std::vector<Frame> stack;

    // Evaluates expr in env, or returns val to the top stack frame.
    bool returning = false;
    const SExpr* expr = startExpr;
    EnvPtr env = std::move(startEnv);
    Value val;

    // Applies a procedure: builtins execute directly, while closures evaluate their body in tail position.
    Value applyFn;
    std::vector<Value> applyArgs;
    const SExpr* applyCall = nullptr;
    bool applying = startFn != nullptr;
    if (applying)
    {
        applyFn = *startFn;
        applyArgs = std::move(*startArgs);
        applyCall = startCall;
    }

    struct ActiveRunScope
    {
        Evaluator* ev;
        ActiveRun run;
        ActiveRunScope(Evaluator* ev, EnvPtr& env, Value& val, Value& applyFn, std::vector<Value>& applyArgs, std::vector<Frame>& stack)
            : ev(ev), run{env, val, applyFn, applyArgs, stack, ev->activeRun}
        {
            ev->activeRun = &run;
        }
        ~ActiveRunScope()
        {
            ev->activeRun = run.prev;
        }
    };
    ActiveRunScope runScope(this, env, val, applyFn, applyArgs, stack);

    // Sets up evaluation of expressions from start index onward as a sequence in bodyEnv.
    auto startBody = [&](const SExpr* form, size_t start, EnvPtr bodyEnv) {
        size_t n = form->elements.size();
        if (start >= n)
        {
            val = Value::nil();
            returning = true;
            return;
        }
        if (start + 1 < n)
            stack.push_back(Frame{Kind::Seq, form, bodyEnv, start + 1});
        expr = &form->elements[start];
        env = std::move(bodyEnv);
        returning = false;
    };

    for (;;)
    {
        if (gcCount() > gcThreshold())
            collectGarbage();

        if (stack.size() > maxDepth)
            maxDepth = stack.size();

        if (applying)
        {
            applying = false;
            const SExpr& call = *applyCall;

            // Unpacks trailing list argument and re-applies the function in tail position.
            while (applyFn.kind == ValueKind::Builtin && applyFn.builtin->name == "apply")
            {
                if (applyArgs.size() < 2)
                    throw CompileError(call, "apply needs a procedure and a list: (apply f a ... list)");
                Value f = applyArgs[0];
                std::vector<Value> spread(applyArgs.begin() + 1, applyArgs.end() - 1);
                if (!listToVector(applyArgs.back(), spread))
                    throw CompileError(call, "apply's last argument must be a list, got " + describeKind(applyArgs.back()));
                applyFn = std::move(f);
                applyArgs = std::move(spread);
            }

            if (applyFn.kind == ValueKind::Builtin)
            {
                const Builtin& b = *applyFn.builtin;
                int n = int(applyArgs.size());
                if (n < b.minArgs || (b.maxArgs >= 0 && n > b.maxArgs))
                {
                    std::string expected = b.minArgs == b.maxArgs ? std::to_string(b.minArgs)
                                           : b.maxArgs < 0       ? "at least " + std::to_string(b.minArgs)
                                                                 : std::to_string(b.minArgs) + " to " + std::to_string(b.maxArgs);
                    throw CompileError(call, b.name + " takes " + expected + " argument(s), got " + std::to_string(n));
                }
                val = b.fn(applyArgs, call);
                applyFn = Value{};
                applyArgs.clear();
                returning = true;
            }
            else if (applyFn.kind == ValueKind::Closure)
            {
                const Closure& c = *applyFn.closure;
                size_t n = applyArgs.size();
                if (n < c.params.size() || (c.restParam.empty() && n > c.params.size()))
                {
                    throw CompileError(call, procName(applyFn) + " takes " + (c.restParam.empty() ? "" : "at least ") + std::to_string(c.params.size()) +
                        " argument(s), got " + std::to_string(n) + " (defined at " + c.loc.toString() + ")");
                }
                auto callEnv = gcAlloc<Env>(c.env);
                for (size_t i = 0; i < c.params.size(); ++i)
                    callEnv->define(c.params[i], std::move(applyArgs[i]));
                if (!c.restParam.empty())
                    callEnv->define(c.restParam, Value::list(std::vector<Value>(applyArgs.begin() + c.params.size(), applyArgs.end())));
                startBody(c.form, c.bodyStart, std::move(callEnv));
                applyFn = Value{};
                applyArgs.clear();
            }
            else if (applyFn.kind == ValueKind::Macro)
            {
                throw CompileError(call, procName(applyFn) + " is a macro, so it can't be called as a procedure (it's expanded where it's written)");
            }
            else
            {
                throw CompileError(call, toString(applyFn) + " (" + describeKind(applyFn) + ") isn't a procedure");
            }
            continue;
        }

        if (!returning)
        {
            const SExpr& e = *expr;
            switch (e.kind)
            {
            case SExprKind::Number:
                val = numberLiteral(e);
                returning = true;
                continue;
            case SExprKind::String:
                val = Value::string(e.text);
                returning = true;
                continue;
            case SExprKind::Keyword:
                val = Value::keyword(e.text);
                returning = true;
                continue;
            case SExprKind::Symbol:
            {
                if (e.text == "#t" || e.text == "#f")
                {
                    val = Value::boolean(e.text == "#t");
                    returning = true;
                    continue;
                }
                const Value* v = env->lookup(e.text);
                if (!v)
                    throw CompileError(e, "Unbound symbol '" + e.text + "'");
                if (v->kind == ValueKind::Unassigned)
                    throw CompileError(e, "'" + e.text + "' is used before its letrec value is computed");
                val = *v;
                returning = true;
                continue;
            }
            case SExprKind::BracketList: // Bracketed lists behave identically to parenthesized lists.
            case SExprKind::List:
                break;
            }

            if (e.elements.empty())
                throw CompileError(e, std::string(e.kind == SExprKind::List ? "()" : "[]") + " isn't an expression; nil is the empty list");

            const SExpr& head = e.elements[0];
            if (head.isSymbol())
            {
                const std::string& h = head.text;

                // Evaluates core syntactic forms.
                if (h == "quote")
                {
                    val = datumFromSExpr(e.elements[1]);
                    returning = true;
                    continue;
                }
                if (h == "if")
                {
                    stack.push_back(Frame{Kind::If, &e, env});
                    expr = &e.elements[1];
                    continue;
                }
                if (h == "lambda")
                {
                    val = makeClosure(e.elements[1], &e, 2, env);
                    returning = true;
                    continue;
                }
                if (h == "begin")
                {
                    startBody(&e, 1, env);
                    continue;
                }
                if (h == "define" || h == "set!")
                {
                    stack.push_back(Frame{h == "define" ? Kind::Define : Kind::Set, &e, env});
                    expr = &e.elements[2];
                    continue;
                }
            }

            // Evaluates procedure and argument expressions from left to right.
            stack.push_back(Frame{Kind::Call, &e, env, 1});
            expr = &e.elements[0];
            continue;
        }

        // Handles returning val to the enclosing continuation frame.
        if (stack.empty())
            return val;

        Frame& f = stack.back();
        const SExpr& form = *f.form;
        switch (f.kind)
        {
        case Kind::Seq:
        {
            size_t i = f.idx;
            EnvPtr bodyEnv = f.env;
            if (i + 1 == form.elements.size())
                stack.pop_back(); // The final expression is evaluated in tail position.
            else
                f.idx++;
            expr = &form.elements[i];
            env = std::move(bodyEnv);
            returning = false;
            break;
        }
        case Kind::If:
        {
            EnvPtr ifEnv = f.env;
            stack.pop_back();
            if (val.isTruthy())
            {
                expr = &form.elements[2];
                env = std::move(ifEnv);
                returning = false;
            }
            else if (form.elements.size() == 4)
            {
                expr = &form.elements[3];
                env = std::move(ifEnv);
                returning = false;
            }
            else
            {
                val = Value::nil();
            }
            break;
        }
        case Kind::Define:
        {
            // Attaches the binding name and doc comments to named closures or macros.
            if ((val.kind == ValueKind::Closure || val.kind == ValueKind::Macro) && val.closure->name.empty())
            {
                auto named = gcAlloc<Closure>(*val.closure);
                named->name = form.elements[1].text;
                named->docs = form.docs;
                if (form.trailingDoc)
                    named->docs.push_back(*form.trailingDoc);
                val.closure = named;
            }
            // Attaches doc comments to record instances that define a docs field.
            else if (form.hasDocs() && recordField(val, "docs"))
            {
                std::vector<Value> lines;
                for (const auto& line : form.docs)
                    lines.push_back(Value::string(line));
                if (form.trailingDoc)
                    lines.push_back(Value::string(*form.trailingDoc));
                val = recordWith(val, "docs", Value::list(lines));
            }
            f.env->define(form.elements[1].text, val);
            stack.pop_back();
            val = Value::nil();
            break;
        }
        case Kind::Set:
        {
            Value* slot = f.env->find(form.elements[1].text);
            if (!slot)
                throw CompileError(form, "set! of '" + form.elements[1].text + "', which isn't bound (define it first)");
            *slot = val;
            stack.pop_back();
            val = Value::nil();
            break;
        }
        case Kind::Call:
        {
            f.vals.push_back(val);
            if (f.idx < form.elements.size())
            {
                expr = &form.elements[f.idx++];
                env = f.env;
                returning = false;
                break;
            }
            Frame done = std::move(f);
            stack.pop_back();
            applyFn = std::move(done.vals[0]);
            applyArgs.assign(std::make_move_iterator(done.vals.begin() + 1), std::make_move_iterator(done.vals.end()));
            applyCall = &form;
            applying = true;
            break;
        }
        }
    }
}

} // namespace Luwu::Dsl
