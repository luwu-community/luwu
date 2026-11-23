// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/UnknownVariable.h"

#include "Luau/Typos.h"
#include "Luau/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <limits>

namespace Luau
{

namespace
{

using Declaration = UnknownVariableExplainer::Declaration;
using DeclarationKind = UnknownVariableExplainer::DeclarationKind;

// The most in-scope locals a misspelled name is compared with, nearest first. A typo is usually of a name nearby.
constexpr size_t kMaxTypoCandidates = 256;

// A typo is at most two edits. Each edit changes at most two letter counts.
constexpr size_t kMaxTypoLetterCountDifference = 4;

// The most earlier declarations of a name the inner-scope search looks at for one use, latest first
constexpr size_t kMaxInnerScopeCandidates = 64;

// How far apart the letter counts of `a` and `b` are. It's cheap and rules out most names before an edit distance.
size_t letterCountDifference(const std::string& a, const std::string& b)
{
    int counts[256] = {};
    for (unsigned char c : a)
        counts[c]++;
    for (unsigned char c : b)
        counts[c]--;
    size_t difference = 0;
    for (int count : counts)
        difference += size_t(count < 0 ? -count : count);
    return difference;
}

struct DeclarationCollector : AstVisitor
{
    std::vector<Declaration>& declarations;
    DenseHashMap<const AstNode*, const AstStatBlock*>& parentBlocks;

    DeclarationCollector(std::vector<Declaration>& declarations, DenseHashMap<const AstNode*, const AstStatBlock*>& parentBlocks)
        : declarations(declarations)
        , parentBlocks(parentBlocks)
    {
    }

    bool visit(AstStatBlock* node) override
    {
        for (const AstStat* statement : node->body)
            parentBlocks[statement] = node;
        return true;
    }

    void add(AstLocal* local, AstNode* owner, DeclarationKind kind)
    {
        if (local)
            declarations.push_back({local, owner, kind});
    }

    bool visit(AstStatLocal* node) override
    {
        for (AstLocal* var : node->vars)
            add(var, node, DeclarationKind::Local);
        return true;
    }

    bool visit(AstStatLocalFunction* node) override
    {
        add(node->name, node, DeclarationKind::LocalFunction);
        return true;
    }

    bool visit(AstStatClass* node) override
    {
        add(node->name, node, DeclarationKind::Class);
        return true;
    }

    bool visit(AstExprFunction* node) override
    {
        add(node->self, node, DeclarationKind::Parameter);
        for (AstLocal* arg : node->args)
            add(arg, node, DeclarationKind::Parameter);
        return true;
    }

    bool visit(AstStatFor* node) override
    {
        add(node->var, node, DeclarationKind::LoopVariable);
        return true;
    }

    bool visit(AstStatForIn* node) override
    {
        for (AstLocal* var : node->vars)
            add(var, node, DeclarationKind::LoopVariable);
        return true;
    }
};

bool isStatement(const Declaration& declaration)
{
    return declaration.kind == DeclarationKind::Local || declaration.kind == DeclarationKind::LocalFunction ||
           declaration.kind == DeclarationKind::Class;
}

// Where a declaring statement's name becomes usable. A `local` can't use its own name in its initializer.
Position scopeStart(const Declaration& declaration)
{
    if (declaration.kind == DeclarationKind::Local)
        return declaration.owner->location.end;
    return declaration.local->location.begin;
}

const AstStatBlock* loopBody(const AstNode* loop)
{
    if (const AstStatFor* forLoop = loop->as<AstStatFor>())
        return forLoop->body;
    if (const AstStatForIn* forIn = loop->as<AstStatForIn>())
        return forIn->body;
    return nullptr;
}

// Finds the child of `parent` that holds `pos`, without descending further. Names aren't used inside types.
struct ChildHolding : AstVisitor
{
    const AstNode* parent;
    Position pos;
    AstNode* child = nullptr;

    ChildHolding(const AstNode* parent, Position pos)
        : parent(parent)
        , pos(pos)
    {
    }

    bool visit(AstType* node) override
    {
        return false;
    }

    bool visit(AstNode* node) override
    {
        if (node == parent)
            return true;
        if (!child && node->location.contains(pos))
            child = node;
        return false;
    }
};

// The nodes from `root` down to the innermost one holding `pos`. Blocks are binary searched since their statements
// are in source order.
std::vector<AstNode*> ancestryOf(AstStatBlock* root, Position pos)
{
    std::vector<AstNode*> ancestry{root};
    AstNode* node = root;
    while (node)
    {
        AstNode* next = nullptr;
        if (AstStatBlock* block = node->as<AstStatBlock>())
        {
            auto it = std::partition_point(
                block->body.begin(),
                block->body.end(),
                [&](const AstStat* statement)
                {
                    return statement->location.end <= pos;
                }
            );
            if (it != block->body.end() && (*it)->location.contains(pos))
                next = *it;
        }
        else
        {
            ChildHolding finder{node, pos};
            node->visit(&finder);
            next = finder.child;
        }

        if (next)
            ancestry.push_back(next);
        node = next;
    }
    return ancestry;
}

// Where a name is used and what's around it.
struct UseSite
{
    Position at;
    std::vector<AstNode*> ancestry;
    DenseHashSet<const AstNode*> ancestors{nullptr};
    const DenseHashMap<const AstNode*, const AstStatBlock*>& parentBlocks;
    // The innermost function around the use, or the root
    const AstNode* function = nullptr;

    UseSite(AstStatBlock* root, Position at, const DenseHashMap<const AstNode*, const AstStatBlock*>& parentBlocks)
        : at(at)
        , ancestry(ancestryOf(root, at))
        , parentBlocks(parentBlocks)
        , function(root)
    {
        for (AstNode* node : ancestry)
        {
            ancestors.insert(node);
            if (node->is<AstExprFunction>())
                function = node;
        }
    }

    // Whether a statement declaring a local sits directly in a block around the use
    bool inEnclosingBlock(const Declaration& declaration) const
    {
        const AstStatBlock* const* block = parentBlocks.find(declaration.owner);
        return block && ancestors.contains(*block);
    }

    bool inScope(const Declaration& declaration) const
    {
        switch (declaration.kind)
        {
        case DeclarationKind::Parameter:
            return ancestors.contains(declaration.owner);
        case DeclarationKind::LoopVariable:
        {
            const AstStatBlock* body = loopBody(declaration.owner);
            return body && ancestors.contains(body);
        }
        default:
            return inEnclosingBlock(declaration) && scopeStart(declaration) <= at;
        }
    }
};

// Whether a function starts between `node` and the end of `ancestry`
bool crossesFunction(const std::vector<AstNode*>& ancestry, const AstNode* node)
{
    auto it = std::find(ancestry.begin(), ancestry.end(), node);
    return std::any_of(
        it,
        ancestry.end(),
        [](const AstNode* below)
        {
            return below->is<AstExprFunction>();
        }
    );
}

// What a reader would call the statement a binding should move above
const char* constructName(const AstNode* statement)
{
    if (statement->is<AstStatIf>())
        return "'if' statement";
    if (statement->is<AstStatWhile>() || statement->is<AstStatRepeat>() || statement->is<AstStatFor>() || statement->is<AstStatForIn>())
        return "loop";
    if (statement->is<AstStatBlock>())
        return "'do' block";
    return nullptr;
}

} // namespace

UnknownVariableExplainer::UnknownVariableExplainer(AstStatBlock* root)
    : root(root)
{
}

void UnknownVariableExplainer::index()
{
    indexed = true;
    if (!root)
        return;

    DeclarationCollector collector{declarations, parentBlocks};
    root->visit(&collector);
    for (size_t i = 0; i < declarations.size(); ++i)
        declarationsByName[declarations[i].local->name].push_back(i);
}

const std::vector<std::string>& UnknownVariableExplainer::globalsOf(const Scope& scope)
{
    // The module's scope is the one just inside the outermost, which holds the builtins
    const Scope* module = &scope;
    while (module->parent && module->parent->parent)
        module = module->parent.get();

    if (const std::vector<std::string>* cached = globalsByModuleScope.find(module))
        return *cached;

    std::vector<std::string>& globals = globalsByModuleScope[module];
    for (const Scope* current = module; current; current = current->parent.get())
    {
        for (const auto& [symbol, binding] : current->bindings)
        {
            if (symbol.global.value)
                globals.emplace_back(symbol.global.value);
        }
    }
    return globals;
}

// Words Luwu uses or reserves as keywords. A stray one is unfinished syntax, not a misspelling: `match x` isn't `math`.
static bool isLuwuKeyword(const std::string& name)
{
    static const char* const keywords[] = {"match", "case", "class", "trait", "implements", "needs", "give", "when"};
    return std::find(std::begin(keywords), std::end(keywords), name) != std::end(keywords);
}

// The name in scope that `written` is probably a misspelling of. Typos keep the first letter: `number` isn't `tonumber`.
static std::optional<std::string> closestName(
    const std::vector<Declaration>& declarations,
    const UseSite& site,
    const std::string& written,
    const std::vector<std::string>& globals
)
{
    if (isLuwuKeyword(written))
        return std::nullopt;

    std::optional<std::string> closest;
    size_t closestDistance = std::numeric_limits<size_t>::max();
    auto consider = [&](const std::string& candidate)
    {
        const bool sameFirstLetter = !candidate.empty() && !written.empty() && tolower(candidate[0]) == tolower(written[0]);
        if (!sameFirstLetter || letterCountDifference(written, candidate) > kMaxTypoLetterCountDifference)
            return;
        std::optional<size_t> distance = typoDistance(written, candidate);
        if (distance && *distance < closestDistance)
        {
            closest = candidate;
            closestDistance = *distance;
        }
    };
    auto declaredBefore = std::partition_point(
        declarations.begin(),
        declarations.end(),
        [&](const Declaration& declaration)
        {
            return declaration.local->location.begin < site.at;
        }
    );
    size_t candidates = 0;
    for (auto it = declaredBefore; it != declarations.begin() && candidates < kMaxTypoCandidates;)
    {
        --it;
        if (site.inScope(*it))
        {
            consider(it->local->name.value);
            candidates++;
        }
    }
    for (const std::string& global : globals)
        consider(global);
    return closest;
}

std::optional<std::string> UnknownVariableExplainer::misspelledName(const AstExprGlobal* use, const std::vector<std::string>& globals)
{
    if (!indexed)
        index();
    if (!root)
        return std::nullopt;

    const UseSite site{root, use->location.begin, parentBlocks};
    return closestName(declarations, site, use->name.value, globals);
}

UnknownVariableExplanation UnknownVariableExplainer::explain(const AstExprGlobal* use, const std::vector<std::string>& globals)
{
    if (!indexed)
        index();

    UnknownVariableExplanation result;
    if (!root)
        return result;

    const UseSite site{root, use->location.begin, parentBlocks};

    // A misspelled name in scope is the likeliest fix
    if (std::optional<std::string> closest = closestName(declarations, site, use->name.value, globals))
    {
        result.unbound = UnknownSymbol::Unbound::Typo;
        result.meant = *closest;
        return result;
    }

    const std::vector<size_t>* sameName = declarationsByName.find(use->name);
    if (!sameName)
        return result;

    // A local declared after the use in a block around it
    for (size_t i : *sameName)
    {
        const Declaration& declaration = declarations[i];
        if (isStatement(declaration) && site.inEnclosingBlock(declaration) && site.at < scopeStart(declaration))
        {
            result.unbound = UnknownSymbol::Unbound::BeforeDefinition;
            result.definition = declaration.local->location;
            return result;
        }
    }

    // A local in an earlier `if`, loop or `do` block of the same function. Python keeps it after the block but Luau doesn't.
    // `sameName` is in source order, so the declarations before the use are a prefix of it.
    auto declaredBefore = std::partition_point(
        sameName->begin(),
        sameName->end(),
        [&](size_t i)
        {
            return declarations[i].local->location.end <= site.at;
        }
    );
    const size_t considered = std::min(size_t(declaredBefore - sameName->begin()), kMaxInnerScopeCandidates);
    const AstNode* holder = nullptr;
    std::vector<const Declaration*> held;
    for (auto it = declaredBefore - considered; it != declaredBefore; ++it)
    {
        const Declaration& declaration = declarations[*it];
        const bool inUseFunction = site.function->location.encloses(declaration.local->location);
        if (!isStatement(declaration) || !inUseFunction)
            continue;

        // The statement of a block around the use that holds the declaration
        std::vector<AstNode*>* cachedAncestry = declarationAncestries.find(declaration.local);
        if (!cachedAncestry)
        {
            declarationAncestries[declaration.local] = ancestryOf(root, declaration.local->location.begin);
            cachedAncestry = declarationAncestries.find(declaration.local);
        }
        const std::vector<AstNode*>& declarationAncestry = *cachedAncestry;
        size_t sharedBlock = declarationAncestry.size();
        for (size_t j = 0; j < declarationAncestry.size(); ++j)
        {
            if (declarationAncestry[j]->is<AstStatBlock>() && site.ancestors.contains(declarationAncestry[j]))
                sharedBlock = j;
        }
        if (sharedBlock + 1 >= declarationAncestry.size())
            continue;

        const AstNode* block = declarationAncestry[sharedBlock];
        const AstNode* statement = declarationAncestry[sharedBlock + 1];
        const bool sameFunction = !crossesFunction(declarationAncestry, block) && !crossesFunction(site.ancestry, block);
        if (statement == declaration.owner || !sameFunction || !constructName(statement))
            continue;

        // The closest such statement wins. Every binding of the name inside it gets a marker.
        if (statement != holder)
        {
            holder = statement;
            held.clear();
        }
        held.push_back(&declaration);
    }

    if (!holder)
        return result;

    result.unbound = UnknownSymbol::Unbound::InnerScope;
    result.definition = held.front()->local->location;
    const std::string line = std::to_string(use->location.begin.line + 1);
    for (const Declaration* declaration : held)
    {
        if (noted.contains(declaration->local))
            continue;
        noted.insert(declaration->local);
        result.notes.push_back(
            {declaration->local->location,
             "Consider moving this binding above this " + std::string(constructName(holder)) + " to use it on line " + line,
             use->location}
        );
    }
    return result;
}

UnknownSymbol unknownVariableError(
    UnknownVariableExplainer& explainer,
    const AstExprGlobal* use,
    const std::vector<std::string>& globals,
    std::vector<DiagnosticNote>& notes
)
{
    UnknownVariableExplanation explanation = explainer.explain(use, globals);
    UnknownSymbol error{use->name.value, UnknownSymbol::Binding};
    error.unbound = explanation.unbound;
    error.meant = std::move(explanation.meant);
    error.definition = explanation.definition;
    for (DiagnosticNote& note : explanation.notes)
        notes.push_back(std::move(note));
    return error;
}

UnknownSymbol unknownVariableError(
    UnknownVariableExplainer& explainer,
    const AstExprGlobal* use,
    const Scope& scope,
    std::vector<DiagnosticNote>& notes
)
{
    return unknownVariableError(explainer, use, explainer.globalsOf(scope), notes);
}

std::optional<UnknownSymbol> misspelledLocalError(UnknownVariableExplainer& explainer, const AstExprGlobal* use)
{
    std::optional<std::string> meant = explainer.misspelledName(use, {});
    if (!meant)
        return std::nullopt;

    // A global named like a local's plural is meant: `players = {}` beside `local player`
    const std::string written = use->name.value;
    const bool pluralPair = written == *meant + "s" || written == *meant + "es" || *meant == written + "s" || *meant == written + "es";
    if (pluralPair)
        return std::nullopt;

    UnknownSymbol error{use->name.value, UnknownSymbol::Binding};
    error.unbound = UnknownSymbol::Unbound::Typo;
    error.meant = std::move(*meant);
    return error;
}

std::string unknownVariableMessage(const UnknownSymbol& error)
{
    const std::string name = "'" + error.name + "'";
    const std::string definedOn = error.definition ? std::to_string(error.definition->begin.line + 1) : "";
    switch (error.unbound)
    {
    case UnknownSymbol::Unbound::Typo:
        return typoMessage(error.name, error.meant);
    case UnknownSymbol::Unbound::BeforeDefinition:
        if (error.definition)
            return "Can't use " + name + " defined on line " + definedOn + " before it exists";
        break;
    case UnknownSymbol::Unbound::InnerScope:
        if (error.definition)
            return name + " is not accessible here because it was defined in an inner scope on line " + definedOn;
        break;
    case UnknownSymbol::Unbound::NotDefined:
        break;
    }
    return "Unknown variable " + name + ", define it with 'local " + error.name + " = value'";
}

} // namespace Luau
