// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/Scope.h"

LUAU_FASTFLAG(LuauSolverV2);

namespace Luau
{

Scope::Scope(TypePackId returnType)
    : parent(nullptr)
    , returnType(returnType)
    , level(TypeLevel())
{
}

Scope::Scope(const ScopePtr& parent, int subLevel)
    : parent(parent)
    , returnType(parent->returnType)
    , level(parent->level.incr())
{
    level = level.incr();
    level.subLevel = subLevel;
}

void Scope::addBuiltinTypeBinding(const Name& name, const TypeFun& tyFun)
{
    exportedTypeBindings[name] = tyFun;
    builtinTypeNames.insert(name);
}

std::optional<TypeId> Scope::lookup(Symbol sym) const
{
    auto r = const_cast<Scope*>(this)->lookupEx(sym);
    if (r)
        return r->first->typeId;
    else
        return std::nullopt;
}

std::optional<std::pair<TypeId, Scope*>> Scope::lookupEx(DefId def)
{
    Scope* s = this;

    while (true)
    {
        if (TypeId* it = s->lvalueTypes.find(def))
            return std::pair{*it, s};
        else if (TypeId* it = s->rvalueRefinements.find(def))
            return std::pair{*it, s};

        if (s->parent)
            s = s->parent.get();
        else
            return std::nullopt;
    }
}

std::optional<std::pair<Binding*, Scope*>> Scope::lookupEx(Symbol sym)
{
    Scope* s = this;

    while (true)
    {
        auto it = s->bindings.find(sym);
        if (it != s->bindings.end())
            return std::pair{&it->second, s};

        if (s->parent)
            s = s->parent.get();
        else
            return std::nullopt;
    }
}

std::optional<TypeId> Scope::lookupUnrefinedType(DefId def) const
{
    for (const Scope* current = this; current; current = current->parent.get())
    {
        if (auto ty = current->lvalueTypes.find(def))
            return *ty;
    }

    return std::nullopt;
}

std::optional<TypeId> Scope::lookupRValueRefinementType(DefId def) const
{
    for (const Scope* current = this; current; current = current->parent.get())
    {
        if (auto ty = current->rvalueRefinements.find(def))
            return *ty;
    }

    return std::nullopt;
}

std::optional<TypeId> Scope::lookup(DefId def) const
{
    for (const Scope* current = this; current; current = current->parent.get())
    {
        if (auto ty = current->rvalueRefinements.find(def))
            return *ty;
        if (auto ty = current->lvalueTypes.find(def))
            return *ty;
    }

    return std::nullopt;
}

std::optional<TypeFun> Scope::lookupType(const Name& name) const
{
    const Scope* scope = this;
    while (true)
    {
        auto it = scope->exportedTypeBindings.find(name);
        if (it != scope->exportedTypeBindings.end())
            return it->second;

        it = scope->privateTypeBindings.find(name);
        if (it != scope->privateTypeBindings.end())
            return it->second;

        if (scope->parent)
            scope = scope->parent.get();
        else
            return std::nullopt;
    }
}

std::optional<TypeFun> Scope::lookupTraitRef(const AstClassTraitRef& ref) const
{
    if (AstExprGlobal* global = ref.trait->as<AstExprGlobal>())
        return lookupType(global->name.value);

    // Luwu Declare Statements (rfcs/declare-statements.md): a declared trait has no value, so a class implementing one names
    // a local holding it, `local Greeter = ... :: trait<Greeter>` or `const G: trait<Greeter> = ...`. The annotation says
    // which trait; without one, the local is taken to be named after it, as a global is.
    if (AstExprLocal* local = ref.trait->as<AstExprLocal>())
    {
        AstTypeReference* traitValue = local->local->annotation ? local->local->annotation->as<AstTypeReference>() : nullptr;
        bool annotated = traitValue && !traitValue->prefix && traitValue->name == "trait" && traitValue->parameters.size == 1;
        AstType* argument = annotated ? traitValue->parameters.data[0].type : nullptr;
        AstTypeReference* named = argument ? argument->as<AstTypeReference>() : nullptr;
        if (!named)
            return lookupType(local->local->name.value);

        if (named->prefix)
            return lookupImportedType(named->prefix->value, named->name.value);

        return lookupType(named->name.value);
    }

    AstExprIndexName* index = ref.trait->as<AstExprIndexName>();
    if (!index)
        return std::nullopt;

    if (AstExprLocal* moduleLocal = index->expr->as<AstExprLocal>())
        return lookupImportedType(moduleLocal->local->name.value, index->index.value);
    if (AstExprGlobal* moduleGlobal = index->expr->as<AstExprGlobal>())
        return lookupImportedType(moduleGlobal->name.value, index->index.value);

    return std::nullopt;
}

std::optional<TypeFun> Scope::lookupImportedType(const Name& moduleAlias, const Name& name) const
{
    const Scope* scope = this;
    while (scope)
    {
        auto it = scope->importedTypeBindings.find(moduleAlias);
        if (it == scope->importedTypeBindings.end())
        {
            scope = scope->parent.get();
            continue;
        }

        auto it2 = it->second.find(name);
        if (it2 == it->second.end())
        {
            scope = scope->parent.get();
            continue;
        }

        return it2->second;
    }

    return std::nullopt;
}

std::optional<TypePackId> Scope::lookupPack(const Name& name) const
{
    const Scope* scope = this;
    while (true)
    {
        auto it = scope->privateTypePackBindings.find(name);
        if (it != scope->privateTypePackBindings.end())
            return it->second;

        if (scope->parent)
            scope = scope->parent.get();
        else
            return std::nullopt;
    }
}

std::optional<Binding> Scope::linearSearchForBinding(const std::string& name, bool traverseScopeChain) const
{
    const Scope* scope = this;

    while (scope)
    {
        for (const auto& [n, binding] : scope->bindings)
        {
            if (n.local && n.local->name == name.c_str())
                return binding;
            else if (n.global.value && n.global == name.c_str())
                return binding;
        }

        scope = scope->parent.get();

        if (!traverseScopeChain)
            break;
    }

    return std::nullopt;
}

std::optional<std::pair<Symbol, Binding>> Scope::linearSearchForBindingPair(const std::string& name, bool traverseScopeChain) const
{
    const Scope* scope = this;

    while (scope)
    {
        for (auto& [n, binding] : scope->bindings)
        {
            if (n.local && n.local->name == name.c_str())
                return {{n, binding}};
            else if (n.global.value && n.global == name.c_str())
                return {{n, binding}};
        }

        scope = scope->parent.get();

        if (!traverseScopeChain)
            break;
    }

    return std::nullopt;
}

// Updates the `this` scope with the assignments from the `childScope` including ones that doesn't exist in `this`.
void Scope::inheritAssignments(const ScopePtr& childScope)
{
    for (const auto& [k, a] : childScope->lvalueTypes)
        lvalueTypes[k] = a;
}

// Updates the `this` scope with the refinements from the `childScope` excluding ones that doesn't exist in `this`.
void Scope::inheritRefinements(const ScopePtr& childScope)
{
    for (const auto& [k, a] : childScope->rvalueRefinements)
    {
        if (lookup(NotNull{k}))
            rvalueRefinements[k] = a;
    }

    for (const auto& [k, a] : childScope->refinements)
    {
        Symbol symbol = getBaseSymbol(k);
        if (lookup(symbol))
            refinements[k] = a;
    }
}


bool Scope::shouldWarnGlobal(std::string name) const
{
    for (const Scope* current = this; current; current = current->parent.get())
    {
        if (current->globalsToWarn.contains(name))
            return true;
    }
    return false;
}

std::optional<Location> Scope::isInvalidTypeAlias(const std::string& name) const
{
    for (auto scope = this; scope; scope = scope->parent.get())
    {
        if (auto loc = scope->invalidTypeAliases.find(name))
            return {*loc};
    }

    return std::nullopt;
}

// Luwu: sibling scopes can enclose one another, so each step takes the narrowest child that encloses the location, not the
// first. A `for ... in` loop's scope spans its whole statement, header included, and is created before the header is
// checked, so a function written in the header (`for _, p in fs.listdir(dir, true, function(p) ... end) do`) has a
// scope that is the loop scope's later sibling. Upstream takes the loop scope, and the function's `return` is then
// checked against the enclosing function's return type. Of two children with the same location, the first wins.
NotNull<Scope> Scope::findNarrowestScopeContaining(Location location)
{
    Scope* bestScope = this;

    while (true)
    {
        Scope* narrowest = nullptr;
        for (NotNull<Scope> child : bestScope->children)
        {
            if (!child->location.encloses(location))
                continue;

            bool narrower = !narrowest || (narrowest->location.encloses(child->location) && narrowest->location != child->location);
            if (narrower)
                narrowest = child.get();
        }

        if (!narrowest)
            return NotNull{bestScope};

        bestScope = narrowest;
    }
}


bool subsumesStrict(Scope* left, Scope* right)
{
    if (!left || !right)
        return false;

    while (right)
    {
        if (right->parent.get() == left)
            return true;

        right = right->parent.get();
    }

    return false;
}

bool subsumes(Scope* left, Scope* right)
{
    if (!left || !right)
        return false;

    return left == right || subsumesStrict(left, right);
}

} // namespace Luau
