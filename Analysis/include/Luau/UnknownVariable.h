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

    // The global names of `scope` and its parents. They're collected once per outermost scope.
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
    DenseHashMap<const Scope*, std::vector<std::string>> globalsByTopScope{nullptr};
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

// Luwu: the message for an unknown variable. The type checkers and the UnknownGlobal lint share it.
std::string unknownVariableMessage(const UnknownSymbol& error);

} // namespace Luau
