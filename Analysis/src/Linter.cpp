// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Linter.h"

#include "Luau/AstQuery.h"
#include "Luau/AstUtils.h"
#include "Luau/LinterConfig.h"
#include "Luau/Module.h"
#include "Luau/PrettyPrinter.h"
#include "Luau/Scope.h"
#include "Luau/TypeInfer.h"
#include "Luau/StringUtils.h"
#include "Luau/ToString.h"
#include "Luau/Common.h"

#include <algorithm>
#include <cmath>
#include <climits>
#include <unordered_set>

LUAU_FASTINTVARIABLE(LuauSuggestionDistance, 4)
LUAU_FASTFLAGVARIABLE(LuauFunctionUnusedRecursiveLinting)
LUAU_FASTFLAG(DebugLuwuDoExpr)
LUAU_FASTFLAG(DebugLuwuCompilerTrustsTypeAnnotations)
LUAU_FASTFLAGVARIABLE(LuwuTableRemoveFootgunLint)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuTraits)
// Defined by the VM, which introduces `none`; without it there is nothing to mix up with `nil`.
LUAU_FASTFLAG(LuwuNonePrimitive)

namespace Luau
{

struct LintContext
{
    struct Global
    {
        TypeId type = nullptr;
        std::optional<const char*> deprecated;
    };

    std::vector<LintWarning> result;
    LintOptions options;

    AstStat* root;

    AstName placeholder;
    // Luwu Attributes (rfcs/attributes-for-types-variables-fields-classes.md): `@deprecated` and `@[deprecated]`
    // both intern this name, so when it's absent the module has no deprecation attributes to collect.
    AstName deprecatedAttribute;
    DenseHashMap<AstName, Global> builtinGlobals;
    ScopePtr scope;
    const Module* module;

    // Luwu: the lints `@[nolint(...)]` turns off for each binding a `local` or `const` declares, for lints about a
    // binding that report where it's used rather than where it's declared (LoopConcat on `s ..= x`)
    DenseHashMap<AstLocal*, LintMask> bindingNolints;

    LintContext()
        : root(nullptr)
        , builtinGlobals(AstName())
        , module(nullptr)
        , bindingNolints(nullptr)
    {
    }

    bool warningEnabled(LintWarning::Code code)
    {
        return options.warningMask.test(code);
    }

    std::optional<TypeId> getType(AstExpr* expr)
    {
        if (!module)
            return std::nullopt;

        auto it = module->astTypes.find(expr);
        if (!it)
            return std::nullopt;

        return *it;
    }
};

struct WarningComparator
{
    int compare(const Position& lhs, const Position& rhs) const
    {
        if (lhs.line != rhs.line)
            return lhs.line < rhs.line ? -1 : 1;
        if (lhs.column != rhs.column)
            return lhs.column < rhs.column ? -1 : 1;
        return 0;
    }

    int compare(const Location& lhs, const Location& rhs) const
    {
        if (int c = compare(lhs.begin, rhs.begin))
            return c;
        if (int c = compare(lhs.end, rhs.end))
            return c;
        return 0;
    }

    bool operator()(const LintWarning& lhs, const LintWarning& rhs) const
    {
        if (int c = compare(lhs.location, rhs.location))
            return c < 0;

        return lhs.code < rhs.code;
    }
};

LUAU_PRINTF_ATTR(4, 5)
static void emitWarning(LintContext& context, LintWarning::Code code, const Location& location, const char* format, ...)
{
    if (!context.warningEnabled(code))
        return;

    va_list args;
    va_start(args, format);
    std::string message = vformat(format, args);
    va_end(args);

    LintWarning warning = {code, location, std::move(message)};
    context.result.push_back(warning);
}

static bool similar(AstExpr* lhs, AstExpr* rhs)
{
    if (lhs->classIndex != rhs->classIndex)
        return false;

#define CASE(T) else if (T* le = lhs->as<T>(), *re = rhs->as<T>(); le && re)

    if (false)
        return false;
    CASE(AstExprGroup) return similar(le->expr, re->expr);
    CASE(AstExprConstantNil) return true;
    CASE(AstExprConstantBool) return le->value == re->value;
    CASE(AstExprConstantNumber) return le->value == re->value;
    CASE(AstExprConstantInteger) return le->value == re->value;
    CASE(AstExprConstantString) return le->value.size == re->value.size && memcmp(le->value.data, re->value.data, le->value.size) == 0;
    CASE(AstExprLocal) return le->local == re->local;
    CASE(AstExprGlobal) return le->name == re->name;
    CASE(AstExprVarargs) return true;
    CASE(AstExprIndexName) return le->index == re->index && similar(le->expr, re->expr);
    CASE(AstExprIndexExpr) return similar(le->expr, re->expr) && similar(le->index, re->index);
    CASE(AstExprFunction) return false; // rarely meaningful in context of this pass, avoids having to process statement nodes
    CASE(AstExprUnary) return le->op == re->op && similar(le->expr, re->expr);
    CASE(AstExprBinary) return le->op == re->op && similar(le->left, re->left) && similar(le->right, re->right);
    CASE(AstExprTypeAssertion) return le->expr == re->expr; // the type doesn't affect execution semantics, avoids having to process type nodes
    CASE(AstExprError) return false;
    CASE(AstExprCall)
    {
        if (le->args.size != re->args.size || le->self != re->self)
            return false;

        if (!similar(le->func, re->func))
            return false;

        for (size_t i = 0; i < le->args.size; ++i)
            if (!similar(le->args.data[i], re->args.data[i]))
                return false;

        return true;
    }
    CASE(AstExprTable)
    {
        if (le->items.size != re->items.size)
            return false;

        for (size_t i = 0; i < le->items.size; ++i)
        {
            const AstExprTable::Item& li = le->items.data[i];
            const AstExprTable::Item& ri = re->items.data[i];

            if (li.kind != ri.kind)
                return false;

            if (bool(li.key) != bool(ri.key))
                return false;
            else if (li.key && !similar(li.key, ri.key))
                return false;

            if (!similar(li.value, ri.value))
                return false;
        }

        return true;
    }
    // Luwu If Local (rfcs/if-local.md): a `when` chain is never similar to anything, since its bindings are new locals
    CASE(AstExprIfElse) return le->clauses.size == 0 && re->clauses.size == 0 && similar(le->condition, re->condition) &&
                               similar(le->trueExpr, re->trueExpr) && similar(le->falseExpr, re->falseExpr);
    CASE(AstExprInterpString)
    {
        if (le->strings.size != re->strings.size)
            return false;

        if (le->expressions.size != re->expressions.size)
            return false;

        for (size_t i = 0; i < le->strings.size; ++i)
            if (le->strings.data[i].size != re->strings.data[i].size ||
                memcmp(le->strings.data[i].data, re->strings.data[i].data, le->strings.data[i].size) != 0)
                return false;

        for (size_t i = 0; i < le->expressions.size; ++i)
            if (!similar(le->expressions.data[i], re->expressions.data[i]))
                return false;

        return true;
    }
    CASE(AstExprInstantiate)
    {
        return similar(le->expr, re->expr);
    }
    // Luwu Do Expressions (rfcs/do-expressions.md): a block runs statements, so two are never the same check
    CASE(AstExprDo) return false;
    // Luwu Table Comprehensions (rfcs/table-comprehensions.md): each evaluation builds a new table
    CASE(AstExprTableComprehension) return false;
    else
    {
        LUAU_ASSERT(!"Unknown expression type");
        return false;
    }

#undef CASE
}

class LintGlobalLocal : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintGlobalLocal pass;
        pass.context = &context;

        for (auto& global : context.builtinGlobals)
        {
            Global& g = pass.globals[global.first];

            g.builtin = true;
            g.deprecated = global.second.deprecated;
        }

        context.root->visit(&pass);

        pass.report();
    }

private:
    struct FunctionInfo
    {
        explicit FunctionInfo(AstExprFunction* ast)
            : ast(ast)
            , dominatedGlobals({})
            , conditionalExecution(false)
        {
        }

        AstExprFunction* ast;
        DenseHashSet<AstName> dominatedGlobals;
        bool conditionalExecution;
    };

    struct Global
    {
        AstExprGlobal* firstRef = nullptr;

        std::vector<AstExprFunction*> functionRef;

        bool assigned = false;
        bool builtin = false;
        bool declared = false;
        bool definedInModuleScope = false;
        bool definedAsFunction = false;
        bool readBeforeWritten = false;
        std::optional<const char*> deprecated;
    };

    LintContext* context;

    DenseHashMap<AstName, Global> globals;
    std::vector<AstExprGlobal*> globalRefs;
    std::vector<FunctionInfo> functionStack;


    LintGlobalLocal()
        : globals(AstName())
    {
    }

    void report()
    {
        for (size_t i = 0; i < globalRefs.size(); ++i)
        {
            AstExprGlobal* gv = globalRefs[i];
            Global* g = globals.find(gv->name);

            if (!g || (!g->assigned && !g->builtin && !g->declared))
                emitWarning(
                    *context, LintWarning::Code_UnknownGlobal, gv->location, "Unknown global '%s'; consider assigning to it first", gv->name.value
                );
            else if (g->deprecated)
            {
                if (const char* replacement = *g->deprecated; replacement && strlen(replacement))
                    emitWarning(
                        *context,
                        LintWarning::Code_DeprecatedGlobal,
                        gv->location,
                        "Global '%s' is deprecated, use '%s' instead",
                        gv->name.value,
                        replacement
                    );
                else
                    emitWarning(*context, LintWarning::Code_DeprecatedGlobal, gv->location, "Global '%s' is deprecated", gv->name.value);
            }
        }

        for (auto& global : globals)
        {
            const Global& g = global.second;

            // A declared global exists at runtime whoever writes it, so writing it isn't a sign it should be a local.
            if (g.declared)
                continue;

            if (g.functionRef.size() && g.assigned && g.firstRef->name != context->placeholder)
            {
                AstExprFunction* top = g.functionRef.back();

                if (top->debugname.value)
                    emitWarning(
                        *context,
                        LintWarning::Code_GlobalUsedAsLocal,
                        g.firstRef->location,
                        "Global '%s' is only used in the enclosing function '%s'; consider changing it to local",
                        g.firstRef->name.value,
                        top->debugname.value
                    );
                else
                    emitWarning(
                        *context,
                        LintWarning::Code_GlobalUsedAsLocal,
                        g.firstRef->location,
                        "Global '%s' is only used in the enclosing function defined at line %d; consider changing it to local",
                        g.firstRef->name.value,
                        top->location.begin.line + 1
                    );
            }
            else if (g.assigned && !g.readBeforeWritten && !g.definedInModuleScope && g.firstRef->name != context->placeholder)
            {
                emitWarning(
                    *context,
                    LintWarning::Code_GlobalUsedAsLocal,
                    g.firstRef->location,
                    "Global '%s' is never read before being written. Consider changing it to local",
                    g.firstRef->name.value
                );
            }
        }
    }

    bool visit(AstExprFunction* node) override
    {
        functionStack.emplace_back(node);

        node->body->visit(this);

        functionStack.pop_back();

        return false;
    }

    bool visit(AstExprGlobal* node) override
    {
        if (!functionStack.empty() && !functionStack.back().dominatedGlobals.contains(node->name))
        {
            Global& g = globals[node->name];
            g.readBeforeWritten = true;
        }
        trackGlobalRef(node);

        if (node->name == context->placeholder)
            emitWarning(
                *context, LintWarning::Code_PlaceholderRead, node->location, "Placeholder value '_' is read here; consider using a named variable"
            );

        return true;
    }

    bool visit(AstExprLocal* node) override
    {
        if (node->local->name == context->placeholder)
            emitWarning(
                *context, LintWarning::Code_PlaceholderRead, node->location, "Placeholder value '_' is read here; consider using a named variable"
            );

        return true;
    }

    // Luwu Declare Statements (rfcs/declare-statements.md): a declaration says the global exists for the whole file.
    bool visit(AstStatDeclareGlobal* node) override
    {
        globals[node->name].declared = true;
        return false;
    }

    bool visit(AstStatDeclareFunction* node) override
    {
        globals[node->name].declared = true;
        return false;
    }

    bool visit(AstStatAssign* node) override
    {
        for (size_t i = 0; i < node->vars.size; ++i)
        {
            AstExpr* var = node->vars.data[i];

            if (AstExprGlobal* gv = var->as<AstExprGlobal>())
            {
                Global& g = globals[gv->name];

                if (functionStack.empty())
                {
                    g.definedInModuleScope = true;
                }
                else
                {
                    if (!functionStack.back().conditionalExecution)
                    {
                        functionStack.back().dominatedGlobals.insert(gv->name);
                    }
                }

                if (g.builtin)
                    emitWarning(
                        *context,
                        LintWarning::Code_BuiltinGlobalWrite,
                        gv->location,
                        "Built-in global '%s' is overwritten here; consider using a local or changing the name",
                        gv->name.value
                    );
                else
                    g.assigned = true;

                trackGlobalRef(gv);
            }
            else if (var->is<AstExprLocal>())
            {
                // We don't visit locals here because it's a local *write*, and visit(AstExprLocal*) assumes it's a local *read*
            }
            else
            {
                var->visit(this);
            }
        }

        for (size_t i = 0; i < node->values.size; ++i)
            node->values.data[i]->visit(this);

        return false;
    }

    bool visit(AstStatFunction* node) override
    {
        if (AstExprGlobal* gv = node->name->as<AstExprGlobal>())
        {
            Global& g = globals[gv->name];

            if (g.builtin)
                emitWarning(
                    *context,
                    LintWarning::Code_BuiltinGlobalWrite,
                    gv->location,
                    "Built-in global '%s' is overwritten here; consider using a local or changing the name",
                    gv->name.value
                );
            else
            {
                g.assigned = true;
                g.definedAsFunction = true;
                g.definedInModuleScope = functionStack.empty();
            }

            trackGlobalRef(gv);
        }

        return true;
    }

    class HoldConditionalExecution
    {
    public:
        HoldConditionalExecution(LintGlobalLocal& p)
            : p(p)
        {
            if (!p.functionStack.empty() && !p.functionStack.back().conditionalExecution)
            {
                resetToFalse = true;
                p.functionStack.back().conditionalExecution = true;
            }
        }
        ~HoldConditionalExecution()
        {
            if (resetToFalse)
                p.functionStack.back().conditionalExecution = false;
        }

    private:
        bool resetToFalse = false;
        LintGlobalLocal& p;
    };

    bool visit(AstStatIf* node) override
    {
        HoldConditionalExecution ce(*this);
        node->visitCondition(this);
        node->thenbody->visit(this);
        if (node->elsebody)
            node->elsebody->visit(this);

        return false;
    }

    bool visit(AstStatWhile* node) override
    {
        HoldConditionalExecution ce(*this);
        node->condition->visit(this);
        node->body->visit(this);

        return false;
    }

    bool visit(AstStatRepeat* node) override
    {
        HoldConditionalExecution ce(*this);
        node->condition->visit(this);
        node->body->visit(this);

        return false;
    }

    bool visit(AstStatFor* node) override
    {
        HoldConditionalExecution ce(*this);
        node->from->visit(this);
        node->to->visit(this);

        if (node->step)
            node->step->visit(this);

        node->body->visit(this);

        return false;
    }

    bool visit(AstStatForIn* node) override
    {
        HoldConditionalExecution ce(*this);
        for (AstExpr* expr : node->values)
            expr->visit(this);

        node->body->visit(this);

        return false;
    }

    void trackGlobalRef(AstExprGlobal* node)
    {
        Global& g = globals[node->name];

        globalRefs.push_back(node);

        if (!g.firstRef)
        {
            g.firstRef = node;

            // to reduce the cost of tracking we only track this for user globals
            if (!g.builtin)
            {
                g.functionRef.clear();
                g.functionRef.reserve(functionStack.size());
                for (const FunctionInfo& entry : functionStack)
                {
                    g.functionRef.push_back(entry.ast);
                }
            }
        }
        else
        {
            // to reduce the cost of tracking we only track this for user globals
            if (!g.builtin)
            {
                // we need to find a common prefix between all uses of a global
                size_t prefix = 0;

                while (prefix < g.functionRef.size() && prefix < functionStack.size() && g.functionRef[prefix] == functionStack[prefix].ast)
                    prefix++;

                g.functionRef.resize(prefix);
            }
        }
    }
};

class LintSameLineStatement : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintSameLineStatement pass;

        pass.context = &context;
        pass.lastLine = ~0u;

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    unsigned int lastLine;

    bool visit(AstStatBlock* node) override
    {
        for (size_t i = 1; i < node->body.size; ++i)
        {
            const Location& last = node->body.data[i - 1]->location;
            const Location& location = node->body.data[i]->location;

            if (location.begin.line != last.end.line)
                continue;

            // We warn once per line with multiple statements
            if (location.begin.line == lastLine)
                continue;

            // There's a common pattern where local variables are computed inside a do block that starts on the same line; we white-list this pattern
            if (node->body.data[i - 1]->is<AstStatLocal>() && node->body.data[i]->is<AstStatBlock>())
                continue;

            // Another common pattern is using multiple statements on the same line with semi-colons on each of them. White-list this pattern too.
            if (node->body.data[i - 1]->hasSemicolon)
                continue;

            // Luwu Destructuring (rfcs/destructuring.md): the statements a destructuring declaration desugars to
            // are one statement in the source.
            if (AstStatLocal* local = node->body.data[i]->as<AstStatLocal>(); local && local->destructuredFrom)
                continue;

            emitWarning(
                *context,
                LintWarning::Code_SameLineStatement,
                location,
                "A new statement is on the same line; add semi-colon on previous statement to silence"
            );

            lastLine = location.begin.line;
        }

        return true;
    }

    bool visit(AstStatClass* node) override
    {
        for (size_t i = 1; i < node->members.size; ++i)
        {
            Location last = Luau::visit([](auto&& member) -> Location
                { return member.nameLocation; }, node->members.data[i - 1]);
            Location location = Luau::visit([](auto&& member) -> Location
                { return member.nameLocation; }, node->members.data[i]);

            if (location.begin.line != last.end.line)
                continue;

            if (location.begin.line == lastLine)
                continue;

            bool lastHasSemicolon =
                Luau::visit([](auto&& member) -> bool
                    { return member.hasSemicolon; }, node->members.data[i - 1]);

            if (lastHasSemicolon)
                continue;

            emitWarning(
                *context,
                LintWarning::Code_SameLineStatement,
                location,
                "Each class field should be on its own line; separate fields with a semicolon to silence"
            );

            lastLine = location.begin.line;
        }

        return true;
    }
};

class LintMultiLineStatement : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintMultiLineStatement pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    struct Statement
    {
        Location start;
        unsigned int lastLine;
        bool flagged;
    };

    std::vector<Statement> stack;

    bool visit(AstExpr* node) override
    {
        Statement& top = stack.back();

        if (!top.flagged)
        {
            Location location = node->location;

            if (location.begin.line > top.lastLine)
            {
                top.lastLine = location.begin.line;

                if (location.begin.column <= top.start.begin.column)
                {
                    emitWarning(
                        *context, LintWarning::Code_MultiLineStatement, location, "Statement spans multiple lines; use indentation to silence"
                    );

                    top.flagged = true;
                }
            }
        }

        return true;
    }

    bool visit(AstExprTable* node) override
    {
        (void)node;

        return false;
    }

    // Luwu Do Expressions (rfcs/do-expressions.md): a statement on the line its `do` expression starts on starts
    // mid-line, so it's measured against the indentation of the statement around the expression. One on its own line
    // is measured against itself, as usual.
    bool visit(AstExprDo* node) override
    {
        visit(static_cast<AstExpr*>(node));

        Location outer = stack.back().start;

        for (AstStat* stmt : node->body->body)
        {
            Location start = stmt->location;
            if (start.begin.line == node->location.begin.line)
                start.begin.column = outer.begin.column;

            stack.push_back({start, stmt->location.begin.line, false});
            stmt->visit(this);
            stack.pop_back();
        }

        // The statement around the expression has been checked down to the expression's end
        stack.back().lastLine = std::max(stack.back().lastLine, node->location.end.line);

        return false;
    }

    bool visit(AstStatRepeat* node) override
    {
        node->body->visit(this);

        return false;
    }

    bool visit(AstStatBlock* node) override
    {
        for (size_t i = 0; i < node->body.size; ++i)
        {
            AstStat* stmt = node->body.data[i];

            Statement s = {stmt->location, stmt->location.begin.line, false};
            stack.push_back(s);

            stmt->visit(this);

            stack.pop_back();
        }

        return false;
    }
};

class LintLocalHygiene : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintLocalHygiene pass;
        pass.context = &context;

        for (auto& global : context.builtinGlobals)
            pass.globals[global.first].builtin = true;

        context.root->visit(&pass);

        pass.report();
    }

private:
    LintContext* context;

    struct Local
    {
        AstNode* defined = nullptr;
        std::optional<Location> function;
        bool import;
        bool used;
        bool softUsed;
        bool arg;
    };

    struct Global
    {
        bool used;
        bool builtin;
        AstExprGlobal* firstRef;
    };

    DenseHashMap<AstLocal*, Local> locals;
    DenseHashMap<AstName, AstLocal*> imports;
    DenseHashMap<AstName, Global> globals;
    DenseHashMap<AstName, AstStatClass*> classes;

    LintLocalHygiene()
        : locals(NULL)
        , imports(AstName())
        , globals(AstName())
        , classes(AstName())
    {
    }

    void report()
    {
        for (auto& l : locals)
        {
            bool shadowsClass = l.second.defined && reportClassShadow(l.first);

            if (l.second.used)
            {
                if (!shadowsClass)
                    reportUsedLocal(l.first, l.second);
            }
            else if (l.second.defined)
                reportUnusedLocal(l.first, l.second);
        }
    }

    // Luwu Classes (rfcs/classes): classes are declared at the top level and a class binding is
    // const, so a `local` or `local function` with a class's name anywhere in its module (before or
    // after the class, at any depth) hides the class from the code that follows it.
    bool reportClassShadow(AstLocal* local)
    {
        AstStatClass** cls = classes.find(local->name);
        if (!cls)
            return false;

        emitWarning(
            *context,
            LintWarning::Code_LocalShadow,
            local->location,
            "Variable '%s' shadows %s '%s' declared at line %d",
            local->name.value,
            (*cls)->isTrait ? "trait" : "class",
            (*cls)->name->name.value,
            (*cls)->name->location.begin.line + 1
        );
        return true;
    }

    void reportUsedLocal(AstLocal* local, const Local& info)
    {
        if (AstLocal* shadow = local->shadow)
        {
            // LintDuplicateFunctions will catch this.
            Local* shadowLocal = locals.find(shadow);
            if (context->options.isEnabled(LintWarning::Code_DuplicateFunction) && info.function && shadowLocal && shadowLocal->function)
                return;

            // LintDuplicateLocal will catch this.
            if (context->options.isEnabled(LintWarning::Code_DuplicateLocal) && shadowLocal && shadowLocal->defined == info.defined)
                return;

            // don't warn on inter-function shadowing since it is much more fragile wrt refactoring
            if (shadow->functionDepth == local->functionDepth)
                emitWarning(
                    *context,
                    LintWarning::Code_LocalShadow,
                    local->location,
                    "Variable '%s' shadows previous declaration at line %d",
                    local->name.value,
                    shadow->location.begin.line + 1
                );
        }
        else if (Global* global = globals.find(local->name))
        {
            if (global->builtin)
                ; // there are many builtins with common names like 'table'; some of them are deprecated as well
            else if (global->firstRef)
            {
                emitWarning(
                    *context,
                    LintWarning::Code_LocalShadow,
                    local->location,
                    "Variable '%s' shadows a global variable used at line %d",
                    local->name.value,
                    global->firstRef->location.begin.line + 1
                );
            }
            else
            {
                emitWarning(*context, LintWarning::Code_LocalShadow, local->location, "Variable '%s' shadows a global variable", local->name.value);
            }
        }
    }

    void reportUnusedLocal(AstLocal* local, const Local& info)
    {
        if (local->name.value[0] == '_')
            return;

        if (info.function)
            if (info.softUsed)
                emitWarning(
                    *context,
                    LintWarning::Code_FunctionUnused,
                    local->location,
                    "Function '%s' is never used outside its own body; prefix with '_' to silence",
                    local->name.value
                );
            else
                emitWarning(
                    *context,
                    LintWarning::Code_FunctionUnused,
                    local->location,
                    "Function '%s' is never used; prefix with '_' to silence",
                    local->name.value
                );
        else if (info.import)
        {
            emitWarning(*context, LintWarning::Code_ImportUnused, local->location, "Import '%s' is never used; prefix with '_' to silence", local->name.value);
        }
        else
        {
            emitWarning(*context, LintWarning::Code_LocalUnused, local->location, "Variable '%s' is never used; prefix with '_' to silence", local->name.value);
        }

    }

    bool isRequireCall(AstExpr* expr)
    {
        AstExprCall* call = expr->as<AstExprCall>();
        if (!call)
            return false;

        AstExprGlobal* glob = call->func->as<AstExprGlobal>();
        if (!glob)
            return false;

        return glob->name == "require";
    }

    bool visit(AstStatAssign* node) override
    {
        for (AstExpr* var : node->vars)
        {
            // We don't visit locals here because it's a local *write*, and visit(AstExprLocal*) assumes it's a local *read*
            if (!var->is<AstExprLocal>())
                var->visit(this);
        }

        for (AstExpr* value : node->values)
            value->visit(this);

        return false;
    }

    bool visit(AstStatLocal* node) override
    {
        if (node->vars.size == 1 && node->values.size == 1)
        {
            Local& l = locals[node->vars.data[0]];

            l.defined = node;
            l.import = isRequireCall(node->values.data[0]);

            if (l.import)
                imports[node->vars.data[0]->name] = node->vars.data[0];
        }
        else
        {
            for (size_t i = 0; i < node->vars.size; ++i)
            {
                Local& l = locals[node->vars.data[i]];

                l.defined = node;
            }
        }

        return true;
    }

    bool visit(AstStatLocalFunction* node) override
    {
        Local& l = locals[node->name];

        l.defined = node;
        l.function.emplace(node->location);

        return true;
    }

    bool visit(AstStatClass* node) override
    {
        classes[node->name->name] = node;
        return true;
    }

    bool visit(AstExprLocal* node) override
    {
        Local& l = locals[node->local];

        if (l.function && l.function.value().contains(node->location.begin))
            l.softUsed = true;
        else
            l.used = true;

        return true;
    }

    bool visit(AstExprGlobal* node) override
    {
        Global& global = globals[node->name];

        global.used = true;
        if (!global.firstRef)
            global.firstRef = node;

        return true;
    }

    bool visit(AstType*) override
    {
        return true;
    }

    bool visit(AstTypePack* node) override
    {
        return true;
    }

    bool visit(AstTypeReference* node) override
    {
        if (!node->prefix)
            return true;

        if (!imports.contains(*node->prefix))
            return true;

        AstLocal* astLocal = imports[*node->prefix];
        Local& local = locals[astLocal];
        LUAU_ASSERT(local.import);
        local.used = true;

        return true;
    }

    bool visit(AstExprFunction* node) override
    {
        if (node->self)
            locals[node->self].arg = true;

        for (size_t i = 0; i < node->args.size; ++i)
            locals[node->args.data[i]].arg = true;

        return true;
    }
};

class LintUnusedFunction : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintUnusedFunction pass;
        pass.context = &context;

        context.root->visit(&pass);

        pass.report();
    }

private:
    LintContext* context;

    struct Global
    {
        Location location;
        std::optional<Location> function;
        bool softUsed;
        bool used;
    };

    DenseHashMap<AstName, Global> globals;

    LintUnusedFunction()
        : globals(AstName())
    {
    }

    void report()
    {
        for (auto& g : globals)
        {
            if (!g.second.function || g.second.used || g.first.value[0] == '_')
                continue;
            if (g.second.softUsed)
                emitWarning(
                    *context,
                    LintWarning::Code_FunctionUnused,
                    g.second.location,
                    "Function '%s' is never used outside its own body; prefix with '_' to silence",
                    g.first.value
                );
            else
                emitWarning(
                    *context,
                    LintWarning::Code_FunctionUnused,
                    g.second.location,
                    "Function '%s' is never used; prefix with '_' to silence",
                    g.first.value
                );
        }
    }

    bool visit(AstStatFunction* node) override
    {
        AstExprGlobal* expr = node->name->as<AstExprGlobal>();
        if (expr) {
            Global& g = globals[expr->name];

            g.function.emplace(node->location);
            g.location = expr->location;

            node->func->visit(this);

            return false;
        }

        return true;
    }

    bool visit(AstExprGlobal* node) override
    {
        Global& g = globals[node->name];

        if (g.function && g.function.value().contains(node->location.begin))
            g.softUsed = true;
        else
            g.used = true;

        return true;
    }
};

class LintUnreachableCode : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintUnreachableCode pass;
        pass.context = &context;

        pass.analyze(context.root);
        context.root->visit(&pass);
    }

private:
    LintContext* context;

    // Note: this enum is order-sensitive!
    // The order is in the "severity" of the termination and affects merging of status codes from different branches
    // For example, if one branch breaks and one returns, the merged result is "break"
    enum Status
    {
        Unknown,
        Continue,
        Break,
        Give,
        Return,
        Error,
    };

    const char* getReason(Status status)
    {
        switch (status)
        {
        case Continue:
            return "continue";

        case Break:
            return "break";

        case Give:
            return "give";

        case Return:
            return "return";

        case Error:
            return "error";

        default:
            return "unknown";
        }
    }

    Status analyze(AstStat* node)
    {
        if (AstStatBlock* stat = node->as<AstStatBlock>())
        {
            for (size_t i = 0; i < stat->body.size; ++i)
            {
                AstStat* si = stat->body.data[i];
                Status step = analyze(si);

                if (step != Unknown)
                {
                    if (i + 1 == stat->body.size)
                        return step;

                    AstStat* next = stat->body.data[i + 1];

                    // silence the warning for common pattern of Error (coming from error()) + Return
                    // Luwu Do Expressions (rfcs/do-expressions.md): or + Give, which a `do` expression's block needs last
                    bool leavesAfterError = next->is<AstStatReturn>() || next->is<AstStatGive>();
                    if (step == Error && si->is<AstStatExpr>() && leavesAfterError && i + 2 == stat->body.size)
                        return Error;

                    emitWarning(
                        *context,
                        LintWarning::Code_UnreachableCode,
                        next->location,
                        "Unreachable code (previous statement always %ss)",
                        getReason(step)
                    );
                    return step;
                }
            }

            return Unknown;
        }
        else if (AstStatIf* stat = node->as<AstStatIf>())
        {
            Status ifs = analyze(stat->thenbody);
            Status elses = stat->elsebody ? analyze(stat->elsebody) : Unknown;

            return std::min(ifs, elses);
        }
        else if (AstStatWhile* stat = node->as<AstStatWhile>())
        {
            analyze(stat->body);

            return Unknown;
        }
        else if (AstStatRepeat* stat = node->as<AstStatRepeat>())
        {
            analyze(stat->body);

            return Unknown;
        }
        else if (node->is<AstStatBreak>())
        {
            return Break;
        }
        else if (node->is<AstStatContinue>())
        {
            return Continue;
        }
        else if (node->is<AstStatReturn>())
        {
            return Return;
        }
        else if (node->is<AstStatGive>())
        {
            return Give;
        }
        else if (AstStatExpr* stat = node->as<AstStatExpr>())
        {
            if (AstExprCall* call = stat->expr->as<AstExprCall>())
                if (doesCallError(call))
                    return Error;

            return Unknown;
        }
        else if (AstStatFor* stat = node->as<AstStatFor>())
        {
            analyze(stat->body);

            return Unknown;
        }
        else if (AstStatForIn* stat = node->as<AstStatForIn>())
        {
            analyze(stat->body);

            return Unknown;
        }
        else
        {
            return Unknown;
        }
    }

    bool visit(AstExprFunction* node) override
    {
        analyze(node->body);

        return true;
    }

    // Luwu Do Expressions (rfcs/do-expressions.md): a `do` expression's block is checked like a function body
    bool visit(AstExprDo* node) override
    {
        analyze(node->body);

        return true;
    }
};

class LintUnknownType : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintUnknownType pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    enum TypeKind
    {
        Kind_Unknown,
        Kind_Primitive, // primitive type supported by VM - boolean/userdata/etc. No differentiation between types of userdata.
        Kind_Vector,    // TODO: deprecated and not set, but read in 'visit'
        Kind_Userdata,  // custom userdata type
    };

    TypeKind getTypeKind(const std::string& name)
    {
        if (name == "nil" || name == "boolean" || name == "userdata" || name == "number" || name == "string" || name == "table" ||
            name == "function" || name == "thread" || name == "buffer" || name == "none")
            return Kind_Primitive;

        if (name == "vector")
            return Kind_Primitive;

        // Luwu Classes (rfcs/classes): `type` and `typeof` answer "class" and "object", and "trait" for a trait's value
        if (FFlag::LuwuClasses && (name == "class" || name == "object"))
            return Kind_Primitive;
        if (FFlag::LuwuClasses && FFlag::LuwuTraits && name == "trait")
            return Kind_Primitive;

        if (std::optional<TypeFun> maybeTy = context->scope->lookupType(name))
            return Kind_Userdata;

        return Kind_Unknown;
    }

    void validateType(AstExprConstantString* expr, std::initializer_list<TypeKind> expected, const char* expectedString)
    {
        std::string name(expr->value.data, expr->value.size);
        TypeKind kind = getTypeKind(name);

        if (kind == Kind_Unknown)
        {
            emitWarning(*context, LintWarning::Code_UnknownType, expr->location, "Unknown type '%s'", name.c_str());
            return;
        }

        for (TypeKind ek : expected)
        {
            if (kind == ek)
                return;
        }

        emitWarning(*context, LintWarning::Code_UnknownType, expr->location, "Unknown type '%s' (expected %s)", name.c_str(), expectedString);
    }

    bool visit(AstExprBinary* node) override
    {
        if (node->op == AstExprBinary::CompareNe || node->op == AstExprBinary::CompareEq)
        {
            AstExpr* lhs = node->left;
            AstExpr* rhs = node->right;

            if (!rhs->is<AstExprConstantString>())
                std::swap(lhs, rhs);

            AstExprCall* call = lhs->as<AstExprCall>();
            AstExprConstantString* arg = rhs->as<AstExprConstantString>();

            if (call && arg)
            {
                AstExprGlobal* g = call->func->as<AstExprGlobal>();

                if (g && g->name == "type")
                {
                    validateType(arg, {Kind_Primitive, Kind_Vector}, "primitive type");
                }
                else if (g && g->name == "typeof")
                {
                    validateType(arg, {Kind_Primitive, Kind_Userdata}, "primitive or userdata type");
                }
            }
        }

        return true;
    }
};

class LintForRange : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintForRange pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    double getLoopEnd(double from, double to)
    {
        return from + floor(to - from);
    }

    static std::optional<double> constantNumber(AstExpr* expr)
    {
        if (AstExprConstantNumber* number = expr->as<AstExprConstantNumber>())
            return number->value;
        if (AstExprUnary* unary = expr->as<AstExprUnary>(); unary && unary->op == AstExprUnary::Op::Minus)
            if (AstExprConstantNumber* number = unary->expr->as<AstExprConstantNumber>())
                return -number->value;
        return std::nullopt;
    }

    static bool isLength(AstExpr* expr)
    {
        AstExprUnary* unary = expr->as<AstExprUnary>();
        return unary && unary->op == AstExprUnary::Op::Len;
    }

    // Luwu: a step whose sign points away from the end, so the loop never runs (`for i = 1, n, -1`). `#t` counts as at
    // least 1, which is when the loop would have anything to do.
    void checkStepDirection(AstStatFor* node)
    {
        std::optional<double> step = constantNumber(node->step);
        if (!step || *step == 0)
            return;

        std::optional<double> from = constantNumber(node->from);
        std::optional<double> to = constantNumber(node->to);

        bool upwards = (from && to && *from < *to) || (from && *from <= 1 && isLength(node->to));
        bool downwards = (from && to && *from > *to) || (to && *to <= 1 && isLength(node->from));

        Location rangeLocation(node->from->location, node->step->location);
        if (*step < 0 && upwards)
            emitWarning(
                *context,
                LintWarning::Code_ForRange,
                rangeLocation,
                "For loop counts down but ends above where it starts, so it never runs; did you mean to swap the bounds?"
            );
        else if (*step > 0 && downwards)
            emitWarning(
                *context,
                LintWarning::Code_ForRange,
                rangeLocation,
                "For loop counts up but ends below where it starts, so it never runs; did you mean to swap the bounds, or step by -1?"
            );
    }

    bool visit(AstStatFor* node) override
    {
        if (node->step)
            checkStepDirection(node);

        // note: we silence all warnings below if *any* step is specified, assuming that the user knows best
        if (!node->step)
        {
            AstExprConstantNumber* fc = node->from->as<AstExprConstantNumber>();
            AstExprUnary* fu = node->from->as<AstExprUnary>();
            AstExprConstantNumber* tc = node->to->as<AstExprConstantNumber>();
            AstExprUnary* tu = node->to->as<AstExprUnary>();

            Location rangeLocation(node->from->location, node->to->location);

            // for i=#t,1 do
            if (fu && fu->op == AstExprUnary::Op::Len && tc && tc->value == 1.0)
                emitWarning(
                    *context, LintWarning::Code_ForRange, rangeLocation, "For loop should iterate backwards; did you forget to specify -1 as step?"
                );
            // for i=8,1 do
            else if (fc && tc && fc->value > tc->value)
                emitWarning(
                    *context, LintWarning::Code_ForRange, rangeLocation, "For loop should iterate backwards; did you forget to specify -1 as step?"
                );
            // for i=1,8.75 do
            else if (fc && tc && getLoopEnd(fc->value, tc->value) != tc->value)
                emitWarning(
                    *context,
                    LintWarning::Code_ForRange,
                    rangeLocation,
                    "For loop ends at %g instead of %g; did you forget to specify step?",
                    getLoopEnd(fc->value, tc->value),
                    tc->value
                );
            // for i=0,#t do
            else if (fc && tu && fc->value == 0.0 && tu->op == AstExprUnary::Op::Len)
                emitWarning(*context, LintWarning::Code_ForRange, rangeLocation, "For loop starts at 0, but arrays start at 1");
            // for i=#t,0 do
            else if (fu && fu->op == AstExprUnary::Op::Len && tc && tc->value == 0.0)
                emitWarning(
                    *context,
                    LintWarning::Code_ForRange,
                    rangeLocation,
                    "For loop should iterate backwards; did you forget to specify -1 as step? Also consider changing 0 to 1 since arrays start at 1"
                );
        }

        return true;
    }
};

class LintUnbalancedAssignment : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintUnbalancedAssignment pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    void assign(size_t vars, const AstArray<AstExpr*>& values, const Location& location)
    {
        if (vars != values.size && values.size > 0)
        {
            AstExpr* last = values.data[values.size - 1];

            if (vars < values.size)
                emitWarning(
                    *context,
                    LintWarning::Code_UnbalancedAssignment,
                    location,
                    "Assigning %d values to %d variables leaves some values unused",
                    int(values.size),
                    int(vars)
                );
            else if (last->is<AstExprCall>() || last->is<AstExprVarargs>())
                ; // we don't know how many values the last expression returns
            else if (last->is<AstExprConstantNil>())
                ; // last expression is nil which explicitly silences the nil-init warning
            else
                emitWarning(
                    *context,
                    LintWarning::Code_UnbalancedAssignment,
                    location,
                    "Assigning %d values to %d variables initializes extra variables with nil; add 'nil' to value list to silence",
                    int(values.size),
                    int(vars)
                );
        }
    }

    bool visit(AstStatLocal* node) override
    {
        assign(node->vars.size, node->values, node->location);

        return true;
    }

    bool visit(AstStatAssign* node) override
    {
        assign(node->vars.size, node->values, node->location);

        return true;
    }
};

class LintImplicitReturn : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintImplicitReturn pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    Location getEndLocation(const AstStat* node)
    {
        Location loc = node->location;

        if (node->is<AstStatExpr>() || node->is<AstStatAssign>() || node->is<AstStatLocal>())
            return loc;

        if (loc.begin.line == loc.end.line)
            return loc;

        // assume that we're in context of a statement that has an "end" block
        return Location(Position(loc.end.line, std::max(0, int(loc.end.column) - 3)), loc.end);
    }

    AstStatReturn* getValueReturn(AstStat* node)
    {
        struct Visitor : AstVisitor
        {
            AstStatReturn* result = nullptr;

            bool visit(AstExpr* node) override
            {
                (void)node;
                return false;
            }

            bool visit(AstStatReturn* node) override
            {
                if (!result && node->list.size > 0)
                    result = node;

                return false;
            }
        };

        Visitor visitor;
        node->visit(&visitor);
        return visitor.result;
    }

    bool visit(AstExprFunction* node) override
    {
        const AstStat* bodyf = getFallthrough(node->body);
        AstStat* vret = getValueReturn(node->body);

        if (bodyf && vret)
        {
            Location location = getEndLocation(bodyf);

            if (node->debugname.value)
                emitWarning(
                    *context,
                    LintWarning::Code_ImplicitReturn,
                    location,
                    "Function '%s' can implicitly return no values even though there's an explicit return at line %d; add explicit return to silence",
                    node->debugname.value,
                    vret->location.begin.line + 1
                );
            else
                emitWarning(
                    *context,
                    LintWarning::Code_ImplicitReturn,
                    location,
                    "Function can implicitly return no values even though there's an explicit return at line %d; add explicit return to silence",
                    vret->location.begin.line + 1
                );
        }

        return true;
    }
};

class LintFormatString : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintFormatString pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

    static void fuzz(const char* data, size_t size)
    {
        LintContext context;

        LintFormatString pass;
        pass.context = &context;

        pass.checkStringFormat(data, size);
        pass.checkStringPack(data, size, false);
        pass.checkStringMatch(data, size);
        pass.checkStringReplace(data, size, -1);
        pass.checkDateFormat(data, size);
    }

private:
    LintContext* context;

    static inline bool isAlpha(char ch)
    {
        // use or trick to convert to lower case and unsigned comparison to do range check
        return unsigned((ch | ' ') - 'a') < 26;
    }

    static inline bool isDigit(char ch)
    {
        // use unsigned comparison to do range check for performance
        return unsigned(ch - '0') < 10;
    }

    const char* checkStringFormat(const char* data, size_t size)
    {
        const char* flags = "-+ #0";
        const char* options = "cdiouxXeEfgGqs*";

        for (size_t i = 0; i < size; ++i)
        {
            if (data[i] == '%')
            {
                i++;

                // escaped % doesn't allow for flags/etc.
                if (i < size && data[i] == '%')
                    continue;

                // skip flags
                while (i < size && strchr(flags, data[i]))
                    i++;

                // skip width (up to two digits)
                if (i < size && isDigit(data[i]))
                    i++;
                if (i < size && isDigit(data[i]))
                    i++;

                // skip precision
                if (i < size && data[i] == '.')
                {
                    i++;

                    // up to two digits
                    if (i < size && isDigit(data[i]))
                        i++;
                    if (i < size && isDigit(data[i]))
                        i++;
                }

                if (i == size)
                    return "unfinished format specifier";

                if (!strchr(options, data[i]))
                    return "invalid format specifier: must be a string format specifier or %";
            }
        }

        return nullptr;
    }

    const char* checkStringPack(const char* data, size_t size, bool fixed)
    {
        const char* options = "<>=!bBhHlLjJTiIfdnczsxX ";
        const char* unsized = "<>=!zX ";

        for (size_t i = 0; i < size; ++i)
        {
            if (!strchr(options, data[i]))
                return "unexpected character; must be a pack specifier or space";

            if (data[i] == 'c' && (i + 1 == size || !isDigit(data[i + 1])))
                return "fixed-sized string format must specify the size";

            if (data[i] == 'X' && (i + 1 == size || strchr(unsized, data[i + 1])))
                return "X must be followed by a size specifier";

            if (fixed && (data[i] == 'z' || data[i] == 's'))
                return "pack specifier must be fixed-size";

            if ((data[i] == '!' || data[i] == 'i' || data[i] == 'I' || data[i] == 'c' || data[i] == 's') && i + 1 < size && isDigit(data[i + 1]))
            {
                bool isc = data[i] == 'c';

                unsigned int v = 0;
                while (i + 1 < size && isDigit(data[i + 1]) && v <= (INT_MAX - 9) / 10)
                {
                    v = v * 10 + (data[i + 1] - '0');
                    i++;
                }

                if (i + 1 < size && isDigit(data[i + 1]))
                    return "size specifier is too large";

                if (!isc && (v == 0 || v > 16))
                    return "integer size must be in range [1,16]";
            }
        }

        return nullptr;
    }

    const char* checkStringMatchSet(const char* data, size_t size, const char* magic, const char* classes)
    {
        for (size_t i = 0; i < size; ++i)
        {
            if (data[i] == '%')
            {
                i++;

                if (i == size)
                    return "unfinished character class";

                if (isDigit(data[i]))
                {
                    return "sets can not contain capture references";
                }
                else if (isAlpha(data[i]))
                {
                    // lower case lookup - upper case for every character class is defined as its inverse
                    if (!strchr(classes, data[i] | ' '))
                        return "invalid character class, must refer to a defined class or its inverse";
                }
                else
                {
                    // technically % can escape any non-alphanumeric character but this is error-prone
                    if (!strchr(magic, data[i]))
                        return "expected a magic character after %";
                }

                if (i + 1 < size && data[i + 1] == '-')
                    return "character range can't include character sets";
            }
            else if (data[i] == '-')
            {
                if (i + 1 < size && data[i + 1] == '%')
                    return "character range can't include character sets";
            }
        }

        return nullptr;
    }

    const char* checkStringMatch(const char* data, size_t size, int* outCaptures = nullptr)
    {
        const char* magic = "^$()%.[]*+-?)";
        const char* classes = "acdglpsuwxz";

        std::vector<int> openCaptures;
        int totalCaptures = 0;

        for (size_t i = 0; i < size; ++i)
        {
            if (data[i] == '%')
            {
                i++;

                if (i == size)
                    return "unfinished character class";

                if (isDigit(data[i]))
                {
                    if (data[i] == '0')
                        return "invalid capture reference, must be 1-9";

                    int captureIndex = data[i] - '0';

                    if (captureIndex > totalCaptures)
                        return "invalid capture reference, must refer to a valid capture";

                    for (int open : openCaptures)
                        if (open == captureIndex)
                            return "invalid capture reference, must refer to a closed capture";
                }
                else if (isAlpha(data[i]))
                {
                    if (data[i] == 'b')
                    {
                        if (i + 2 >= size)
                            return "missing brace characters for balanced match";

                        i += 2;
                    }
                    else if (data[i] == 'f')
                    {
                        if (i + 1 >= size || data[i + 1] != '[')
                            return "missing set after a frontier pattern";

                        // we can parse the set with the regular logic
                    }
                    else
                    {
                        // lower case lookup - upper case for every character class is defined as its inverse
                        if (!strchr(classes, data[i] | ' '))
                            return "invalid character class, must refer to a defined class or its inverse";
                    }
                }
                else
                {
                    // technically % can escape any non-alphanumeric character but this is error-prone
                    if (!strchr(magic, data[i]))
                        return "expected a magic character after %";
                }
            }
            else if (data[i] == '[')
            {
                size_t j = i + 1;

                // empty patterns don't exist as per grammar rules, so we skip leading ^ and ]
                if (j < size && data[j] == '^')
                    j++;

                if (j < size && data[j] == ']')
                    j++;

                // scan for the end of the pattern
                while (j < size && data[j] != ']')
                {
                    // % escapes the next character
                    if (j + 1 < size && data[j] == '%')
                        j++;

                    j++;
                }

                if (j == size)
                    return "expected ] at the end of the string to close a set";

                if (const char* error = checkStringMatchSet(data + i + 1, j - i - 1, magic, classes))
                    return error;

                LUAU_ASSERT(data[j] == ']');
                i = j;
            }
            else if (data[i] == '(')
            {
                totalCaptures++;
                openCaptures.push_back(totalCaptures);
            }
            else if (data[i] == ')')
            {
                if (openCaptures.empty())
                    return "unexpected ) without a matching (";
                openCaptures.pop_back();
            }
        }

        if (!openCaptures.empty())
            return "expected ) at the end of the string to close a capture";

        if (outCaptures)
            *outCaptures = totalCaptures;

        return nullptr;
    }

    const char* checkStringReplace(const char* data, size_t size, int captures)
    {
        for (size_t i = 0; i < size; ++i)
        {
            if (data[i] == '%')
            {
                i++;

                if (i == size)
                    return "unfinished replacement";

                if (data[i] != '%' && !isDigit(data[i]))
                    return "unexpected replacement character; must be a digit or %";

                if (isDigit(data[i]) && captures >= 0 && data[i] - '0' > captures)
                    return "invalid capture index, must refer to pattern capture";
            }
        }

        return nullptr;
    }

    const char* checkDateFormat(const char* data, size_t size)
    {
        const char* options = "aAbBcdHIjmMpSUwWxXyYzZ";

        for (size_t i = 0; i < size; ++i)
        {
            if (data[i] == '%')
            {
                i++;

                if (i == size)
                    return "unfinished replacement";

                if (data[i] != '%' && !strchr(options, data[i]))
                    return "unexpected replacement character; must be a date format specifier or %";
            }

            if (data[i] == 0)
                return "date format can not contain null characters";
        }

        return nullptr;
    }

    void matchStringCall(AstName name, AstExpr* self, AstArray<AstExpr*> args)
    {
        if (name == "format")
        {
            if (AstExprConstantString* fmt = self->as<AstExprConstantString>())
                if (const char* error = checkStringFormat(fmt->value.data, fmt->value.size))
                    emitWarning(*context, LintWarning::Code_FormatString, fmt->location, "Invalid format string: %s", error);
        }
        else if (name == "pack" || name == "packsize" || name == "unpack")
        {
            if (AstExprConstantString* fmt = self->as<AstExprConstantString>())
                if (const char* error = checkStringPack(fmt->value.data, fmt->value.size, name == "packsize"))
                    emitWarning(*context, LintWarning::Code_FormatString, fmt->location, "Invalid pack format: %s", error);
        }
        else if ((name == "match" || name == "gmatch") && args.size > 0)
        {
            if (AstExprConstantString* pat = args.data[0]->as<AstExprConstantString>())
                if (const char* error = checkStringMatch(pat->value.data, pat->value.size))
                    emitWarning(*context, LintWarning::Code_FormatString, pat->location, "Invalid match pattern: %s", error);
        }
        else if (name == "find" && args.size > 0 && args.size <= 2)
        {
            if (AstExprConstantString* pat = args.data[0]->as<AstExprConstantString>())
                if (const char* error = checkStringMatch(pat->value.data, pat->value.size))
                    emitWarning(*context, LintWarning::Code_FormatString, pat->location, "Invalid match pattern: %s", error);
        }
        else if (name == "find" && args.size >= 3)
        {
            AstExprConstantBool* mode = args.data[2]->as<AstExprConstantBool>();

            // find(_, _, _, true) is a raw string find, not a pattern match
            if (mode && !mode->value)
                if (AstExprConstantString* pat = args.data[0]->as<AstExprConstantString>())
                    if (const char* error = checkStringMatch(pat->value.data, pat->value.size))
                        emitWarning(*context, LintWarning::Code_FormatString, pat->location, "Invalid match pattern: %s", error);
        }
        else if (name == "gsub" && args.size > 1)
        {
            int captures = -1;

            if (AstExprConstantString* pat = args.data[0]->as<AstExprConstantString>())
                if (const char* error = checkStringMatch(pat->value.data, pat->value.size, &captures))
                    emitWarning(*context, LintWarning::Code_FormatString, pat->location, "Invalid match pattern: %s", error);

            if (AstExprConstantString* rep = args.data[1]->as<AstExprConstantString>())
                if (const char* error = checkStringReplace(rep->value.data, rep->value.size, captures))
                    emitWarning(*context, LintWarning::Code_FormatString, rep->location, "Invalid match replacement: %s", error);
        }
    }

    void matchCall(AstExprCall* node)
    {
        AstExprIndexName* func = node->func->as<AstExprIndexName>();
        if (!func)
            return;

        if (node->self)
        {
            AstExprGroup* group = func->expr->as<AstExprGroup>();
            AstExpr* self = group ? group->expr : func->expr;

            if (self->is<AstExprConstantString>())
                matchStringCall(func->index, self, node->args);
            else if (std::optional<TypeId> type = context->getType(self))
                if (isString(*type))
                    matchStringCall(func->index, self, node->args);
            return;
        }

        AstExprGlobal* lib = func->expr->as<AstExprGlobal>();
        if (!lib)
            return;

        if (lib->name == "string")
        {
            if (node->args.size > 0)
            {
                AstArray<AstExpr*> rest = {node->args.data + 1, node->args.size - 1};

                matchStringCall(func->index, node->args.data[0], rest);
            }
        }
        else if (lib->name == "os")
        {
            if (func->index == "date" && node->args.size > 0)
            {
                if (AstExprConstantString* fmt = node->args.data[0]->as<AstExprConstantString>())
                    if (const char* error = checkDateFormat(fmt->value.data, fmt->value.size))
                        emitWarning(*context, LintWarning::Code_FormatString, fmt->location, "Invalid date format: %s", error);
            }
        }
    }

    bool visit(AstExprCall* node) override
    {
        matchCall(node);
        return true;
    }
};

class LintTableLiteral : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintTableLiteral pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    bool visit(AstExprTable* node) override
    {
        int count = 0;

        for (const AstExprTable::Item& item : node->items)
            if (item.kind == AstExprTable::Item::Kind::List)
                count++;

        DenseHashMap<AstArray<char>*, int, AstArrayPredicate, AstArrayPredicate> names(nullptr);
        DenseHashMap<int, int> indices(-1);

        for (const AstExprTable::Item& item : node->items)
        {
            if (!item.key)
                continue;

            if (AstExprConstantString* expr = item.key->as<AstExprConstantString>())
            {
                int& line = names[&expr->value];

                if (line)
                    emitWarning(
                        *context,
                        LintWarning::Code_TableLiteral,
                        expr->location,
                        "Table field '%.*s' is a duplicate; previously defined at line %d",
                        int(expr->value.size),
                        expr->value.data,
                        line
                    );
                else
                    line = expr->location.begin.line + 1;
            }
            else if (AstExprConstantNumber* expr = item.key->as<AstExprConstantNumber>())
            {
                if (expr->value >= 1 && expr->value <= double(count) && double(int(expr->value)) == expr->value)
                    emitWarning(
                        *context,
                        LintWarning::Code_TableLiteral,
                        expr->location,
                        "Table index %d is a duplicate; previously defined as a list entry",
                        int(expr->value)
                    );
                else if (expr->value >= 0 && expr->value <= double(INT_MAX) && double(int(expr->value)) == expr->value)
                {
                    int& line = indices[int(expr->value)];

                    if (line)
                        emitWarning(
                            *context,
                            LintWarning::Code_TableLiteral,
                            expr->location,
                            "Table index %d is a duplicate; previously defined at line %d",
                            int(expr->value),
                            line
                        );
                    else
                        line = expr->location.begin.line + 1;
                }
            }
        }

        return true;
    }

    bool visit(AstType*) override
    {
        return true;
    }

    bool visit(AstTypePack* node) override
    {
        return true;
    }

    bool visit(AstTypeTable* node) override
    {
        struct Rec
        {
            AstTableAccess access;
            Location location;
        };

        if (context->module->checkedInNewSolver)
        {
            DenseHashMap<AstName, Rec> names(AstName{});

            for (const AstTableProp& item : node->props)
            {
                Rec* rec = names.find(item.name);
                if (!rec)
                {
                    names[item.name] = Rec{item.access, item.location};
                    continue;
                }

                if (int(rec->access) & int(item.access))
                {
                    if (rec->access == item.access)
                        emitWarning(
                            *context,
                            LintWarning::Code_TableLiteral,
                            item.location,
                            "Table type field '%s' is a duplicate; previously defined at line %d",
                            item.name.value,
                            rec->location.begin.line + 1
                        );
                    else if (rec->access == AstTableAccess::ReadWrite)
                        emitWarning(
                            *context,
                            LintWarning::Code_TableLiteral,
                            item.location,
                            "Table type field '%s' is already read-write; previously defined at line %d",
                            item.name.value,
                            rec->location.begin.line + 1
                        );
                    else if (rec->access == AstTableAccess::Read)
                        emitWarning(
                            *context,
                            LintWarning::Code_TableLiteral,
                            rec->location,
                            "Table type field '%s' already has a read type defined at line %d",
                            item.name.value,
                            rec->location.begin.line + 1
                        );
                    else if (rec->access == AstTableAccess::Write)
                        emitWarning(
                            *context,
                            LintWarning::Code_TableLiteral,
                            rec->location,
                            "Table type field '%s' already has a write type defined at line %d",
                            item.name.value,
                            rec->location.begin.line + 1
                        );
                    else
                        LUAU_ASSERT(!"Unreachable");
                }
                else
                    rec->access = AstTableAccess(int(rec->access) | int(item.access));
            }

            return true;
        }

        DenseHashMap<AstName, int> names(AstName{});

        for (const AstTableProp& item : node->props)
        {
            int& line = names[item.name];

            if (line)
                emitWarning(
                    *context,
                    LintWarning::Code_TableLiteral,
                    item.location,
                    "Table type field '%s' is a duplicate; previously defined at line %d",
                    item.name.value,
                    line
                );
            else
                line = item.location.begin.line + 1;
        }

        return true;
    }

    struct AstArrayPredicate
    {
        size_t operator()(const AstArray<char>* value) const
        {
            return hashRange(value->data, value->size);
        }

        bool operator()(const AstArray<char>* lhs, const AstArray<char>* rhs) const
        {
            return (lhs && rhs) ? lhs->size == rhs->size && memcmp(lhs->data, rhs->data, lhs->size) == 0 : lhs == rhs;
        }
    };
};

class LintUninitializedLocal : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintUninitializedLocal pass;
        pass.context = &context;

        context.root->visit(&pass);

        pass.report();
    }

private:
    struct Local
    {
        bool defined;
        bool initialized;
        bool assigned;
        AstExprLocal* firstUse;
    };

    LintContext* context;
    DenseHashMap<AstLocal*, Local> locals;

    LintUninitializedLocal()
        : locals(NULL)
    {
    }

    void report()
    {
        for (auto& lp : locals)
        {
            AstLocal* local = lp.first;
            const Local& l = lp.second;

            if (l.defined && !l.initialized && !l.assigned && l.firstUse)
            {
                emitWarning(
                    *context,
                    LintWarning::Code_UninitializedLocal,
                    l.firstUse->location,
                    "Variable '%s' defined at line %d is never initialized or assigned; initialize with 'nil' to silence",
                    local->name.value,
                    local->location.begin.line + 1
                );
            }
        }
    }

    bool visit(AstStatLocal* node) override
    {
        AstExpr* last = node->values.size ? node->values.data[node->values.size - 1] : nullptr;
        bool vararg = last && (last->is<AstExprVarargs>() || last->is<AstExprCall>());

        for (size_t i = 0; i < node->vars.size; ++i)
        {
            Local& l = locals[node->vars.data[i]];

            l.defined = true;
            l.initialized = vararg || i < node->values.size;
        }

        return true;
    }

    bool visit(AstStatAssign* node) override
    {
        for (size_t i = 0; i < node->vars.size; ++i)
            visitAssign(node->vars.data[i]);

        for (size_t i = 0; i < node->values.size; ++i)
            node->values.data[i]->visit(this);

        return false;
    }

    bool visit(AstStatFunction* node) override
    {
        visitAssign(node->name);
        node->func->visit(this);

        return false;
    }

    bool visit(AstExprLocal* node) override
    {
        Local& l = locals[node->local];

        if (!l.firstUse)
            l.firstUse = node;

        return false;
    }

    void visitAssign(AstExpr* var)
    {
        if (AstExprLocal* lv = var->as<AstExprLocal>())
        {
            Local& l = locals[lv->local];

            l.assigned = true;
        }
        else
        {
            var->visit(this);
        }
    }
};

class LintDuplicateFunction : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintDuplicateFunction pass{&context};
        context.root->visit(&pass);
    }

private:
    LintContext* context;
    DenseHashMap<std::string, Location> defns;

    LintDuplicateFunction(LintContext* context)
        : context(context)
        , defns("")
    {
    }

    bool visit(AstStatBlock* block) override
    {
        defns.clear();

        for (AstStat* stat : block->body)
        {
            if (AstStatFunction* func = stat->as<AstStatFunction>())
                trackFunction(func->name->location, buildName(func->name));
            else if (AstStatLocalFunction* func = stat->as<AstStatLocalFunction>())
                trackFunction(func->name->location, func->name->name.value);
        }

        return true;
    }

    void trackFunction(Location location, const std::string& name)
    {
        if (name.empty())
            return;

        Location& defn = defns[name];

        if (defn.end.line == 0 && defn.end.column == 0)
            defn = location;
        else
            report(name, location, defn);
    }

    std::string buildName(AstExpr* expr)
    {
        if (AstExprLocal* local = expr->as<AstExprLocal>())
            return local->local->name.value;
        else if (AstExprGlobal* global = expr->as<AstExprGlobal>())
            return global->name.value;
        else if (AstExprIndexName* indexName = expr->as<AstExprIndexName>())
        {
            std::string lhs = buildName(indexName->expr);
            if (lhs.empty())
                return lhs;

            lhs += '.';
            lhs += indexName->index.value;
            return lhs;
        }
        else
            return std::string();
    }

    void report(const std::string& name, Location location, Location otherLocation)
    {
        emitWarning(
            *context,
            LintWarning::Code_DuplicateFunction,
            location,
            "Duplicate function definition: '%s' also defined on line %d",
            name.c_str(),
            otherLocation.begin.line + 1
        );
    }
};

class LintDeprecatedApi : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintDeprecatedApi pass{&context};

        // Variables and classes are collected first because a class can be used above the line that
        // declares it. Type names are scoped per block instead; see visit(AstStatBlock*).
        if (context.deprecatedAttribute.value)
        {
            AttributeCollector collector{&pass};
            context.root->visit(&collector);
        }

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    LintDeprecatedApi(LintContext* context)
        : context(context)
    {
    }

    // A deprecation declared by an attribute rather than inferred from a type. `noun` is what the
    // warning calls the thing, so one report path serves variables, classes and types.
    struct AttributeDeprecation
    {
        AstAttr::DeprecatedInfo info;
        const char* noun = "Variable";
    };

    // Keyed by the binding itself, so shadowing is handled for free.
    DenseHashMap<AstLocal*, AttributeDeprecation> deprecatedLocals{nullptr};
    // A class's name is deliberately not pushed as a local (that is what makes classes hoist), so a
    // use of it resolves as a global and has to be matched by name.
    DenseHashMap<const char*, AttributeDeprecation> deprecatedGlobals{nullptr};

    // A type name in scope. A type reference names an alias rather than pointing at it, so type names
    // are resolved against this stack, innermost last: an alias is visible throughout its block, and
    // a generic parameter or a nearer alias with the same name shadows it.
    struct TypeName
    {
        AstName name;
        // Empty for a name that is not deprecated.
        std::optional<AttributeDeprecation> deprecation;
    };
    std::vector<TypeName> typeNames;
    // No type name is tracked at all unless the file deprecates one.
    bool anyDeprecatedTypeName = false;
    // The node whose children visitWithTypeNames is currently visiting; see there.
    AstNode* expandingNode = nullptr;

    // A deprecated class still refers to itself inside its own body, the same way a deprecated
    // function recurses; those uses are not what the warning is for.
    std::vector<AstStatClass*> classScopeStack;

    struct AttributeCollector : AstVisitor
    {
        LintDeprecatedApi* outer;

        explicit AttributeCollector(LintDeprecatedApi* outer)
            : outer(outer)
        {
        }

        bool visit(AstStatLocal* node) override
        {
            if (std::optional<AstAttr::DeprecatedInfo> info = findDeprecatedInfo(node->attributes))
            {
                for (AstLocal* var : node->vars)
                    outer->deprecatedLocals[var] = AttributeDeprecation{*info, "Variable"};
            }

            return true;
        }

        bool visit(AstStatTypeAlias* node) override
        {
            if (findDeprecatedInfo(node->attributes))
                outer->anyDeprecatedTypeName = true;

            return true;
        }

        bool visit(AstStatClass* node) override
        {
            // A class is a value and a type under the same name, and its value use resolves as a
            // global rather than a local. Its type name is scoped like an alias's.
            if (std::optional<AstAttr::DeprecatedInfo> info = findDeprecatedInfo(node->attributes))
            {
                outer->deprecatedGlobals[node->name->name.value] = AttributeDeprecation{*info, "Class"};
                outer->anyDeprecatedTypeName = true;
            }

            return true;
        }
    };

    static std::optional<AttributeDeprecation> declaredDeprecation(const AstArray<AstAttr*>& attributes, const char* noun)
    {
        if (std::optional<AstAttr::DeprecatedInfo> info = findDeprecatedInfo(attributes))
            return AttributeDeprecation{*info, noun};

        return std::nullopt;
    }

    const AttributeDeprecation* lookupTypeName(AstName name) const
    {
        for (auto it = typeNames.rbegin(); it != typeNames.rend(); ++it)
        {
            if (it->name == name)
                return it->deprecation ? &*it->deprecation : nullptr;
        }

        return nullptr;
    }

    // Visits `node`'s children with `generics` (and `self`, a name that must not report inside its
    // own definition) in scope as type names. The AST has no post-visit hook to pop them at, so this
    // visits `node` again itself: that re-entrant visit sees `expandingNode` and lets the default
    // traversal walk the children.
    template<typename Node>
    bool visitWithTypeNames(Node* node, const AstArray<AstGenericType*>& generics, std::optional<AstName> self = std::nullopt)
    {
        const bool nothingToScope = !anyDeprecatedTypeName || (generics.size == 0 && !self);
        if (nothingToScope || node == expandingNode)
            return true;

        size_t mark = typeNames.size();
        if (self)
            typeNames.push_back(TypeName{*self, std::nullopt});
        for (AstGenericType* generic : generics)
            typeNames.push_back(TypeName{generic->name, std::nullopt});

        AstNode* outer = expandingNode;
        expandingNode = node;
        node->visit(this);
        expandingNode = outer;

        typeNames.resize(mark);
        return false;
    }

    // Type aliases and classes are visible throughout the block that declares them, above the
    // declaration included.
    bool visit(AstStatBlock* node) override
    {
        if (!anyDeprecatedTypeName)
            return true;

        size_t mark = typeNames.size();
        for (AstStat* stat : node->body)
        {
            if (AstStatTypeAlias* alias = stat->as<AstStatTypeAlias>())
                typeNames.push_back(TypeName{alias->name, declaredDeprecation(alias->attributes, "Type")});
            else if (AstStatTypeFunction* function = stat->as<AstStatTypeFunction>())
                typeNames.push_back(TypeName{function->name, std::nullopt});
            else if (AstStatClass* cls = stat->as<AstStatClass>())
                typeNames.push_back(TypeName{cls->name->name, declaredDeprecation(cls->attributes, "Class")});
        }

        if (typeNames.size() == mark)
            return true;

        for (AstStat* stat : node->body)
            stat->visit(this);

        typeNames.resize(mark);
        return false;
    }

    bool visit(AstStatTypeAlias* node) override
    {
        return visitWithTypeNames(node, node->generics, node->name);
    }

    bool visit(AstExprFunction* node) override
    {
        return visitWithTypeNames(node, node->generics);
    }

    bool visit(AstTypeFunction* node) override
    {
        return visitWithTypeNames(node, node->generics);
    }

    bool visit(AstStatDeclareFunction* node) override
    {
        return visitWithTypeNames(node, node->generics);
    }

    // Types are not walked by default; a deprecated type can be nested anywhere inside one.
    bool visit(AstType*) override
    {
        return true;
    }

    bool visit(AstTypePack*) override
    {
        return true;
    }

    // A field that is itself `@deprecated` already says it is only kept for compatibility, so the
    // deprecated type it is declared with is not reported again.
    bool visit(AstTypeTable* node) override
    {
        for (const AstTableProp& prop : node->props)
        {
            if (!findDeprecatedInfo(prop.attributes))
                prop.type->visit(this);
        }

        if (node->indexer)
        {
            node->indexer->indexType->visit(this);
            if (!findDeprecatedInfo(node->indexer->attributes))
                node->indexer->resultType->visit(this);
        }

        return false;
    }

    bool visit(AstExprLocal* node) override
    {
        if (const AttributeDeprecation* deprecation = deprecatedLocals.find(node->local))
            reportAttributeDeprecation(node->location, deprecation->noun, node->local->name.value, deprecation->info);

        const FunctionType* fty = getFunctionType(node);
        bool shouldReport = fty && fty->isDeprecatedFunction && !inScope(fty);

        if (shouldReport)
        {
            if (fty->deprecatedInfo != nullptr)
            {
                report(node->location, node->local->name.value, *fty->deprecatedInfo);
            }
            else
            {
                report(node->location, node->local->name.value);
            }
        }

        return true;
    }

    bool visit(AstExprGlobal* node) override
    {
        if (const AttributeDeprecation* deprecation = deprecatedGlobals.find(node->name.value);
            deprecation && !inClassScopeNamed(node->name.value))
            reportAttributeDeprecation(node->location, deprecation->noun, node->name.value, deprecation->info);

        const FunctionType* fty = getFunctionType(node);
        bool shouldReport = fty && fty->isDeprecatedFunction && !inScope(fty);

        if (shouldReport)
        {
            if (fty->deprecatedInfo != nullptr)
            {
                report(node->location, node->name.value, *fty->deprecatedInfo);
            }
            else
            {
                report(node->location, node->name.value);
            }
        }

        return true;
    }

    bool visit(AstTypeReference* node) override
    {
        // A prefixed reference names another module's type; the attribute there is that module's.
        if (!node->prefix)
        {
            if (const AttributeDeprecation* deprecation = lookupTypeName(node->name); deprecation && !inClassScopeNamed(node->name.value))
                reportAttributeDeprecation(node->location, deprecation->noun, node->name.value, deprecation->info);
        }

        return true;
    }

    bool visit(AstExprTable* node) override
    {
        for (const AstExprTable::Item& item : node->items)
        {
            if (item.key)
                item.key->visit(this);

            // An attribute on the entry replaces whatever the value carries, so binding an
            // already-deprecated value into a deprecated entry is not reported. Only a bare
            // reference is skipped; anything larger is still walked.
            const bool valueIsBareReference = item.value->is<AstExprLocal>() || item.value->is<AstExprGlobal>();
            if (valueIsBareReference && findDeprecatedInfo(item.attributes))
                continue;

            item.value->visit(this);
        }

        return false;
    }

    bool visit(AstStatClass* node) override
    {
        classScopeStack.push_back(node);

        size_t mark = typeNames.size();
        if (anyDeprecatedTypeName)
        {
            for (AstGenericType* generic : node->generics)
                typeNames.push_back(TypeName{generic->name, std::nullopt});
        }

        // A deprecated field's own type is not reported, as in a table type.
        auto visitField = [&](const AstArray<AstAttr*>& attributes, AstType* type, AstExpr* defaultValue)
        {
            if (type && !findDeprecatedInfo(attributes))
                type->visit(this);
            if (defaultValue)
                defaultValue->visit(this);
        };

        if (const AstClassPrimaryConstructor* primaryConstructor = node->primaryConstructor)
        {
            for (size_t i = 0; i < primaryConstructor->args.size; ++i)
            {
                AstLocal* arg = primaryConstructor->args.data[i];
                AstExpr* defaultValue = i < primaryConstructor->argsDefaults.size ? primaryConstructor->argsDefaults.data[i] : nullptr;
                visitField(arg->attributes, arg->annotation, defaultValue);
            }
        }

        for (const AstClassMember& member : node->members)
        {
            if (const AstClassMethod* method = member.get_if<AstClassMethod>())
                check(method->function);
            else if (const AstClassProperty* prop = member.get_if<AstClassProperty>())
                visitField(prop->attributes, prop->ty, prop->defaultValue);
        }

        typeNames.resize(mark);
        classScopeStack.pop_back();
        return false;
    }

    // A class that still uses its own deprecated member internally is the normal state of affairs
    // during a deprecation, the same way a deprecated function's recursive calls are.
    bool inClassScopeNamed(const char* name) const
    {
        if (!name)
            return false;

        for (AstStatClass* cls : classScopeStack)
        {
            if (strcmp(cls->name->name.value, name) == 0)
                return true;
        }

        return false;
    }

    // Whether `object` is the instance type of a class whose body is being visited. Matched by where
    // the type was declared, so another type that happens to share the class's name is still reported.
    bool inOwnClass(const ExternType* object) const
    {
        const bool declaredHere = !context->module || object->definitionModuleName == context->module->name;
        if (!declaredHere || !object->definitionLocation)
            return false;

        for (AstStatClass* cls : classScopeStack)
        {
            if (cls->location == *object->definitionLocation)
                return true;
        }

        return false;
    }

    bool visit(AstStatLocalFunction* node) override
    {
        check(node->func);
        return false;
    }

    bool visit(AstStatFunction* node) override
    {
        check(node->func);
        return false;
    }

    bool visit(AstExprIndexName* node) override
    {
        if (std::optional<TypeId> ty = context->getType(node->expr))
            check(node, follow(*ty));
        else if (AstExprGlobal* global = node->expr->as<AstExprGlobal>())
            check(node->location, global->name, node->index);

        return true;
    }

    bool visit(AstExprCall* node) override
    {
        // getfenv/setfenv are deprecated, however they are still used in some test frameworks and don't have a great general replacement
        // for now we warn about the deprecation only when they are used with a numeric first argument; this produces fewer warnings and makes use
        // of getfenv/setfenv a little more localized
        if (!node->self && node->args.size >= 1)
        {
            if (AstExprGlobal* fenv = node->func->as<AstExprGlobal>(); fenv && (fenv->name == "getfenv" || fenv->name == "setfenv"))
            {
                AstExpr* level = node->args.data[0];
                std::optional<TypeId> ty = context->getType(level);

                if ((ty && isNumber(*ty)) || level->is<AstExprConstantNumber>())
                {
                    // some common uses of getfenv(n) can be replaced by debug.info if the goal is to get the caller's identity
                    const char* suggestion = (fenv->name == "getfenv") ? "; consider using 'debug.info' instead" : "";

                    emitWarning(
                        *context, LintWarning::Code_DeprecatedApi, node->location, "Function '%s' is deprecated%s", fenv->name.value, suggestion
                    );
                }
            }
        }

        return true;
    }

    void check(AstExprIndexName* node, TypeId ty)
    {
        if (const ExternType* cty = get<ExternType>(ty))
        {
            if (const Property* prop = lookupExternTypeProp(cty, node->index.value))
            {
                if (prop->deprecated)
                {
                    if (!inOwnClass(cty))
                        report(node->location, *prop, cty->name.c_str(), node->index.value);
                }
                else if (std::optional<TypeId> ty = prop->readTy)
                {
                    const FunctionType* fty = get<FunctionType>(follow(ty));
                    bool shouldReport = fty && fty->isDeprecatedFunction && !inScope(fty);

                    if (shouldReport)
                    {
                        const char* className = nullptr;
                        if (AstExprGlobal* global = node->expr->as<AstExprGlobal>())
                            className = global->name.value;

                        const char* functionName = node->index.value;
                        if (fty->deprecatedInfo != nullptr)
                        {
                            report(node->location, className, functionName, *fty->deprecatedInfo);
                        }
                        else
                        {
                            report(node->location, className, functionName);
                        }
                    }
                }
            }
        }
        else if (const TableType* tty = get<TableType>(ty))
        {
            auto prop = tty->props.find(node->index.value);

            if (prop != tty->props.end())
            {
                if (prop->second.deprecated)
                {
                    // strip synthetic typeof() for builtin tables
                    if (tty->name && tty->name->compare(0, 7, "typeof(") == 0 && tty->name->back() == ')')
                        report(node->location, prop->second, tty->name->substr(7, tty->name->length() - 8).c_str(), node->index.value);
                    else
                        report(node->location, prop->second, tty->name ? tty->name->c_str() : nullptr, node->index.value);
                }
                else
                {
                    if (std::optional<TypeId> ty = prop->second.readTy)
                    {
                        const FunctionType* fty = get<FunctionType>(follow(ty));
                        bool shouldReport = fty && fty->isDeprecatedFunction && !inScope(fty);

                        if (shouldReport)
                        {
                            const char* className = nullptr;
                            if (AstExprGlobal* global = node->expr->as<AstExprGlobal>())
                                className = global->name.value;

                            const char* functionName = node->index.value;

                            if (fty->deprecatedInfo != nullptr)
                            {
                                report(node->location, className, functionName, *fty->deprecatedInfo);
                            }
                            else
                            {
                                report(node->location, className, functionName);
                            }
                        }
                    }
                }
            }
        }
    }

    void check(const Location& location, AstName global, AstName index)
    {
        if (const LintContext::Global* gv = context->builtinGlobals.find(global))
        {
            if (const TableType* tty = get<TableType>(gv->type))
            {
                auto prop = tty->props.find(index.value);

                if (prop != tty->props.end() && prop->second.deprecated)
                    report(location, prop->second, global.value, index.value);
            }
        }
    }

    void check(AstExprFunction* func)
    {
        LUAU_ASSERT(func);

        const FunctionType* fty = getFunctionType(func);
        bool isDeprecated = fty && fty->isDeprecatedFunction;
        // If a function is deprecated, we don't want to flag its recursive uses.
        // So we push it on a stack while its body is being analyzed.
        // When a deprecated function is used, we check the stack to ensure that we are not inside that function.
        if (isDeprecated)
            pushScope(fty);

        func->visit(this);

        if (isDeprecated)
            popScope(fty);
    }

    void report(const Location& location, const Property& prop, const char* container, const char* field)
    {
        std::string suggestion = prop.deprecatedSuggestion.empty() ? "" : format(", use '%s' instead", prop.deprecatedSuggestion.c_str());

        if (container)
            emitWarning(*context, LintWarning::Code_DeprecatedApi, location, "Member '%s.%s' is deprecated%s", container, field, suggestion.c_str());
        else
            emitWarning(*context, LintWarning::Code_DeprecatedApi, location, "Member '%s' is deprecated%s", field, suggestion.c_str());
    }

    void report(const Location& location, const char* tableName, const char* functionName)
    {
        if (tableName)
            emitWarning(*context, LintWarning::Code_DeprecatedApi, location, "Member '%s.%s' is deprecated", tableName, functionName);
        else
            emitWarning(*context, LintWarning::Code_DeprecatedApi, location, "Member '%s' is deprecated", functionName);
    }

    void report(const Location& location, const char* tableName, const char* functionName, const AstAttr::DeprecatedInfo& info)
    {
        std::string usePart = info.use ? format(", use '%s' instead", info.use->c_str()) : "";
        std::string reasonPart = info.reason ? format(". %s", info.reason->c_str()) : "";
        if (tableName)
            emitWarning(
                *context,
                LintWarning::Code_DeprecatedApi,
                location,
                "Member '%s.%s' is deprecated%s%s",
                tableName,
                functionName,
                usePart.c_str(),
                reasonPart.c_str()
            );
        else
            emitWarning(
                *context,
                LintWarning::Code_DeprecatedApi,
                location,
                "Member '%s' is deprecated%s%s",
                functionName,
                usePart.c_str(),
                reasonPart.c_str()
            );
    }

    void report(const Location& location, const char* functionName)
    {
        emitWarning(*context, LintWarning::Code_DeprecatedApi, location, "Function '%s' is deprecated", functionName);
    }

    void reportAttributeDeprecation(const Location& location, const char* noun, const char* name, const AstAttr::DeprecatedInfo& info)
    {
        std::string usePart = info.use ? format(", use '%s' instead", info.use->c_str()) : "";
        std::string reasonPart = info.reason ? format(". %s", info.reason->c_str()) : "";
        emitWarning(
            *context, LintWarning::Code_DeprecatedApi, location, "%s '%s' is deprecated%s%s", noun, name, usePart.c_str(), reasonPart.c_str()
        );
    }

    void report(const Location& location, const char* functionName, const AstAttr::DeprecatedInfo& info)
    {
        std::string usePart = info.use ? format(", use '%s' instead", info.use->c_str()) : "";
        std::string reasonPart = info.reason ? format(". %s", info.reason->c_str()) : "";
        emitWarning(
            *context, LintWarning::Code_DeprecatedApi, location, "Function '%s' is deprecated%s%s", functionName, usePart.c_str(), reasonPart.c_str()
        );
    }

    std::vector<const FunctionType*> functionTypeScopeStack;

    void pushScope(const FunctionType* fty)
    {
        LUAU_ASSERT(fty);

        functionTypeScopeStack.push_back(fty);
    }

    void popScope(const FunctionType* fty)
    {
        LUAU_ASSERT(fty);

        LUAU_ASSERT(fty == functionTypeScopeStack.back());
        functionTypeScopeStack.pop_back();
    }

    bool inScope(const FunctionType* fty) const
    {
        LUAU_ASSERT(fty);

        return std::find(functionTypeScopeStack.begin(), functionTypeScopeStack.end(), fty) != functionTypeScopeStack.end();
    }

    const FunctionType* getFunctionType(AstExpr* node)
    {
        std::optional<TypeId> ty = context->getType(node);
        if (!ty)
            return nullptr;

        const FunctionType* fty = get<FunctionType>(follow(ty));

        return fty;
    }
};

class LintTableOperations : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        if (!context.module)
            return;

        LintTableOperations pass{&context};
        context.root->visit(&pass);
    }

private:
    LintContext* context;
    DenseHashSet<AstLocal*> tableFindResultLocals;

    LintTableOperations(LintContext* context)
        : context(context)
    {
    }

    static bool isTableFindCall(AstExpr* expr)
    {
        if (!expr)
            return false;

        if (AstExprTypeAssertion* typeAssertion = expr->as<AstExprTypeAssertion>())
            return isTableFindCall(typeAssertion->expr);

        if (AstExprGroup* group = expr->as<AstExprGroup>())
            return isTableFindCall(group->expr);

        if (AstExprCall* call = expr->as<AstExprCall>())
        {
            if (AstExprIndexName* indexName = call->func->as<AstExprIndexName>())
            {
                if (indexName->index == "find")
                {
                    if (AstExprGlobal* global = indexName->expr->as<AstExprGlobal>())
                        return global->name == "table";
                }
            }
        }

        return false;
    }

    bool isTableFindResult(AstExpr* expr)
    {
        if (!expr)
            return false;

        if (AstExprTypeAssertion* typeAssertion = expr->as<AstExprTypeAssertion>())
            return isTableFindResult(typeAssertion->expr);

        if (AstExprGroup* group = expr->as<AstExprGroup>())
            return isTableFindResult(group->expr);

        if (isTableFindCall(expr))
            return true;

        if (AstExprLocal* local = expr->as<AstExprLocal>())
            return tableFindResultLocals.contains(local->local);

        return false;
    }

    bool visit(AstStatLocal* node) override
    {
        for (size_t i = 0; i < node->vars.size && i < node->values.size; ++i)
        {
            if (isTableFindCall(node->values.data[i]))
                tableFindResultLocals.insert(node->vars.data[i]);
        }

        return true;
    }

    bool visit(AstStatAssign* node) override
    {
        for (size_t i = 0; i < node->vars.size && i < node->values.size; ++i)
        {
            if (AstExprLocal* local = node->vars.data[i]->as<AstExprLocal>(); local && isTableFindCall(node->values.data[i]))
                tableFindResultLocals.insert(local->local);
        }

        return true;
    }

    bool visit(AstExprUnary* node) override
    {
        if (node->op == AstExprUnary::Op::Len)
            checkIndexer(node, node->expr, "#");

        return true;
    }

    bool visit(AstExprCall* node) override
    {
        if (AstExprGlobal* func = node->func->as<AstExprGlobal>())
        {
            if (func->name == "ipairs" && node->args.size == 1)
                checkIndexer(node, node->args.data[0], "ipairs");
        }
        else if (AstExprIndexName* func = node->func->as<AstExprIndexName>())
        {
            if (AstExprGlobal* tablib = func->expr->as<AstExprGlobal>(); tablib && tablib->name == "table")
                checkTableCall(node, func);
        }

        return true;
    }

    void checkIndexer(AstExpr* node, AstExpr* expr, const char* op)
    {
        std::optional<Luau::TypeId> ty = context->getType(expr);
        if (!ty)
            return;

        const TableType* tty = get<TableType>(follow(*ty));
        if (!tty)
            return;

        if (!tty->indexer && !tty->props.empty() && tty->state != TableState::Generic)
            emitWarning(
                *context, LintWarning::Code_TableOperations, node->location, "Using '%s' on a table without an array part is likely a bug", op
            );
        else if (tty->indexer && isString(tty->indexer->indexType)) // note: to avoid complexity of subtype tests we just check if the key is a string
            emitWarning(*context, LintWarning::Code_TableOperations, node->location, "Using '%s' on a table with string keys is likely a bug", op);
    }

    void checkTableCall(AstExprCall* node, AstExprIndexName* func)
    {
        AstExpr** args = node->args.data;

        if (func->index == "insert" && node->args.size == 2)
        {
            if (AstExprCall* tail = args[1]->as<AstExprCall>())
            {
                if (std::optional<TypeId> funty = context->getType(tail->func))
                {
                    size_t ret = getReturnCount(follow(*funty));

                    if (ret > 1)
                        emitWarning(
                            *context,
                            LintWarning::Code_TableOperations,
                            tail->location,
                            "table.insert may change behavior if the call returns more than one result; consider adding parentheses around second "
                            "argument"
                        );
                }
            }
        }

        if (func->index == "insert" && node->args.size >= 3)
        {
            // table.insert(t, 0, ?)
            if (isConstant(args[1], 0.0))
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    args[1]->location,
                    "table.insert uses index 0 but arrays are 1-based; did you mean 1 instead?"
                );

            // table.insert(t, #t, ?)
            if (isLength(args[1], args[0]))
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    args[1]->location,
                    "table.insert will insert the value before the last element, which is likely a bug; consider removing the second argument or "
                    "wrap it in parentheses to silence"
                );

            // table.insert(t, #t+1, ?)
            if (AstExprBinary* add = args[1]->as<AstExprBinary>();
                add && add->op == AstExprBinary::Add && isLength(add->left, args[0]) && isConstant(add->right, 1.0))
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    args[1]->location,
                    "table.insert will append the value to the table; consider removing the second argument for efficiency"
                );
        }

        if (func->index == "remove" && node->args.size >= 2)
        {
            // table.remove(t, 0)
            if (isConstant(args[1], 0.0))
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    args[1]->location,
                    "table.remove uses index 0 but arrays are 1-based; did you mean 1 instead?"
                );

            // note: it's tempting to check for table.remove(t, #t), which is equivalent to table.remove(t), but it's correct, occurs frequently,
            // and also reads better.

            // table.remove(t, #t-1)
            if (AstExprBinary* sub = args[1]->as<AstExprBinary>();
                sub && sub->op == AstExprBinary::Sub && isLength(sub->left, args[0]) && isConstant(sub->right, 1.0))
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    args[1]->location,
                    "table.remove will remove the value before the last element, which is likely a bug; consider removing the second argument or "
                    "wrap it in parentheses to silence"
                );

            // table.remove(t, <optional number expression>) -- common footgun when passing table.find
            if (FFlag::LuwuTableRemoveFootgunLint && !args[1]->is<AstExprConstantNil>())
            {
                bool warnForOptionalNumber = isTableFindResult(args[1]);

                if (!warnForOptionalNumber)
                {
                    if (std::optional<TypeId> ty = context->getType(args[1]))
                    {
                        TypeId t = follow(*ty);

                        bool hasNumber = false;
                        if (isNumber(t))
                            hasNumber = true;
                        else if (const UnionType* ut = get<UnionType>(t))
                        {
                            for (TypeId part : ut->options)
                            {
                                if (isNumber(part))
                                {
                                    hasNumber = true;
                                    break;
                                }
                            }
                        }

                        warnForOptionalNumber = isOptional(t) && hasNumber;
                    }
                }

                if (warnForOptionalNumber)
                {
                    emitWarning(
                        *context,
                        LintWarning::Code_TableOperations,
                        args[1]->location,
                        "If this is `nil`, `table.remove` will remove the last element of the array.\nConsider using `table.drop` instead. If order is not important, use a key/value table for better performance."
                    );
                }
            }
        }

        if (func->index == "move" && node->args.size >= 4)
        {
            // table.move(t, 0, _, _)
            if (isConstant(args[1], 0.0))
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    args[1]->location,
                    "table.move uses index 0 but arrays are 1-based; did you mean 1 instead?"
                );

            // table.move(t, _, _, 0)
            else if (isConstant(args[3], 0.0))
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    args[3]->location,
                    "table.move uses index 0 but arrays are 1-based; did you mean 1 instead?"
                );
        }

        if (func->index == "create" && node->args.size == 2)
        {
            // table.create(n, {...})
            if (args[1]->is<AstExprTable>())
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    args[1]->location,
                    "table.create with a table literal will reuse the same object for all elements; consider using a for loop instead"
                );

            // table.create(n, {...} :: ?)
            if (AstExprTypeAssertion* as = args[1]->as<AstExprTypeAssertion>(); as && as->expr->is<AstExprTable>())
                emitWarning(
                    *context,
                    LintWarning::Code_TableOperations,
                    as->expr->location,
                    "table.create with a table literal will reuse the same object for all elements; consider using a for loop instead"
                );
        }
    }

    bool isConstant(AstExpr* expr, double value)
    {
        AstExprConstantNumber* n = expr->as<AstExprConstantNumber>();
        return n && n->value == value;
    }

    bool isLength(AstExpr* expr, AstExpr* table)
    {
        AstExprUnary* n = expr->as<AstExprUnary>();
        return n && n->op == AstExprUnary::Op::Len && similar(n->expr, table);
    }

    size_t getReturnCount(TypeId ty)
    {
        if (auto ftv = get<FunctionType>(ty))
            return size(ftv->retTypes);

        if (auto itv = get<IntersectionType>(ty))
        {
            // We don't process the type recursively to avoid having to deal with self-recursive intersection types
            size_t result = 0;

            for (TypeId part : itv->parts)
                if (auto ftv = get<FunctionType>(follow(part)))
                    result = std::max(result, size(ftv->retTypes));

            return result;
        }

        return 0;
    }
};

class LintDuplicateCondition : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintDuplicateCondition pass{&context};
        context.root->visit(&pass);
    }

private:
    LintContext* context;

    LintDuplicateCondition(LintContext* context)
        : context(context)
    {
    }

    bool visit(AstStatIf* stat) override
    {
        if (!stat->elsebody)
            return true;

        if (!stat->elsebody->is<AstStatIf>())
            return true;

        // if..elseif chain detected, we need to unroll it
        std::vector<AstExpr*> conditions;
        conditions.reserve(2);

        AstStatIf* head = stat;
        while (head)
        {
            head->visitCondition(this);
            head->thenbody->visit(this);

            // Luwu If Local (rfcs/if-local.md): a `when` chain binds new locals, so it never repeats another condition
            if (head->clauses.size == 0)
                conditions.push_back(head->condition);

            if (head->elsebody && head->elsebody->is<AstStatIf>())
            {
                head = head->elsebody->as<AstStatIf>();
                continue;
            }

            if (head->elsebody)
                head->elsebody->visit(this);

            break;
        }

        detectDuplicates(conditions);

        // block recursive visits so that we only analyze each chain once
        return false;
    }

    bool visit(AstExprIfElse* expr) override
    {
        if (!expr->falseExpr->is<AstExprIfElse>())
            return true;

        // if..elseif chain detected, we need to unroll it
        std::vector<AstExpr*> conditions;
        conditions.reserve(2);

        AstExprIfElse* head = expr;
        while (head)
        {
            head->visitCondition(this);
            head->trueExpr->visit(this);

            // Luwu If Local (rfcs/if-local.md): a `when` chain binds new locals, so it never repeats another condition
            if (head->clauses.size == 0)
                conditions.push_back(head->condition);

            if (head->falseExpr->is<AstExprIfElse>())
            {
                head = head->falseExpr->as<AstExprIfElse>();
                continue;
            }

            head->falseExpr->visit(this);
            break;
        }

        detectDuplicates(conditions);

        // block recursive visits so that we only analyze each chain once
        return false;
    }

    bool visit(AstExprBinary* expr) override
    {
        if (expr->op != AstExprBinary::And && expr->op != AstExprBinary::Or)
            return true;

        // for And expressions, it's idiomatic to use "a and a or b" as a ternary replacement, so we detect this pattern
        if (expr->op == AstExprBinary::Or)
        {
            AstExprBinary* la = expr->left->as<AstExprBinary>();

            if (la && la->op == AstExprBinary::And)
            {
                AstExprBinary* lb = la->left->as<AstExprBinary>();
                AstExprBinary* rb = la->right->as<AstExprBinary>();

                // check that the length of and-chain is exactly 2
                if (!(lb && lb->op == AstExprBinary::And) && !(rb && rb->op == AstExprBinary::And))
                {
                    la->left->visit(this);
                    la->right->visit(this);
                    expr->right->visit(this);
                    return false;
                }
            }
        }

        // unroll condition chain
        std::vector<AstExpr*> conditions;
        conditions.reserve(2);

        extractOpChain(conditions, expr, expr->op);

        detectDuplicates(conditions);

        // block recursive visits so that we only analyze each chain once
        return false;
    }

    void extractOpChain(std::vector<AstExpr*>& conditions, AstExpr* expr, AstExprBinary::Op op)
    {
        if (AstExprBinary* bin = expr->as<AstExprBinary>(); bin && bin->op == op)
        {
            extractOpChain(conditions, bin->left, op);
            extractOpChain(conditions, bin->right, op);
        }
        else if (AstExprGroup* group = expr->as<AstExprGroup>())
        {
            extractOpChain(conditions, group->expr, op);
        }
        else
        {
            conditions.push_back(expr);
        }
    }

    void detectDuplicates(const std::vector<AstExpr*>& conditions)
    {
        // Limit the distance at which we consider duplicates to reduce N^2 complexity to KN
        const size_t kMaxDistance = 5;

        for (size_t i = 0; i < conditions.size(); ++i)
        {
            for (size_t j = std::max(i, kMaxDistance) - kMaxDistance; j < i; ++j)
            {
                if (similar(conditions[j], conditions[i]))
                {
                    if (conditions[i]->location.begin.line == conditions[j]->location.begin.line)
                        emitWarning(
                            *context,
                            LintWarning::Code_DuplicateCondition,
                            conditions[i]->location,
                            "Condition has already been checked on column %d",
                            conditions[j]->location.begin.column + 1
                        );
                    else
                        emitWarning(
                            *context,
                            LintWarning::Code_DuplicateCondition,
                            conditions[i]->location,
                            "Condition has already been checked on line %d",
                            conditions[j]->location.begin.line + 1
                        );
                    break;
                }
            }
        }
    }
};

class LintDuplicateLocal : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintDuplicateLocal pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    DenseHashMap<AstLocal*, AstNode*> locals;

    LintDuplicateLocal()
        : locals(nullptr)
    {
    }

    bool visit(AstStatLocal* node) override
    {
        // early out for performance
        if (node->vars.size == 1)
            return true;

        for (size_t i = 0; i < node->vars.size; ++i)
            locals[node->vars.data[i]] = node;

        for (size_t i = 0; i < node->vars.size; ++i)
        {
            AstLocal* local = node->vars.data[i];

            if (local->shadow && locals[local->shadow] == node && !ignoreDuplicate(local))
            {
                if (local->shadow->location.begin.line == local->location.begin.line)
                    emitWarning(
                        *context,
                        LintWarning::Code_DuplicateLocal,
                        local->location,
                        "Variable '%s' already defined on column %d",
                        local->name.value,
                        local->shadow->location.begin.column + 1
                    );
                else
                    emitWarning(
                        *context,
                        LintWarning::Code_DuplicateLocal,
                        local->location,
                        "Variable '%s' already defined on line %d",
                        local->name.value,
                        local->shadow->location.begin.line + 1
                    );
            }
        }

        return true;
    }

    bool visit(AstExprFunction* node) override
    {
        if (node->self)
            locals[node->self] = node;

        for (size_t i = 0; i < node->args.size; ++i)
            locals[node->args.data[i]] = node;

        for (size_t i = 0; i < node->args.size; ++i)
        {
            AstLocal* local = node->args.data[i];

            if (local->shadow && locals[local->shadow] == node && !ignoreDuplicate(local))
            {
                if (local->shadow == node->self)
                    emitWarning(*context, LintWarning::Code_DuplicateLocal, local->location, "Function parameter 'self' already defined implicitly");
                else if (local->shadow->location.begin.line == local->location.begin.line)
                    emitWarning(
                        *context,
                        LintWarning::Code_DuplicateLocal,
                        local->location,
                        "Function parameter '%s' already defined on column %d",
                        local->name.value,
                        local->shadow->location.begin.column + 1
                    );
                else
                    emitWarning(
                        *context,
                        LintWarning::Code_DuplicateLocal,
                        local->location,
                        "Function parameter '%s' already defined on line %d",
                        local->name.value,
                        local->shadow->location.begin.line + 1
                    );
            }
        }

        return true;
    }

    bool ignoreDuplicate(AstLocal* local)
    {
        return local->name == "_";
    }
};

static AstExpr* unparenthesized(AstExpr* expr);

class LintMisleadingAndOr : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintMisleadingAndOr pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    // Luwu: expressions used as conditions, where `a and b or c` is boolean logic rather than a ternary
    DenseHashSet<AstExpr*> conditions{nullptr};

    void markCondition(AstExpr* expr)
    {
        expr = unparenthesized(expr);
        conditions.insert(expr);

        if (AstExprBinary* binary = expr->as<AstExprBinary>(); binary && (binary->op == AstExprBinary::And || binary->op == AstExprBinary::Or))
        {
            markCondition(binary->left);
            markCondition(binary->right);
        }
        else if (AstExprUnary* unary = expr->as<AstExprUnary>(); unary && unary->op == AstExprUnary::Op::Not)
            markCondition(unary->expr);
    }

    bool visit(AstStatIf* node) override
    {
        markCondition(node->condition);
        return true;
    }

    bool visit(AstStatWhile* node) override
    {
        markCondition(node->condition);
        return true;
    }

    bool visit(AstStatRepeat* node) override
    {
        markCondition(node->condition);
        return true;
    }

    bool visit(AstExprIfElse* node) override
    {
        markCondition(node->condition);
        return true;
    }

    bool visit(AstExprUnary* node) override
    {
        if (node->op == AstExprUnary::Op::Not)
            markCondition(node->expr);
        return true;
    }

    // Luwu: `a and b or c` meant as logic: a condition, or an `or` operand that only makes sense as a boolean. Falling
    // through to `c` when `b` is false is then exactly right, and `if a then b else c` would change what it does.
    bool isBooleanLogic(AstExprBinary* node)
    {
        if (conditions.contains(node))
            return true;

        AstExpr* last = unparenthesized(node->right);
        if (last->is<AstExprConstantBool>())
            return true;
        if (AstExprUnary* unary = last->as<AstExprUnary>())
            return unary->op == AstExprUnary::Op::Not;
        if (AstExprBinary* binary = last->as<AstExprBinary>())
            return binary->op == AstExprBinary::CompareEq || binary->op == AstExprBinary::CompareNe ||
                   binary->op == AstExprBinary::CompareLt || binary->op == AstExprBinary::CompareLe ||
                   binary->op == AstExprBinary::CompareGt || binary->op == AstExprBinary::CompareGe;
        return false;
    }

    bool visit(AstExprBinary* node) override
    {
        if (node->op != AstExprBinary::Or)
            return true;

        AstExprBinary* and_ = node->left->as<AstExprBinary>();
        if (!and_ || and_->op != AstExprBinary::And)
            return true;

        const char* alt = nullptr;

        if (and_->right->is<AstExprConstantNil>())
            alt = "nil";
        // Luwu: `none` is falsy too
        else if (AstExprGlobal* global = and_->right->as<AstExprGlobal>(); global && global->name == "none")
            alt = "none";
        else if (AstExprConstantBool* c = and_->right->as<AstExprConstantBool>(); c && c->value == false)
            alt = "false";

        // Luwu: the same mistake when the first alternative only might be falsy (`c and v or d` with `v: boolean?`)
        // A literal's value is right there, and the checker can widen a `true` to `boolean`
        const char* maybe = nullptr;
        AstExpr* middle = and_->right;
        bool literal = middle->is<AstExprConstantBool>() || middle->is<AstExprConstantNumber>() || middle->is<AstExprConstantString>() ||
                       middle->is<AstExprTable>() || middle->is<AstExprFunction>() || middle->is<AstExprInterpString>();
        bool logic = isBooleanLogic(node);
        if (!alt && !literal && !logic)
        {
            if (std::optional<TypeId> type = context->getType(and_->right))
                maybe = canBeFalsy(*type);
        }

        if (alt)
            emitWarning(
                *context,
                LintWarning::Code_MisleadingAndOr,
                node->location,
                "this 'a and b or c' always evaluates to 'c' because 'b' is %s, use 'if a then b else c' instead",
                alt
            );
        else if (maybe)
            emitWarning(
                *context,
                LintWarning::Code_MisleadingAndOr,
                node->location,
                "an 'a and b or c' expression is misleading when 'b' is falsy, use 'if a then b else c' instead"
            );

        // Luwu: `a and b or c` as a whole, which only means `if a then b else c` while `b` can't be falsy
        if (!logic)
            emitWarning(
                *context,
                LintWarning::Code_LuaAndOr,
                node->location,
                "'a and b or c' gives 'c' whenever 'b' is falsy, not only when 'a' is; use 'if a then b else c'"
            );

        return true;
    }

    // "nil" or "false" when a value of type `ty` can be that, otherwise nullptr. `any` and `unknown` say nothing.
    static const char* canBeFalsy(TypeId ty)
    {
        ty = follow(ty);

        if (const UnionType* options = get<UnionType>(ty))
        {
            for (TypeId option : options)
                if (const char* falsy = canBeFalsy(option))
                    return falsy;
            return nullptr;
        }

        if (const PrimitiveType* primitive = get<PrimitiveType>(ty))
        {
            if (primitive->type == PrimitiveType::NilType)
                return "nil";
            if (primitive->type == PrimitiveType::NoneType)
                return "none";
            if (primitive->type == PrimitiveType::Boolean)
                return "false";
            return nullptr;
        }

        const SingletonType* singleton = get<SingletonType>(ty);
        const BooleanSingleton* boolean = singleton ? get<BooleanSingleton>(singleton) : nullptr;
        if (boolean && !boolean->value)
            return "false";

        return nullptr;
    }
};

class LintIntegerParsing : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintIntegerParsing pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    bool visit(AstExprConstantNumber* node) override
    {
        switch (node->parseResult)
        {
        case ConstantNumberParseResult::Ok:
        case ConstantNumberParseResult::Malformed:
            break;
        case ConstantNumberParseResult::Imprecise:
            emitWarning(
                *context,
                LintWarning::Code_IntegerParsing,
                node->location,
                "Number literal exceeded available precision and was truncated to closest representable number"
            );
            break;
        case ConstantNumberParseResult::BinOverflow:
            emitWarning(
                *context,
                LintWarning::Code_IntegerParsing,
                node->location,
                "Binary number literal exceeded available precision and was truncated to 2^64"
            );
            break;
        case ConstantNumberParseResult::HexOverflow:
            emitWarning(
                *context,
                LintWarning::Code_IntegerParsing,
                node->location,
                "Hexadecimal number literal exceeded available precision and was truncated to 2^64"
            );
            break;
        case ConstantNumberParseResult::IntOverflow:
            emitWarning(*context, LintWarning::Code_IntegerParsing, node->location, "Integer number literal was clamped because it was out of range");
            break;
        }

        return true;
    }
};

// Luwu: KeywordShadow and BuiltinShadow. A binding named after a contextual keyword or a builtin global still works, but
// it turns something off or hides it. Fields, table keys, members and type names aren't bindings and are left alone.
class LintNameShadow : AstVisitor
{
public:
    struct Enabled
    {
        bool keyword = false;
        bool builtin = false;
    };

    LUAU_NOINLINE static void process(LintContext& context, Enabled enabled)
    {
        LintNameShadow pass;
        pass.context = &context;
        pass.enabled = enabled;

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    Enabled enabled;

    // What the keyword `name` is for, or null when `name` isn't one of the linted keywords. `type` is a keyword too, but
    // it is reported as the builtin function it also names.
    static const char* keywordPurpose(AstName name)
    {
        if (name == "continue")
            return "used for skipping an iteration of a loop";
        if (name == "class")
            return "used for declaring classes";
        if (name == "implements")
            return "used for listing the traits a class implements";
        if (name == "declare")
            return "used for declarations";
        if (name == "export")
            return "used for exporting values and types from a module";
        if (name == "const")
            return "used for declaring bindings that can't be reassigned";
        return nullptr;
    }

    void reportKeyword(AstName name, const Location& location, const char* purpose)
    {
        emitWarning(
            *context,
            LintWarning::Code_KeywordShadow,
            location,
            "'%s' is a keyword %s and should not be used as an identifier; naming bindings '%s' will become a hard error in the future",
            name.value,
            purpose,
            name.value
        );
    }

    // The standard globals every Luau and Luwu program has. Globals an embedder declares (seal's `p`, Roblox's `game`)
    // aren't in it: a local named `p` is ordinary code.
    static bool isStandardGlobal(AstName name)
    {
        static const std::unordered_set<std::string_view> kStandardGlobals = {
            "assert", "error", "getfenv", "setfenv", "getmetatable", "setmetatable", "ipairs", "pairs", "next", "pcall",
            "xpcall", "print", "rawequal", "rawget", "rawset", "rawlen", "select", "tonumber", "tostring", "type", "typeof",
            "unpack", "gcinfo", "collectgarbage", "newproxy", "require", "bit32", "buffer", "coroutine", "debug", "math",
            "os", "string", "table", "utf8", "vector", "none", "_G", "_VERSION",
        };
        return kStandardGlobals.count(name.value) != 0;
    }

    void check(AstName name, const Location& location)
    {
        if (const char* purpose = keywordPurpose(name))
        {
            if (enabled.keyword)
                reportKeyword(name, location, purpose);
            return;
        }

        if (!enabled.builtin || !isStandardGlobal(name))
            return;

        emitWarning(*context, LintWarning::Code_BuiltinShadow, location, "'%s' hides the builtin '%s' here", name.value, name.value);
    }

    // Writing a builtin global is BuiltinGlobalWrite's to report; only the keyword check applies to a global
    void checkGlobalTarget(AstExpr* target)
    {
        AstExprGlobal* global = target->as<AstExprGlobal>();
        if (global && enabled.keyword)
        {
            if (const char* purpose = keywordPurpose(global->name))
                reportKeyword(global->name, global->location, purpose);
        }
    }

    bool visit(AstStatLocal* node) override
    {
        for (AstLocal* local : node->vars)
            check(local->name, local->location);
        return true;
    }

    bool visit(AstStatLocalFunction* node) override
    {
        check(node->name->name, node->name->location);
        return true;
    }

    bool visit(AstStatFunction* node) override
    {
        checkGlobalTarget(node->name);
        return true;
    }

    bool visit(AstExprFunction* node) override
    {
        for (AstLocal* arg : node->args)
            check(arg->name, arg->location);
        return true;
    }

    bool visit(AstStatFor* node) override
    {
        check(node->var->name, node->var->location);
        return true;
    }

    bool visit(AstStatForIn* node) override
    {
        for (AstLocal* var : node->vars)
            check(var->name, var->location);
        return true;
    }

    bool visit(AstStatAssign* node) override
    {
        for (AstExpr* target : node->vars)
            checkGlobalTarget(target);
        return true;
    }

    bool visit(AstStatCompoundAssign* node) override
    {
        checkGlobalTarget(node->var);
        return true;
    }
};

// Luwu Do Expressions (rfcs/do-expressions.md): ReturnOnNextLine and OrContinue
class LintDoExpressions : AstVisitor
{
public:
    struct Enabled
    {
        bool returnOnNextLine = false;
        bool orContinue = false;
    };

    LUAU_NOINLINE static void process(LintContext& context, Enabled enabled)
    {
        LintDoExpressions pass;
        pass.context = &context;
        pass.enabled = enabled;

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    Enabled enabled;

    // The `do` expressions around the current position in this function, innermost last; true for one written as
    // `x or return v`
    std::vector<bool> doExprs;
    // Loops around the current position in this function: `or do continue` is only a fix inside one
    int loopDepth = 0;

    bool visitLoopBody(AstStatBlock* body)
    {
        loopDepth++;
        body->visit(this);
        loopDepth--;
        return false;
    }

    bool visit(AstStatWhile* node) override
    {
        node->condition->visit(this);
        return visitLoopBody(node->body);
    }

    bool visit(AstStatRepeat* node) override
    {
        visitLoopBody(node->body);
        node->condition->visit(this);
        return false;
    }

    bool visit(AstStatFor* node) override
    {
        node->from->visit(this);
        node->to->visit(this);
        if (node->step)
            node->step->visit(this);
        return visitLoopBody(node->body);
    }

    bool visit(AstStatForIn* node) override
    {
        for (AstExpr* value : node->values)
            value->visit(this);
        return visitLoopBody(node->body);
    }

    bool visit(AstExprDo* node) override
    {
        doExprs.push_back(node->shorthand);
        node->body->visit(this);
        doExprs.pop_back();

        return false;
    }

    bool visit(AstExprFunction* node) override
    {
        std::vector<bool> outer;
        std::swap(outer, doExprs);
        int outerLoopDepth = loopDepth;
        loopDepth = 0;

        node->body->visit(this);

        std::swap(outer, doExprs);
        loopDepth = outerLoopDepth;

        return false;
    }

    // `return`'s values may start on the next line, as everywhere in Lua. In a `do` expression the `return` is often meant
    // to have no value, and then whatever starts the next line becomes the value to return.
    bool visit(AstStatReturn* node) override
    {
        if (!enabled.returnOnNextLine || doExprs.empty() || node->list.size == 0)
            return true;

        AstExpr* first = node->list.data[0];
        if (first->location.begin.line == node->location.begin.line)
            return true;

        emitWarning(
            *context,
            LintWarning::Code_ReturnOnNextLine,
            first->location,
            "This is the value 'return' returns, since it starts on the line after it; if 'return' should have no value, write %s",
            doExprs.back() ? "'(return)'" : "'(do return)'"
        );

        return true;
    }

    bool visit(AstExprBinary* node) override
    {
        if (!enabled.orContinue || node->op != AstExprBinary::Or || loopDepth == 0)
            return true;

        AstExprGlobal* global = node->right->as<AstExprGlobal>();
        if (global && global->name == "continue")
            emitWarning(
                *context,
                LintWarning::Code_OrContinue,
                global->location,
                "'continue' here reads a global variable named 'continue', since this module uses that name; to continue the loop, write 'or do continue'"
            );

        return true;
    }
};

class LintComparisonPrecedence : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintComparisonPrecedence pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    static bool isEquality(AstExprBinary::Op op)
    {
        return op == AstExprBinary::CompareNe || op == AstExprBinary::CompareEq;
    }

    static bool isComparison(AstExprBinary::Op op)
    {
        return op == AstExprBinary::CompareNe || op == AstExprBinary::CompareEq || op == AstExprBinary::CompareLt || op == AstExprBinary::CompareLe ||
               op == AstExprBinary::CompareGt || op == AstExprBinary::CompareGe;
    }

    static bool isNot(AstExpr* node)
    {
        AstExprUnary* expr = node->as<AstExprUnary>();

        return expr && expr->op == AstExprUnary::Op::Not;
    }

    bool visit(AstExprBinary* node) override
    {
        if (!isComparison(node->op))
            return true;

        // not X == Y; we silence this for not X == not Y as it's likely an intentional boolean comparison
        if (isNot(node->left) && !isNot(node->right))
        {
            std::string op = toString(node->op);

            if (isEquality(node->op))
                emitWarning(
                    *context,
                    LintWarning::Code_ComparisonPrecedence,
                    node->location,
                    "not X %s Y is equivalent to (not X) %s Y; consider using X %s Y, or add parentheses to silence",
                    op.c_str(),
                    op.c_str(),
                    node->op == AstExprBinary::CompareEq ? "~=" : "=="
                );
            else
                emitWarning(
                    *context,
                    LintWarning::Code_ComparisonPrecedence,
                    node->location,
                    "not X %s Y is equivalent to (not X) %s Y; add parentheses to silence",
                    op.c_str(),
                    op.c_str()
                );
        }
        else if (AstExprBinary* left = node->left->as<AstExprBinary>(); left && isComparison(left->op))
        {
            std::string lop = toString(left->op);
            std::string rop = toString(node->op);

            if (isEquality(left->op) || isEquality(node->op))
                emitWarning(
                    *context,
                    LintWarning::Code_ComparisonPrecedence,
                    node->location,
                    "X %s Y %s Z is equivalent to (X %s Y) %s Z; add parentheses to silence",
                    lop.c_str(),
                    rop.c_str(),
                    lop.c_str(),
                    rop.c_str()
                );
            else
                emitWarning(
                    *context,
                    LintWarning::Code_ComparisonPrecedence,
                    node->location,
                    "X %s Y %s Z is equivalent to (X %s Y) %s Z; did you mean X %s Y and Y %s Z?",
                    lop.c_str(),
                    rop.c_str(),
                    lop.c_str(),
                    rop.c_str(),
                    lop.c_str(),
                    rop.c_str()
                );
        }

        return true;
    }
};

static void fillBuiltinGlobals(LintContext& context, const AstNameTable& names, const ScopePtr& env)
{
    ScopePtr current = env;
    while (true)
    {
        for (auto& [global, binding] : current->bindings)
        {
            AstName name = names.get(global.c_str());

            if (name.value)
            {
                auto& g = context.builtinGlobals[name];
                g.type = binding.typeId;
                if (binding.deprecated)
                    g.deprecated = binding.deprecatedSuggestion.c_str();
            }
        }

        if (current->parent)
            current = current->parent;
        else
            break;
    }
}

static const char* fuzzyMatch(std::string_view str, const char* const* array, size_t size)
{
    if (FInt::LuauSuggestionDistance == 0)
        return nullptr;

    size_t bestDistance = FInt::LuauSuggestionDistance;
    size_t bestMatch = size;

    for (size_t i = 0; i < size; ++i)
    {
        size_t ed = editDistance(str, array[i]);

        if (ed <= bestDistance)
        {
            bestDistance = ed;
            bestMatch = i;
        }
    }

    return bestMatch < size ? array[bestMatch] : nullptr;
}

static void lintComments(LintContext& context, const std::vector<HotComment>& hotcomments)
{
    bool seenMode = false;

    for (const HotComment& hc : hotcomments)
    {
        // We reserve --!<space> for various informational (non-directive) comments
        if (hc.content.empty() || hc.content[0] == ' ' || hc.content[0] == '\t')
            continue;

        if (!hc.header)
        {
            emitWarning(
                context,
                LintWarning::Code_CommentDirective,
                hc.location,
                "Comment directive is ignored because it is placed after the first non-comment token"
            );
        }
        else
        {
            size_t space = hc.content.find_first_of(" \t");
            std::string_view first = std::string_view(hc.content).substr(0, space);

            if (first == "nolint")
            {
                size_t notspace = hc.content.find_first_not_of(" \t", space);

                if (space == std::string::npos || notspace == std::string::npos)
                {
                    // disables all lints
                }
                // Luwu: `All` is not a lint, but `--!nolint All` is valid
                else if (!LintWarning::isAllName(hc.content.c_str() + notspace) &&
                         LintWarning::parseName(hc.content.c_str() + notspace) == LintWarning::Code_Unknown)
                {
                    const char* rule = hc.content.c_str() + notspace;

                    // skip Unknown
                    if (const char* suggestion = fuzzyMatch(rule, kWarningNames + 1, LintWarning::Code__Count - 1))
                        emitWarning(
                            context,
                            LintWarning::Code_CommentDirective,
                            hc.location,
                            "nolint directive refers to unknown lint rule '%s'; did you mean '%s'?",
                            rule,
                            suggestion
                        );
                    else
                        emitWarning(
                            context, LintWarning::Code_CommentDirective, hc.location, "nolint directive refers to unknown lint rule '%s'", rule
                        );
                }
            }
            // Luwu: `--!lint Name` turns on a lint that's off by default
            else if (first == "lint")
            {
                size_t notspace = hc.content.find_first_not_of(" \t", space);
                const char* rule = notspace == std::string::npos ? nullptr : hc.content.c_str() + notspace;

                if (space == std::string::npos || !rule)
                    emitWarning(
                        context,
                        LintWarning::Code_CommentDirective,
                        hc.location,
                        "lint directive needs the lint to turn on, like '--!lint ConstLocal'"
                    );
                else if (LintWarning::isAllName(rule))
                    emitWarning(
                        context,
                        LintWarning::Code_CommentDirective,
                        hc.location,
                        "'All' can only turn lints off; name the lint to turn on, like '--!lint ConstLocal'"
                    );
                else if (LintWarning::parseName(rule) == LintWarning::Code_Unknown)
                {
                    if (const char* suggestion = fuzzyMatch(rule, kWarningNames + 1, LintWarning::Code__Count - 1))
                        emitWarning(
                            context,
                            LintWarning::Code_CommentDirective,
                            hc.location,
                            "lint directive refers to unknown lint rule '%s'; did you mean '%s'?",
                            rule,
                            suggestion
                        );
                    else
                        emitWarning(
                            context, LintWarning::Code_CommentDirective, hc.location, "lint directive refers to unknown lint rule '%s'", rule
                        );
                }
            }
            else if (first == "nocheck" || first == "nonstrict" || first == "strict")
            {
                if (space != std::string::npos)
                    emitWarning(
                        context,
                        LintWarning::Code_CommentDirective,
                        hc.location,
                        "Comment directive with the type checking mode has extra symbols at the end of the line"
                    );
                else if (seenMode)
                    emitWarning(
                        context,
                        LintWarning::Code_CommentDirective,
                        hc.location,
                        "Comment directive with the type checking mode has already been used"
                    );
                else
                    seenMode = true;
            }
            else if (first == "optimize")
            {
                size_t notspace = hc.content.find_first_not_of(" \t", space);

                if (space == std::string::npos || notspace == std::string::npos)
                    emitWarning(context, LintWarning::Code_CommentDirective, hc.location, "optimize directive requires an optimization level");
                else
                {
                    const char* level = hc.content.c_str() + notspace;

                    if (strcmp(level, "0") != 0 && strcmp(level, "1") != 0 && strcmp(level, "2") != 0)
                        emitWarning(
                            context,
                            LintWarning::Code_CommentDirective,
                            hc.location,
                            "optimize directive uses unknown optimization level '%s', 0..2 expected",
                            level
                        );
                }
            }
            else if (first == "native")
            {
                if (space != std::string::npos)
                    emitWarning(
                        context, LintWarning::Code_CommentDirective, hc.location, "native directive has extra symbols at the end of the line"
                    );
            }
            else if (first == "trust")
            {
                // Luwu Classes (rfcs/classes): the directive only counts when the embedder allows it (see
                // DebugLuwuCompilerTrustsTypeAnnotations in Compiler.cpp).
                if (space != std::string::npos)
                    emitWarning(
                        context, LintWarning::Code_CommentDirective, hc.location, "trust directive has extra symbols at the end of the line"
                    );
                else if (!FFlag::DebugLuwuCompilerTrustsTypeAnnotations)
                    emitWarning(
                        context,
                        LintWarning::Code_CommentDirective,
                        hc.location,
                        "trust directive has no effect because DebugLuwuCompilerTrustsTypeAnnotations is disabled"
                    );
            }
            else
            {
                static const char* kHotComments[] = {
                    "nolint",
                    "lint",
                    "nocheck",
                    "nonstrict",
                    "strict",
                    "optimize",
                    "native",
                    "trust",
                };

                if (const char* suggestion = fuzzyMatch(first, kHotComments, std::size(kHotComments)))
                    emitWarning(
                        context,
                        LintWarning::Code_CommentDirective,
                        hc.location,
                        "Unknown comment directive '%.*s'; did you mean '%s'?",
                        int(first.size()),
                        first.data(),
                        suggestion
                    );
                else
                    emitWarning(
                        context, LintWarning::Code_CommentDirective, hc.location, "Unknown comment directive '%.*s'", int(first.size()), first.data()
                    );
            }
        }
    }
}

// Whether the module's header has the `--!<name>` directive
static bool hasHeaderCommentDirective(const std::vector<HotComment>& hotcomments, std::string_view name)
{
    for (const HotComment& hc : hotcomments)
    {
        if (hc.content.empty() || hc.content[0] == ' ' || hc.content[0] == '\t')
            continue;

        if (hc.header)
        {
            size_t space = hc.content.find_first_of(" \t");
            std::string_view first = std::string_view(hc.content).substr(0, space);

            if (first == name)
                return true;
        }
    }

    return false;
}

static bool hasNativeCommentDirective(const std::vector<HotComment>& hotcomments)
{
    return hasHeaderCommentDirective(hotcomments, "native");
}

static AstExpr* unparenthesized(AstExpr* expr)
{
    while (AstExprGroup* group = expr->as<AstExprGroup>())
        expr = group->expr;
    return expr;
}

// A short name for a value in a lint message (`x`, `t.value`, `obj:method`), or nullopt when it has none.
static std::optional<std::string> shortExprName(AstExpr* expr)
{
    if (AstExprLocal* local = expr->as<AstExprLocal>())
        return std::string(local->local->name.value);
    if (AstExprGlobal* global = expr->as<AstExprGlobal>())
        return std::string(global->name.value);
    if (AstExprIndexName* index = expr->as<AstExprIndexName>())
    {
        if (std::optional<std::string> base = shortExprName(index->expr))
            return *base + index->op + index->index.value;
    }
    return std::nullopt;
}

// Luwu: `x ~= nil` where `x` can be `none` but never `nil` is always true, and almost certainly meant
// `x ~= none`; likewise `x ~= none` where `x` can be `nil` but never `none`. Both are falsy, so the two
// are easy to mix up, and the comparison silently lets the other one through.
class LintNilNoneComparison : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintNilNoneComparison pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    struct Nullish
    {
        bool nil = false;
        bool none = false;
        // Whether it can also be `false`, which decides whether `if x then` means the same as the fix.
        bool canBeFalse = false;
    };

    // `if`/`while` conditions, and the keyword that introduces each, so the fix can also be offered as
    // a plain truthiness test.
    DenseHashMap<AstExpr*, const char*> conditions{nullptr};

    bool visit(AstStatIf* node) override
    {
        // Luwu If Local (rfcs/if-local.md): a `when` chain's first expression may be a binding's value, not a test
        if (node->clauses.size == 0)
            conditions[unparenthesized(node->condition)] = "if";
        return true;
    }

    bool visit(AstStatWhile* node) override
    {
        conditions[unparenthesized(node->condition)] = "while";
        return true;
    }

    // Which of `nil` and `none` a value of `ty` can be. False when the type can't say (`any`,
    // `unknown`, generics, anything unsolved), since then neither comparison is known to be useless.
    static bool collect(TypeId ty, Nullish& out)
    {
        ty = follow(ty);

        // The iterator flattens nested unions and skips cycles, so no option is itself a union.
        if (const UnionType* u = get<UnionType>(ty))
        {
            for (TypeId option : u)
            {
                if (!collect(option, out))
                    return false;
            }
            return true;
        }

        if (const PrimitiveType* primitive = get<PrimitiveType>(ty))
        {
            if (primitive->type == PrimitiveType::NilType)
                out.nil = true;
            else if (primitive->type == PrimitiveType::NoneType)
                out.none = true;
            else if (primitive->type == PrimitiveType::Boolean)
                out.canBeFalse = true;
            return true;
        }

        if (const SingletonType* singleton = get<SingletonType>(ty))
        {
            if (const BooleanSingleton* b = get<BooleanSingleton>(singleton); b && !b->value)
                out.canBeFalse = true;
            return true;
        }

        return get<TableType>(ty) || get<MetatableType>(ty) || get<FunctionType>(ty) || get<ExternType>(ty);
    }

    static bool isNoneGlobal(AstExpr* expr)
    {
        AstExprGlobal* global = expr->as<AstExprGlobal>();
        return global && global->name == "none";
    }

    bool visit(AstExprBinary* node) override
    {
        if (node->op != AstExprBinary::CompareNe && node->op != AstExprBinary::CompareEq)
            return true;

        AstExpr* value = node->left;
        AstExpr* sentinel = node->right;
        if (value->is<AstExprConstantNil>() || isNoneGlobal(value))
            std::swap(value, sentinel);

        bool comparesNil = sentinel->is<AstExprConstantNil>();
        bool comparesNone = isNoneGlobal(sentinel);
        if (!comparesNil && !comparesNone)
            return true;

        std::optional<TypeId> ty = context->getType(value);
        Nullish nullish;
        if (!ty || !collect(*ty, nullish))
            return true;

        // Only the mix-up: a value that can be neither is a different (and less likely) mistake.
        const char* compared = comparesNil ? "nil" : "none";
        const char* instead = comparesNil ? "none" : "nil";
        bool mixedUp = comparesNil ? (!nullish.nil && nullish.none) : (!nullish.none && nullish.nil);
        if (!mixedUp)
            return true;

        bool notEqual = node->op == AstExprBinary::CompareNe;
        const char* op = notEqual ? "~=" : "==";
        const char* always = notEqual ? "true" : "false";
        std::optional<std::string> name = shortExprName(value);

        // In a condition, testing the value itself is the other fix, when `false` can't slip through it.
        std::string truthiness;
        const char* const* keyword = conditions.find(node);
        const bool canSuggestTruthiness = keyword && name && !nullish.canBeFalse;
        if (canSuggestTruthiness)
        {
            const char* negation = notEqual ? "" : "not ";
            const char* bodyKeyword = strcmp(*keyword, "if") == 0 ? "then" : "do";
            truthiness = format(" or '%s %s%s %s'", *keyword, negation, name->c_str(), bodyKeyword);
        }

        if (name)
            emitWarning(
                *context,
                LintWarning::Code_NilNoneComparison,
                node->location,
                "'%s' can be '%s' but never '%s', so this is always %s; did you mean '%s %s %s'%s?",
                name->c_str(),
                instead,
                compared,
                always,
                name->c_str(),
                op,
                instead,
                truthiness.c_str()
            );
        else
            emitWarning(
                *context,
                LintWarning::Code_NilNoneComparison,
                node->location,
                "This value can be '%s' but never '%s', so this is always %s; did you mean to compare with '%s'?",
                instead,
                compared,
                always,
                instead
            );

        return true;
    }
};

// Luwu: `OptimizationHint` reports code the compiler could make faster if it were written differently.
//
// Luwu Classes (rfcs/classes): most hints are about proving a value's class. Without `--!trust`, the compiler doesn't
// act on type annotations, so `if cat then cat:meow() end` with `cat: Cat?` proves nothing about `cat`'s class and
// `cat:meow()` stays an ordinary method call. `if class.isinstance(cat, Cat) then` (or `assert(...)` of it) proves it,
// which lets the compiler inline `Cat`'s methods and read its fields at fixed offsets. These hints are only for classes
// declared in this file, the only ones the compiler can inline.
class LintOptimizationHint : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context, bool classHints)
    {
        LintOptimizationHint pass;
        pass.context = &context;

        if (classHints)
        {
            ClassCollector collector{&pass.classes};
            context.root->visit(&collector);
        }

        context.root->visit(&pass);
        pass.reportFieldMethodCalls(context.root);
    }

private:
    // Below this many method calls through the same field in one function, binding it to a local isn't worth a hint.
    static constexpr size_t kFieldMethodCallHintThreshold = 2;

    LintContext* context;
    // The names of the classes declared in this file. Empty when class hints are off.
    std::unordered_set<std::string> classes;

    struct ClassCollector : AstVisitor
    {
        std::unordered_set<std::string>* classes;

        explicit ClassCollector(std::unordered_set<std::string>* classes)
            : classes(classes)
        {
        }

        bool visit(AstStatClass* node) override
        {
            if (!node->isTrait)
                classes->insert(node->name->name.value);
            return true;
        }
    };

    // Whether code reads a field of `local` or calls a method on it, outside nested functions: a `class.isinstance`
    // proof doesn't reach into a closure.
    struct LocalIndexFinder : AstVisitor
    {
        AstLocal* local;
        bool found = false;

        explicit LocalIndexFinder(AstLocal* local)
            : local(local)
        {
        }

        bool visit(AstExprIndexName* node) override
        {
            AstExprLocal* indexed = unparenthesized(node->expr)->as<AstExprLocal>();
            if (indexed && indexed->local == local)
                found = true;
            return !found;
        }

        bool visit(AstExprFunction*) override
        {
            return false;
        }
    };

    // `a.field:method(...)` calls in one function, by `a` and `field`, not counting nested functions. A field the
    // function also assigns can't be read into a local once, so those are marked.
    struct FieldMethodCallCounter : AstVisitor
    {
        struct Calls
        {
            AstExprIndexName* field = nullptr; // the first `a.field` called through
            size_t count = 0;
            bool assigned = false;
        };

        std::vector<std::pair<std::pair<AstLocal*, AstName>, Calls>> calls;

        // `a.field` as a key, or nullopt when it isn't one
        static std::optional<std::pair<AstLocal*, AstName>> keyOf(AstExpr* expr)
        {
            AstExprIndexName* field = unparenthesized(expr)->as<AstExprIndexName>();
            AstExprLocal* base = field && field->op == '.' ? unparenthesized(field->expr)->as<AstExprLocal>() : nullptr;
            if (!base)
                return std::nullopt;

            return std::pair<AstLocal*, AstName>{base->local, field->index};
        }

        Calls& entry(const std::pair<AstLocal*, AstName>& key)
        {
            auto it = std::find_if(
                calls.begin(),
                calls.end(),
                [&](const auto& existing)
                {
                    return existing.first == key;
                }
            );
            if (it != calls.end())
                return it->second;

            calls.push_back({key, Calls{}});
            return calls.back().second;
        }

        void markAssigned(AstExpr* target)
        {
            if (std::optional<std::pair<AstLocal*, AstName>> key = keyOf(target))
                entry(*key).assigned = true;
        }

        bool visit(AstExprCall* node) override
        {
            AstExprIndexName* method = node->self ? node->func->as<AstExprIndexName>() : nullptr;
            std::optional<std::pair<AstLocal*, AstName>> key = method ? keyOf(method->expr) : std::nullopt;
            if (!key)
                return true;

            Calls& counted = entry(*key);
            if (!counted.field)
                counted.field = unparenthesized(method->expr)->as<AstExprIndexName>();
            ++counted.count;
            return true;
        }

        bool visit(AstStatAssign* node) override
        {
            for (AstExpr* var : node->vars)
                markAssigned(var);
            return true;
        }

        bool visit(AstStatCompoundAssign* node) override
        {
            markAssigned(node->var);
            return true;
        }

        bool visit(AstExprFunction*) override
        {
            return false;
        }
    };

    struct FunctionCollector : AstVisitor
    {
        std::vector<AstStatBlock*> bodies;

        bool visit(AstExprFunction* node) override
        {
            bodies.push_back(node->body);
            return true;
        }
    };

    static bool isNilOrNone(AstExpr* expr)
    {
        AstExprGlobal* global = expr->as<AstExprGlobal>();
        return expr->is<AstExprConstantNil>() || (global && global->name == "none");
    }

    // The local `x`, `x ~= nil` or `x ~= none` narrows when it's true
    static AstExprLocal* narrowedLocal(AstExpr* condition)
    {
        condition = unparenthesized(condition);
        if (AstExprLocal* local = condition->as<AstExprLocal>())
            return local;

        AstExprBinary* binary = condition->as<AstExprBinary>();
        if (!binary || binary->op != AstExprBinary::CompareNe)
            return nullptr;

        AstExpr* value = unparenthesized(binary->left);
        AstExpr* sentinel = unparenthesized(binary->right);
        if (isNilOrNone(value))
            std::swap(value, sentinel);

        if (!isNilOrNone(sentinel))
            return nullptr;

        return value->as<AstExprLocal>();
    }

    static bool isNilOrNoneType(AstType* type)
    {
        if (type->is<AstTypeOptional>())
            return true;

        AstTypeReference* reference = type->as<AstTypeReference>();
        return reference && !reference->prefix && (reference->name == "nil" || reference->name == "none");
    }

    // `Cat?`, `Cat | nil` or `Cat | none`, where `Cat` is a class declared in this file
    std::optional<AstName> optionalClassAnnotation(AstType* annotation) const
    {
        if (!annotation)
            return std::nullopt;

        while (AstTypeGroup* group = annotation->as<AstTypeGroup>())
            annotation = group->type;

        AstTypeUnion* typeUnion = annotation->as<AstTypeUnion>();
        if (!typeUnion)
            return std::nullopt;

        std::optional<AstName> className;
        bool optional = false;
        for (AstType* part : typeUnion->types)
        {
            while (AstTypeGroup* group = part->as<AstTypeGroup>())
                part = group->type;

            if (isNilOrNoneType(part))
            {
                optional = true;
                continue;
            }

            AstTypeReference* reference = part->as<AstTypeReference>();
            bool isLocalClass = reference && !reference->prefix && classes.count(reference->name.value) > 0;
            if (!isLocalClass || className)
                return std::nullopt;

            className = reference->name;
        }

        if (!optional)
            return std::nullopt;

        return className;
    }

    // The class a nil check on `condition` should have been a `class.isinstance` check for
    std::optional<AstName> nilCheckedClass(AstExpr* condition, AstExprLocal*& narrowed) const
    {
        narrowed = narrowedLocal(condition);
        if (!narrowed)
            return std::nullopt;

        return optionalClassAnnotation(narrowed->local->annotation);
    }

    bool visit(AstStatIf* node) override
    {
        // Luwu If Local (rfcs/if-local.md): only an ordinary condition is a nil check of a local
        if (node->clauses.size != 0)
            return true;

        AstExprLocal* narrowed = nullptr;
        std::optional<AstName> className = nilCheckedClass(node->condition, narrowed);
        if (!className)
            return true;

        LocalIndexFinder finder{narrowed->local};
        node->thenbody->visit(&finder);
        if (!finder.found)
            return true;

        const char* name = narrowed->local->name.value;
        emitWarning(
            *context,
            LintWarning::Code_MethodsNotInlined,
            node->condition->location,
            "Checking '%s' for nil doesn't prove to the compiler that it's a '%s', since type annotations are only trusted under "
            "'--!trust'; use 'if class.isinstance(%s, %s) then' so '%s' methods can be inlined here",
            name,
            className->value,
            name,
            className->value,
            className->value
        );

        return true;
    }

    // `assert(cat)` proves nothing about `cat`'s class; `assert(class.isinstance(cat, Cat))` proves it for the rest of the
    // block
    bool visit(AstStatBlock* node) override
    {
        for (size_t i = 0; i < node->body.size; ++i)
        {
            AstStatExpr* statement = node->body.data[i]->as<AstStatExpr>();
            AstExprCall* call = statement ? statement->expr->as<AstExprCall>() : nullptr;
            AstExprGlobal* callee = call ? call->func->as<AstExprGlobal>() : nullptr;
            if (!callee || callee->name != "assert" || call->args.size == 0)
                continue;

            AstExprLocal* narrowed = nullptr;
            std::optional<AstName> className = nilCheckedClass(call->args.data[0], narrowed);
            if (!className)
                continue;

            LocalIndexFinder finder{narrowed->local};
            for (size_t j = i + 1; j < node->body.size && !finder.found; ++j)
                node->body.data[j]->visit(&finder);

            if (!finder.found)
                continue;

            const char* name = narrowed->local->name.value;
            emitWarning(
                *context,
                LintWarning::Code_MethodsNotInlined,
                call->location,
                "Asserting '%s' isn't nil doesn't prove to the compiler that it's a '%s', since type annotations are only trusted "
                "under '--!trust'; use 'assert(class.isinstance(%s, %s))' so '%s' methods can be inlined after it",
                name,
                className->value,
                name,
                className->value,
                className->value
            );
        }

        return true;
    }

    // `math.floor(a / b)` with numbers is `a // b`, which is a single instruction
    void checkFloorDivision(AstExprCall* node)
    {
        AstExprIndexName* function = node->func->as<AstExprIndexName>();
        AstExprGlobal* library = function ? function->expr->as<AstExprGlobal>() : nullptr;
        if (!library || library->name != "math" || function->index != "floor" || node->args.size != 1)
            return;

        AstExprBinary* division = unparenthesized(node->args.data[0])->as<AstExprBinary>();
        if (!division || division->op != AstExprBinary::Div)
            return;

        std::optional<TypeId> left = context->getType(division->left);
        std::optional<TypeId> right = context->getType(division->right);
        if (!left || !right || !isNumber(*left) || !isNumber(*right))
            return;

        emitWarning(
            *context,
            LintWarning::Code_FloorDivision,
            node->location,
            "'math.floor(a / b)' divides and then calls a function; 'a // b' gives the same result in one instruction"
        );
    }

    // Calling `getfenv` or `setfenv` turns off the environment's `safeenv`, which every imported global and fast builtin
    // call in the module depends on. This is separate from their deprecation warning, which only fires for a stack
    // level argument.
    void checkFenv(AstExprCall* node)
    {
        AstExprGlobal* callee = node->self ? nullptr : node->func->as<AstExprGlobal>();
        if (!callee || (callee->name != "getfenv" && callee->name != "setfenv"))
            return;

        emitWarning(
            *context,
            LintWarning::Code_FenvDeoptimization,
            node->location,
            "Using '%s' deoptimizes this entire module and makes your code run slower",
            callee->name.value
        );
    }

    bool visit(AstExprCall* node) override
    {
        checkFloorDivision(node);
        checkFenv(node);
        return true;
    }

    // `self.pos:add(...)` several times in one function: a field can't be proven in place, so none of the calls are
    // inlined. A local holding the field, checked once, can be.
    void reportFieldMethodCalls(AstStat* root)
    {
        if (classes.empty() || !context->module)
            return;

        FunctionCollector functions;
        root->visit(&functions);

        std::vector<AstStat*> bodies{root};
        bodies.insert(bodies.end(), functions.bodies.begin(), functions.bodies.end());

        for (AstStat* body : bodies)
        {
            FieldMethodCallCounter counter;
            body->visit(&counter);

            for (const auto& [key, calls] : counter.calls)
            {
                if (!calls.field || calls.assigned || calls.count < kFieldMethodCallHintThreshold)
                    continue;

                std::optional<TypeId> fieldType = context->getType(calls.field);
                const ExternType* object = fieldType ? get<ExternType>(follow(*fieldType)) : nullptr;
                const char* kind = fieldType ? luwuNominalKind(*fieldType) : nullptr;
                bool isObject = kind && std::string_view(kind) == "object";
                bool isLocalClassObject = object && isObject && classes.count(object->name) > 0;
                if (!isLocalClassObject)
                    continue;

                const char* base = key.first->name.value;
                const char* field = key.second.value;
                emitWarning(
                    *context,
                    LintWarning::Code_MethodsNotInlined,
                    calls.field->location,
                    "'%s.%s' has %zu method calls here, but a field can't be proven to be a '%s' in place, so none of them can be "
                    "inlined; check it once in a local: 'local %s = %s.%s' and 'assert(class.isinstance(%s, %s))'",
                    base,
                    field,
                    calls.count,
                    object->name.c_str(),
                    field,
                    base,
                    field,
                    field,
                    object->name.c_str()
                );
            }
        }
    }
};

// Luwu: whether `a` and `b` name the same variable or field chain (`x`, `t.items`)
static bool sameTarget(AstExpr* a, AstExpr* b)
{
    a = unparenthesized(a);
    b = unparenthesized(b);

    if (AstExprLocal* localA = a->as<AstExprLocal>())
    {
        AstExprLocal* localB = b->as<AstExprLocal>();
        return localB && localA->local == localB->local;
    }

    if (AstExprGlobal* globalA = a->as<AstExprGlobal>())
    {
        AstExprGlobal* globalB = b->as<AstExprGlobal>();
        return globalB && globalA->name == globalB->name;
    }

    AstExprIndexName* indexA = a->as<AstExprIndexName>();
    AstExprIndexName* indexB = b->as<AstExprIndexName>();
    return indexA && indexB && indexA->index == indexB->index && sameTarget(indexA->expr, indexB->expr);
}

// Luwu: `expr` quoted for a message, or `fallback` when it has no short name
static std::string describe(AstExpr* expr, const char* fallback)
{
    std::optional<std::string> name = shortExprName(unparenthesized(expr));
    return name ? "'" + *name + "'" : std::string(fallback);
}

// Luwu: the loop parts of OptimizationHint, each with its own code so it can be turned off alone. Each is quadratic in
// the number of iterations: the work it repeats grows with every one. The message gives the cost for the loop nest it's
// in (see `cost`).
class LintLoopHints : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context, bool concat, bool tableInsert, bool tableRemove)
    {
        LintLoopHints pass;
        pass.context = &context;
        pass.concat = concat;
        pass.tableInsert = tableInsert;
        pass.tableRemove = tableRemove;

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    bool concat = false;
    bool tableInsert = false;
    bool tableRemove = false;

    // The loops around the code being visited in the function being visited, outermost first
    std::vector<AstStat*> loops;
    // Whether the code being visited is inside a function, rather than at the top of the module
    bool inFunction = false;
    // Bindings declared by a `local` or `const`, which `@[nolint]` can be written over (a parameter or a loop variable
    // can't)
    DenseHashSet<AstLocal*> declaredLocals{nullptr};

    bool visit(AstStatLocal* node) override
    {
        for (AstLocal* local : node->vars)
            declaredLocals.insert(local);
        return true;
    }

    struct LoopScope
    {
        LintLoopHints* pass;

        LoopScope(LintLoopHints* pass, AstStat* loop)
            : pass(pass)
        {
            pass->loops.push_back(loop);
        }

        ~LoopScope()
        {
            pass->loops.pop_back();
        }
    };

    bool visit(AstStatWhile* node) override
    {
        node->condition->visit(this);
        LoopScope scope{this, node};
        node->body->visit(this);
        return false;
    }

    bool visit(AstStatRepeat* node) override
    {
        LoopScope scope{this, node};
        node->body->visit(this);
        node->condition->visit(this);
        return false;
    }

    bool visit(AstStatFor* node) override
    {
        node->from->visit(this);
        node->to->visit(this);
        if (node->step)
            node->step->visit(this);

        LoopScope scope{this, node};
        node->body->visit(this);
        return false;
    }

    bool visit(AstStatForIn* node) override
    {
        for (AstExpr* value : node->values)
            value->visit(this);

        LoopScope scope{this, node};
        node->body->visit(this);
        return false;
    }

    // A function's body runs when it's called, not once per iteration of a loop it's written in
    bool visit(AstExprFunction* node) override
    {
        std::vector<AstStat*> outer;
        outer.swap(loops);
        bool outerInFunction = inFunction;
        inFunction = true;
        node->body->visit(this);
        inFunction = outerInFunction;
        loops.swap(outer);
        return false;
    }

    // The binding a target is reached through: `s` for `s` and `s.parts.text`
    static AstLocal* rootLocal(AstExpr* target)
    {
        target = unparenthesized(target);
        while (true)
        {
            if (AstExprIndexName* index = target->as<AstExprIndexName>())
                target = unparenthesized(index->expr);
            else if (AstExprIndexExpr* index = target->as<AstExprIndexExpr>())
                target = unparenthesized(index->expr);
            else
                break;
        }

        AstExprLocal* local = target->as<AstExprLocal>();
        return local ? local->local : nullptr;
    }

    // `@[nolint(code)]` (or `@[nolint(OptimizationHint)]`, which each of these lints is part of) over the target's
    // `local` turns the warning off for that binding wherever it's used
    bool silencedByBinding(AstExpr* target, LintWarning::Code code) const
    {
        AstLocal* local = rootLocal(target);
        const LintMask* mask = local ? context->bindingNolints.find(local) : nullptr;
        return mask && (mask->test(code) || mask->test(LintWarning::Code_OptimizationHint));
    }

    // The last line of the help: where `@[nolint(name)]` would silence this
    std::string silenceHint(AstExpr* target, const char* name) const
    {
        AstLocal* local = rootLocal(target);
        if (local && declaredLocals.contains(local))
            return format("  - Add '@[nolint(%s)]' over '%s' to silence", name, local->name.value);
        if (inFunction)
            return format("  - Add '@[nolint(%s)]' over the function to silence", name);
        return format("  - Add '--!nolint %s' to the top of the file to silence", name);
    }

    // Whether `target` lives across `loop`'s iterations: a local declared before the loop, or anything that isn't a
    // local
    static bool outlives(AstExpr* target, AstStat* loop)
    {
        AstExprLocal* local = unparenthesized(target)->as<AstExprLocal>();
        return !local || local->local->location.begin < loop->location.begin;
    }

    bool outlivesInnermostLoop(AstExpr* target) const
    {
        return !loops.empty() && outlives(target, loops.back());
    }

    // The total cost of repeating the work on `target` throughout the loop nest, as `O(n²)`. A loop that `target`
    // outlives lets it grow across its iterations, so it adds 2 to the exponent: n more repetitions, each on something
    // n times bigger. A loop that declares `target` inside starts it over each iteration, so it only repeats the work
    // and adds 1. `s ..= x` is O(n²) in one loop and O(n⁴) in two, or O(n³) when `s` is declared between them.
    std::string cost(AstExpr* target) const
    {
        int exponent = 0;
        for (AstStat* loop : loops)
            exponent += outlives(target, loop) ? 2 : 1;

        static const char* const superscripts[] = {"⁰", "¹", "²", "³", "⁴", "⁵", "⁶", "⁷", "⁸", "⁹"};

        std::string power;
        if (exponent != 1)
            for (char digit : std::to_string(exponent))
                power += superscripts[digit - '0'];

        return "O(n" + power + ")";
    }

    void reportConcat(AstExpr* target, const Location& location)
    {
        if (!concat || !outlivesInnermostLoop(target) || silencedByBinding(target, LintWarning::Code_LoopConcat))
            return;

        emitWarning(
            *context,
            LintWarning::Code_LoopConcat,
            location,
            "Appending to %s in a loop is %s because it copies the string every iteration\n\n"
            "Help (expensive loop concat):\n"
            "  - Consider building an array of strings and 'table.concat' when finished\n"
            "  - If size is known up front, use a `buffer` instead\n"
            "%s",
            describe(target, "a string").c_str(),
            cost(target).c_str(),
            silenceHint(target, "LoopConcat").c_str()
        );
    }

    bool visit(AstStatCompoundAssign* node) override
    {
        if (node->op == AstExprBinary::Concat)
            reportConcat(node->var, node->location);
        return true;
    }

    // `s = s .. x` is the same as `s ..= x`
    bool visit(AstStatAssign* node) override
    {
        if (node->vars.size != 1 || node->values.size != 1)
            return true;

        AstExprBinary* value = unparenthesized(node->values.data[0])->as<AstExprBinary>();
        if (value && value->op == AstExprBinary::Concat && sameTarget(value->left, node->vars.data[0]))
            reportConcat(node->vars.data[0], node->location);

        return true;
    }

    static bool isConstantOne(AstExpr* expr)
    {
        AstExprConstantNumber* number = unparenthesized(expr)->as<AstExprConstantNumber>();
        return number && number->value == 1.0;
    }

    bool visit(AstExprCall* node) override
    {
        if (loops.empty() || node->self)
            return true;

        AstExprIndexName* function = node->func->as<AstExprIndexName>();
        AstExprGlobal* library = function ? function->expr->as<AstExprGlobal>() : nullptr;
        if (!library || library->name != "table" || node->args.size < 2)
            return true;

        AstExpr* target = node->args.data[0];
        if (!outlivesInnermostLoop(target) || !isConstantOne(node->args.data[1]))
            return true;

        if (tableInsert && function->index == "insert" && node->args.size == 3 &&
            !silencedByBinding(target, LintWarning::Code_InefficientTableInsert))
            emitWarning(
                *context,
                LintWarning::Code_InefficientTableInsert,
                node->location,
                "Inserting at the front of %s in a loop is %s because it moves every element every iteration\n\n"
                "Help (expensive loop insert):\n"
                "  - Consider appending instead and iterating in reverse, or reversing once when done\n"
                "%s",
                describe(target, "the table").c_str(),
                cost(target).c_str(),
                silenceHint(target, "InefficientTableInsert").c_str()
            );
        else if (tableRemove && function->index == "remove" && node->args.size == 2 &&
                 !silencedByBinding(target, LintWarning::Code_InefficientTableRemove))
            emitWarning(
                *context,
                LintWarning::Code_InefficientTableRemove,
                node->location,
                "Removing the first element of %s in a loop is %s because it moves every other element every iteration\n\n"
                "Help (expensive loop remove):\n"
                "  - Consider reading from a head index instead (`local item = queue[head]; head += 1`)\n"
                "%s",
                describe(target, "the table").c_str(),
                cost(target).c_str(),
                silenceHint(target, "InefficientTableRemove").c_str()
            );

        return true;
    }
};

// Luwu Declare Statements (rfcs/declare-statements.md): a file may declare a global that the loaded definitions
// already declare, for code that also runs where those definitions aren't loaded. Declaring it with the same type is
// silent; a different type is reported, since the file then disagrees with its environment about that global.
class LintDeclareMismatch : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        if (!context.module)
            return;

        LintDeclareMismatch pass;
        pass.context = &context;
        pass.moduleScope = context.module->getModuleScope();
        if (!pass.moduleScope)
            return;

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    ScopePtr moduleScope;

    void check(AstName name, const Location& location)
    {
        const LintContext::Global* global = context->builtinGlobals.find(name);
        if (!global || !global->type)
            return;

        auto binding = moduleScope->bindings.find(Symbol(name));
        if (binding == moduleScope->bindings.end())
            return;

        // A declared type that didn't resolve is already reported as an error
        if (get<ErrorType>(follow(binding->second.typeId)))
            return;

        ToStringOptions exact{/* exhaustive= */ true};
        if (toString(binding->second.typeId, exact) == toString(global->type, exact))
            return;

        emitWarning(
            *context,
            LintWarning::Code_DeclareMismatch,
            location,
            "'%s' is declared here as '%s', but the loaded definitions declare it as '%s'; add '--!nolint DeclareMismatch' if "
            "this is intended",
            name.value,
            toString(binding->second.typeId).c_str(),
            toString(global->type).c_str()
        );
    }

    // What a declared type says, in a form two declarations of it can be compared by: an extern type or a class prints
    // only its name, so its members, parent, indexer and metamethods are written out, and a class's class value too.
    static std::string describeType(TypeId ty, bool describeClassValue)
    {
        ToStringOptions exact{/* exhaustive= */ true};

        const ExternType* externType = get<ExternType>(follow(ty));
        if (!externType)
            return toString(ty, exact);

        std::string result = externType->name;
        if (externType->parent)
            result += " extends " + toString(*externType->parent, exact);

        for (const auto& [name, prop] : externType->props())
        {
            result += "; " + name;
            if (prop.isPrivate)
                result += " private";
            if (prop.isConst)
                result += " const";
            if (prop.isFinal)
                result += " final";
            if (prop.readTy)
                result += " read " + toString(*prop.readTy, exact);
            if (prop.writeTy)
                result += " write " + toString(*prop.writeTy, exact);
        }

        if (externType->indexer)
            result += "; [" + toString(externType->indexer->indexType, exact) + "]: " + toString(externType->indexer->indexResultType, exact);

        if (externType->metatable)
            result += "; metatable " + toString(*externType->metatable, exact);

        if (describeClassValue && externType->relation)
        {
            if (const Klass* klass = externType->relation->get_if<Klass>())
                result += "; class " + describeType(klass->ty, /* describeClassValue= */ false);
        }

        return result;
    }

    void checkType(AstName name, const Location& location)
    {
        std::optional<TypeFun> environmentType = context->scope->lookupType(name.value);
        if (!environmentType)
            return;

        std::optional<TypeFun> declaredType = moduleScope->lookupType(name.value);
        if (!declaredType || declaredType->type == environmentType->type)
            return;

        bool sameParams = declaredType->typeParams.size() == environmentType->typeParams.size() &&
                          declaredType->typePackParams.size() == environmentType->typePackParams.size();
        if (sameParams && describeType(declaredType->type, true) == describeType(environmentType->type, true))
            return;

        emitWarning(
            *context,
            LintWarning::Code_DeclareMismatch,
            location,
            "Type '%s' is declared here differently from the loaded definitions; add '--!nolint DeclareMismatch' if this is intended",
            name.value
        );
    }

    bool visit(AstStatDeclareGlobal* node) override
    {
        // `declare name` takes the environment's type, so it can't disagree with it
        if (node->type)
            check(node->name, node->nameLocation);
        return false;
    }

    bool visit(AstStatDeclareFunction* node) override
    {
        check(node->name, node->nameLocation);
        return false;
    }

    bool visit(AstStatDeclareExternType* node) override
    {
        checkType(node->name, node->location);
        return false;
    }

    bool visit(AstStatDeclareClass* node) override
    {
        checkType(node->shape->name->name, node->shape->name->location);
        return false;
    }

    bool visit(AstExprFunction* node) override
    {
        return false;
    }
};

// Luwu: a type assertion is a single expression, so `f(... :: number)` passes only the first of the
// values, exactly like `f((...))`, while looking like it only changes their type. Only reported where the
// values would otherwise all be used (the end of an argument, return, table or assignment list); wrapping
// the cast in parentheses says the truncation is intended.
class LintVarargCast : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintVarargCast pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    // Whether the call's type says it can produce more than one value. A call that produces one
    // (`require(x) :: any`, `tostring(x) :: string`) loses nothing to the cast, and without type
    // information nothing is known, so neither is reported.
    bool returnsSeveralValues(AstExprCall* call)
    {
        const FunctionType* function = nullptr;
        if (std::optional<TypeId> ty = context->getType(call->func))
            function = get<FunctionType>(follow(*ty));

        // An overloaded function's type is an intersection; the solver records the overload it chose.
        if (!function && context->module)
        {
            if (const TypeId* chosen = context->module->astOverloadResolvedTypes.find(call))
                function = get<FunctionType>(follow(*chosen));
        }

        if (!function)
            return false;

        auto [head, tail] = flatten(function->retTypes);
        if (head.size() > 1)
            return true;

        if (!tail)
            return false;

        TypePackId rest = follow(*tail);
        return get<VariadicTypePack>(rest) || get<GenericTypePack>(rest);
    }

    void check(AstExpr* expr)
    {
        AstExprTypeAssertion* cast = expr->as<AstExprTypeAssertion>();
        if (!cast)
            return;

        // `... :: any :: number` is one cast as far as the values are concerned.
        AstExpr* operand = cast->expr;
        while (AstExprTypeAssertion* inner = operand->as<AstExprTypeAssertion>())
            operand = inner->expr;

        std::string what;
        const char* unit = "value";
        if (operand->is<AstExprVarargs>())
            what = "'...'";
        else if (AstExprCall* call = operand->as<AstExprCall>(); call && returnsSeveralValues(call))
        {
            std::optional<std::string> name = shortExprName(call->func);
            what = name ? "'" + *name + "()'" : std::string("this call");
            unit = "result";
        }
        else
            return;

        emitWarning(
            *context,
            LintWarning::Code_VarargCast,
            cast->location,
            "This type cast silently truncates %s to its first %s at runtime; use a helper function to convert these %ss to '...%s', or wrap "
            "this in parentheses to silence",
            what.c_str(),
            unit,
            unit,
            toString(cast->annotation).c_str()
        );
    }

    bool visit(AstExprCall* node) override
    {
        if (node->args.size > 0)
            check(node->args.data[node->args.size - 1]);
        return true;
    }

    bool visit(AstStatReturn* node) override
    {
        if (node->list.size > 0)
            check(node->list.data[node->list.size - 1]);
        return true;
    }

    bool visit(AstExprTable* node) override
    {
        if (node->items.size > 0 && node->items.data[node->items.size - 1].kind == AstExprTable::Item::Kind::List)
            check(node->items.data[node->items.size - 1].value);
        return true;
    }

    // With no more names than values, the last value only ever supplies one, cast or not.
    bool visit(AstStatLocal* node) override
    {
        if (node->values.size > 0 && node->vars.size > node->values.size)
            check(node->values.data[node->values.size - 1]);
        return true;
    }

    bool visit(AstStatAssign* node) override
    {
        if (node->values.size > 0 && node->vars.size > node->values.size)
            check(node->values.data[node->values.size - 1]);
        return true;
    }
};

struct LintRedundantNativeAttribute : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintRedundantNativeAttribute pass;
        pass.context = &context;
        context.root->visit(&pass);
    }

private:
    LintContext* context;

    bool visit(AstExprFunction* node) override
    {
        node->body->visit(this);

        for (const auto attribute : node->attributes)
        {
            if (attribute->type == AstAttr::Type::Native)
            {
                emitWarning(
                    *context,
                    LintWarning::Code_RedundantNativeAttribute,
                    attribute->location,
                    "native attribute on a function is redundant in a native module; consider removing it"
                );
            }
        }

        return false;
    }
};

// Luwu: mistakes in loops that make them do something other than what they look like they do. Each has its own code.
class LintLoopMistakes : AstVisitor
{
public:
    struct Enabled
    {
        bool removeWhileIterating = false;
        bool loopVariableWrite = false;
        bool iteratedTableWrite = false;
        bool foreverLoop = false;
        bool uselessLoop = false;
        bool stringIndexZero = false;
    };

    LUAU_NOINLINE static void process(LintContext& context, Enabled enabled)
    {
        LintLoopMistakes pass;
        pass.context = &context;
        pass.enabled = enabled;

        CaptureCollector captures{&pass.captured, &pass.capturedWrites};
        context.root->visit(&captures);

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    Enabled enabled;

    // Locals some nested function reads or writes, and the ones it writes
    DenseHashSet<AstLocal*> captured{nullptr};
    DenseHashSet<AstLocal*> capturedWrites{nullptr};

    struct CaptureCollector : AstVisitor
    {
        DenseHashSet<AstLocal*>* captured;
        DenseHashSet<AstLocal*>* capturedWrites;

        CaptureCollector(DenseHashSet<AstLocal*>* captured, DenseHashSet<AstLocal*>* capturedWrites)
            : captured(captured)
            , capturedWrites(capturedWrites)
        {
        }

        void write(AstExpr* target)
        {
            AstExprLocal* local = unparenthesized(target)->as<AstExprLocal>();
            if (local && local->upvalue)
                capturedWrites->insert(local->local);
        }

        bool visit(AstExprLocal* node) override
        {
            if (node->upvalue)
                captured->insert(node->local);
            return true;
        }

        bool visit(AstStatAssign* node) override
        {
            for (AstExpr* var : node->vars)
                write(var);
            return true;
        }

        bool visit(AstStatCompoundAssign* node) override
        {
            write(node->var);
            return true;
        }
    };

    // `name(...)`, `lib.name(...)` or `obj:name(...)`: the last name in the callee
    static std::optional<std::string_view> calleeName(AstExprCall* call)
    {
        AstExpr* func = unparenthesized(call->func);
        if (AstExprGlobal* global = func->as<AstExprGlobal>())
            return std::string_view(global->name.value);
        if (AstExprLocal* local = func->as<AstExprLocal>())
            return std::string_view(local->local->name.value);
        if (AstExprIndexName* index = func->as<AstExprIndexName>())
            return std::string_view(index->index.value);
        return std::nullopt;
    }

    // A call that yields or waits: a loop around one is usually a service loop that something else ends
    static bool isYieldingCall(AstExprCall* call)
    {
        std::optional<std::string_view> name = calleeName(call);
        if (!name)
            return false;

        std::string lowered(*name);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
        return lowered.find("yield") != std::string::npos || lowered.find("wait") != std::string::npos ||
               lowered.find("sleep") != std::string::npos;
    }

    static bool isErrorCall(AstStat* stat)
    {
        AstStatExpr* expr = stat->as<AstStatExpr>();
        AstExprCall* call = expr ? expr->expr->as<AstExprCall>() : nullptr;
        AstExprGlobal* callee = call ? call->func->as<AstExprGlobal>() : nullptr;
        return callee && callee->name == "error";
    }

    // Whether a loop body can end the loop or hand control elsewhere: `break` (at this loop's level), `return`,
    // `error(...)`, or a yielding call. Nested functions run later, so they don't count.
    struct ExitFinder : AstVisitor
    {
        size_t nestedLoops = 0;
        bool found = false;
        bool foundContinue = false;

        bool visitLoopBody(AstStatBlock* body)
        {
            ++nestedLoops;
            body->visit(this);
            --nestedLoops;
            return false;
        }

        bool visit(AstStatWhile* node) override
        {
            node->condition->visit(this);
            return visitLoopBody(node->body);
        }

        bool visit(AstStatRepeat* node) override
        {
            node->condition->visit(this);
            return visitLoopBody(node->body);
        }

        bool visit(AstStatFor* node) override
        {
            return visitLoopBody(node->body);
        }

        bool visit(AstStatForIn* node) override
        {
            for (AstExpr* value : node->values)
                value->visit(this);
            return visitLoopBody(node->body);
        }

        bool visit(AstStatBreak*) override
        {
            if (nestedLoops == 0)
                found = true;
            return false;
        }

        bool visit(AstStatContinue*) override
        {
            if (nestedLoops == 0)
                foundContinue = true;
            return false;
        }

        bool visit(AstStatReturn*) override
        {
            found = true;
            return false;
        }

        bool visit(AstStatExpr* node) override
        {
            if (isErrorCall(node))
                found = true;
            return true;
        }

        bool visit(AstExprCall* node) override
        {
            if (isYieldingCall(node))
                found = true;
            return true;
        }

        bool visit(AstExprFunction*) override
        {
            return false;
        }
    };

    // How every path through a loop body leaves the loop: through `break`, `return` or `error(...)`
    enum class Leaves
    {
        Never,          // some path reaches the end of the body
        Directly,       // an unconditional `break`, `return` or `error(...)`
        ThroughBranches // every branch of an `if`/`else` leaves
    };

    static Leaves howBlockLeaves(AstStatBlock* block)
    {
        for (AstStat* stat : block->body)
        {
            if (stat->is<AstStatBreak>() || stat->is<AstStatReturn>() || isErrorCall(stat))
                return Leaves::Directly;

            if (AstStatBlock* inner = stat->as<AstStatBlock>())
            {
                if (Leaves leaves = howBlockLeaves(inner); leaves != Leaves::Never)
                    return leaves;
            }

            // Luwu Table Comprehensions (rfcs/table-comprehensions.md): an item evaluates its key and value every time, so a
            // `do` expression there that always leaves (`{ for i = 1, 10 do (do break) }`) leaves the iteration
            if (AstStatComprehensionItem* item = stat->as<AstStatComprehensionItem>())
            {
                for (AstExpr* part : {item->key, item->value})
                {
                    AstExprDo* leaving = part ? unparenthesized(part)->as<AstExprDo>() : nullptr;
                    if (leaving && howBlockLeaves(leaving->body) != Leaves::Never)
                        return Leaves::Directly;
                }
            }

            AstStatIf* branch = stat->as<AstStatIf>();
            bool everyBranchLeaves = branch && branch->elsebody && howBlockLeaves(branch->thenbody) != Leaves::Never &&
                                     elseLeaves(branch->elsebody);
            if (everyBranchLeaves)
                return Leaves::ThroughBranches;
        }
        return Leaves::Never;
    }

    static bool elseLeaves(AstStat* elsebody)
    {
        if (AstStatBlock* block = elsebody->as<AstStatBlock>())
            return howBlockLeaves(block) != Leaves::Never;
        if (AstStatIf* elseif = elsebody->as<AstStatIf>())
            return elseif->elsebody && howBlockLeaves(elseif->thenbody) != Leaves::Never && elseLeaves(elseif->elsebody);
        return false;
    }

    // `takesFirstItem`: a `for ... in` loop, where leaving unconditionally is the idiom for taking the first item
    // (`for k in pairs(t) do return k end`), so only leaving through every branch of an `if` is reported
    void checkUselessLoop(AstStat* loop, AstStatBlock* body, bool takesFirstItem)
    {
        if (!enabled.uselessLoop)
            return;

        Leaves leaves = howBlockLeaves(body);
        if (leaves == Leaves::Never || (takesFirstItem && leaves == Leaves::Directly))
            return;

        // `continue` on some path reaches the next iteration
        ExitFinder finder;
        body->visit(&finder);
        if (finder.foundContinue)
            return;

        emitWarning(
            *context,
            LintWarning::Code_UselessLoop,
            loop->location,
            "This loop runs at most once: every path through its body leaves it on the first iteration; if a 'return' or 'break' "
            "belongs after the loop, move it out"
        );
    }

    // ForeverLoop: what a condition reads, when it reads only locals, constants and `#` of locals
    struct ConditionReads
    {
        std::vector<AstLocal*> locals;
        std::vector<AstLocal*> lengths; // `#t`
    };

    static bool collectConditionReads(AstExpr* expr, ConditionReads& reads)
    {
        expr = unparenthesized(expr);

        if (AstExprLocal* local = expr->as<AstExprLocal>())
        {
            reads.locals.push_back(local->local);
            return true;
        }

        if (expr->is<AstExprConstantBool>() || expr->is<AstExprConstantNil>() || expr->is<AstExprConstantNumber>() ||
            expr->is<AstExprConstantString>())
            return true;

        if (AstExprUnary* unary = expr->as<AstExprUnary>())
        {
            if (unary->op == AstExprUnary::Op::Len)
            {
                AstExprLocal* table = unparenthesized(unary->expr)->as<AstExprLocal>();
                if (!table)
                    return false;
                reads.lengths.push_back(table->local);
                return true;
            }
            return collectConditionReads(unary->expr, reads);
        }

        if (AstExprBinary* binary = expr->as<AstExprBinary>())
            return collectConditionReads(binary->left, reads) && collectConditionReads(binary->right, reads);

        return false;
    }

    // Whether a loop body writes one of `locals`, or uses one of `tables` in any way but reading an element
    struct ConditionChangeFinder : AstVisitor
    {
        const ConditionReads* reads;
        bool found = false;

        explicit ConditionChangeFinder(const ConditionReads* reads)
            : reads(reads)
        {
        }

        bool isConditionLocal(AstExpr* expr) const
        {
            AstExprLocal* local = unparenthesized(expr)->as<AstExprLocal>();
            return local && std::find(reads->locals.begin(), reads->locals.end(), local->local) != reads->locals.end();
        }

        bool isLengthTable(AstLocal* local) const
        {
            return std::find(reads->lengths.begin(), reads->lengths.end(), local) != reads->lengths.end();
        }

        bool visit(AstStatAssign* node) override
        {
            for (AstExpr* var : node->vars)
                found = found || isConditionLocal(var);
            return true;
        }

        bool visit(AstStatCompoundAssign* node) override
        {
            found = found || isConditionLocal(node->var);
            return true;
        }

        // Reading an element of a table leaves its length alone; any other use of it might not
        bool visit(AstExprIndexExpr* node) override
        {
            AstExprLocal* table = unparenthesized(node->expr)->as<AstExprLocal>();
            if (table && isLengthTable(table->local))
            {
                node->index->visit(this);
                return false;
            }
            return true;
        }

        bool visit(AstExprLocal* node) override
        {
            if (isLengthTable(node->local))
                found = true;
            return true;
        }
    };

    void checkForeverLoop(AstStat* loop, AstExpr* condition, AstStatBlock* body)
    {
        if (!enabled.foreverLoop)
            return;

        ConditionReads reads;
        if (!collectConditionReads(condition, reads) || (reads.locals.empty() && reads.lengths.empty()))
            return;

        for (AstLocal* local : reads.locals)
        {
            // something else can change it, or (in `repeat ... until`) the body declares it afresh every iteration
            bool declaredInBody = body->location.encloses(local->location);
            if (capturedWrites.contains(local) || declaredInBody)
                return;
        }

        for (AstLocal* table : reads.lengths)
        {
            if (captured.contains(table))
                return;
        }

        ExitFinder exits;
        body->visit(&exits);
        if (exits.found)
            return;

        ConditionChangeFinder changes{&reads};
        body->visit(&changes);
        if (changes.found)
            return;

        std::string what;
        if (!reads.locals.empty())
            what = "'" + std::string(reads.locals.front()->name.value) + "'";
        else
            what = "the length of '" + std::string(reads.lengths.front()->name.value) + "'";

        emitWarning(
            *context,
            LintWarning::Code_ForeverLoop,
            loop->location,
            "Nothing in this loop changes %s, so once it starts it never stops; did you forget to update it?",
            what.c_str()
        );
    }

    // The table a `for ... in` loop iterates, and whether it's in key order (`pairs`, `next` or the table itself) or
    // index order (`ipairs`)
    struct Iterated
    {
        AstExpr* table = nullptr;
        bool byIndex = false;
    };

    static Iterated iteratedTable(AstStatForIn* loop)
    {
        if (loop->values.size == 0)
            return {};

        AstExpr* first = unparenthesized(loop->values.data[0]);
        if (AstExprCall* call = first->as<AstExprCall>())
        {
            AstExprGlobal* callee = call->func->as<AstExprGlobal>();
            if (!callee || call->args.size != 1 || loop->values.size != 1)
                return {};
            if (callee->name == "ipairs")
                return {call->args.data[0], true};
            if (callee->name == "pairs")
                return {call->args.data[0], false};
            return {};
        }

        // `for k, v in next, t`
        if (AstExprGlobal* global = first->as<AstExprGlobal>(); global && global->name == "next" && loop->values.size >= 2)
            return {loop->values.data[1], false};

        if (loop->values.size == 1)
            return {first, false};

        return {};
    }

    // RemoveWhileIterating: `table.remove(t, i)` statements in a block, at the block's own level
    struct RemoveFinder : AstVisitor
    {
        AstExpr* table;
        AstLocal* index;
        AstExprCall* found = nullptr;

        RemoveFinder(AstExpr* table, AstLocal* index)
            : table(table)
            , index(index)
        {
        }

        bool visit(AstExprCall* node) override
        {
            AstExprIndexName* function = node->func->as<AstExprIndexName>();
            AstExprGlobal* library = function ? function->expr->as<AstExprGlobal>() : nullptr;
            bool isRemove = library && library->name == "table" && function->index == "remove" && node->args.size == 2;
            AstExprLocal* removed = isRemove ? unparenthesized(node->args.data[1])->as<AstExprLocal>() : nullptr;
            if (removed && removed->local == index && sameTarget(node->args.data[0], table))
                found = node;
            return !found;
        }

        // the block walk visits nested blocks itself, with their own following statements
        bool visit(AstStatBlock*) override
        {
            return false;
        }

        bool visit(AstExprFunction*) override
        {
            return false;
        }
    };

    void checkRemovesIn(AstStatBlock* block, AstExpr* table, AstLocal* index)
    {
        for (size_t i = 0; i < block->body.size; ++i)
        {
            AstStat* stat = block->body.data[i];

            RemoveFinder finder{table, index};
            stat->visit(&finder);

            if (finder.found)
            {
                // `table.remove(t, i)` then `break` or `return` never reaches the next element
                AstStat* next = i + 1 < block->body.size ? block->body.data[i + 1] : nullptr;
                bool leavesRightAfter = next && (next->is<AstStatBreak>() || next->is<AstStatReturn>() || isErrorCall(next));
                if (!leavesRightAfter)
                    emitWarning(
                        *context,
                        LintWarning::Code_RemoveWhileIterating,
                        finder.found->location,
                        "Removing element '%s' from %s while iterating it forwards skips the element after it, which moves into slot "
                        "'%s'; iterate backwards ('for %s = #t, 1, -1 do') or build a new table",
                        index->name.value,
                        describe(table, "the table").c_str(),
                        index->name.value,
                        index->name.value
                    );
            }

            // nested blocks of this statement (if/do/loops), not functions
            NestedBlockCollector nested;
            stat->visit(&nested);
            for (AstStatBlock* inner : nested.blocks)
                checkRemovesIn(inner, table, index);
        }
    }

    // The blocks directly inside a statement: its own bodies, not the blocks nested inside those
    struct NestedBlockCollector : AstVisitor
    {
        std::vector<AstStatBlock*> blocks;

        bool visit(AstStatBlock* node) override
        {
            blocks.push_back(node);
            return false;
        }

        bool visit(AstExprFunction*) override
        {
            return false;
        }
    };

    void checkRemoveWhileIterating(AstExpr* table, AstLocal* index, AstStatBlock* body)
    {
        if (enabled.removeWhileIterating)
            checkRemovesIn(body, table, index);
    }

    // LoopVariableWrite
    struct LoopVariableWrites : AstVisitor
    {
        const std::vector<AstLocal*>* vars;
        std::vector<std::pair<AstStat*, AstLocal*>> writes;
        DenseHashSet<AstExprLocal*> writeTargets{nullptr};

        explicit LoopVariableWrites(const std::vector<AstLocal*>* vars)
            : vars(vars)
        {
        }

        AstLocal* loopVariable(AstExpr* target)
        {
            AstExprLocal* local = unparenthesized(target)->as<AstExprLocal>();
            if (!local || std::find(vars->begin(), vars->end(), local->local) == vars->end())
                return nullptr;
            return local->local;
        }

        bool visit(AstStatAssign* node) override
        {
            for (AstExpr* var : node->vars)
            {
                if (AstLocal* local = loopVariable(var))
                {
                    writes.emplace_back(node, local);
                    writeTargets.insert(unparenthesized(var)->as<AstExprLocal>());
                }
            }
            return true;
        }

        bool visit(AstStatCompoundAssign* node) override
        {
            if (AstLocal* local = loopVariable(node->var))
                writes.emplace_back(node, local);
            return true;
        }
    };

    // Whether `local` is read after `after` ends, within `body`
    struct ReadAfterFinder : AstVisitor
    {
        AstLocal* local;
        Position after;
        const DenseHashSet<AstExprLocal*>* writeTargets;
        bool found = false;

        ReadAfterFinder(AstLocal* local, Position after, const DenseHashSet<AstExprLocal*>* writeTargets)
            : local(local)
            , after(after)
            , writeTargets(writeTargets)
        {
        }

        bool visit(AstExprLocal* node) override
        {
            if (node->local == local && !writeTargets->contains(node) && after < node->location.begin)
                found = true;
            return !found;
        }
    };

    void checkLoopVariableWrites(const std::vector<AstLocal*>& vars, AstStatBlock* body, bool numeric, AstStatForIn* forIn)
    {
        if (!enabled.loopVariableWrite)
            return;

        LoopVariableWrites writes{&vars};
        body->visit(&writes);

        for (const auto& [stat, local] : writes.writes)
        {
            if (numeric)
            {
                emitWarning(
                    *context,
                    LintWarning::Code_LoopVariableWrite,
                    stat->location,
                    "Assigning to the loop variable '%s' doesn't change which iteration runs next; use a 'while' loop to control the "
                    "counter",
                    local->name.value
                );
                continue;
            }

            // In a `for ... in` loop, reusing the variable as a scratch value is fine; a write nothing reads is the mistake
            ReadAfterFinder reads{local, stat->location.end, &writes.writeTargets};
            body->visit(&reads);
            if (reads.found)
                continue;

            std::string fix = "write through the table instead";
            Iterated iterated = iteratedTable(forIn);
            std::optional<std::string> tableName = iterated.table ? shortExprName(unparenthesized(iterated.table)) : std::nullopt;
            bool namedKey = forIn->vars.size >= 2 && std::string_view(forIn->vars.data[0]->name.value) != "_";
            if (tableName && namedKey && local == forIn->vars.data[1])
                fix = "write through the table instead ('" + *tableName + "[" + forIn->vars.data[0]->name.value + "] = ...')";

            emitWarning(
                *context,
                LintWarning::Code_LoopVariableWrite,
                stat->location,
                "Assigning to '%s' only changes the loop's copy, and nothing reads it afterwards; to change the table, %s",
                local->name.value,
                fix.c_str()
            );
        }
    }

    // IteratedTableWrite
    struct TableWriteFinder : AstVisitor
    {
        AstExpr* table;
        AstLocal* key;
        std::vector<AstStat*> writes;

        TableWriteFinder(AstExpr* table, AstLocal* key)
            : table(table)
            , key(key)
        {
        }

        bool writesOtherKey(AstExpr* target) const
        {
            target = unparenthesized(target);
            if (AstExprIndexExpr* index = target->as<AstExprIndexExpr>())
            {
                AstExprLocal* indexLocal = unparenthesized(index->index)->as<AstExprLocal>();
                bool ownKey = key && indexLocal && indexLocal->local == key;
                return sameTarget(index->expr, table) && !ownKey;
            }

            AstExprIndexName* field = target->as<AstExprIndexName>();
            return field && field->op == '.' && sameTarget(field->expr, table);
        }

        bool visit(AstStatAssign* node) override
        {
            for (AstExpr* var : node->vars)
            {
                if (writesOtherKey(var))
                {
                    writes.push_back(node);
                    break;
                }
            }
            return true;
        }

        bool visit(AstStatCompoundAssign* node) override
        {
            if (writesOtherKey(node->var))
                writes.push_back(node);
            return true;
        }

        bool visit(AstExprFunction*) override
        {
            return false;
        }
    };

    // An array (`{T}`) may have its existing elements changed while it's iterated; a map may not get new keys
    bool isArray(AstExpr* table) const
    {
        std::optional<TypeId> ty = context->getType(table);
        const TableType* tt = ty ? get<TableType>(follow(*ty)) : nullptr;
        return tt && tt->indexer && tt->props.empty() && isNumber(tt->indexer->indexType);
    }

    void checkIteratedTableWrites(AstStatForIn* loop)
    {
        if (!enabled.iteratedTableWrite)
            return;

        Iterated iterated = iteratedTable(loop);
        if (!iterated.table || iterated.byIndex || isArray(iterated.table))
            return;

        AstLocal* key = loop->vars.size >= 1 ? loop->vars.data[0] : nullptr;
        TableWriteFinder finder{iterated.table, key};
        loop->body->visit(&finder);

        for (AstStat* write : finder.writes)
        {
            std::string keyName = key ? "'" + std::string(key->name.value) + "'" : std::string("the loop's key");
            emitWarning(
                *context,
                LintWarning::Code_IteratedTableWrite,
                write->location,
                "Writing a key of %s other than %s while iterating it is undefined: the loop may skip or repeat entries; collect "
                "the changes and apply them after the loop",
                describe(iterated.table, "the table").c_str(),
                keyName.c_str()
            );
        }
    }

    // Loops
    bool visit(AstStatFor* node) override
    {
        checkLoopVariableWrites({node->var}, node->body, /* numeric */ true, nullptr);
        checkUselessLoop(node, node->body, /* takesFirstItem */ false);

        // `for i = 1, #t` (counting up) is a forward pass over `t`
        AstExprUnary* length = unparenthesized(node->to)->as<AstExprUnary>();
        AstExprConstantNumber* step = node->step ? unparenthesized(node->step)->as<AstExprConstantNumber>() : nullptr;
        bool countsUp = !node->step || (step && step->value > 0);
        if (length && length->op == AstExprUnary::Op::Len && countsUp)
            checkRemoveWhileIterating(length->expr, node->var, node->body);

        return true;
    }

    bool visit(AstStatForIn* node) override
    {
        std::vector<AstLocal*> vars(node->vars.begin(), node->vars.end());
        checkLoopVariableWrites(vars, node->body, /* numeric */ false, node);
        checkUselessLoop(node, node->body, /* takesFirstItem */ true);
        checkIteratedTableWrites(node);

        Iterated iterated = iteratedTable(node);
        if (iterated.table && node->vars.size >= 1)
            checkRemoveWhileIterating(iterated.table, node->vars.data[0], node->body);

        return true;
    }

    bool visit(AstStatWhile* node) override
    {
        checkForeverLoop(node, node->condition, node->body);
        checkUselessLoop(node, node->body, /* takesFirstItem */ false);
        return true;
    }

    bool visit(AstStatRepeat* node) override
    {
        checkForeverLoop(node, node->condition, node->body);
        return true;
    }

    // StringIndexZero: strings are indexed from 1
    bool visit(AstExprCall* node) override
    {
        if (!enabled.stringIndexZero)
            return true;

        AstExprIndexName* function = node->func->as<AstExprIndexName>();
        if (!function || (function->index != "byte" && function->index != "sub"))
            return true;

        // `string.byte(s, 0)` or `s:byte(0)` on a string
        AstExpr* start = nullptr;
        AstExprGlobal* library = function->expr->as<AstExprGlobal>();
        if (!node->self && library && library->name == "string" && node->args.size >= 2)
            start = node->args.data[1];
        else if (node->self && node->args.size >= 1)
        {
            std::optional<TypeId> receiver = context->getType(function->expr);
            if (receiver && isString(*receiver))
                start = node->args.data[0];
        }

        AstExprConstantNumber* zero = start ? unparenthesized(start)->as<AstExprConstantNumber>() : nullptr;
        if (!zero || zero->value != 0.0)
            return true;

        if (function->index == "byte")
            emitWarning(
                *context,
                LintWarning::Code_StringIndexZero,
                start->location,
                "Strings are indexed from 1: 'byte' at 0 is before the first character and returns nothing"
            );
        else
            emitWarning(
                *context,
                LintWarning::Code_StringIndexZero,
                start->location,
                "Strings are indexed from 1: 'sub' treats a start of 0 as 1, so an end index written for 0-based indexing is one "
                "character short"
            );

        return true;
    }
};

// Luwu: values used in ways that can't do what they look like they do. Each has its own code.
class LintValueMistakes : AstVisitor
{
public:
    struct Enabled
    {
        bool newValueComparison = false;
        bool tableTruthiness = false;
        bool discardedResult = false;
    };

    LUAU_NOINLINE static void process(LintContext& context, Enabled enabled)
    {
        LintValueMistakes pass;
        pass.context = &context;
        pass.enabled = enabled;

        if (enabled.tableTruthiness)
        {
            ContainerLocalCollector collector{&pass.containerLocals, &pass.assignedLocals};
            context.root->visit(&collector);
        }

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    Enabled enabled;

    // TableTruthiness candidates: annotated parameters and locals a table literal initializes. A map read or a call
    // result typed as a table can still be nil (a map read is typed without the nil it can give), so those aren't.
    DenseHashSet<AstLocal*> containerLocals{nullptr};
    DenseHashSet<AstLocal*> assignedLocals{nullptr};

    struct ContainerLocalCollector : AstVisitor
    {
        DenseHashSet<AstLocal*>* containerLocals;
        DenseHashSet<AstLocal*>* assignedLocals;

        ContainerLocalCollector(DenseHashSet<AstLocal*>* containerLocals, DenseHashSet<AstLocal*>* assignedLocals)
            : containerLocals(containerLocals)
            , assignedLocals(assignedLocals)
        {
        }

        bool visit(AstExprFunction* node) override
        {
            for (AstLocal* arg : node->args)
            {
                if (arg->annotation)
                    containerLocals->insert(arg);
            }
            return true;
        }

        bool visit(AstStatLocal* node) override
        {
            for (size_t i = 0; i < node->vars.size && i < node->values.size; ++i)
            {
                if (unparenthesized(node->values.data[i])->is<AstExprTable>())
                    containerLocals->insert(node->vars.data[i]);
            }
            return true;
        }

        void assigned(AstExpr* target)
        {
            if (AstExprLocal* local = unparenthesized(target)->as<AstExprLocal>())
                assignedLocals->insert(local->local);
        }

        bool visit(AstStatAssign* node) override
        {
            for (AstExpr* var : node->vars)
                assigned(var);
            return true;
        }

        bool visit(AstStatCompoundAssign* node) override
        {
            assigned(node->var);
            return true;
        }
    };

    // NewValueComparison: a table or function literal makes a new value, which nothing else can be equal to
    bool visit(AstExprBinary* node) override
    {
        if (!enabled.newValueComparison || (node->op != AstExprBinary::CompareEq && node->op != AstExprBinary::CompareNe))
            return true;

        AstExpr* left = unparenthesized(node->left);
        AstExpr* right = unparenthesized(node->right);
        bool table = left->is<AstExprTable>() || right->is<AstExprTable>();
        bool function = left->is<AstExprFunction>() || right->is<AstExprFunction>();
        if (!table && !function)
            return true;

        const char* always = node->op == AstExprBinary::CompareEq ? "false" : "true";
        if (table)
            emitWarning(
                *context,
                LintWarning::Code_NewValueComparison,
                node->location,
                "This comparison is always %s: a table literal makes a new table, which is never equal to another value; to check "
                "whether a table is empty, use 'next(t) == nil'",
                always
            );
        else
            emitWarning(
                *context,
                LintWarning::Code_NewValueComparison,
                node->location,
                "This comparison is always %s: a function literal makes a new function, which is never equal to another value",
                always
            );

        return true;
    }

    // TableTruthiness: an array, a map or an empty table (a table with no fields) is truthy even when it's empty, which
    // people used to Python and JavaScript's falsy empty containers don't expect. A table with fields is left alone:
    // testing a record that isn't optional is a defensive nil check, not this mistake.
    bool isContainer(AstExpr* expr) const
    {
        AstExprLocal* local = expr->as<AstExprLocal>();
        if (!local || !containerLocals.contains(local->local) || assignedLocals.contains(local->local))
            return false;

        std::optional<TypeId> ty = context->getType(expr);
        const TableType* table = ty ? get<TableType>(follow(*ty)) : nullptr;
        return table && table->props.empty();
    }

    void checkCondition(AstExpr* condition)
    {
        if (!enabled.tableTruthiness)
            return;

        condition = unparenthesized(condition);
        AstExprUnary* negation = condition->as<AstExprUnary>();
        bool negated = negation && negation->op == AstExprUnary::Op::Not;
        AstExpr* tested = negated ? unparenthesized(negation->expr) : condition;
        if (!isContainer(tested))
            return;

        std::string name = describe(tested, "this table");
        if (negated)
            emitWarning(
                *context,
                LintWarning::Code_TableTruthiness,
                condition->location,
                "'not %s' is always false: a table is truthy even when it's empty; to check whether %s is empty, use 'next(%s) == nil'",
                shortExprName(tested).value_or("t").c_str(),
                name.c_str(),
                shortExprName(tested).value_or("t").c_str()
            );
        else
            emitWarning(
                *context,
                LintWarning::Code_TableTruthiness,
                condition->location,
                "%s is a table, which is truthy even when it's empty; to check whether it has entries, use 'next(%s) ~= nil' (or "
                "'#%s > 0' for an array)",
                name.c_str(),
                shortExprName(tested).value_or("t").c_str(),
                shortExprName(tested).value_or("t").c_str()
            );
    }

    bool visit(AstStatIf* node) override
    {
        // Luwu If Local (rfcs/if-local.md): each clause of a `when` chain is tested for truthiness, a binding's value
        // included
        if (node->clauses.size == 0)
            checkCondition(node->condition);

        for (const AstIfClause& clause : node->clauses)
            checkCondition(clause.expr);

        return true;
    }

    bool visit(AstStatWhile* node) override
    {
        checkCondition(node->condition);
        return true;
    }

    bool visit(AstStatRepeat* node) override
    {
        checkCondition(node->condition);
        return true;
    }

    // DiscardedResult: builtins that only compute a result, by library (empty for globals). Ones with an effect besides
    // their result (`math.random`, `table.freeze`, `buffer.write*`) aren't here.
    static bool isPureBuiltin(std::string_view library, std::string_view name)
    {
        static const std::unordered_set<std::string> kGlobals = {
            "tostring", "tonumber", "type", "typeof", "rawget", "rawequal", "rawlen", "select", "getmetatable", "next", "ipairs", "pairs", "unpack",
        };
        static const std::unordered_set<std::string> kString = {
            "byte", "char", "find", "format", "gmatch", "gsub", "len", "lower", "match", "rep", "reverse", "sub", "upper", "split",
            "pack", "packsize", "unpack",
        };
        static const std::unordered_set<std::string> kTable = {
            "clone", "concat", "create", "find", "pack", "unpack", "maxn", "getn", "isfrozen",
        };
        static const std::unordered_set<std::string> kMath = {
            "abs", "acos", "asin", "atan", "atan2", "ceil", "clamp", "cos", "cosh", "deg", "exp", "floor", "fmod", "frexp", "ldexp", "lerp",
            "log", "log10", "map", "max", "min", "modf", "noise", "pow", "rad", "round", "sign", "sin", "sinh", "sqrt", "tan", "tanh",
        };
        static const std::unordered_set<std::string> kBit32 = {
            "arshift", "band", "bnot", "bor", "btest", "bxor", "byteswap", "countlz", "countrz", "extract", "lrotate", "lshift", "replace",
            "rrotate", "rshift",
        };
        static const std::unordered_set<std::string> kUtf8 = {"char", "codes", "codepoint", "len", "offset"};
        static const std::unordered_set<std::string> kBuffer = {
            "create", "fromstring", "tostring", "len", "readi8", "readu8", "readi16", "readu16", "readi32", "readu32", "readf32", "readf64",
            "readstring", "readbits",
        };
        static const std::unordered_set<std::string> kOs = {"time", "clock", "date", "difftime"};

        const std::unordered_set<std::string>* names = nullptr;
        if (library.empty())
            names = &kGlobals;
        else if (library == "string")
            names = &kString;
        else if (library == "table")
            names = &kTable;
        else if (library == "math")
            names = &kMath;
        else if (library == "bit32")
            names = &kBit32;
        else if (library == "utf8")
            names = &kUtf8;
        else if (library == "buffer")
            names = &kBuffer;
        else if (library == "os")
            names = &kOs;

        return names && names->count(std::string(name)) > 0;
    }

    // `string.upper`, `tostring`, or `s:upper` on a string: which pure builtin a call is, if any
    bool callsPureBuiltin(AstExprCall* call) const
    {
        if (AstExprGlobal* global = call->func->as<AstExprGlobal>())
            return isPureBuiltin("", global->name.value);

        AstExprIndexName* function = call->func->as<AstExprIndexName>();
        if (!function)
            return false;

        if (call->self)
        {
            std::optional<TypeId> receiver = context->getType(function->expr);
            return receiver && isString(*receiver) && isPureBuiltin("string", function->index.value);
        }

        AstExprGlobal* library = function->expr->as<AstExprGlobal>();
        return library && isPureBuiltin(library->name.value, function->index.value);
    }

    bool visit(AstStatExpr* node) override
    {
        if (!enabled.discardedResult)
            return true;

        AstExprCall* call = node->expr->as<AstExprCall>();
        if (!call)
            return true;

        std::string callee = describe(call->func, "this function");

        // `const _ = ...` is how to discard a result on purpose
        std::string discard = "const _ = " + shortExprName(call->func).value_or("f") + "(...)";

        if (callsPureBuiltin(call))
        {
            // `s:upper()` on its own line: strings can't be changed in place
            AstExprIndexName* method = call->self ? call->func->as<AstExprIndexName>() : nullptr;
            std::optional<std::string> receiver = method ? shortExprName(unparenthesized(method->expr)) : std::nullopt;
            if (receiver)
                emitWarning(
                    *context,
                    LintWarning::Code_DiscardedResult,
                    call->location,
                    "%s returns a new string and doesn't change '%s', so calling it without using the result does nothing; did you "
                    "mean '%s = %s:%s(...)'? To discard the result on purpose, write '%s'",
                    callee.c_str(),
                    receiver->c_str(),
                    receiver->c_str(),
                    receiver->c_str(),
                    method->index.value,
                    discard.c_str()
                );
            else
                emitWarning(
                    *context,
                    LintWarning::Code_DiscardedResult,
                    call->location,
                    "%s only returns a result, so calling it without using the result does nothing; use the result, or write '%s' to "
                    "discard it on purpose",
                    callee.c_str(),
                    discard.c_str()
                );
            return true;
        }

        std::optional<TypeId> calleeType = context->getType(call->func);
        const FunctionType* function = calleeType ? get<FunctionType>(follow(*calleeType)) : nullptr;
        if (!function || !function->isNodiscard)
            return true;

        std::string reason = function->nodiscardReason.empty() ? std::string() : ": " + function->nodiscardReason;
        emitWarning(
            *context,
            LintWarning::Code_DiscardedResult,
            call->location,
            "The result of %s shouldn't be discarded%s; to discard it on purpose, write '%s'",
            callee.c_str(),
            reason.c_str(),
            discard.c_str()
        );

        return true;
    }
};

// Luwu: `local` declarations that could be `const`: every variable they declare is never reassigned. Off by default, since
// it's about style -- the compiler already knows which locals are never reassigned -- and it would flag most existing
// code; a module turns it on with `--!lint ConstLocal`, a codebase in its config.
class LintConstLocal : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintConstLocal pass;
        pass.context = &context;

        AssignmentCollector assignments{&pass.assigned};
        context.root->visit(&assignments);

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    DenseHashSet<AstLocal*> assigned{nullptr};

    struct AssignmentCollector : AstVisitor
    {
        DenseHashSet<AstLocal*>* assigned;

        explicit AssignmentCollector(DenseHashSet<AstLocal*>* assigned)
            : assigned(assigned)
        {
        }

        void write(AstExpr* target)
        {
            if (AstExprLocal* local = unparenthesized(target)->as<AstExprLocal>())
                assigned->insert(local->local);
        }

        bool visit(AstStatAssign* node) override
        {
            for (AstExpr* var : node->vars)
                write(var);
            return true;
        }

        bool visit(AstStatCompoundAssign* node) override
        {
            write(node->var);
            return true;
        }
    };

    bool visit(AstStatLocal* node) override
    {
        // A destructuring declaration desugars to several locals, and one without a value can't be `const`
        bool candidate = !node->isConst && node->values.size > 0 && !node->destructure && !node->destructuredFrom;
        if (!candidate)
            return true;

        std::string names;
        for (size_t i = 0; i < node->vars.size; ++i)
        {
            if (assigned.contains(node->vars.data[i]))
                return true;

            if (i > 0)
                names += i + 1 == node->vars.size ? " and " : ", ";
            names += "'" + std::string(node->vars.data[i]->name.value) + "'";
        }

        const char* verb = node->vars.size == 1 ? "is" : "are";
        emitWarning(
            *context,
            LintWarning::Code_ConstLocal,
            node->vars.data[0]->location,
            "%s %s immutable and can be marked 'const'",
            names.c_str(),
            verb
        );
        return true;
    }

    bool visit(AstStatLocalFunction* node) override
    {
        if (node->isConst || assigned.contains(node->name))
            return true;

        emitWarning(
            *context,
            LintWarning::Code_ConstLocal,
            node->name->location,
            "'%s' should be a 'const function'",
            node->name->name.value
        );
        return true;
    }
};

// Luwu: `xs[#xs / 2]`. `/` always gives a float, so an index computed with it reads `nil` whenever the result isn't
// whole, which for `#xs / 2` is every odd length.
class LintFloatIndex : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintFloatIndex pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    // The `/` that makes `expr` a float: `expr` itself, or one inside the arithmetic around it (`#xs / 2 + 1`). A call
    // in between (`math.floor(n / 2)`) makes its own value, so it isn't looked into.
    static AstExprBinary* findDivision(AstExpr* expr)
    {
        expr = unparenthesized(expr);

        if (AstExprBinary* binary = expr->as<AstExprBinary>())
        {
            if (binary->op == AstExprBinary::Div)
                return binary;

            bool keepsFraction = binary->op == AstExprBinary::Add || binary->op == AstExprBinary::Sub || binary->op == AstExprBinary::Mul;
            if (!keepsFraction)
                return nullptr;

            if (AstExprBinary* division = findDivision(binary->left))
                return division;
            return findDivision(binary->right);
        }

        if (AstExprUnary* unary = expr->as<AstExprUnary>(); unary && unary->op == AstExprUnary::Op::Minus)
            return findDivision(unary->expr);

        return nullptr;
    }

    bool visit(AstExprIndexExpr* node) override
    {
        if (AstExprBinary* division = findDivision(node->index))
            emitWarning(
                *context,
                LintWarning::Code_FloatIndex,
                division->location,
                "'/' always gives a float, so this index reads nil whenever the result isn't whole; use '//' to divide to an "
                "integer"
            );

        return true;
    }
};

// Luwu: a Luwu-only library function read in a Luau or Lua module (its config's `language`, from a `.luau` or `.lua`
// file). Luwu code goes in `.luwu` files.
class LintLuwuOnlyApi : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        if (!context.module || context.module->language == Language::Luwu)
            return;

        LintLuwuOnlyApi pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    // Members Luwu adds to libraries upstream Luau also has. Upstream's `class` library has `isinstance` too, and
    // `classof` where Luwu has `of`.
    static bool isLuwuOnlyMember(std::string_view library, std::string_view member)
    {
        if (library == "class")
        {
            static const char* const kLuwuClassMembers[] = {"of", "name", "implements", "fields"};
            for (const char* luwuMember : kLuwuClassMembers)
            {
                if (member == luwuMember)
                    return true;
            }
            return false;
        }
        if (library == "table")
            return member == "drop";
        if (library == "buffer")
            return member == "isfrozen";
        return false;
    }

    bool visit(AstExprIndexName* node) override
    {
        // A local named `table` is the reader's own, so only the global library counts.
        AstExprGlobal* global = node->expr->as<AstExprGlobal>();
        if (!global || !isLuwuOnlyMember(global->name.value, node->index.value))
            return true;

        std::string qualified = std::string(global->name.value) + "." + node->index.value;
        const char* language = context->module->language == Language::Lua ? "Lua" : "Luau";
        emitWarning(
            *context,
            LintWarning::Code_LuwuOnlyApi,
            node->location,
            "'%s' only exists in Luwu, but this is a %s file; rename it to '.luwu' if it's Luwu code",
            qualified.c_str(),
            language
        );
        return true;
    }
};

// Luwu: `pairs` and `ipairs`, which generalized iteration (`for k, v in t`) replaces. Each is its own lint so a module
// can keep using one without turning off DeprecatedApi or the other.
class LintLuaIterators : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintLuaIterators pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    bool visit(AstExprGlobal* node) override
    {
        if (node->name == "pairs")
            emitWarning(
                *context,
                LintWarning::Code_Pairs,
                node->location,
                "'pairs' is deprecated; use 'for key, value in t do' directly.\n"
                "\n"
                "Help (pairs):\n"
                "  - In Luau and Luwu you can directly iterate over tables, objects, or extern types (generalized iteration)\n"
                "  - Unlike 'pairs', generalized iteration uses the value's '__iter' metamethod when it has one\n"
                "  - If you want to skip '__iter' or '__call', add '@[nolint(Pairs)]' or '--!nolint LuaIterators' to silence"
            );
        else if (node->name == "ipairs")
            emitWarning(
                *context,
                LintWarning::Code_Ipairs,
                node->location,
                "'ipairs' is deprecated; use 'for index, value in t do' directly.\n"
                "\n"
                "Help (ipairs):\n"
                "  - In Luau and Luwu you can directly iterate over tables, objects, or extern types (generalized iteration)\n"
                "  - Unlike 'ipairs', generalized iteration doesn't stop at the first 'nil'\n"
                "  - If you want to stop at the first nil, add '@[nolint(Ipairs)]' or '--!nolint LuaIterators' to silence"
            );

        return true;
    }
};

// Luwu: `x = x` and `t.x = t.x`, which do nothing; usually a typo for another name
class LintSelfAssignment : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintSelfAssignment pass;
        pass.context = &context;
        pass.strict = context.module && context.module->mode == Mode::Strict;

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    // Assigning a field to itself can be meant to run its `__newindex`, so only strict reports it.
    // TODO(strictness presets): the middle preset should report it too (STRICTNESS.CLAUDE.md, S13).
    bool strict = false;

    bool visit(AstStatAssign* node) override
    {
        size_t pairs = std::min(node->vars.size, node->values.size);
        for (size_t i = 0; i < pairs; ++i)
        {
            AstExpr* var = node->vars.data[i];
            if (!sameTarget(var, node->values.data[i]))
                continue;

            AstExpr* target = unparenthesized(var);
            bool isName = target->is<AstExprLocal>() || target->is<AstExprGlobal>();
            if (!isName && !strict)
                continue;

            emitWarning(
                *context,
                LintWarning::Code_SelfAssignment,
                Location(var->location, node->values.data[i]->location),
                "Assigning %s to itself does nothing; did you mean to assign something else?",
                describe(var, "this").c_str()
            );
        }

        return true;
    }
};

// Luwu Traits (rfcs/classes/traits.md): a trait method returning `self`, or a new object of self's class
// (`class.of(self)(...)`), with the trait as its return type. That type forgets self's class and its other traits;
// `Self` keeps them. Returning `self` is right; the return type is what to fix, so that is where it is reported. With
// the return type left out, a method returning `self` already returns `Self`.
class LintReturnSelf : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintReturnSelf pass;
        pass.context = &context;

        context.root->visit(&pass);
    }

private:
    LintContext* context;

    // The return type annotation of a method that says it returns the trait
    static std::optional<Location> traitReturnType(const AstClassMethod& method, AstName trait)
    {
        AstExprFunction* fn = method.function;
        if (!fn->returnAnnotation)
            return std::nullopt;

        AstTypePackExplicit* pack = fn->returnAnnotation->as<AstTypePackExplicit>();
        if (!pack || pack->typeList.types.size != 1 || pack->typeList.tailType)
            return std::nullopt;

        AstTypeReference* ref = pack->typeList.types.data[0]->as<AstTypeReference>();
        if (!ref || ref->prefix || ref->name != trait)
            return std::nullopt;

        return ref->location;
    }

    bool visit(AstStatClass* cls) override
    {
        if (!cls->isTrait)
            return true;

        for (const AstClassMember& member : cls->members)
        {
            const AstClassMethod* method = member.get_if<AstClassMethod>();
            AstExprFunction* fn = method ? method->function : nullptr;
            if (!fn || method->expectLocation)
                continue;

            std::optional<Location> returnType = traitReturnType(*method, cls->name->name);
            if (returnType && returnsSelf(fn, methodSelf(fn)))
                emitWarning(
                    *context,
                    LintWarning::Code_ReturnSelf,
                    *returnType,
                    "Did you mean to return 'Self' here?\n\n"
                    "Help (method returns trait instead of Self):\n"
                    "  - Returning '%s' here loses self's class and any other traits on it\n"
                    "  - Callers rely on knowing self's class to pass into other functions\n"
                    "  - Return 'Self' here so the type checker knows to use the class type instead of the trait type",
                    cls->name->name.value
                );
        }

        return true;
    }
};

// Luwu: a computed value stored in a local and overwritten, in the same block, before anything reads it. The work that
// produced it was for nothing, which usually means a use of it was forgotten. Only computed values count (calls,
// arithmetic, ...): `local x = 0` then `x = f()` is just a default.
class LintDeadStore : AstVisitor
{
public:
    LUAU_NOINLINE static void process(LintContext& context)
    {
        LintDeadStore pass;
        pass.context = &context;

        // A local some closure uses can be read whenever that closure runs, which a block-by-block look can't see
        CaptureFinder captures{&pass.captured};
        context.root->visit(&captures);

        context.root->visit(&pass);
    }

private:
    LintContext* context;
    DenseHashSet<AstLocal*> captured{nullptr};

    struct CaptureFinder : AstVisitor
    {
        DenseHashSet<AstLocal*>* captured;

        explicit CaptureFinder(DenseHashSet<AstLocal*>* captured)
            : captured(captured)
        {
        }

        bool visit(AstExprLocal* node) override
        {
            if (node->upvalue)
                captured->insert(node->local);
            return true;
        }
    };

    // The locals a statement reads, and whether it can leave the block early (so what follows it might not run)
    struct Effects : AstVisitor
    {
        DenseHashSet<AstLocal*> reads{nullptr};
        bool leaves = false;
        int loopDepth = 0;
        int functionDepth = 0;

        bool visit(AstExprLocal* node) override
        {
            reads.insert(node->local);
            return true;
        }

        // Writing a local isn't reading it
        bool visit(AstStatAssign* node) override
        {
            for (AstExpr* var : node->vars)
                if (!unparenthesized(var)->is<AstExprLocal>())
                    var->visit(this);
            for (AstExpr* value : node->values)
                value->visit(this);
            return false;
        }

        bool visit(AstExprFunction* node) override
        {
            ++functionDepth;
            node->body->visit(this);
            --functionDepth;
            return false;
        }

        bool visit(AstStatReturn* node) override
        {
            leaves |= functionDepth == 0;
            return true;
        }

        bool visit(AstStatBreak* node) override
        {
            leaves |= functionDepth == 0 && loopDepth == 0;
            return true;
        }

        bool visit(AstStatContinue* node) override
        {
            leaves |= functionDepth == 0 && loopDepth == 0;
            return true;
        }

        // A `break` or `continue` inside a nested loop leaves that loop, not the block being looked at
        bool visit(AstStatWhile* node) override
        {
            node->condition->visit(this);
            ++loopDepth;
            node->body->visit(this);
            --loopDepth;
            return false;
        }

        bool visit(AstStatRepeat* node) override
        {
            ++loopDepth;
            node->body->visit(this);
            --loopDepth;
            node->condition->visit(this);
            return false;
        }

        bool visit(AstStatFor* node) override
        {
            node->from->visit(this);
            node->to->visit(this);
            if (node->step)
                node->step->visit(this);
            ++loopDepth;
            node->body->visit(this);
            --loopDepth;
            return false;
        }

        bool visit(AstStatForIn* node) override
        {
            for (AstExpr* value : node->values)
                value->visit(this);
            ++loopDepth;
            node->body->visit(this);
            --loopDepth;
            return false;
        }
    };

    struct Store
    {
        AstLocal* local;
        Location location;
    };

    static bool isComputed(AstExpr* value)
    {
        value = unparenthesized(value);
        return value->is<AstExprCall>() || value->is<AstExprBinary>() || value->is<AstExprUnary>() || value->is<AstExprIfElse>() ||
               value->is<AstExprInterpString>();
    }

    static void forgetRead(std::vector<Store>& pending, const Effects& effects)
    {
        if (effects.leaves)
        {
            pending.clear();
            return;
        }

        pending.erase(
            std::remove_if(
                pending.begin(),
                pending.end(),
                [&](const Store& store)
                {
                    return effects.reads.contains(store.local);
                }
            ),
            pending.end()
        );
    }

    void remember(std::vector<Store>& pending, AstLocal* local, AstExpr* value, const Location& location)
    {
        if (isComputed(value) && !captured.contains(local))
            pending.push_back(Store{local, location});
    }

    bool visit(AstStatBlock* block) override
    {
        std::vector<Store> pending;

        for (AstStat* stat : block->body)
        {
            Effects effects;

            if (AstStatLocal* local = stat->as<AstStatLocal>())
            {
                for (AstExpr* value : local->values)
                    value->visit(&effects);
                forgetRead(pending, effects);

                if (local->vars.size == 1 && local->values.size == 1)
                    remember(pending, local->vars.data[0], local->values.data[0], local->vars.data[0]->location);
                continue;
            }

            AstStatAssign* assign = stat->as<AstStatAssign>();
            if (!assign)
            {
                stat->visit(&effects);
                forgetRead(pending, effects);
                continue;
            }

            // The values and any table being indexed are read before the locals are written
            assign->visit(&effects);
            forgetRead(pending, effects);

            for (size_t i = 0; i < assign->vars.size; ++i)
            {
                AstExprLocal* target = unparenthesized(assign->vars.data[i])->as<AstExprLocal>();
                if (!target)
                    continue;

                auto overwritten = std::find_if(
                    pending.begin(),
                    pending.end(),
                    [&](const Store& store)
                    {
                        return store.local == target->local;
                    }
                );

                if (overwritten != pending.end())
                {
                    emitWarning(
                        *context,
                        LintWarning::Code_DeadStore,
                        overwritten->location,
                        "The value stored in '%s' here is never read: line %d overwrites it first; did you forget to use it?",
                        target->local->name.value,
                        assign->location.begin.line + 1
                    );
                    pending.erase(overwritten);
                }

                if (assign->vars.size == assign->values.size)
                    remember(pending, target->local, assign->values.data[i], assign->vars.data[i]->location);
            }
        }

        return true;
    }
};

// Luwu: `--!nolint` with no lint names, the way LintWarning::parseMask recognizes it
static bool isBareNolint(const HotComment& hc)
{
    return hc.header && hc.content.compare(0, 6, "nolint") == 0 && hc.content.find_first_not_of(" \t", 6) == std::string::npos;
}

// Luwu: the group a lint is part of, or Code_Unknown. A part follows its group: it's on only where the group is on too,
// so turning the group off turns all its parts off, and each part can still be turned off alone.
static LintWarning::Code lintGroupOf(LintWarning::Code code)
{
    switch (code)
    {
    case LintWarning::Code_LoopConcat:
    case LintWarning::Code_InefficientTableInsert:
    case LintWarning::Code_InefficientTableRemove:
    case LintWarning::Code_MethodsNotInlined:
    case LintWarning::Code_FloorDivision:
    case LintWarning::Code_FenvDeoptimization:
        return LintWarning::Code_OptimizationHint;
    case LintWarning::Code_Pairs:
    case LintWarning::Code_Ipairs:
        return LintWarning::Code_LuaIterators;
    default:
        return LintWarning::Code_Unknown;
    }
}

static bool lintEnabledIn(const LintMask& mask, LintWarning::Code code)
{
    LintWarning::Code group = lintGroupOf(code);
    return mask.test(code) && (group == LintWarning::Code_Unknown || mask.test(group));
}

// Luwu: `@[nolint(...)]` or `@[lint(...)]` on a function, class or class field: the lints it turns off or on within
// `range`. Inner scopes apply after the ones around them.
struct LintScope
{
    Location range;
    LintMask enable;
    LintMask disable;
};

class LintScopeCollector : AstVisitor
{
public:
    // Also reports names that aren't lints, as CommentDirective does for `--!nolint`
    static std::vector<LintScope> collect(LintContext& context)
    {
        LintScopeCollector collector;
        collector.context = &context;
        context.root->visit(&collector);
        return std::move(collector.scopes);
    }

private:
    LintContext* context;
    std::vector<LintScope> scopes;
    // Functions whose attributes a statement form already took, with the statement's wider range
    DenseHashSet<AstExprFunction*> handled{nullptr};

    static std::optional<std::string> argumentName(AstExpr* arg)
    {
        if (AstExprGlobal* global = arg->as<AstExprGlobal>())
            return std::string(global->name.value);
        if (AstExprLocal* local = arg->as<AstExprLocal>())
            return std::string(local->local->name.value);
        if (AstExprConstantString* string = arg->as<AstExprConstantString>())
            return std::string(string->value.data, string->value.size);
        return std::nullopt;
    }

    LintMask maskOf(AstAttr* attr)
    {
        // Without names, every lint but BareNolint, which asks whether that was meant (`@[nolint]`; the parser makes
        // `@[lint]` name them)
        if (attr->args.size == 0)
        {
            if (context->warningEnabled(LintWarning::Code_BareNolint))
                emitWarning(
                    *context,
                    LintWarning::Code_BareNolint,
                    attr->location,
                    "'@nolint' without lint names turns off every lint in here; did you forget to specify lints? Name them "
                    "('@[nolint(LocalUnused)]'), or write '@[nolint(All)]' to turn them all off on purpose"
                );

            return LintMask().set().reset(LintWarning::Code_BareNolint);
        }

        LintMask mask;
        for (AstExpr* arg : attr->args)
        {
            std::optional<std::string> name = argumentName(arg);
            if (!name)
                continue;

            // `@[lint(All)]` would also turn on the lints the type checking mode turns off because the checker already
            // reports them (UnknownGlobal in strict mode), which is why `@[lint]` has to name what it turns on
            if (LintWarning::isAllName(name->c_str()))
            {
                if (attr->type == AstAttr::Type::Nolint)
                    mask.set();
                else if (context->warningEnabled(LintWarning::Code_CommentDirective))
                    emitWarning(
                        *context,
                        LintWarning::Code_CommentDirective,
                        arg->location,
                        "'All' can only turn lints off; name the lints to turn on, like '@[lint(LocalUnused)]'"
                    );
                continue;
            }

            LintWarning::Code code = LintWarning::parseName(name->c_str());
            if (code != LintWarning::Code_Unknown)
            {
                mask.set(code);
                continue;
            }

            if (!context->warningEnabled(LintWarning::Code_CommentDirective))
                continue;

            if (const char* suggestion = fuzzyMatch(*name, kWarningNames + 1, LintWarning::Code__Count - 1))
                emitWarning(
                    *context,
                    LintWarning::Code_CommentDirective,
                    arg->location,
                    "%s attribute refers to unknown lint rule '%s'; did you mean '%s'?",
                    attr->name.value,
                    name->c_str(),
                    suggestion
                );
            else
                emitWarning(
                    *context,
                    LintWarning::Code_CommentDirective,
                    arg->location,
                    "%s attribute refers to unknown lint rule '%s'",
                    attr->name.value,
                    name->c_str()
                );
        }
        return mask;
    }

    void addScopes(const AstArray<AstAttr*>& attributes, const Location& range, const AstArray<AstLocal*>* bindings = nullptr)
    {
        for (AstAttr* attr : attributes)
        {
            if (attr->type == AstAttr::Type::Nolint)
            {
                LintMask mask = maskOf(attr);
                scopes.push_back(LintScope{range, LintMask(), mask});

                if (bindings)
                    for (AstLocal* local : *bindings)
                        context->bindingNolints[local] |= mask;
            }
            else if (attr->type == AstAttr::Type::Lint)
                scopes.push_back(LintScope{range, maskOf(attr), LintMask()});
        }
    }

    // A `local` or `const`: the declaration itself, and the bindings wherever a lint reports their use (see
    // LintContext::bindingNolints)
    bool visit(AstStatLocal* node) override
    {
        addScopes(node->attributes, node->location, &node->vars);
        return true;
    }

    bool visit(AstExprFunction* node) override
    {
        if (!handled.contains(node))
            addScopes(node->attributes, node->location);
        return true;
    }

    // The statement forms also cover the function's name, where warnings about the function itself point
    bool visit(AstStatFunction* node) override
    {
        addScopes(node->func->attributes, node->location);
        handled.insert(node->func);
        return true;
    }

    bool visit(AstStatLocalFunction* node) override
    {
        addScopes(node->func->attributes, node->location);
        handled.insert(node->func);
        return true;
    }

    bool visit(AstStatClass* node) override
    {
        addScopes(node->attributes, node->location);

        for (const AstClassMember& member : node->members)
        {
            const AstClassProperty* field = member.get_if<AstClassProperty>();
            if (!field)
                continue;

            Location range = field->nameLocation;
            if (field->defaultValue)
                range = Location(range, field->defaultValue->location);
            addScopes(field->attributes, range);
        }

        return true;
    }
};

// Drops the warnings a scope turned off, and the ones a module-level setting turned off that no scope turned back on.
// `moduleMask` is what the module's options allow outside every scope.
static void filterByLintScopes(std::vector<LintWarning>& warnings, std::vector<LintScope> scopes, const LintMask& moduleMask)
{
    // Scopes nest, so the ones around a position start no later than the ones inside it
    std::sort(
        scopes.begin(),
        scopes.end(),
        [](const LintScope& a, const LintScope& b)
        {
            if (a.range.begin != b.range.begin)
                return a.range.begin < b.range.begin;
            return b.range.end < a.range.end;
        }
    );

    auto disabledAt = [&](const LintWarning& warning)
    {
        LintMask mask = moduleMask;
        for (const LintScope& scope : scopes)
        {
            if (scope.range.encloses(warning.location))
                mask = (mask | scope.enable) & ~scope.disable;
        }
        return !lintEnabledIn(mask, warning.code);
    };

    warnings.erase(std::remove_if(warnings.begin(), warnings.end(), disabledAt), warnings.end());
}

std::vector<LintWarning> lint(
    AstStat* root,
    const AstNameTable& names,
    const ScopePtr& env,
    const Module* module,
    const std::vector<HotComment>& hotcomments,
    const LintOptions& options
)
{
    LintContext context;

    context.options = options;
    context.root = root;
    context.placeholder = names.get("_");
    context.deprecatedAttribute = names.get("deprecated");
    context.scope = env;
    context.module = module;

    fillBuiltinGlobals(context, names, env);

    // Luwu: a bare `--!nolint` turns off every lint but this one (see LintWarning::parseMask)
    if (context.warningEnabled(LintWarning::Code_BareNolint))
    {
        for (const HotComment& hc : hotcomments)
        {
            if (isBareNolint(hc))
                emitWarning(
                    context,
                    LintWarning::Code_BareNolint,
                    hc.location,
                    "'--!nolint' without lint names turns off every lint; did you forget to specify lints? Name them "
                    "('--!nolint LocalUnused'), or write '--!nolint All' to turn them all off on purpose"
                );
        }
    }

    // Luwu: a lint a scope turns on has to run even where the module turns it off; filterByLintScopes then drops what
    // isn't on where it was reported
    std::vector<LintScope> scopes = LintScopeCollector::collect(context);
    for (const LintScope& scope : scopes)
        context.options.warningMask |= scope.enable;

    // Luwu: these lints suggest Luau replacements for Lua idioms, which a Lua module can't use
    if (module && module->language == Language::Lua)
    {
        context.options.warningMask.reset(LintWarning::Code_LuaIterators);
        context.options.warningMask.reset(LintWarning::Code_Pairs);
        context.options.warningMask.reset(LintWarning::Code_Ipairs);
        context.options.warningMask.reset(LintWarning::Code_LuaAndOr);
    }

    if (context.warningEnabled(LintWarning::Code_UnknownGlobal) || context.warningEnabled(LintWarning::Code_DeprecatedGlobal) ||
        context.warningEnabled(LintWarning::Code_GlobalUsedAsLocal) || context.warningEnabled(LintWarning::Code_PlaceholderRead) ||
        context.warningEnabled(LintWarning::Code_BuiltinGlobalWrite))
    {
        LintGlobalLocal::process(context);
    }

    if (context.warningEnabled(LintWarning::Code_MultiLineStatement))
        LintMultiLineStatement::process(context);

    if (context.warningEnabled(LintWarning::Code_SameLineStatement))
        LintSameLineStatement::process(context);

    if (context.warningEnabled(LintWarning::Code_LocalShadow) || context.warningEnabled(LintWarning::Code_FunctionUnused) ||
        context.warningEnabled(LintWarning::Code_ImportUnused) || context.warningEnabled(LintWarning::Code_LocalUnused))
    {
        LintLocalHygiene::process(context);
    }

    if (context.warningEnabled(LintWarning::Code_FunctionUnused))
        LintUnusedFunction::process(context);

    if (context.warningEnabled(LintWarning::Code_UnreachableCode))
        LintUnreachableCode::process(context);

    if (context.warningEnabled(LintWarning::Code_UnknownType))
        LintUnknownType::process(context);

    if (context.warningEnabled(LintWarning::Code_ForRange))
        LintForRange::process(context);

    if (context.warningEnabled(LintWarning::Code_UnbalancedAssignment))
        LintUnbalancedAssignment::process(context);

    if (context.warningEnabled(LintWarning::Code_ImplicitReturn))
        LintImplicitReturn::process(context);

    if (context.warningEnabled(LintWarning::Code_FormatString))
        LintFormatString::process(context);

    if (context.warningEnabled(LintWarning::Code_TableLiteral))
        LintTableLiteral::process(context);

    if (context.warningEnabled(LintWarning::Code_UninitializedLocal))
        LintUninitializedLocal::process(context);

    if (context.warningEnabled(LintWarning::Code_DuplicateFunction))
        LintDuplicateFunction::process(context);

    if (context.warningEnabled(LintWarning::Code_DeprecatedApi))
        LintDeprecatedApi::process(context);

    if (context.warningEnabled(LintWarning::Code_TableOperations))
        LintTableOperations::process(context);

    if (context.warningEnabled(LintWarning::Code_DuplicateCondition))
        LintDuplicateCondition::process(context);

    if (context.warningEnabled(LintWarning::Code_DuplicateLocal))
        LintDuplicateLocal::process(context);

    if (context.warningEnabled(LintWarning::Code_MisleadingAndOr) || context.warningEnabled(LintWarning::Code_LuaAndOr))
        LintMisleadingAndOr::process(context);

    if (context.warningEnabled(LintWarning::Code_CommentDirective))
        lintComments(context, hotcomments);

    if (context.warningEnabled(LintWarning::Code_IntegerParsing))
        LintIntegerParsing::process(context);

    if (context.warningEnabled(LintWarning::Code_ComparisonPrecedence))
        LintComparisonPrecedence::process(context);

    LintNameShadow::Enabled nameShadow;
    nameShadow.keyword = context.warningEnabled(LintWarning::Code_KeywordShadow);
    nameShadow.builtin = context.warningEnabled(LintWarning::Code_BuiltinShadow);
    if (nameShadow.keyword || nameShadow.builtin)
        LintNameShadow::process(context, nameShadow);

    LintDoExpressions::Enabled doExpressions;
    doExpressions.returnOnNextLine = context.warningEnabled(LintWarning::Code_ReturnOnNextLine);
    doExpressions.orContinue = context.warningEnabled(LintWarning::Code_OrContinue);
    if (FFlag::DebugLuwuDoExpr && (doExpressions.returnOnNextLine || doExpressions.orContinue))
        LintDoExpressions::process(context, doExpressions);

    if (FFlag::LuwuNonePrimitive && context.warningEnabled(LintWarning::Code_NilNoneComparison))
        LintNilNoneComparison::process(context);

    // Luwu: deliberately unflagged. A cast truncating a call's values is a footgun in plain Luau code too.
    if (context.warningEnabled(LintWarning::Code_VarargCast))
        LintVarargCast::process(context);

    if (context.warningEnabled(LintWarning::Code_DeclareMismatch))
        LintDeclareMismatch::process(context);

    LintLoopMistakes::Enabled loopMistakes;
    loopMistakes.removeWhileIterating = context.warningEnabled(LintWarning::Code_RemoveWhileIterating);
    loopMistakes.loopVariableWrite = context.warningEnabled(LintWarning::Code_LoopVariableWrite);
    loopMistakes.iteratedTableWrite = context.warningEnabled(LintWarning::Code_IteratedTableWrite);
    loopMistakes.foreverLoop = context.warningEnabled(LintWarning::Code_ForeverLoop);
    loopMistakes.uselessLoop = context.warningEnabled(LintWarning::Code_UselessLoop);
    loopMistakes.stringIndexZero = context.warningEnabled(LintWarning::Code_StringIndexZero);
    if (context.warningEnabled(LintWarning::Code_ConstLocal))
        LintConstLocal::process(context);

    if (context.warningEnabled(LintWarning::Code_FloatIndex))
        LintFloatIndex::process(context);

    if (context.warningEnabled(LintWarning::Code_LuwuOnlyApi))
        LintLuwuOnlyApi::process(context);

    // Luwu: turning LuaIterators off turns off its parts
    if (context.warningEnabled(LintWarning::Code_LuaIterators) &&
        (context.warningEnabled(LintWarning::Code_Pairs) || context.warningEnabled(LintWarning::Code_Ipairs)))
        LintLuaIterators::process(context);

    if (context.warningEnabled(LintWarning::Code_SelfAssignment))
        LintSelfAssignment::process(context);

    if (context.warningEnabled(LintWarning::Code_DeadStore))
        LintDeadStore::process(context);

    if (context.warningEnabled(LintWarning::Code_ReturnSelf))
        LintReturnSelf::process(context);

    LintValueMistakes::Enabled valueMistakes;
    valueMistakes.newValueComparison = context.warningEnabled(LintWarning::Code_NewValueComparison);
    valueMistakes.tableTruthiness = context.warningEnabled(LintWarning::Code_TableTruthiness);
    valueMistakes.discardedResult = context.warningEnabled(LintWarning::Code_DiscardedResult);
    if (valueMistakes.newValueComparison || valueMistakes.tableTruthiness || valueMistakes.discardedResult)
        LintValueMistakes::process(context, valueMistakes);

    bool anyLoopMistake = loopMistakes.removeWhileIterating || loopMistakes.loopVariableWrite || loopMistakes.iteratedTableWrite ||
                          loopMistakes.foreverLoop || loopMistakes.uselessLoop || loopMistakes.stringIndexZero;
    if (anyLoopMistake)
        LintLoopMistakes::process(context, loopMistakes);

    // Luwu: turning OptimizationHint off also turns off its parts, which have codes of their own so each can be turned
    // off alone
    if (context.warningEnabled(LintWarning::Code_OptimizationHint))
    {
        // Luwu Classes (rfcs/classes): a trusted file (the flag on and `--!trust`) already acts on the annotations
        bool trustsAnnotations = FFlag::DebugLuwuCompilerTrustsTypeAnnotations && hasHeaderCommentDirective(hotcomments, "trust");
        bool methodsNotInlined = context.warningEnabled(LintWarning::Code_MethodsNotInlined);
        if (methodsNotInlined || context.warningEnabled(LintWarning::Code_FloorDivision) ||
            context.warningEnabled(LintWarning::Code_FenvDeoptimization))
            LintOptimizationHint::process(context, methodsNotInlined && FFlag::LuwuClasses && !trustsAnnotations);

        bool concat = context.warningEnabled(LintWarning::Code_LoopConcat);
        bool tableInsert = context.warningEnabled(LintWarning::Code_InefficientTableInsert);
        bool tableRemove = context.warningEnabled(LintWarning::Code_InefficientTableRemove);
        if (concat || tableInsert || tableRemove)
            LintLoopHints::process(context, concat, tableInsert, tableRemove);
    }

    if (context.warningEnabled(LintWarning::Code_RedundantNativeAttribute))
    {
        if (hasNativeCommentDirective(hotcomments))
            LintRedundantNativeAttribute::process(context);
    }

    if (!scopes.empty())
        filterByLintScopes(context.result, std::move(scopes), options.warningMask);

    std::sort(context.result.begin(), context.result.end(), WarningComparator());

    return context.result;
}

std::vector<AstName> getDeprecatedGlobals(const AstNameTable& names)
{
    LintContext context;

    std::vector<AstName> result;
    result.reserve(context.builtinGlobals.size());

    for (auto& p : context.builtinGlobals)
        if (p.second.deprecated)
            result.push_back(p.first);

    return result;
}

void fuzzFormatString(const char* data, size_t size)
{
    LintFormatString::fuzz(data, size);
}

} // namespace Luau
