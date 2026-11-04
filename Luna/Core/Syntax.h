// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Core/Ast.h"
#include "Core/Value.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Luwu::Dsl
{

class Evaluator;

// Scope allocation and management for Flatt's "Sets of Scopes" hygiene system.
uint32_t allocScope();

void addScope(Syntax* stx, uint32_t scope);
void removeScope(Syntax* stx, uint32_t scope);
void flipScope(Syntax* stx, uint32_t scope);

void addScope(SExpr& expr, uint32_t scope);
void flipScope(SExpr& expr, uint32_t scope);

// Conversions between parsed S-expressions, first-class Syntax objects, and datums.
Value syntaxFromSExpr(const SExpr& expr, const ScopeSet& initialScopes = {});
SExpr sexprFromSyntax(const Value& val);
Value syntaxToDatum(const Value& stx);
Value datumToSyntax(const Value& ctxt, const Value& datum, const std::optional<SourceLocation>& loc = std::nullopt);

// Identifier predicates and hygiene comparisons.
bool isIdentifier(const Value& val);
bool boundIdentifierEqual(const Value& a, const Value& b);
bool freeIdentifierEqual(const Value& a, const Value& b);

// Pattern variable tree representing scalar matches (depth 0) or repetition levels (depth >= 1).
struct PatternTree
{
    int depth = 0;
    Value leaf;                       // Used when depth == 0
    std::vector<PatternTree> children; // Used when depth >= 1
};

using PatternEnv = std::unordered_map<std::string, PatternTree>;

// Matches a syntax-case pattern against a syntax object.
bool matchPattern(
    const SExpr& pat,
    const Value& target,
    const std::vector<SExpr>& literals,
    PatternEnv& env,
    int currentDepth,
    Evaluator& eval,
    const SExpr& call);

// Transcribes a template into a syntax object substituting pattern variables and splicing ellipses.
Value transcribeTemplate(
    const SExpr& tmpl,
    const PatternEnv& env,
    uint32_t introScope,
    Evaluator& eval,
    const SExpr& call);

} // namespace Luwu::Dsl
