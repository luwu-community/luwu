// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"
#include "Lexer.h"

#include <vector>

namespace Luwu::Dsl
{

class Parser
{
public:
    explicit Parser(Lexer& lexer);

    std::vector<SExpr> parseProgram();
    SExpr parseExpr();

private:
    Token advance();
    const Token& peek() const;
    bool match(TokenType type);
    Token expect(TokenType type, const std::string& message);

    Lexer& lexer;
    Token current;
};

} // namespace Luwu::Dsl
