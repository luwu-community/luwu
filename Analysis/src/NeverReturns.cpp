// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/NeverReturns.h"

#include "Luau/Module.h"
#include "Luau/ModuleResolver.h"
#include "Luau/Scope.h"
#include "Luau/Type.h"
#include "Luau/TypeInfer.h"
#include "Luau/TypePack.h"

namespace Luau
{

namespace
{

// The local or global an assignment target writes, looking through field indexes: `t.x.y = v` writes `t`
const AstExpr* assignedRoot(const AstExpr* target)
{
    while (true)
    {
        if (const AstExprIndexName* index = target->as<AstExprIndexName>())
            target = index->expr;
        else if (const AstExprIndexExpr* index = target->as<AstExprIndexExpr>())
            target = index->expr;
        else
            return target;
    }
}

// The locals and globals a module assigns to anywhere. Their declared types don't decide what their calls do.
struct WrittenNames : AstVisitor
{
    DenseHashSet<const AstLocal*> locals{nullptr};
    DenseHashSet<AstName> globals{AstName{}};

    void write(const AstExpr* target)
    {
        const AstExpr* root = assignedRoot(target);
        if (const AstExprLocal* local = root->as<AstExprLocal>())
            locals.insert(local->local);
        else if (const AstExprGlobal* global = root->as<AstExprGlobal>())
            globals.insert(global->name);
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

    bool visit(AstStatFunction* node) override
    {
        write(node->name);
        return true;
    }
};

// Whether a return annotation starts with `never`: `(): never` or `-> (never, string)`
bool annotationReturnsNever(const AstTypePack* pack)
{
    const AstTypePackExplicit* explicitPack = pack ? pack->as<AstTypePackExplicit>() : nullptr;
    if (!explicitPack || explicitPack->typeList.types.size == 0)
        return false;

    const AstTypeReference* first = explicitPack->typeList.types.data[0]->as<AstTypeReference>();
    return first && !first->prefix && first->parameters.size == 0 && first->name == "never";
}

// Whether a function type can't return: its results include `never`, or every overload's do
bool typeReturnsNever(TypeId ty)
{
    ty = follow(ty);
    if (const FunctionType* function = get<FunctionType>(ty))
    {
        for (TypeId result : function->retTypes)
        {
            if (get<NeverType>(follow(result)))
                return true;
        }
        return false;
    }

    if (const IntersectionType* overloads = get<IntersectionType>(ty))
    {
        for (TypeId overload : overloads->parts)
        {
            if (!typeReturnsNever(overload))
                return false;
        }
        return !overloads->parts.empty();
    }

    return false;
}

// The type of property `name` of a declared table or extern type
std::optional<TypeId> declaredPropertyType(TypeId ty, const std::string& name)
{
    ty = follow(ty);
    const Property* property = nullptr;
    if (const TableType* table = get<TableType>(ty))
    {
        auto it = table->props.find(name);
        if (it != table->props.end())
            property = &it->second;
    }
    else if (const ExternType* externType = get<ExternType>(ty))
    {
        property = lookupExternTypeProp(externType, name);
    }

    if (!property)
        return std::nullopt;
    return property->readTy;
}

// What a name or expression is known to hold before inference: a declared type, or a function annotated to return
// `never`, which has no type until it's solved
struct KnownValue
{
    std::optional<TypeId> type;
    bool returnsNever = false;
};

struct NeverReturningCallFinder : AstVisitor
{
    const std::shared_ptr<Scope>& globalScope;
    NotNull<ModuleResolver> moduleResolver;
    const ModuleName& moduleName;
    const std::vector<RequireCycle>& requireCycles;
    const WrittenNames& written;

    DenseHashMap<const AstLocal*, KnownValue> locals{nullptr};
    NeverReturningCalls result;

    NeverReturningCallFinder(
        const std::shared_ptr<Scope>& globalScope,
        NotNull<ModuleResolver> moduleResolver,
        const ModuleName& moduleName,
        const std::vector<RequireCycle>& requireCycles,
        const WrittenNames& written
    )
        : globalScope(globalScope)
        , moduleResolver(moduleResolver)
        , moduleName(moduleName)
        , requireCycles(requireCycles)
        , written(written)
    {
    }

    // The first value a required module returns, once that module has been checked
    std::optional<TypeId> requiredModuleType(const AstExprCall* call)
    {
        std::optional<AstExpr*> path = TypeChecker::matchRequire(*call);
        if (!path)
            return std::nullopt;

        std::optional<ModuleInfo> info = moduleResolver->resolveModuleInfo(moduleName, **path);
        if (!info)
            return std::nullopt;

        // a module in a require cycle with this one isn't checked yet
        for (const RequireCycle& cycle : requireCycles)
        {
            if (!cycle.path.empty() && cycle.path.front() == info->name)
                return std::nullopt;
        }

        ModulePtr required = moduleResolver->getModule(info->name);
        if (!required || !required->returnType)
            return std::nullopt;
        return first(required->returnType);
    }

    KnownValue resolve(const AstExpr* expr)
    {
        while (const AstExprGroup* group = expr->as<AstExprGroup>())
            expr = group->expr;

        if (const AstExprGlobal* global = expr->as<AstExprGlobal>())
        {
            if (!globalScope || written.globals.contains(global->name))
                return {};
            return {globalScope->lookup(global->name)};
        }

        if (const AstExprLocal* local = expr->as<AstExprLocal>())
        {
            const KnownValue* known = locals.find(local->local);
            if (!known || written.locals.contains(local->local))
                return {};
            return *known;
        }

        if (const AstExprIndexName* index = expr->as<AstExprIndexName>())
        {
            KnownValue base = resolve(index->expr);
            if (!base.type)
                return {};
            return {declaredPropertyType(*base.type, index->index.value)};
        }

        if (const AstExprCall* call = expr->as<AstExprCall>())
            return {requiredModuleType(call)};

        if (const AstExprFunction* function = expr->as<AstExprFunction>())
            return {std::nullopt, annotationReturnsNever(function->returnAnnotation)};

        return {};
    }

    // What `local` holds when it's declared with `value`, or with no value when that's null
    KnownValue declaredValue(const AstLocal* local, const AstExpr* value)
    {
        // An annotation decides the local's type, whatever its value is.
        if (local->annotation)
        {
            const AstTypeFunction* function = local->annotation->as<AstTypeFunction>();
            return {std::nullopt, function && annotationReturnsNever(function->returnTypes)};
        }

        if (!value)
            return {};
        return resolve(value);
    }

    bool visit(AstStatLocal* node) override
    {
        for (size_t i = 0; i < node->vars.size; ++i)
        {
            const AstExpr* value = i < node->values.size ? node->values.data[i] : nullptr;
            KnownValue known = declaredValue(node->vars.data[i], value);
            if (known.type || known.returnsNever)
                locals[node->vars.data[i]] = known;
        }
        return true;
    }

    bool visit(AstStatLocalFunction* node) override
    {
        if (annotationReturnsNever(node->func->returnAnnotation))
            locals[node->name] = KnownValue{std::nullopt, true};
        return true;
    }

    bool visit(AstStatExpr* node) override
    {
        const AstExprCall* call = node->expr->as<AstExprCall>();
        if (!call)
            return true;

        KnownValue callee = resolve(call->func);
        bool neverReturns = callee.returnsNever || (callee.type && typeReturnsNever(*callee.type));
        if (neverReturns)
            result.calls.insert(call);
        return true;
    }
};

} // namespace

NeverReturningCalls findNeverReturningCalls(
    AstStatBlock* root,
    const std::shared_ptr<Scope>& globalScope,
    NotNull<ModuleResolver> moduleResolver,
    const ModuleName& moduleName,
    const std::vector<RequireCycle>& requireCycles
)
{
    WrittenNames written;
    root->visit(&written);

    NeverReturningCallFinder finder{globalScope, moduleResolver, moduleName, requireCycles, written};
    root->visit(&finder);
    return std::move(finder.result);
}

bool callNeverReturns(const AstExprCall* call, const NeverReturningCalls* known)
{
    return doesCallError(call) || (known && known->calls.contains(call));
}

} // namespace Luau
