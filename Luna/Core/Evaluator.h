// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Core/Value.h"
#include "Core/Describe.h"
#include "Core/Syntax.h"

#include <deque>
#include <map>
#include <ostream>
#include <functional>
#include <optional>
#include <set>
#include <vector>

namespace Luwu::Dsl
{

// Evaluates Luna's generation-time language, a Scheme. Each top-level form is first expanded to the core forms
// (quote, if, lambda, define, set!, begin, application and eval-time forms): macros, quasiquote and the derived forms
// (let, let*, letrec, named let, cond, and, or) become core forms. The machine executes register bytecode,
// keeps calls on a heap stack, and replaces tail-call frames.
class Evaluator
{
public:
    Evaluator();
    ~Evaluator();
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;
    Evaluator(Evaluator&&) = delete;
    Evaluator& operator=(Evaluator&&) = delete;

    void collectGarbage();

    // Expands and evaluates a program's top-level forms in order and returns the last value
    Value evalProgram(const std::vector<SExpr>& program);

    // Expands and evaluates one form (at the top level when env is the global environment)
    Value eval(const SExpr& expr);
    Value eval(const SExpr& expr, const EnvPtr& env);

    // The expansion of a form to core forms; `topLevel` allows define-macro
    SExpr expand(const SExpr& form, bool topLevel);

    // Calls a procedure from C++; `call` locates errors
    Value apply(const Value& fn, std::vector<Value> args, const SExpr& call);

    // A definition form: define, define-macro, or define-syntax
    bool isDefinition(const std::string& name) const
    {
        return name == "define" || name == "define-macro" || name == "define-syntax";
    }
    // A global macro: a ~ comment on a call of one goes to the definition it expands to (define-record, ...), or to the
    // expansion's value when it isn't one
    bool isMacro(const std::string& name) const
    {
        const Value* v = global->lookup(name);
        return v && v->isMacro();
    }


    // --describe entries: the core and derived forms, the documented builtins, and the macros
    // and procedures documented with ~ comments
    void getFormDocs(std::vector<FormDoc>& out) const;

    const EnvPtr& globals() const { return global; }

    // Deepest VM call stack seen, for tests that check tail calls run in constant space
    size_t maxStackDepth() const { return maxDepth; }

    // Native procedures which call back into Luna must root their saved values.
    class Roots
    {
    public:
        Roots(Evaluator& evaluator, std::vector<Value> values = {}) : evaluator(evaluator), values(std::move(values))
        { evaluator.nativeRoots.push_back(&this->values); }
        ~Roots() { evaluator.nativeRoots.pop_back(); }
        Roots(const Roots&) = delete;
        Roots& operator=(const Roots&) = delete;
        Evaluator& evaluator;
        std::vector<Value> values;
    };


    // Writes (display ...) output
    std::ostream* out = nullptr;
    // Optional compiler diagnostics. Execution never writes to this stream.
    std::ostream* bytecodeDump = nullptr;

    // The macro calls being expanded, innermost last, with where each argument datum came from (syntax-error)
    struct Expansion
    {
        const SExpr* call;
        const DatumOrigins* origins;
        bool topLevel; // the call is a top-level form (macro-at-top-level?)
        const std::vector<Value>* args = nullptr;
    };
    std::vector<Expansion> expansions;

    // Pattern environment stack for syntax-case / syntax templates
    std::vector<PatternEnv> patternEnvStack;
    uint32_t currentIntroScope = 0;
    std::set<uint32_t> macroIntroScopes;
    // Lexical scopes remain meaningful in first-class syntax after expansion.
    std::string syntaxBindingKey(const std::string& name, const ScopeSet& scopes) const;

    // Keeps source forms alive for bytecode locations and syntax templates.
    const SExpr& keep(SExpr form);
    void resetGensym();

private:
    struct ExpandScope;
    struct BytecodeRun;
    struct SyntaxBinding { std::string name; ScopeSet scopes; };
    std::vector<SyntaxBinding> syntaxBindings;

    BytecodeRun* activeBytecode = nullptr;

    Value run(const SExpr* expr, EnvPtr env, const Value* fn, std::vector<Value>* args, const SExpr* call,
        const struct BytecodeFunction* prepared = nullptr);
    Value executeBytecode(const struct BytecodeFunction* code, EnvPtr env);
    void markBytecode(std::vector<const GcCell*>& worklist) const;
    SExpr expandIn(const SExpr& e, ExpandScope& scope, bool topLevel);
    std::vector<SExpr> expandBody(const SExpr& form, size_t start, ExpandScope& scope);

    EnvPtr global;
    size_t maxDepth = 0;
    std::deque<SExpr> kept;
    int64_t gensymCounter = 0;
    std::vector<const std::vector<Value>*> nativeRoots;
};

void installBuiltins(Env& env, int64_t& gensymCounter);
void installCollections(Evaluator& evaluator);
// A builtin procedure; with syntax and desc it has a --describe entry
void defineBuiltin(Env& env, const std::string& name, int minArgs, int maxArgs, std::function<Value(const std::vector<Value>&, const SExpr&)> fn,
    const std::string& syntax = "", const std::string& desc = "");
const char* lunaPrelude();

} // namespace Luwu::Dsl
