// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace Luwu::Dsl
{

enum class TokenType
{
    LParen,      // (
    RParen,      // )
    LBracket,    // [
    RBracket,    // ]
    Symbol,      // e.g. define-rule, set-savedpc, load-ptr, store-ptr!
    Keyword,     // e.g. :uint, :lua_State.ci
    Number,      // e.g. 123, 0x10
    String,      // e.g. "..."
    DocComment,  // ~ to the end of the line; text excludes the ~ and one following space
    Prefix,      // ' ` , ,@ (quote, quasiquote, unquote, unquote-splicing the next expression)
    Eof,
};

struct Token
{
    TokenType type = TokenType::Eof;
    std::string text;
    int64_t intValue = 0;
    int line = 1;
    int col = 1;
};

class Lexer
{
public:
    Lexer(std::string_view source, std::string filename = "<input>");

    Token nextToken();

    const std::string& getFilename() const { return filename; }

private:
    char peek() const;
    char advance();
    bool isAtEnd() const;
    void skipWhitespaceAndComments();

    Token makeToken(TokenType type, std::string text);
    Token lexNumber(int startCol);
    Token lexSymbolOrKeyword(int startCol);
    Token lexString(int startCol);
    Token lexDocComment(int startCol);

    std::string_view source;
    std::string filename;
    size_t cursor = 0;
    int line = 1;
    int col = 1;
};

} // namespace Luwu::Dsl
