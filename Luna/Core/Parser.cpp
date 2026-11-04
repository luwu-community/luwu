// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Parser.h"
#include "Core/CompileError.h"

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

// Finds the outermost expressions that start and end on `line`; the last of them (the one the comment follows).
// `path` is the chain of lists around the current position, `bestPath` the chain around the result.
static void findTrailingTarget(std::vector<SExpr>& exprs, int line, std::vector<SExpr*>& path, SExpr*& best, std::vector<SExpr*>& bestPath)
{
    for (auto& expr : exprs)
    {
        if (expr.loc.line > line)
            break;
        if (expr.loc.line == line && expr.endLine == line)
        {
            best = &expr;
            bestPath = path;
        }
        else if (expr.loc.line <= line && expr.endLine >= line)
        {
            path.push_back(&expr);
            findTrailingTarget(expr.elements, line, path, best, bestPath);
            path.pop_back();
        }
    }
}

void Parser::attachTrailingDocs(std::vector<SExpr>& program)
{
    for (const auto& doc : trailingDocs)
    {
        SExpr* target = nullptr;
        std::vector<SExpr*> path, targetPath;
        findTrailingTarget(program, doc.line, path, target, targetPath);
        // Part of a form that starts on this line (its head, an argument): the comment is about the form
        while (target && !targetPath.empty() && targetPath.back()->loc.line == doc.line)
        {
            target = targetPath.back();
            targetPath.pop_back();
        }
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
    for (auto& form : program)
        wrapDocs(form);
    return program;
}

static SExpr docAtom(SExprKind kind, const std::string& text, const SourceLocation& loc)
{
    SExpr e;
    e.kind = kind;
    e.text = text;
    e.loc = loc;
    e.endLine = loc.line;
    return e;
}

// ~ comments wrap the expression they document: leading lines give (doc-comment "line"... e), a trailing one
// (doc-comment :trailing "text" e). The wrapper stands where e stood, so every form keeps its structure.
void Parser::wrapDocs(SExpr& expr)
{
    for (auto& el : expr.elements)
        wrapDocs(el);
    if (!expr.hasDocs())
        return;

    std::vector<std::string> leading = std::move(expr.docs);
    std::optional<std::string> trailing = std::move(expr.trailingDoc);
    expr.docs.clear();
    expr.trailingDoc.reset();

    auto wrap = [&](std::vector<SExpr> args) {
        SExpr w;
        w.kind = SExprKind::List;
        w.loc = expr.loc;
        w.endLine = expr.endLine;
        w.elements.push_back(docAtom(SExprKind::Symbol, "doc-comment", expr.loc));
        for (auto& a : args)
            w.elements.push_back(std::move(a));
        w.elements.push_back(std::move(expr));
        expr = std::move(w);
    };
    if (trailing)
        wrap({docAtom(SExprKind::Keyword, ":trailing", expr.loc), docAtom(SExprKind::String, *trailing, expr.loc)});
    if (!leading.empty())
    {
        std::vector<SExpr> lines;
        for (const auto& line : leading)
            lines.push_back(docAtom(SExprKind::String, line, expr.loc));
        wrap(std::move(lines));
    }
}

SExpr Parser::parseExpr()
{
    // 'x `x ,x ,@x read as (quote x) (quasiquote x) (unquote x) (unquote-splicing x)
    // #'x #`x #,x #,@x read as (syntax x) (quasisyntax x) (unsyntax x) (unsyntax-splicing x)
    if (current.type == TokenType::Prefix)
    {
        Token prefix = advance();
        if (current.type == TokenType::Eof || current.type == TokenType::RParen || current.type == TokenType::RBracket)
            throw CompileError(SourceLocation{lexer.getFilename(), prefix.line, prefix.col}, "Expected an expression after " + prefix.text);
        SourceLocation loc{lexer.getFilename(), prefix.line, prefix.col};
        const char* name = "quote";
        if (prefix.text == "'") name = "quote";
        else if (prefix.text == "`") name = "quasiquote";
        else if (prefix.text == ",") name = "unquote";
        else if (prefix.text == ",@") name = "unquote-splicing";
        else if (prefix.text == "#'") name = "syntax";
        else if (prefix.text == "#`") name = "quasisyntax";
        else if (prefix.text == "#,") name = "unsyntax";
        else if (prefix.text == "#,@") name = "unsyntax-splicing";
        SExpr head;
        head.kind = SExprKind::Symbol;
        head.text = name;
        head.loc = loc;
        head.endLine = prefix.line;
        SExpr expr;
        expr.kind = SExprKind::List;
        expr.loc = loc;
        expr.elements.push_back(std::move(head));
        expr.elements.push_back(parseExpr());
        expr.endLine = expr.elements.back().endLine;
        return expr;
    }

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
