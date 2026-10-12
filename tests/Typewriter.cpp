// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// Luwu typewriter tests: catch syntax and type error cascades by typing code into a file one character at a time, the
// way a user would in an editor running luwu-lsp. Typewriter.h says what counts as a cascade.
#include "Typewriter.h"

#include "Fixture.h"

#include "Luau/Error.h"
#include "Luau/StringUtils.h"

#include "doctest.h"

#include <vector>

namespace Luau
{

namespace
{

// Each opener an editor closes for you, with its closer
const char kAutoClosedPairs[][2] = {{'(', ')'}, {'[', ']'}, {'{', '}'}, {'"', '"'}, {'`', '`'}};

// The parse error for a block still missing its `end`. Until the `end` is typed, the block runs to the end of the file.
bool isUnclosedBlock(const ParseError& error)
{
    return error.getMessage().rfind("Expected 'end' (to close", 0) == 0;
}

bool hasUnclosedBlock(const ParseResult& parsed)
{
    for (const ParseError& error : parsed.errors)
    {
        if (isUnclosedBlock(error))
            return true;
    }
    return false;
}

struct Typewriter
{
    Fixture& fixture;
    bool autocompleteEnd = false;
    std::string buffer;
    size_t cursor = 0;

    // Text the editor inserted after the cursor that hasn't been typed over yet, innermost last, with how much of it
    // has been typed over
    struct Closer
    {
        std::string text;
        size_t typed = 0;
    };
    std::vector<Closer> pendingClosers;

    void type(char c)
    {
        if (stepOverCloser(c))
            return;

        buffer.insert(cursor, 1, c);
        cursor++;

        for (const auto& pair : kAutoClosedPairs)
        {
            if (c == pair[0])
                insertCloser(std::string(1, pair[1]));
        }

        // Like luwu-lsp's autocompleteEnd: Enter after a block opener inserts its `end`
        if (autocompleteEnd && c == '\n' && hasUnclosedBlock(fixture.tryParse(buffer)))
            insertCloser("end");
    }

private:
    void insertCloser(const std::string& text)
    {
        buffer.insert(cursor, text);
        pendingClosers.push_back({text, 0});
    }

    bool stepOverCloser(char c)
    {
        if (pendingClosers.empty())
            return false;

        Closer& closer = pendingClosers.back();
        bool matches = closer.text[closer.typed] == c && cursor < buffer.size() && buffer[cursor] == c;
        if (!matches)
            return false;

        cursor++;
        closer.typed++;
        if (closer.typed == closer.text.size())
            pendingClosers.pop_back();
        return true;
    }
};

Position positionOf(const std::string& source, size_t offset)
{
    unsigned line = 0;
    unsigned column = 0;
    for (size_t i = 0; i < offset; ++i)
    {
        if (source[i] == '\n')
        {
            line++;
            column = 0;
        }
        else
        {
            column++;
        }
    }
    return Position{line, column};
}

// What's wrong with `source` after one keystroke. `untouched` is where the code after the cursor starts.
std::vector<std::string> problemsAfterKeystroke(Fixture& fixture, const std::string& source, Position untouched)
{
    std::vector<std::string> problems;

    // A block whose `end` isn't typed yet gets one error, and holds the code after it until then, so type errors there
    // aren't the keystroke's fault
    ParseResult parsed = fixture.tryParse(source);
    bool unclosedBlock = hasUnclosedBlock(parsed);
    size_t allowedErrors = unclosedBlock ? 2 : 1;
    if (parsed.errors.size() > allowedErrors)
    {
        for (const ParseError& error : parsed.errors)
            problems.push_back(format("parse error at %s: %s", toString(error.getLocation()).c_str(), error.getMessage().c_str()));
    }

    if (unclosedBlock)
        return problems;

    CheckResult checked = fixture.check(source);
    for (const TypeError& error : checked.errors)
    {
        bool cascades = !get<SyntaxError>(error) && untouched <= error.location.begin;
        if (cascades)
            problems.push_back(format("type error after the cursor at %s: %s", toString(error.location).c_str(), toString(error).c_str()));
    }

    return problems;
}

std::string errorsIn(Fixture& fixture, const std::string& source)
{
    std::string errors;
    for (const TypeError& error : fixture.check(source).errors)
        errors += toString(error.location) + ": " + toString(error) + "\n";
    return errors;
}

} // namespace

std::string typewrite(
    Fixture& fixture,
    const std::string& before,
    const std::string& text,
    const std::string& after,
    bool autocompleteEnd
)
{
    Typewriter writer{fixture, autocompleteEnd, before + after, before.size(), {}};
    std::string report;

    for (char c : text)
    {
        writer.type(c);

        size_t untouchedOffset = writer.buffer.size() - after.size();
        std::vector<std::string> problems = problemsAfterKeystroke(fixture, writer.buffer, positionOf(writer.buffer, untouchedOffset));
        if (problems.empty())
            continue;

        report += format("after typing '%s':\n", writer.buffer.substr(before.size(), writer.cursor - before.size()).c_str());
        for (const std::string& problem : problems)
            report += "  " + problem + "\n";
    }

    return report;
}

void checkTypewriting(Fixture& fixture, const std::string& before, const std::string& text, const std::string& after)
{
    std::string finished = before + text + after;
    INFO(finished);
    REQUIRE_EQ("", errorsIn(fixture, before + after));
    REQUIRE_EQ("", errorsIn(fixture, finished));
    for (bool autocompleteEnd : {true, false})
    {
        INFO("autocompleteEnd: " << (autocompleteEnd ? "on" : "off"));
        CHECK_EQ("", typewrite(fixture, before, text, after, autocompleteEnd));
    }
}

} // namespace Luau
