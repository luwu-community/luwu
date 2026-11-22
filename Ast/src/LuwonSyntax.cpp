// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/LuwonSyntax.h"

#include <string>
#include <string.h>

namespace Luau
{

static const char* kOnlyConstructorsMessage = "Only 'vector.create', 'buffer.fromstring' and classes can be called in a data file";

// The name of the global or local `expr` reads, or "" for any other expression.
static const char* nameOf(const AstExpr* expr)
{
    if (const AstExprGlobal* global = expr->as<AstExprGlobal>())
        return global->name.value;
    if (const AstExprLocal* local = expr->as<AstExprLocal>())
        return local->local->name.value;
    return "";
}

// Whether `expr` reads a global or a local called `name`.
static bool isNamed(const AstExpr* expr, const char* name)
{
    return strcmp(nameOf(expr), name) == 0;
}

LuwonConstructor luwonConstructorOf(const AstExprCall* call)
{
    if (call->self)
        return LuwonConstructor::None;

    const AstExprGlobal* global = call->func->as<AstExprGlobal>();
    if (global && global->name != kParseNameError)
        return LuwonConstructor::Class;

    const AstExprIndexName* index = call->func->as<AstExprIndexName>();
    if (!index)
        return LuwonConstructor::None;

    if (isNamed(index->expr, "vector") && index->index == "create")
        return LuwonConstructor::VectorCreate;
    if (isNamed(index->expr, "buffer") && index->index == "fromstring")
        return LuwonConstructor::BufferFromString;

    for (const LuwonBuiltinClass& builtin : luwonBuiltinClasses())
    {
        if (isNamed(index->expr, builtin.library) && index->index == builtin.name)
            return LuwonConstructor::Class;
    }

    return LuwonConstructor::None;
}

std::vector<LuwonBuiltinClass> luwonBuiltinClasses()
{
    return {};
}

std::string luwonClassNameOf(const AstExprCall* call)
{
    if (const AstExprGlobal* global = call->func->as<AstExprGlobal>())
        return global->name.value;

    // A builtin class, `library.name`
    const AstExprIndexName* index = call->func->as<AstExprIndexName>();
    LUAU_ASSERT(index);
    return std::string(nameOf(index->expr)) + "." + index->index.value;
}

namespace
{

struct LuwonSyntaxChecker
{
    std::vector<ParseError>& errors;

    void report(const Location& location, std::string message)
    {
        errors.emplace_back(location, std::move(message));
    }

    void checkExpr(AstExpr* expr)
    {
        if (expr->is<AstExprConstantNil>() || expr->is<AstExprConstantBool>() || expr->is<AstExprConstantNumber>() ||
            expr->is<AstExprConstantString>() || expr->is<AstExprLocal>() || expr->is<AstExprError>())
            return;

        if (expr->is<AstExprConstantInteger>())
            report(expr->location, "Luau's 'i' integer suffix isn't allowed in a data file");
        else if (AstExprGlobal* global = expr->as<AstExprGlobal>())
            checkGlobal(global);
        else if (AstExprGroup* group = expr->as<AstExprGroup>())
            checkExpr(group->expr);
        else if (AstExprTypeAssertion* assertion = expr->as<AstExprTypeAssertion>())
            checkExpr(assertion->expr);
        else if (AstExprTable* table = expr->as<AstExprTable>())
            checkTable(table);
        else if (AstExprCall* call = expr->as<AstExprCall>())
            checkCall(call);
        else if (AstExprIndexName* index = expr->as<AstExprIndexName>())
            checkIndexName(index);
        else if (AstExprIndexExpr* index = expr->as<AstExprIndexExpr>())
        {
            checkExpr(index->expr);
            checkExpr(index->index);
        }
        else if (AstExprUnary* unary = expr->as<AstExprUnary>())
            checkUnary(unary);
        else if (AstExprBinary* binary = expr->as<AstExprBinary>())
            checkBinary(binary);
        else if (AstExprInterpString* interp = expr->as<AstExprInterpString>())
        {
            for (AstExpr* part : interp->expressions)
                checkExpr(part);
        }
        else if (expr->is<AstExprFunction>())
            report(expr->location, "Data files can't define functions");
        else if (expr->is<AstExprIfElse>())
            report(expr->location, "Data files can't branch: if-else expressions aren't allowed");
        else if (expr->is<AstExprVarargs>())
            report(expr->location, "'...' isn't allowed in a data file");
        else
            report(expr->location, "Only constant values are allowed in a data file");
    }

    void checkGlobal(AstExprGlobal* global)
    {
        if (global->name == "none" || global->name == kParseNameError)
            return;

        report(global->location, std::string("Unknown name '") + global->name.value + "': a data file can only use consts declared above");
    }

    void checkTable(AstExprTable* table)
    {
        for (const AstExprTable::Item& item : table->items)
        {
            if (item.key)
                checkExpr(item.key);
            checkExpr(item.value);
        }
    }

    void checkCall(AstExprCall* call)
    {
        const AstExprIndexName* function = call->func->as<AstExprIndexName>();
        if (function && function->index == kParseNameError)
            return;

        if (luwonConstructorOf(call) == LuwonConstructor::None)
        {
            report(call->location, kOnlyConstructorsMessage);
            return;
        }

        for (AstExpr* arg : call->args)
            checkExpr(arg);
    }

    void checkIndexName(AstExprIndexName* index)
    {
        // `vector.` while the name is still being typed: the parser already reports it
        if (index->index == kParseNameError)
            return;

        // `vector.create` without a call, or another member of a constructor's library
        const AstExprGlobal* global = index->expr->as<AstExprGlobal>();
        if (global && (global->name == "vector" || global->name == "buffer"))
        {
            report(index->location, kOnlyConstructorsMessage);
            return;
        }

        checkExpr(index->expr);
    }

    void checkUnary(AstExprUnary* unary)
    {
        if (unary->op == AstExprUnary::Op::Not)
            report(unary->location, "'not' isn't allowed in a data file, since data files don't branch");
        else if (unary->op == AstExprUnary::Op::Len)
            report(unary->location, "'#' isn't allowed in a data file");
        else
            checkExpr(unary->expr);
    }

    void checkBinary(AstExprBinary* binary)
    {
        switch (binary->op)
        {
        case AstExprBinary::And:
        case AstExprBinary::Or:
            report(binary->location, "'" + toString(binary->op) + "' isn't allowed in a data file, since data files don't branch");
            return;
        case AstExprBinary::CompareNe:
        case AstExprBinary::CompareEq:
        case AstExprBinary::CompareLt:
        case AstExprBinary::CompareLe:
        case AstExprBinary::CompareGt:
        case AstExprBinary::CompareGe:
            report(binary->location, "'" + toString(binary->op) + "' isn't allowed in a data file: comparisons only matter for branching");
            return;
        default:
            checkExpr(binary->left);
            checkExpr(binary->right);
            return;
        }
    }

    void checkLocal(AstStatLocal* local)
    {
        // Luwu Destructuring (rfcs/destructuring.md): a pattern's fields read the hidden local the statement holding the
        // pattern declares, and that statement is checked
        if (local->isDesugaredField())
            return;

        if (!local->isConst && local->destructure)
            report(local->location, "Destructure with 'const': data files have no 'local' bindings");
        else if (!local->isConst)
        {
            const char* name = local->vars.size > 0 ? local->vars.data[0]->name.value : "it";
            report(local->location, std::string("Declare '") + name + "' with 'const': data files have no 'local' bindings");
        }
        else if (local->isExported)
            report(local->location, "Remove 'export': a data file's value is what it returns");

        for (AstExpr* value : local->values)
            checkExpr(value);
    }

    void checkReturn(AstStatReturn* ret)
    {
        if (ret->list.size != 1)
        {
            report(ret->location, "Return exactly one value from a data file");
            return;
        }

        checkExpr(ret->list.data[0]);
    }

    void checkStat(AstStat* stat)
    {
        // Types and declarations are only analysis, and the reader skips them
        bool typesOnly = stat->is<AstStatTypeAlias>() || stat->is<AstStatTypeFunction>() || stat->is<AstStatDeclareGlobal>() ||
                         stat->is<AstStatDeclareFunction>() || stat->is<AstStatDeclareExternType>();
        if (typesOnly || stat->is<AstStatError>())
            return;

        if (AstStatLocal* local = stat->as<AstStatLocal>())
            checkLocal(local);
        else if (AstStatReturn* ret = stat->as<AstStatReturn>())
            checkReturn(ret);
        else if (stat->is<AstStatAssign>() || stat->is<AstStatCompoundAssign>())
            report(stat->location, "Data files can't reassign anything: every binding is const");
        else if (stat->is<AstStatIf>())
            report(stat->location, "Data files can't branch: 'if' statements aren't allowed");
        else if (stat->is<AstStatWhile>() || stat->is<AstStatRepeat>() || stat->is<AstStatFor>() || stat->is<AstStatForIn>())
            report(stat->location, "Data files can't loop");
        else if (stat->is<AstStatFunction>() || stat->is<AstStatLocalFunction>())
            report(stat->location, "Data files can't define functions");
        else if (stat->is<AstStatClass>())
            report(stat->location, "Data files can't declare classes, since methods are code");
        else if (AstStatExpr* expr = stat->as<AstStatExpr>())
            checkStatExpr(expr);
        else
            report(stat->location, "Data files can only hold 'const' and 'type' declarations and a final 'return'");
    }

    // A call on its own line: nothing receives its value, so even a constructor is a mistake. An expression statement is
    // always a call; the parser reports anything else.
    void checkStatExpr(AstStatExpr* stat)
    {
        if (stat->expr->is<AstExprCall>())
            report(stat->location, "Only 'vector.create', 'buffer.fromstring' and classes can be called in a data file, to give a const or the returned value");
    }

    void checkChunk(AstStatBlock* root)
    {
        for (AstStat* stat : root->body)
            checkStat(stat);

        bool endsInReturn = root->body.size > 0 && root->body.data[root->body.size - 1]->is<AstStatReturn>();
        if (!endsInReturn)
            report(Location(root->location.end, 0), "End the data file with 'return <value>'");
    }
};

} // namespace

void checkLuwonSyntax(AstStatBlock* root, std::vector<ParseError>& errors)
{
    LuwonSyntaxChecker checker{errors};
    checker.checkChunk(root);
}

} // namespace Luau
