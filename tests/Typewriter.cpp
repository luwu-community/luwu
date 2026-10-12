// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// Luwu typewriter tests: catch syntax and type error cascades by typing code into a file one character at a time, the
// way a user would in an editor running luwu-lsp. Typewriter.h says what counts as a cascade.
#include "Typewriter.h"

#include "Fixture.h"

#include "Luau/Error.h"
#include "Luau/Linter.h"
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

// A parse error that only says a block isn't finished yet: its `end` is missing, or its header is still being typed
// (`if x` before `then`). Each block being typed reports these whatever the keystroke.
bool isUnfinishedBlock(const ParseError& error)
{
    const std::string& message = error.getMessage();
    bool headerInProgress = message.rfind("Expected 'then' when parsing", 0) == 0 || message.rfind("Expected 'do' when parsing", 0) == 0;
    return isUnclosedBlock(error) || headerInProgress;
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

    // `rest` is what will be typed after `c`
    void type(char c, const std::string& rest)
    {
        if (stepOverCloser(c, rest))
            return;

        buffer.insert(cursor, 1, c);
        cursor++;

        for (const auto& pair : kAutoClosedPairs)
        {
            if (c == pair[0])
                insertCloser(std::string(1, pair[1]));
        }

        // Like luwu-lsp's autocompleteEnd: Enter after a block opener leaves the cursor on the new line and puts `end` on
        // the line after it, indented like the opener
        if (autocompleteEnd && c == '\n' && hasUnclosedBlock(fixture.tryParse(buffer)))
            insertCloser("\n" + indentationOfLineBefore(cursor - 1) + "end");
    }

private:
    // The leading whitespace of the line that ends at `newline`
    std::string indentationOfLineBefore(size_t newline) const
    {
        size_t lineStart = newline;
        while (lineStart > 0 && buffer[lineStart - 1] != '\n')
            lineStart--;

        size_t end = lineStart;
        while (end < newline && (buffer[end] == ' ' || buffer[end] == '\t'))
            end++;
        return buffer.substr(lineStart, end - lineStart);
    }

    void insertCloser(const std::string& text)
    {
        buffer.insert(cursor, text);
        pendingClosers.push_back({text, 0});
    }

    bool stepOverCloser(char c, const std::string& rest)
    {
        if (pendingClosers.empty())
            return false;

        Closer& closer = pendingClosers.back();
        bool matches = closer.text[closer.typed] == c && cursor < buffer.size() && buffer[cursor] == c;
        if (!matches)
            return false;

        // Enter always adds a line in an editor. It only moves onto an inserted `end` line when that `end` is what the
        // user types next; otherwise it's a new line inside the block.
        bool entersEndLine = c == '\n' && closer.typed == 0;
        if (entersEndLine && rest.compare(0, closer.text.size() - 1, closer.text, 1, std::string::npos) != 0)
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

// Whether `message` is one a case expects alongside another, like a second span pointing at the real problem
bool isIntentional(const std::string& message, const std::vector<std::string>& intentional)
{
    for (const std::string& expected : intentional)
    {
        if (message.find(expected) != std::string::npos)
            return true;
    }
    return false;
}

// Type errors in `source` from `typeErrorsFrom` on, and lint warnings from `lintsFrom` on, with the file checked and
// linted the way luwu-lsp does
std::vector<std::string> problemsFrom(
    Fixture& fixture,
    const std::string& source,
    Position typeErrorsFrom,
    Position lintsFrom,
    const std::vector<std::string>& intentional
)
{
    std::vector<std::string> problems;

    FrontendOptions options = fixture.getFrontend().options;
    options.runLintChecks = true;
    CheckResult checked = fixture.check(source, options);

    for (const TypeError& error : checked.errors)
    {
        std::string message = toString(error);
        bool reported = !get<SyntaxError>(error) && typeErrorsFrom <= error.location.begin && !isIntentional(message, intentional);
        if (reported)
            problems.push_back(format("type error at %s: %s", toString(error.location).c_str(), message.c_str()));
    }

    for (const std::vector<LintWarning>* warnings : {&checked.lintResult.errors, &checked.lintResult.warnings})
    {
        for (const LintWarning& warning : *warnings)
        {
            if (lintsFrom <= warning.location.begin && !isIntentional(warning.text, intentional))
            {
                problems.push_back(format(
                    "lint at %s: %s: %s", toString(warning.location).c_str(), LintWarning::getName(warning.code), warning.text.c_str()
                ));
            }
        }
    }

    return problems;
}

// What's wrong with `source` after one keystroke. `untouched` is where the code after the cursor starts.
std::vector<std::string> problemsAfterKeystroke(
    Fixture& fixture,
    const std::string& source,
    Position untouched,
    const std::vector<std::string>& intentional
)
{
    std::vector<std::string> problems;

    // An unfinished block's own errors don't count, and the block holds the code after it until its `end` is typed,
    // so type errors and lints there aren't the keystroke's fault
    ParseResult parsed = fixture.tryParse(source);
    bool unfinishedBlock = false;
    size_t errors = 0;
    for (const ParseError& error : parsed.errors)
    {
        if (isUnfinishedBlock(error))
            unfinishedBlock = true;
        else if (!isIntentional(error.getMessage(), intentional))
            errors++;
    }

    if (errors > 1)
    {
        for (const ParseError& error : parsed.errors)
            problems.push_back(format("parse error at %s: %s", toString(error.getLocation()).c_str(), error.getMessage().c_str()));
    }

    if (unfinishedBlock)
        return problems;

    for (const std::string& problem : problemsFrom(fixture, source, untouched, untouched, intentional))
        problems.push_back("after the cursor, " + problem);

    return problems;
}

std::string joined(const std::vector<std::string>& lines)
{
    std::string result;
    for (const std::string& line : lines)
        result += line + "\n";
    return result;
}

} // namespace

std::string typewrite(
    Fixture& fixture,
    const std::string& before,
    const std::string& text,
    const std::string& after,
    bool autocompleteEnd,
    const std::vector<std::string>& intentional
)
{
    Typewriter writer{fixture, autocompleteEnd, before + after, before.size(), {}};
    std::string report;

    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        writer.type(c, text.substr(i + 1));

        size_t untouchedOffset = writer.buffer.size() - after.size();
        std::vector<std::string> problems = problemsAfterKeystroke(fixture, writer.buffer, positionOf(writer.buffer, untouchedOffset), intentional);
        if (problems.empty())
            continue;

        report += format("after typing '%s':\n", writer.buffer.substr(before.size(), writer.cursor - before.size()).c_str());
        for (const std::string& problem : problems)
            report += "  " + problem + "\n";
    }

    return report;
}

void checkTypewriting(
    Fixture& fixture,
    const std::string& before,
    const std::string& text,
    const std::string& after,
    const std::vector<std::string>& intentional
)
{
    // No type errors anywhere, and no lints in `after`. `before` may lint: it can declare what only `text` uses.
    std::string untyped = before + after;
    std::string finished = before + text + after;
    INFO(finished);
    REQUIRE_EQ("", joined(problemsFrom(fixture, untyped, Position{0, 0}, positionOf(untyped, before.size()), {})));
    REQUIRE_EQ("", joined(problemsFrom(fixture, finished, Position{0, 0}, positionOf(finished, before.size() + text.size()), {})));
    for (bool autocompleteEnd : {true, false})
    {
        std::string mode = autocompleteEnd ? "autocompleteEnd on" : "autocompleteEnd off";
        INFO(mode);
        CHECK_EQ("", typewrite(fixture, before, text, after, autocompleteEnd, intentional));
    }
}

} // namespace Luau
