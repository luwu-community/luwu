// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/UnreachableProof.h"

#include "Luau/AstQuery.h"
#include "Luau/Module.h"
#include "Luau/Type.h"

#include <optional>
#include <string_view>
#include <vector>

namespace Luau
{

namespace
{

// A condition and whether it holds or fails where the code is
struct Condition
{
    AstExpr* expr = nullptr;
    bool holds = true;
};

// Calls `f` with each type a value of `ty` can have: the members of a union, or `ty` itself
template<typename F>
void forEachOption(TypeId ty, F&& f)
{
    ty = follow(ty);
    if (const UnionType* ut = get<UnionType>(ty))
    {
        for (TypeId option : ut->options)
            f(follow(option));
    }
    else
    {
        f(ty);
    }
}

// What `type()` gives for a value of `ty`; nullopt when it can't be told from the type alone. A table type is a shape:
// a vector, an extern type's value or a class object with those fields fits it too, so it says nothing about `type()`.
std::optional<std::string_view> typeName(TypeId ty)
{
    if (const PrimitiveType* pt = get<PrimitiveType>(ty))
    {
        switch (pt->type)
        {
        case PrimitiveType::NilType:
            return "nil";
        case PrimitiveType::Boolean:
            return "boolean";
        case PrimitiveType::Number:
            return "number";
        case PrimitiveType::String:
            return "string";
        case PrimitiveType::Thread:
            return "thread";
        case PrimitiveType::Function:
            return "function";
        case PrimitiveType::Buffer:
            return "buffer";
        default:
            return std::nullopt;
        }
    }
    if (const SingletonType* st = get<SingletonType>(ty))
        return get<BooleanSingleton>(st) ? "boolean" : "string";
    if (get<FunctionType>(ty))
        return "function";
    return std::nullopt;
}

// Whether a value of `option` is always falsy, or always truthy; neither when it can't be told from the type
bool isFalsyOnly(TypeId option)
{
    if (const PrimitiveType* pt = get<PrimitiveType>(option))
        return pt->type == PrimitiveType::NilType || pt->type == PrimitiveType::NoneType;
    if (const SingletonType* st = get<SingletonType>(option))
    {
        const BooleanSingleton* b = get<BooleanSingleton>(st);
        return b && !b->value;
    }
    return false;
}

// Every value a table type fits is truthy: a table, a vector, an extern type's value or a class object
bool isTruthyOnly(TypeId option)
{
    if (const PrimitiveType* pt = get<PrimitiveType>(option))
        return pt->type != PrimitiveType::NilType && pt->type != PrimitiveType::NoneType && pt->type != PrimitiveType::Boolean;
    if (const SingletonType* st = get<SingletonType>(option))
    {
        const BooleanSingleton* b = get<BooleanSingleton>(st);
        return !b || b->value;
    }
    return get<TableType>(option) || get<MetatableType>(option) || get<FunctionType>(option);
}

AstExpr* stripGroups(AstExpr* expr)
{
    while (AstExprGroup* group = expr->as<AstExprGroup>())
        expr = group->expr;
    return expr;
}

bool isConstant(AstExpr* expr)
{
    bool scalar = expr->is<AstExprConstantBool>() || expr->is<AstExprConstantString>() || expr->is<AstExprConstantNumber>();
    return scalar || expr->is<AstExprConstantNil>();
}

// Whether a value of `option` can equal the constant `constant`; true when it can't be told
bool optionCanEqual(TypeId option, AstExpr* constant)
{
    std::optional<std::string_view> name = typeName(option);
    if (constant->is<AstExprConstantNil>())
        return !name || *name == "nil";

    if (AstExprConstantBool* b = constant->as<AstExprConstantBool>())
    {
        if (const SingletonType* st = get<SingletonType>(option); st && get<BooleanSingleton>(st))
            return get<BooleanSingleton>(st)->value == b->value;
        return !name || *name == "boolean";
    }
    if (AstExprConstantString* s = constant->as<AstExprConstantString>())
    {
        if (const SingletonType* st = get<SingletonType>(option); st && get<StringSingleton>(st))
            return get<StringSingleton>(st)->value == std::string_view(s->value.data, s->value.size);
        return !name || *name == "string";
    }
    if (constant->is<AstExprConstantNumber>())
        return !name || *name == "number";
    return true;
}

// Whether every value of `option` equals the constant: a singleton of it, or `nil` for `nil`
bool optionAlwaysEquals(TypeId option, AstExpr* constant)
{
    if (constant->is<AstExprConstantNil>())
    {
        const PrimitiveType* pt = get<PrimitiveType>(option);
        return pt && pt->type == PrimitiveType::NilType;
    }
    const SingletonType* st = get<SingletonType>(option);
    if (!st)
        return false;
    if (AstExprConstantBool* b = constant->as<AstExprConstantBool>())
        return get<BooleanSingleton>(st) && get<BooleanSingleton>(st)->value == b->value;
    if (AstExprConstantString* s = constant->as<AstExprConstantString>())
        return get<StringSingleton>(st) && get<StringSingleton>(st)->value == std::string_view(s->value.data, s->value.size);
    return false;
}

// What a test reads: the local itself, or one of its properties (`prop` is its name, null for the local)
struct Term
{
    const char* prop = nullptr;
};

// A test on a term that holds (or fails) where the code is: its truthiness, `==` against a constant, or
// `type()`/`typeof()` against a type name
struct Test
{
    enum class Kind
    {
        Truthy,
        Equals,
        TypeName,
    };

    Kind kind = Kind::Truthy;
    Term term;
    AstExpr* constant = nullptr;
    std::string_view typeName;
    bool holds = true;

    // Whether a value of `option` can pass the test
    bool admits(TypeId option) const
    {
        switch (kind)
        {
        case Kind::Truthy:
            return holds ? !isFalsyOnly(option) : !isTruthyOnly(option);
        case Kind::Equals:
            return holds ? optionCanEqual(option, constant) : !optionAlwaysEquals(option, constant);
        case Kind::TypeName:
        {
            std::optional<std::string_view> name = Luau::typeName(option);
            return !name || (*name == typeName) == holds;
        }
        }
        return true;
    }
};

// `type(x)` or `typeof(x)`: the `x`
AstExpr* typeNameCallArgument(AstExpr* expr)
{
    AstExprCall* call = stripGroups(expr)->as<AstExprCall>();
    AstExprGlobal* callee = call ? call->func->as<AstExprGlobal>() : nullptr;
    bool isTypeCall = callee && (callee->name == "type" || callee->name == "typeof") && call->args.size == 1;
    return isTypeCall ? call->args.data[0] : nullptr;
}

// Decides one member of the local's declared type against the tests where the code is
struct MemberProof
{
    const AstLocal* local;
    // The member of the declared type being ruled out
    TypeId member;

    // `expr` as a term: the local, or `local.prop`
    std::optional<Term> termOf(AstExpr* expr) const
    {
        expr = stripGroups(expr);
        if (AstExprLocal* read = expr->as<AstExprLocal>(); read && read->local == local)
            return Term{};

        AstExprIndexName* index = expr->as<AstExprIndexName>();
        AstExprLocal* base = index && index->op == '.' ? stripGroups(index->expr)->as<AstExprLocal>() : nullptr;
        if (!base || base->local != local)
            return std::nullopt;
        return Term{index->index.value};
    }

    // The type of a term in this member; nullopt for a property it doesn't have, which no test can rule out
    std::optional<TypeId> termType(const Term& term) const
    {
        if (!term.prop)
            return member;

        const TableType* table = get<TableType>(member);
        if (const MetatableType* mt = get<MetatableType>(member))
            table = get<TableType>(follow(mt->table));
        const TableType::Props* props = table ? &table->props : nullptr;
        if (const ExternType* externType = get<ExternType>(member))
            props = &externType->props();
        if (!props)
            return std::nullopt;

        auto it = props->find(term.prop);
        if (it == props->end() || !it->second.readTy)
            return std::nullopt;
        return *it->second.readTy;
    }

    // `expr` holding (or failing) as one test on a term; nullopt when it isn't one
    std::optional<Test> testOf(AstExpr* expr, bool holds) const
    {
        expr = stripGroups(expr);
        AstExprBinary* binary = expr->as<AstExprBinary>();
        bool comparison = binary && (binary->op == AstExprBinary::CompareEq || binary->op == AstExprBinary::CompareNe);
        if (!comparison)
        {
            if (std::optional<Term> term = termOf(expr))
                return Test{Test::Kind::Truthy, *term, nullptr, {}, holds};
            return std::nullopt;
        }

        bool equal = (binary->op == AstExprBinary::CompareEq) == holds;
        AstExpr* left = binary->left;
        AstExpr* right = binary->right;
        if (!isConstant(stripGroups(right)))
            std::swap(left, right);
        right = stripGroups(right);
        if (!isConstant(right))
            return std::nullopt;

        if (std::optional<Term> term = termOf(left))
            return Test{Test::Kind::Equals, *term, right, {}, equal};

        AstExprConstantString* name = right->as<AstExprConstantString>();
        AstExpr* typed = typeNameCallArgument(left);
        std::optional<Term> term = typed ? termOf(typed) : std::nullopt;
        if (!name || !term)
            return std::nullopt;
        return Test{Test::Kind::TypeName, *term, nullptr, std::string_view(name->value.data, name->value.size), equal};
    }

    // Splits a condition into tests that all apply (`a and b` holding, `a or b` failing); the rest stays whole
    void flatten(AstExpr* expr, bool holds, std::vector<Test>& tests, std::vector<Condition>& whole) const
    {
        expr = stripGroups(expr);
        if (AstExprUnary* unary = expr->as<AstExprUnary>(); unary && unary->op == AstExprUnary::Op::Not)
            return flatten(unary->expr, !holds, tests, whole);

        AstExprBinary* binary = expr->as<AstExprBinary>();
        bool conjunction = binary && ((binary->op == AstExprBinary::And && holds) || (binary->op == AstExprBinary::Or && !holds));
        if (conjunction)
        {
            flatten(binary->left, holds, tests, whole);
            flatten(binary->right, holds, tests, whole);
        }
        else if (std::optional<Test> test = testOf(expr, holds))
        {
            tests.push_back(*test);
        }
        else
        {
            whole.push_back({expr, holds});
        }
    }

    // Whether no value of `term` in this member passes all of `tests` on it
    bool termRuledOut(const Term& term, const std::vector<Test>& tests) const
    {
        std::optional<TypeId> ty = termType(term);
        if (!ty)
            return false;

        bool anyAdmitted = false;
        forEachOption(
            *ty,
            [&](TypeId option)
            {
                bool admitted = true;
                for (const Test& test : tests)
                    admitted = admitted && (test.term.prop != term.prop || test.admits(option));
                anyAdmitted = anyAdmitted || admitted;
            }
        );
        return !anyAdmitted;
    }

    // Whether a condition that isn't a plain list of tests (`a or b` holding) rules this member out
    bool rulesOut(AstExpr* expr, bool holds) const
    {
        expr = stripGroups(expr);
        if (AstExprUnary* unary = expr->as<AstExprUnary>(); unary && unary->op == AstExprUnary::Op::Not)
            return rulesOut(unary->expr, !holds);

        if (AstExprBinary* binary = expr->as<AstExprBinary>(); binary && (binary->op == AstExprBinary::And || binary->op == AstExprBinary::Or))
        {
            // `a and b` holds when both do and fails when either does; `a or b` the other way around
            bool both = (binary->op == AstExprBinary::And) == holds;
            bool left = rulesOut(binary->left, holds);
            bool right = rulesOut(binary->right, holds);
            return both ? left || right : left && right;
        }

        std::optional<Test> test = testOf(expr, holds);
        return test && termRuledOut(test->term, {*test});
    }

    bool ruledOutBy(const std::vector<Condition>& conditions) const
    {
        std::vector<Test> tests;
        std::vector<Condition> whole;
        for (const Condition& condition : conditions)
            flatten(condition.expr, condition.holds, tests, whole);

        // Tests on the same term apply together: `not h.enc` then `h.enc` leaves no value for `h.enc`
        for (const Test& test : tests)
        {
            if (termRuledOut(test.term, tests))
                return true;
        }
        for (const Condition& condition : whole)
        {
            if (rulesOut(condition.expr, condition.holds))
                return true;
        }
        return false;
    }
};

// Whether a statement in `body` assigns to `local` or one of its properties: a condition about either can be stale
struct AssignmentFinder : AstVisitor
{
    const AstLocal* local;
    bool found = false;

    explicit AssignmentFinder(const AstLocal* local)
        : local(local)
    {
    }

    bool assigns(AstExpr* target) const
    {
        while (true)
        {
            target = stripGroups(target);
            if (AstExprIndexName* index = target->as<AstExprIndexName>())
                target = index->expr;
            else if (AstExprIndexExpr* index = target->as<AstExprIndexExpr>())
                target = index->expr;
            else
                break;
        }
        AstExprLocal* read = target->as<AstExprLocal>();
        return read && read->local == local;
    }

    bool visit(AstStatAssign* assign) override
    {
        for (AstExpr* var : assign->vars)
            found = found || assigns(var);
        return !found;
    }

    bool visit(AstStatCompoundAssign* assign) override
    {
        found = found || assigns(assign->var);
        return !found;
    }
};

// Whether `block` always leaves before its end: it ends in `return`, `break`, `continue` or a call to `error`
bool alwaysLeaves(AstStatBlock* block)
{
    if (block->body.size == 0)
        return false;

    AstStat* last = block->body.data[block->body.size - 1];
    if (last->is<AstStatReturn>() || last->is<AstStatBreak>() || last->is<AstStatContinue>())
        return true;

    AstStatExpr* statement = last->as<AstStatExpr>();
    AstExprCall* call = statement ? statement->expr->as<AstExprCall>() : nullptr;
    AstExprGlobal* callee = call ? call->func->as<AstExprGlobal>() : nullptr;
    return callee && callee->name == "error";
}

// An `if` whose every branch leaves, with no `else`: the code after it runs only when all of its conditions fail
void addGuardConditions(AstStatIf* guard, std::vector<Condition>& conditions)
{
    size_t before = conditions.size();
    for (AstStatIf* branch = guard; branch;)
    {
        if (!alwaysLeaves(branch->thenbody))
        {
            conditions.resize(before);
            return;
        }
        conditions.push_back({branch->condition, false});

        if (!branch->elsebody)
            return;
        branch = branch->elsebody->as<AstStatIf>();
        // An `else` block runs when they all fail, so the code after the `if` doesn't learn anything from them
        if (!branch)
        {
            conditions.resize(before);
            return;
        }
    }
}

// Luwu Do Expressions (rfcs/do-expressions.md): the code after `x or return` runs only when `x` holds, and after
// `x and continue` only when it fails. After `x and continue or break` it would have to do both.
void addLeaveConditions(AstStatExpr* statement, std::vector<Condition>& conditions)
{
    AstExprBinary* guard = asLeaveGuard(statement->expr);
    if (!guard)
        return;

    if (guard->op == AstExprBinary::And)
    {
        conditions.push_back({guard->left, false});
        return;
    }

    if (AstExprBinary* left = asLeaveEitherWay(statement->expr))
    {
        conditions.push_back({left->left, false});
        conditions.push_back({left->left, true});
        return;
    }

    conditions.push_back({guard->left, true});
}

// The conditions that hold or fail at the end of `ancestry`: the branches around it, the left of an `and`/`or` it's on the
// right of, and the guards that leave, the `assert`s and the `or return` statements before it in each block around it
std::vector<Condition> conditionsAt(const std::vector<AstNode*>& ancestry)
{
    std::vector<Condition> conditions;
    for (size_t i = 1; i < ancestry.size(); ++i)
    {
        AstNode* parent = ancestry[i - 1];
        AstNode* child = ancestry[i];

        if (AstStatIf* ifStatement = parent->as<AstStatIf>())
        {
            if (child == ifStatement->thenbody)
                conditions.push_back({ifStatement->condition, true});
            else if (child == ifStatement->elsebody)
                conditions.push_back({ifStatement->condition, false});
        }
        else if (AstExprIfElse* ifElse = parent->as<AstExprIfElse>())
        {
            if (child == ifElse->trueExpr)
                conditions.push_back({ifElse->condition, true});
            else if (child == ifElse->falseExpr)
                conditions.push_back({ifElse->condition, false});
        }
        else if (AstStatWhile* whileStatement = parent->as<AstStatWhile>(); whileStatement && child == whileStatement->body)
        {
            conditions.push_back({whileStatement->condition, true});
        }
        else if (AstExprBinary* binary = parent->as<AstExprBinary>(); binary && child == binary->right)
        {
            if (binary->op == AstExprBinary::And)
                conditions.push_back({binary->left, true});
            else if (binary->op == AstExprBinary::Or)
                conditions.push_back({binary->left, false});
        }
        else if (AstStatBlock* block = parent->as<AstStatBlock>())
        {
            for (AstStat* statement : block->body)
            {
                if (statement == child)
                    break;

                if (AstStatIf* guard = statement->as<AstStatIf>())
                    addGuardConditions(guard, conditions);

                AstStatExpr* expression = statement->as<AstStatExpr>();
                if (expression)
                    addLeaveConditions(expression, conditions);

                AstExprCall* call = expression ? expression->expr->as<AstExprCall>() : nullptr;
                AstExprGlobal* callee = call ? call->func->as<AstExprGlobal>() : nullptr;
                if (callee && callee->name == "assert" && call->args.size >= 1)
                    conditions.push_back({call->args.data[0], true});
            }
        }
    }
    return conditions;
}

} // namespace

bool provesUnreachable(const SourceModule& source, const AstExprLocal* use, TypeId declared)
{
    std::vector<AstNode*> ancestry = findAstAncestryOfPosition(source, use->location.begin);
    std::vector<Condition> conditions = conditionsAt(ancestry);
    if (conditions.empty())
        return false;

    // Conditions are only known to still hold when nothing in the function assigns to what they test
    AstNode* scope = source.root;
    for (AstNode* node : ancestry)
    {
        if (node->is<AstExprFunction>())
            scope = node;
    }
    AssignmentFinder assignments{use->local};
    scope->visit(&assignments);
    if (assignments.found)
        return false;

    bool proven = true;
    forEachOption(
        declared,
        [&](TypeId member)
        {
            proven = proven && MemberProof{use->local, member}.ruledOutBy(conditions);
        }
    );
    return proven;
}

} // namespace Luau
