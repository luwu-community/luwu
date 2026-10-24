// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Core/Ast.h"

namespace Luwu::Dsl
{

class Evaluator;

// Recognizes doc-comment forms: the reader turns ~ comments into (doc-comment "line"... e),
// or (doc-comment :trailing "text" e) for comments trailing code on the same line.
// Definitions attach comments as documentation; other expressions forward them to doc hooks.
void installDocComments(Evaluator& evaluator);

// Returns true if expression e is a (doc-comment ... e) wrapper enclosing an expression.
bool isDocWrapper(const SExpr& e);
// Strips enclosing doc-comment wrappers from expression e.
const SExpr& unwrapDocs(const SExpr& e);

// Expands a doc-comment form, attaching documentation to definitions, emitting standalone comments, or wrapping values in %doc.
SExpr expandDocComment(const SExpr& form, const Evaluator& evaluator);

} // namespace Luwu::Dsl
