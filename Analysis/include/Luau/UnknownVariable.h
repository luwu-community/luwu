// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Ast.h"
#include "Luau/DenseHash.h"
#include "Luau/Error.h"
#include "Luau/Scope.h"

#include <string>
#include <vector>

namespace Luau
{

// Luwu: why a name isn't a variable where it's used, worked out from the AST alone.
struct UnknownVariableExplanation
{
    UnknownSymbol::Unbound unbound = UnknownSymbol::Unbound::NotDefined;
    // The name in scope it's probably a misspelling of.
    std::string meant;
    // The local with this name that the use can't see.
    std::optional<Location> definition;
    // Info markers on the inner-scope bindings the use could see if they moved.
    std::vector<DiagnosticNote> notes;
};

// Luwu: explains the unknown names in one module. It indexes the module's locals at the first one.
class UnknownVariableExplainer
{
public:
    explicit UnknownVariableExplainer(AstStatBlock* root);

    // `globals` are the global names the use could have meant.
    UnknownVariableExplanation explain(const AstExprGlobal* use, const std::vector<std::string>& globals);

    // The name in scope that `use` is probably a misspelling of, with no other explanation.
    std::optional<std::string> misspelledName(const AstExprGlobal* use, const std::vector<std::string>& globals);

    // The global names of the module `scope` is in and of the scopes around it, collected once per module
    const std::vector<std::string>& globalsOf(const Scope& scope);

    enum class DeclarationKind
    {
        Local,
        LocalFunction,
        Class,
        Parameter,
        LoopVariable,
    };

    struct Declaration
    {
        AstLocal* local;
        // The declaring statement, or the function or loop for a parameter or loop variable
        AstNode* owner;
        DeclarationKind kind;
    };

private:
    void index();

    AstStatBlock* root;
    bool indexed = false;
    // In source order
    std::vector<Declaration> declarations;
    DenseHashMap<AstName, std::vector<size_t>> declarationsByName{AstName()};
    // The block each statement is in
    DenseHashMap<const AstNode*, const AstStatBlock*> parentBlocks{nullptr};
    DenseHashMap<const Scope*, std::vector<std::string>> globalsByModuleScope{nullptr};
    // The nodes from the root down to each declaration, for the inner-scope search
    DenseHashMap<const AstLocal*, std::vector<AstNode*>> declarationAncestries{nullptr};
    // Locals that already have a "consider moving" marker
    DenseHashSet<const AstLocal*> noted{nullptr};
};

// Luwu: the error for a name that isn't bound where it's used. Info markers go in `notes`.
UnknownSymbol unknownVariableError(
    UnknownVariableExplainer& explainer,
    const AstExprGlobal* use,
    const std::vector<std::string>& globals,
    std::vector<DiagnosticNote>& notes
);

// Luwu: the same, with the global names of `scope` and its parents as candidates.
UnknownSymbol unknownVariableError(
    UnknownVariableExplainer& explainer,
    const AstExprGlobal* use,
    const Scope& scope,
    std::vector<DiagnosticNote>& notes
);

// Luwu: the typo error for a global spelled like a local in scope, such as `othrname = 5` beside `local othername`.
std::optional<UnknownSymbol> misspelledLocalError(UnknownVariableExplainer& explainer, const AstExprGlobal* use);

// Luwu: the message for an unknown variable. The type checkers and the UnknownGlobal lint share it.
std::string unknownVariableMessage(const UnknownSymbol& error);

} // namespace Luau
