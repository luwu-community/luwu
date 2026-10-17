// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Parser.h"
#include "CompileError.h"

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
    if (prev.type != TokenType::DocComment)
        lastTokenLine = prev.line;
    current = lexer.nextToken();
    return prev;
}

// A doc comment on the same line as the previous token trails it, otherwise it leads the next expression
void Parser::collectDocs()
{
    while (current.type == TokenType::DocComment)
    {
        if (lastTokenLine != 0 && current.line == lastTokenLine)
        {
            trailingDocs.push_back({current.line, current.col, current.text});
        }
        else
        {
            if (pendingDocs.empty())
                pendingDocsLoc = {lexer.getFilename(), current.line, current.col};
            pendingDocs.push_back(current.text);
        }
        advance();
    }
}

std::vector<std::string> Parser::takePendingDocs()
{
    std::vector<std::string> docs = std::move(pendingDocs);
    pendingDocs.clear();
    return docs;
}

void Parser::requireNoPendingDocs()
{
    if (!pendingDocs.empty())
        throw CompileError(pendingDocsLoc, "Doc comment isn't attached to anything (it must come before an expression)");
}

// Finds the outermost expressions that start and end on `line`; the last of them (the one the comment follows)
static void findTrailingTarget(std::vector<SExpr>& exprs, int line, SExpr*& best)
{
    for (auto& expr : exprs)
    {
        if (expr.loc.line > line)
            break;
        if (expr.loc.line == line && expr.endLine == line)
            best = &expr;
        else if (expr.loc.line <= line && expr.endLine >= line)
            findTrailingTarget(expr.elements, line, best);
    }
}

void Parser::attachTrailingDocs(std::vector<SExpr>& program)
{
    for (const auto& doc : trailingDocs)
    {
        SExpr* target = nullptr;
        findTrailingTarget(program, doc.line, target);
        if (!target)
            throw CompileError(SourceLocation{lexer.getFilename(), doc.line, doc.col},
                "Trailing doc comment has no expression on its line to attach to (only closing brackets)");
        target->trailingDoc = doc.text;
    }
    trailingDocs.clear();
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

    throw CompileError(
        SourceLocation{lexer.getFilename(), current.line, current.col},
        message + " (got '" + current.text + "')"
    );
}

std::vector<SExpr> Parser::parseProgram()
{
    std::vector<SExpr> program;
    collectDocs();
    while (current.type != TokenType::Eof)
    {
        std::vector<std::string> docs = takePendingDocs();
        program.push_back(parseExpr());
        program.back().docs = std::move(docs);
        collectDocs();
    }
    requireNoPendingDocs();
    attachTrailingDocs(program);
    return program;
}

SExpr Parser::parseExpr()
{
    if (current.type == TokenType::LParen)
    {
        Token start = advance();
        SExpr expr;
        expr.kind = SExprKind::List;
        expr.loc = {lexer.getFilename(), start.line, start.col};

        collectDocs();
        while (current.type != TokenType::RParen && current.type != TokenType::Eof)
        {
            std::vector<std::string> docs = takePendingDocs();
            expr.elements.push_back(parseExpr());
            expr.elements.back().docs = std::move(docs);
            collectDocs();
        }
        requireNoPendingDocs();

        expr.endLine = expect(TokenType::RParen, "Expected closing ')'").line;
        return expr;
    }

    if (current.type == TokenType::LBracket)
    {
        Token start = advance();
        SExpr expr;
        expr.kind = SExprKind::BracketList;
        expr.loc = {lexer.getFilename(), start.line, start.col};

        collectDocs();
        while (current.type != TokenType::RBracket && current.type != TokenType::Eof)
        {
            std::vector<std::string> docs = takePendingDocs();
            expr.elements.push_back(parseExpr());
            expr.elements.back().docs = std::move(docs);
            collectDocs();
        }
        requireNoPendingDocs();

        expr.endLine = expect(TokenType::RBracket, "Expected closing ']'").line;
        return expr;
    }

    if (current.type == TokenType::Symbol)
    {
        Token tok = advance();
        SExpr expr;
        expr.kind = SExprKind::Symbol;
        expr.text = tok.text;
        expr.loc = {lexer.getFilename(), tok.line, tok.col};
        expr.endLine = tok.line;
        return expr;
    }

    if (current.type == TokenType::Keyword)
    {
        Token tok = advance();
        SExpr expr;
        expr.kind = SExprKind::Keyword;
        expr.text = tok.text;
        expr.loc = {lexer.getFilename(), tok.line, tok.col};
        expr.endLine = tok.line;
        return expr;
    }

    if (current.type == TokenType::Number)
    {
        Token tok = advance();
        SExpr expr;
        expr.kind = SExprKind::Number;
        expr.text = tok.text;
        expr.intValue = tok.intValue;
        expr.loc = {lexer.getFilename(), tok.line, tok.col};
        expr.endLine = tok.line;
        return expr;
    }

    if (current.type == TokenType::String)
    {
        Token tok = advance();
        SExpr expr;
        expr.kind = SExprKind::String;
        expr.text = tok.text;
        expr.loc = {lexer.getFilename(), tok.line, tok.col};
        expr.endLine = tok.line;
        return expr;
    }

    throw CompileError(
        SourceLocation{lexer.getFilename(), current.line, current.col},
        "Unexpected token '" + current.text + "'"
    );
}

} // namespace Luwu::Dsl
