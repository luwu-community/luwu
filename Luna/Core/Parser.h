// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Core/Ast.h"
#include "Core/Lexer.h"

#include <string>
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
    struct TrailingDoc
    {
        int line;
        int col;
        std::string text;
    };

    Token advance();
    void collectDocs();
    std::vector<std::string> takePendingDocs();
    void requireNoPendingDocs();
    void attachTrailingDocs(std::vector<SExpr>& program);
    void wrapDocs(SExpr& expr);
    const Token& peek() const;
    bool match(TokenType type);
    Token expect(TokenType type, const std::string& message);

    Lexer& lexer;
    Token current;

    int lastTokenLine = 0; // line of the last consumed token that wasn't a doc comment

    std::vector<std::string> pendingDocs; // leading docs waiting for the next expression
    SourceLocation pendingDocsLoc;
    std::vector<TrailingDoc> trailingDocs;
};

} // namespace Luwu::Dsl
