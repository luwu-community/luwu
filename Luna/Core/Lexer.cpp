// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Lexer.h"
#include "Core/CompileError.h"

#include <cctype>
#include <stdexcept>

namespace Luwu::Dsl
{

Lexer::Lexer(std::string_view source, std::string filename)
    : source(source)
    , filename(std::move(filename))
{
}

char Lexer::peek() const
{
    if (isAtEnd())
        return '\0';
    return source[cursor];
}

char Lexer::advance()
{
    if (isAtEnd())
        return '\0';
    char c = source[cursor++];
    if (c == '\n')
    {
        line++;
        col = 1;
    }
    else
    {
        col++;
    }
    return c;
}

bool Lexer::isAtEnd() const
{
    return cursor >= source.size();
}

void Lexer::skipWhitespaceAndComments()
{
    while (!isAtEnd())
    {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            advance();
        }
        else if (c == ';')
        {
            // Skip comment until newline
            while (!isAtEnd() && peek() != '\n')
                advance();
        }
        else
        {
            break;
        }
    }
}

Token Lexer::makeToken(TokenType type, std::string text)
{
    Token tok;
    tok.type = type;
    tok.text = std::move(text);
    tok.line = line;
    tok.col = col;
    return tok;
}

static bool isSymbolChar(char c)
{
    if (std::isalnum(static_cast<unsigned char>(c)))
        return true;
    switch (c)
    {
    case '-':
    case '_':
    case '?':
    case '!':
    case '+':
    case '*':
    case '/':
    case '<':
    case '>':
    case '=':
    case '.':
    case '%':
    case '$':
    case '|': // operand kind unions in define-ircmd, e.g. :Rn|:Kn
    case '&': // operator names, e.g. a module's &&
    case '^':
        return true;
    default:
        return false;
    }
}

Token Lexer::lexNumber(int startCol)
{
    size_t start = cursor;
    int curLine = line;

    if (peek() == '-' || peek() == '+')
        advance();

    bool isHex = false;
    bool isFloat = false;
    if (peek() == '0' && cursor + 1 < source.size() && (source[cursor + 1] == 'x' || source[cursor + 1] == 'X'))
    {
        advance(); // '0'
        advance(); // 'x'
        isHex = true;
        while (!isAtEnd() && std::isxdigit(static_cast<unsigned char>(peek())))
            advance();
    }
    else
    {
        while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek())))
            advance();

        // Floating-point literal (C++ syntax): digits '.' digits [exponent] ['f']
        if (peek() == '.' && cursor + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[cursor + 1])))
        {
            isFloat = true;
            advance();
            while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek())))
                advance();
            if (peek() == 'e' || peek() == 'E')
            {
                advance();
                if (peek() == '-' || peek() == '+')
                    advance();
                while (!isAtEnd() && std::isdigit(static_cast<unsigned char>(peek())))
                    advance();
            }
            if (peek() == 'f')
                advance();
        }
    }

    size_t digitsStart = start + (source[start] == '-' || source[start] == '+') + (isHex ? 2 : 0);

    bool malformed = cursor == digitsStart || (!isAtEnd() && (isSymbolChar(peek()) || peek() == ':'));
    while (!isAtEnd() && (isSymbolChar(peek()) || peek() == ':'))
        advance();

    std::string text(source.substr(start, cursor - start));
    SourceLocation loc{filename, curLine, startCol};
    if (malformed)
        throw CompileError(loc, "Malformed number literal '" + text + "'");

    Token tok;
    tok.type = TokenType::Number;
    tok.text = text;
    try
    {
        if (!isFloat)
            tok.intValue = isHex ? std::stoll(text, nullptr, 16) : std::stoll(text, nullptr, 10);
    }
    catch (const std::out_of_range&)
    {
        throw CompileError(loc, "Number literal '" + text + "' does not fit in a signed 64-bit integer");
    }
    tok.line = curLine;
    tok.col = startCol;
    return tok;
}

Token Lexer::lexSymbolOrKeyword(int startCol)
{
    size_t start = cursor;
    int curLine = line;
    bool isKeyword = (peek() == ':');

    if (isKeyword)
        advance(); // consume ':'

    while (!isAtEnd() && (isSymbolChar(peek()) || peek() == ':'))
        advance();

    std::string text(source.substr(start, cursor - start));
    Token tok;
    tok.type = isKeyword ? TokenType::Keyword : TokenType::Symbol;
    tok.text = text;
    tok.line = curLine;
    tok.col = startCol;
    return tok;
}

Token Lexer::lexString(int startCol)
{
    int curLine = line;
    advance(); // consume opening '"'
    std::string result;

    while (!isAtEnd() && peek() != '"')
    {
        char c = advance();
        if (c == '\\' && !isAtEnd())
        {
            char esc = advance();
            if (esc == 'n')
                result += '\n';
            else if (esc == 't')
                result += '\t';
            else if (esc == '"')
                result += '"';
            else if (esc == '\\')
                result += '\\';
            else
                result += esc;
        }
        else
        {
            result += c;
        }
    }

    if (isAtEnd())
        throw CompileError(SourceLocation{filename, curLine, startCol}, "Unterminated string");

    advance(); // consume closing '"'

    Token tok;
    tok.type = TokenType::String;
    tok.text = result;
    tok.line = curLine;
    tok.col = startCol;
    return tok;
}

Token Lexer::lexDocComment(int startCol)
{
    int startLine = line;
    advance(); // ~
    if (peek() == ' ')
        advance();

    std::string text;
    while (!isAtEnd() && peek() != '\n')
        text += advance();

    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        text.pop_back();

    // The text becomes a // comment, where a trailing backslash would continue the comment onto the next C++ line
    if (!text.empty() && text.back() == '\\')
        throw CompileError(SourceLocation{filename, startLine, startCol}, "Doc comment can't end with a backslash (it would continue the C++ comment onto the next line)");

    Token tok;
    tok.type = TokenType::DocComment;
    tok.text = std::move(text);
    tok.line = startLine;
    tok.col = startCol;
    return tok;
}

Token Lexer::nextToken()
{
    skipWhitespaceAndComments();

    if (isAtEnd())
    {
        Token tok;
        tok.type = TokenType::Eof;
        tok.line = line;
        tok.col = col;
        return tok;
    }

    char c = peek();
    int curCol = col;
    int curLine = line;

    if (c == '~')
        return lexDocComment(curCol);

    if (c == '(')
    {
        advance();
        Token tok;
        tok.type = TokenType::LParen;
        tok.text = "(";
        tok.line = curLine;
        tok.col = curCol;
        return tok;
    }

    if (c == ')')
    {
        advance();
        Token tok;
        tok.type = TokenType::RParen;
        tok.text = ")";
        tok.line = curLine;
        tok.col = curCol;
        return tok;
    }

    if (c == '[')
    {
        advance();
        Token tok;
        tok.type = TokenType::LBracket;
        tok.text = "[";
        tok.line = curLine;
        tok.col = curCol;
        return tok;
    }

    if (c == ']')
    {
        advance();
        Token tok;
        tok.type = TokenType::RBracket;
        tok.text = "]";
        tok.line = curLine;
        tok.col = curCol;
        return tok;
    }

    if (c == '"')
        return lexString(curCol);

    // Booleans: #t and #f (read as the symbols "#t" and "#f", which evaluate to themselves)
    if (c == '#')
    {
        advance();
        char v = isAtEnd() ? '\0' : peek();
        if ((v == 't' || v == 'f') && (cursor + 1 >= source.size() || !(isSymbolChar(source[cursor + 1]) || source[cursor + 1] == ':')))
        {
            advance();
            Token tok;
            tok.type = TokenType::Symbol;
            tok.text = v == 't' ? "#t" : "#f";
            tok.line = curLine;
            tok.col = curCol;
            return tok;
        }
        throw CompileError(SourceLocation{filename, curLine, curCol}, "Unexpected '#': the booleans are #t and #f");
    }

    if (c == '\'' || c == '`' || c == ',')
    {
        advance();
        Token tok;
        tok.type = TokenType::Prefix;
        tok.text = std::string(1, c);
        if (c == ',' && !isAtEnd() && peek() == '@')
        {
            advance();
            tok.text = ",@";
        }
        tok.line = curLine;
        tok.col = curCol;
        return tok;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) ||
        ((c == '-' || c == '+') && cursor + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[cursor + 1]))))
    {
        return lexNumber(curCol);
    }

    if (c == ':' || isSymbolChar(c))
        return lexSymbolOrKeyword(curCol);

    throw CompileError(SourceLocation{filename, curLine, curCol}, std::string("Unexpected character '") + c + "'");
}

} // namespace Luwu::Dsl
