// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/AstUtils.h"
#include "Luau/Ast.h"
#include "Luau/Type.h"

namespace Luau
{

std::optional<TypeGuard> matchTypeGuard(AstExprBinary::Op op, AstExpr* left, AstExpr* right)
{
    if (op != AstExprBinary::CompareEq && op != AstExprBinary::CompareNe)
        return std::nullopt;

    // Luwu: `(type(x)) == "string"` is a type guard too. Upstream matches only the bare call, so parentheses silently
    // drop the refinement.
    auto unwrapGroups = [](AstExpr* expr)
    {
        while (AstExprGroup* group = expr->as<AstExprGroup>())
            expr = group->expr;
        return expr;
    };

    left = unwrapGroups(left);
    right = unwrapGroups(right);

    if (right->is<AstExprCall>())
        std::swap(left, right);

    if (!right->is<AstExprConstantString>())
        return std::nullopt;

    AstExprCall* call = left->as<AstExprCall>();
    AstExprConstantString* string = right->as<AstExprConstantString>();
    if (!call || !string)
        return std::nullopt;

    AstExprGlobal* callee = call->func->as<AstExprGlobal>();
    if (!callee)
        return std::nullopt;

    if (callee->name != "type" && callee->name != "typeof")
        return std::nullopt;

    if (call->args.size != 1)
        return std::nullopt;

    return TypeGuard{
        /*isTypeof*/ callee->name == "typeof",
        /*target*/ call->args.data[0],
        /*type*/ std::string(string->value.data, string->value.size),
    };
}

struct AstExprTableFinder : AstVisitor
{
    NotNull<DenseHashSet<TypeId>> result;
    NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes;

    explicit AstExprTableFinder(NotNull<DenseHashSet<TypeId>> result, NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes)
        : result(result)
        , astTypes(astTypes)
    {
    }

    bool visit(AstExpr* expr) override
    {
        return false;
    }

    bool visit(AstExprTable* tbl) override
    {
        const TypeId* ty = astTypes->find(tbl);
        LUAU_ASSERT(ty);
        if (ty)
            result->insert(*ty);

        return true;
    }
};

void findUniqueTypes(NotNull<DenseHashSet<TypeId>> uniqueTypes, AstExpr* expr, NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes)
{
    AstExprTableFinder finder{uniqueTypes, astTypes};
    expr->visit(&finder);
}

template<typename Iter>
void findUniqueTypes(
    NotNull<DenseHashSet<TypeId>> uniqueTypes,
    Iter startIt,
    Iter endIt,
    NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes
)
{
    while (startIt != endIt)
    {
        AstExpr* expr = *startIt;
        if (expr->is<AstExprTable>())
            findUniqueTypes(uniqueTypes, expr, astTypes);
        ++startIt;
    }
}


void findUniqueTypes(
    NotNull<DenseHashSet<TypeId>> uniqueTypes,
    AstArray<AstExpr*> exprs,
    NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes
)
{
    findUniqueTypes(uniqueTypes, exprs.begin(), exprs.end(), astTypes);
}

void findUniqueTypes(
    NotNull<DenseHashSet<TypeId>> uniqueTypes,
    const std::vector<AstExpr*>& exprs,
    NotNull<const DenseHashMap<const AstExpr*, TypeId>> astTypes
)
{
    findUniqueTypes(uniqueTypes, exprs.begin(), exprs.end(), astTypes);
}

AstLocal* methodSelf(AstExprFunction* fn)
{
    if (fn->self)
        return fn->self;
    if (fn->args.size > 0 && fn->args.data[0]->name == "self")
        return fn->args.data[0];
    return nullptr;
}

namespace
{

struct SelfReturns : AstVisitor
{
    AstLocal* self = nullptr;
    size_t returns = 0;
    size_t selfReturns = 0;

    bool isSelf(AstExpr* expr) const
    {
        AstExprLocal* local = expr->as<AstExprLocal>();
        return local && local->local == self;
    }

    // `class.of(self)(...)`
    bool isNewOfSelfsClass(AstExpr* expr) const
    {
        AstExprCall* construct = expr->as<AstExprCall>();
        return construct && newOfClassOf(construct) == self;
    }

    bool visit(AstExprFunction* fn) override
    {
        return false;
    }

    bool visit(AstStatReturn* ret) override
    {
        ++returns;
        if (ret->list.size == 1 && isSelfValue(ret->list.data[0]))
            ++selfReturns;
        return true;
    }

    bool isSelfValue(AstExpr* value) const
    {
        while (AstExprGroup* group = value->as<AstExprGroup>())
            value = group->expr;
        return isSelf(value) || isNewOfSelfsClass(value);
    }
};

SelfReturns countSelfReturns(AstExprFunction* fn, AstLocal* self)
{
    SelfReturns returns;
    returns.self = self;
    if (self && fn->body)
        fn->body->visit(&returns);
    return returns;
}

} // namespace

AstLocal* newOfClassOf(AstExprCall* call)
{
    AstExprCall* classOf = call->func->as<AstExprCall>();
    AstExprIndexName* of = classOf ? classOf->func->as<AstExprIndexName>() : nullptr;
    AstExprGlobal* classLib = of ? of->expr->as<AstExprGlobal>() : nullptr;

    bool namesClassOf = classLib && classLib->name == "class" && of->index == "of";
    if (!namesClassOf || classOf->args.size != 1)
        return nullptr;

    AstExprLocal* local = classOf->args.data[0]->as<AstExprLocal>();
    return local ? local->local : nullptr;
}

bool returnsSelf(AstExprFunction* fn, AstLocal* self)
{
    return countSelfReturns(fn, self).selfReturns > 0;
}

bool returnsOnlySelf(AstExprFunction* fn, AstLocal* self)
{
    SelfReturns returns = countSelfReturns(fn, self);
    if (returns.selfReturns == 0 || returns.selfReturns != returns.returns || fn->body->body.size == 0)
        return false;

    AstStatReturn* last = fn->body->body.data[fn->body->body.size - 1]->as<AstStatReturn>();
    return last && last->list.size == 1 && returns.isSelfValue(last->list.data[0]);
}

} // namespace Luau
