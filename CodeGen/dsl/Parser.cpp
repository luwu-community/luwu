// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Parser.h"

#include <stdexcept>

namespace Luwu::Dsl
{

Parser::Parser(Lexer& lexer)
    : lexer(lexer)
{
    current = lexer.nextToken();
}

Token Parser::advance()
{
    Token prev = current;
    current = lexer.nextToken();
    return prev;
}

const Token& Parser::peek() const
{
    return current;
}

bool Parser::match(TokenType type)
{
    if (current.type == type)
    {
        advance();
        return true;
    }
    return false;
}

Token Parser::expect(TokenType type, const std::string& message)
{
    if (current.type == type)
        return advance();

    throw std::runtime_error(
        lexer.getFilename() + ":" + std::to_string(current.line) + ":" + std::to_string(current.col) + ": Error: " + message +
        " (got '" + current.text + "')"
    );
}

std::vector<SExpr> Parser::parseProgram()
{
    std::vector<SExpr> program;
    while (current.type != TokenType::Eof)
    {
        program.push_back(parseExpr());
    }
    return program;
}

SExpr Parser::parseExpr()
{
    if (current.type == TokenType::LParen)
    {
        Token start = advance();
        SExpr expr;
        expr.kind = SExprKind::List;
        expr.line = start.line;
        expr.col = start.col;

        while (current.type != TokenType::RParen && current.type != TokenType::Eof)
        {
            expr.elements.push_back(parseExpr());
        }

        expect(TokenType::RParen, "Expected closing ')'");
        return expr;
    }

    if (current.type == TokenType::LBracket)
    {
        Token start = advance();
        SExpr expr;
        expr.kind = SExprKind::BracketList;
        expr.line = start.line;
        expr.col = start.col;

        while (current.type != TokenType::RBracket && current.type != TokenType::Eof)
        {
            expr.elements.push_back(parseExpr());
        }

        expect(TokenType::RBracket, "Expected closing ']'");
        return expr;
    }

    if (current.type == TokenType::Symbol)
    {
        Token tok = advance();
        SExpr expr;
        expr.kind = SExprKind::Symbol;
        expr.text = tok.text;
        expr.line = tok.line;
        expr.col = tok.col;
        return expr;
    }

    if (current.type == TokenType::Keyword)
    {
        Token tok = advance();
        SExpr expr;
        expr.kind = SExprKind::Keyword;
        expr.text = tok.text;
        expr.line = tok.line;
        expr.col = tok.col;
        return expr;
    }

    if (current.type == TokenType::Number)
    {
        Token tok = advance();
        SExpr expr;
        expr.kind = SExprKind::Number;
        expr.text = tok.text;
        expr.intValue = tok.intValue;
        expr.line = tok.line;
        expr.col = tok.col;
        return expr;
    }

    if (current.type == TokenType::String)
    {
        Token tok = advance();
        SExpr expr;
        expr.kind = SExprKind::String;
        expr.text = tok.text;
        expr.line = tok.line;
        expr.col = tok.col;
        return expr;
    }

    throw std::runtime_error(
        lexer.getFilename() + ":" + std::to_string(current.line) + ":" + std::to_string(current.col) + ": Unexpected token '" + current.text + "'"
    );
}

} // namespace Luwu::Dsl
