// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Types.h"

#include <string>
#include <unordered_map>

namespace Luwu::Dsl
{

class Scope
{
public:
    explicit Scope(Scope* parent = nullptr)
        : parent_(parent)
    {
    }

    const Operand* lookup(const std::string& name) const
    {
        auto it = bindings_.find(name);
        if (it != bindings_.end())
            return &it->second;
        if (parent_)
            return parent_->lookup(name);
        return nullptr;
    }

    void bind(const std::string& name, Operand op)
    {
        bindings_[name] = std::move(op);
    }

    void updateOrBind(const std::string& name, Operand op)
    {
        for (Scope* s = this; s != nullptr; s = s->parent_)
        {
            auto it = s->bindings_.find(name);
            if (it != s->bindings_.end())
            {
                it->second = std::move(op);
                return;
            }
        }
        bindings_[name] = std::move(op);
    }

    Scope* parent() const { return parent_; }

    const std::unordered_map<std::string, Operand>& bindings() const { return bindings_; }

private:
    Scope* parent_ = nullptr;
    std::unordered_map<std::string, Operand> bindings_;
};

class ScopeGuard
{
public:
    explicit ScopeGuard(Scope*& currentScope)
        : currentScopeRef_(currentScope), innerScope_(currentScope)
    {
        currentScopeRef_ = &innerScope_;
    }

    ~ScopeGuard()
    {
        currentScopeRef_ = innerScope_.parent();
    }

    Scope& scope() { return innerScope_; }

private:
    Scope*& currentScopeRef_;
    Scope innerScope_;
};

} // namespace Luwu::Dsl
