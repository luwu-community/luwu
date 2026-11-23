// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "ClassesFixture.h"
#include "Fixture.h"
#include "Typewriter.h"

#include "doctest.h"

#include "ScopedFlags.h"

LUAU_FASTFLAG(DebugLuwuMatchExpressions)
LUAU_FASTFLAG(LuwuIfLocal)
LUAU_FASTFLAG(LuwuDoExpr)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuGenericNominals)
LUAU_FASTFLAG(LuwuTraits)

using namespace Luau;

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md)
TEST_SUITE_BEGIN("TypeInferMatch");

TEST_CASE_FIXTURE(BuiltinsFixture, "match_gives_the_union_of_its_arms")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function which(arg: string)
            return match const a = arg
                case is "p" | "patch" then "patch"
                case is "u" | "unpatch" then "unpatch"
                else error(`unexpected {a}`)
        end
        local w = which("p")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "match_narrows_the_subject_in_each_arm")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function describe(value: string | number | boolean | nil): string
            return match const v = value
                case is nil then "nothing"
                case is string then v:upper()
                case is number when v > 0 then tostring(v + 1)
                case is boolean when const flipped = not v then tostring(flipped)
                else "other"
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "is_and_implements_narrow_like_the_class_library")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        class Point(public x: number, public y: number)
        end

        local function f(p: Point | string): number
            if p is Point then
                return p.x
            end
            local s: string = p
            return #s
        end

        local function g(p: Point | string): number
            if p is not Point then
                local s: string = p
                return #s
            end
            return p.y
        end

        local function h(p: Point | string): number
            return match p
                case is Point then p.x + p.y
                else #p
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): a name in a pattern that holds something other than a
// class narrows like `==`: to the name's type when it matches, and only for a single value when it doesn't
TEST_CASE_FIXTURE(ClassesFixture, "pattern_names_holding_values_narrow_like_equality")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
    };

    CheckResult result = check(R"(
        local KINDS = { streaminfo = 0, padding = 1 }
        local OK = "ok" :: "ok"

        local function matched(kind: number | string): number
            if kind is KINDS.streaminfo then
                return kind
            end
            return 0
        end

        local function unmatched(kind: number | string): number
            if kind is not KINDS.streaminfo | KINDS.padding then
                local still: string = kind
                return 0
            end
            return kind
        end

        local function single(status: "ok" | "failed"): "failed"
            if status is not OK then
                return status
            end
            return "failed"
        end
    )");

    // a number other than both kinds still reaches the first branch
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 13);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): a pattern no value can match is reported once, at the
// test, and the code it guards adds nothing about the tested value being 'never'
TEST_CASE_FIXTURE(ClassesFixture, "a_pattern_that_never_matches_is_one_error")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        class Point(public const x: number, public const y: number)
        end
        local function name(): string
            return "x"
        end
        local s = name()
        if s is Point then
            print(s.x)
        end
        local n = match name()
            case is Point then 1
            else 2
        local maybe: string | Point = s
        if maybe is Point then
            print(maybe.x)
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    for (const TypeError& error : result.errors)
    {
        INFO(toString(error));
        const GenericError* never = get<GenericError>(error);
        REQUIRE(never);
        CHECK(never->message.find("can never be 'Point'") != std::string::npos);
    }
    CHECK_EQ(result.errors[0].location.begin.line, 7);
    CHECK_EQ(result.errors[1].location.begin.line, 11);
}

TEST_CASE_FIXTURE(Fixture, "match_mistakes_get_one_error")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuTraits, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    struct Case
    {
        const char* source;
        const char* message;
    };
    for (const Case& c : {
             Case{
                 "local x = 1\nlocal y = match x\n    case is 1 then 'one'\n",
                 "A 'match' expression must be exhaustive; add an 'else' branch to handle all unhandled cases",
             },
             Case{
                 "local x = 1\nlocal y = match x\n    case 1 | 2 then 'small'\n    else 'other'\n",
                 "Write 'is' before a 'case' pattern: 'case is <pattern>'",
             },
             // a name after `case` binds, so one that's a builtin, a class or a const was meant as a test
             Case{
                 "local x = 1\nlocal y = match x\n    case string then 'text'\n    else 'other'\n",
                 "Did you mean 'case is string'? A name after 'case' binds the value, and 'string' is a builtin",
             },
             Case{
                 "class Cat\nend\nlocal x: unknown = 1\nlocal y = match x\n    case Cat then 'cat'\n    else 'other'\n",
                 "Did you mean 'case is Cat'? A name after 'case' binds the value, and 'Cat' is a class",
             },
             Case{
                 "const LIMIT = 2\nlocal x = 1\nlocal y = match x\n    case LIMIT then 'limit'\n    else 'other'\n",
                 "Did you mean 'case is LIMIT'? A name after 'case' binds the value, and 'LIMIT' is already a const",
             },
             // a `match` statement ends with `end`
             Case{
                 "local x = 1\nmatch x\n    case is 1 then print('one')\n    case is 2 then print('two')\nprint(x)\n",
                 "Expected 'end' (to close 'then' at line 4), got <eof>",
             },
             Case{
                 "local x = 1\nlocal y = match x\n    c\nprint(y)\n",
                 "Expected 'case' after the value 'match' tests",
             },
             // the right side of `is` is a pattern, never an expression
             Case{
                 "local x, y = 1, 2\nlocal z = x is not (y)\n",
                 "Expected this to be an 'is' pattern but got a complex expression. Use class.isinstance for a dynamic check",
             },
             Case{
                 "trait Named\nend\nlocal x = 1\nlocal z = x implements -Named\n",
                 "Expected this to be an 'implements' pattern but got a complex expression. Use class.implements for a dynamic check",
             },
             Case{
                 "local x = 1\nlocal z = x is not\n",
                 "Expected a type, class or literal after 'is', got <eof>",
             },
             // types are erased at runtime, so a pattern has no generic arguments to test
             Case{
                 "class Box<T>(inner: T)\nend\nconst b = Box('hi')\nlocal ok = b is Box<string>\n",
                 "Patterns cannot have generic parameters (types are erased at runtime)",
             },
             Case{
                 "class Box<T>(inner: T)\nend\nconst b = Box('hi')\nlocal n = match b\n    case is Box<string> then 1\n    else 2\n",
                 "Patterns cannot have generic parameters (types are erased at runtime)",
             },
         })
    {
        INFO(c.source);
        ParseResult result = tryParse(c.source);
        REQUIRE_EQ(result.errors.size(), 1);
        CHECK_EQ(result.errors[0].getMessage(), c.message);
    }
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): `is Box<string>` is one syntax error, and the test
// still checks for a `Box`
TEST_CASE_FIXTURE(ClassesFixture, "generic_arguments_in_a_pattern_are_one_error")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        class Box<T>(public inner: T)
        end
        const boxy = Box("hi")
        if boxy is Box<string> then
            print(boxy.inner)
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const SyntaxError* syntax = get<SyntaxError>(result.errors[0]);
    REQUIRE(syntax);
    CHECK_EQ(result.errors[0].location.begin.line, 4);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): `is trait` and `is class` test a value's type like the
// other primitive tags, and a local holding a trait works after `implements`
TEST_CASE_FIXTURE(ClassesFixture, "is_trait_narrows_for_implements")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuTraits, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        trait Named
            expect name: string
        end
        class Path(public name: string) implements Named
        end

        local function matches(value: Path, expected: class | trait): boolean
            if expected is trait and value implements expected then
                return true
            end
            if expected is class and value is expected then
                return true
            end
            return expected is not trait and expected is not class
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): `implements A | B` passes when either trait is
// implemented, and narrows to the classes that implement one of them
TEST_CASE_FIXTURE(ClassesFixture, "implements_takes_alternatives")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuTraits, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        trait NotFlac
            expect name: string
        end
        trait Unreadable
            expect name: string
        end
        class Mp3(public name: string) implements NotFlac
        end
        class Broken(public name: string) implements Unreadable
        end
        class Flac(public bits: number)
        end

        local function problem(file: Mp3 | Broken | Flac): string
            if file implements NotFlac | Unreadable then
                return file.name
            end
            return "fine"
        end

        local function described(file: Mp3 | Broken | Flac): string
            return match file
                case implements NotFlac | Unreadable then file.name
                else "fine"
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "implements_needs_a_trait_after_its_bar")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuTraits, true},
    };

    CheckResult result = check(R"(
        trait Named
        end
        local x = 1
        local y = x implements Named |
        local z = y
    )");

    REQUIRE(result.errors.size() >= 1);
    const SyntaxError* syntax = get<SyntaxError>(result.errors[0]);
    REQUIRE(syntax);
    CHECK_EQ(syntax->message, "Expected a trait after '|'");
    CHECK_EQ(result.errors[0].location.begin.line, 4);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): the class after `is` is a name or a dotted path, and
// `is integer` tests for an integer
TEST_CASE_FIXTURE(ClassesFixture, "patterns_name_their_class_by_path")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        class Point(public x: number)
        end
        local geometry = { Point = Point }

        local function f(p: Point | string): number
            if p is geometry.Point then
                return 1
            end
            return 0
        end

        local function whole(n: unknown): boolean
            return n is integer
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// An unnamed subject's hidden local is never reported unused, even before the first arm is written
TEST_CASE_FIXTURE(BuiltinsFixture, "an_unfinished_match_has_no_unused_subject")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    LintResult result = lint("local function f(): string? return nil end\nlocal x = match f() or 'a'\nprint(x)\n");
    for (const LintWarning& warning : result.warnings)
        CHECK_NE(warning.code, LintWarning::Code_LocalUnused);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): `return match` whose arms all `return` their values gets a
// warning on each arm's `return`, which the one before `match` makes unnecessary
TEST_CASE_FIXTURE(BuiltinsFixture, "returns_in_arms_of_a_returned_match_are_unnecessary")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    LintResult result = lint(R"(
local function pick(n: number): string
    return match n
        case is 1 then
            return "one"
        case m when m < 10 then
            return "small"
        else
            return "big"
end
local function mixed(n: number): string
    return match n
        case is 1 then
            return "one"
        else "other"
end
print(pick(1), mixed(1))
)");

    std::vector<const LintWarning*> unnecessary;
    for (const LintWarning& warning : result.warnings)
    {
        if (warning.code == LintWarning::Code_UnreachableCode)
            unnecessary.push_back(&warning);
    }

    REQUIRE_EQ(unnecessary.size(), 3);
    const unsigned lines[] = {4, 6, 8};
    for (size_t i = 0; i < unnecessary.size(); ++i)
    {
        CHECK_EQ(unnecessary[i]->text, "These returns are unnecessary because of the 'return' on line 3");
        CHECK_EQ(unnecessary[i]->location.begin.line, lines[i]);
        CHECK_EQ(unnecessary[i]->location.end.column - unnecessary[i]->location.begin.column, 6);
    }
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): a `match`'s `else` arm can leave like a `then` arm
TEST_CASE_FIXTURE(Fixture, "a_match_else_arm_can_leave")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    for (const char* source : {
             "local function f(n: number)\n    return match n\n        case is 1 then 'one'\n        else return 'other'\nend\n",
             "for i = 1, 3 do\n    local s = match i\n        case is 1 then 'one'\n        else continue\n    print(s)\nend\n",
             "for i = 1, 3 do\n    local s = match i\n        case is 1 then 'one'\n        else break\n    print(s)\nend\n",
             "local s = do\n    local t = match 1\n        case is 1 then 'one'\n        else give 'early'\n    give t\nprint(s)\n",
         })
    {
        INFO(source);
        ParseResult result = tryParse(source);
        CHECK_EQ(result.errors.size(), 0);
    }
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): `when <condition> then <value>` is an arm without
// `case`, for a subject the header names
TEST_CASE_FIXTURE(ClassesFixture, "a_match_arm_can_start_with_when")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        class Box<T>(public inner: T)
        end

        local function random_box(roll: number): Box<string?>
            return match const n = roll
                when n < 50 then
                    Box(nil)
                when n < 100 when n ~= 75 then
                    Box("most")
                case is 75 then
                    Box("75")
                else
                    Box(tostring(n))
        end

        local function size(n: number): string
            return match n
                when n < 10 then "small"
                else "big"
        end

        print(random_box(3), size(1))
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Valid Luau that looks like the new syntax parses as before in a Luau file
TEST_CASE_FIXTURE(Fixture, "match_and_is_stay_names_in_luau_files")
{
    ScopedFastFlag matchExpressions{FFlag::DebugLuwuMatchExpressions, true};

    ParseOptions options;
    options.language = Language::Luau;
    ParseResult result = tryParse("local match, is, x, b = 1, 2, 3, 4\nlocal a = x is = 1\nlocal c = match b = 2\n", options);
    CHECK_EQ(result.errors.size(), 0);
}

// In a Luwu file too, `match` and `is` stay names wherever the code means something without them, whatever the line
// breaks: no `case` follows the `match`, and no name follows the `is`
TEST_CASE_FIXTURE(Fixture, "match_and_is_stay_names_in_valid_code")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuDoExpr, true},
    };

    for (const char* source : {
             "local match, f, t = 1, function() end, {}\nlocal which = match f(1) or error('x') or return;\n",
             "local match, f, t = 1, function() end, {}\nlocal which = match\nf(1) or error('x') or return;\n",
             "local match, f = 1, function() end\nlocal which = match f(1)\nlocal case = 2\n",
             "local match, f = 1, function() end\nlocal which = match f(1)\ncase = 2\n",
             "local match, x, is = 1, 2, function() end\nlocal a = x is(print)\n",
             "local match, x, is = 1, 2, {}\nlocal a = x\nis.y = 1\n",
         })
    {
        INFO(source);
        ParseResult result = tryParse(source);
        CHECK_EQ(result.errors.size(), 0);
    }
}

const char* const kTypewriterBefore = R"(local function maybe(): number?
    return nil
end
)";

const char* const kTypewriterAfter = R"(
local unrelated = 1
print(unrelated + 1)
)";

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_match_expressions")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    checkTypewriting(
        *this,
        kTypewriterBefore,
        "local label = match const n = maybe()\n    case is nil then \"none\"\n    case is number when n > 0 then \"positive\"\n    else \"other\"",
        kTypewriterAfter
    );
    checkTypewriting(
        *this,
        kTypewriterBefore,
        "match const n = maybe()\n    case is nil then\n        print(\"none\")\n    when n > 0 then\n        print(n + 1)\n    else\n        print(n)\nend",
        kTypewriterAfter
    );
    checkTypewriting(*this, kTypewriterBefore, "local n = maybe()\nlocal ok = n is number", kTypewriterAfter);
    checkTypewriting(*this, kTypewriterBefore, "local n = maybe()\nlocal ok = n is 1 | 2 | nil", kTypewriterAfter);
    checkTypewriting(*this, kTypewriterBefore, "local LIMITS = { low = 1 }\nlocal n = maybe()\nlocal ok = n is not LIMITS.low | nil", kTypewriterAfter);
}

TEST_CASE_FIXTURE(ClassesFixture, "typing_implements_alternatives")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuTraits, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    const char* before = R"(trait Named
end
trait Titled
end
class Doc() implements Named
end
local function any(): Doc?
    return nil
end
)";
    checkTypewriting(*this, before, "local d = any()\nlocal ok = d implements Named | Titled", kTypewriterAfter);
    checkTypewriting(
        *this,
        before,
        "local d = any()\nlocal label = match d\n    case implements Named | Titled then \"named\"\n    else \"other\"",
        kTypewriterAfter
    );
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): a `match` statement narrows the subject in each arm,
// and after it when the arms it skipped leave
TEST_CASE_FIXTURE(BuiltinsFixture, "match_statements_narrow_like_if_chains")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function f(v: number | string | nil): number
            match v
                case is nil then
                    return 0
                case is string then
                    local s: string = v
                    return #s
            end
            local n: number = v
            return n
        end

        local function g(v: number?): string
            match const n = v
                case is nil then
                    return "none"
                when n > 0 then
                    local positive: number = n
                    return tostring(positive)
                else
                    return "other"
            end
        end

        print(f(1), g(1))
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): a function whose last statement is a `match` with no
// `else` doesn't return on every path
TEST_CASE_FIXTURE(BuiltinsFixture, "a_match_statement_without_else_can_fall_through")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function f(v: number): string
            match v
                case is 1 then
                    return "one"
                case is 2 then
                    return "two"
            end
        end

        local function g(v: number): string
            match v
                case is 1 then
                    return "one"
                else
                    return "other"
            end
        end

        print(f(1), g(1))
    )");

    // one on `f`'s `end`, and one on the `match` that lets a value through
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    for (const TypeError& error : result.errors)
        CHECK(get<FunctionExitsWithoutReturning>(error));
    CHECK_EQ(result.errors[1].location.begin.line, 3);
    CHECK(toString(result.errors[1]).find("This 'match' has no 'else'") != std::string::npos);
}

// Arms indented under `match` can close with an `end` at their indent before the match's `end`. A single `end` there
// still closes only the match, and the next `end` to its left closes the function.
TEST_CASE_FIXTURE(BuiltinsFixture, "indented_match_arms_can_close_with_their_own_end")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function f(v: number): string
            match v
                case is 1 then
                    return "one"
                else
                    return "other"
                end
            end
        end

        local function g(v: number): string
            match v
                case is 1 then
                    return "one"
                else
                    return "other"
                end
        end

        print(f(1), g(1))
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    for (AstStat* stat : getMainSourceModule()->root->body)
    {
        if (AstStatLocalFunction* fn = stat->as<AstStatLocalFunction>(); fn && fn->name->name == "f")
        {
            AstStatMatch* match = fn->func->body->body.data[0]->as<AstStatMatch>();
            REQUIRE(match);
            REQUIRE(match->armsEndLocation);
            CHECK_EQ(match->armsEndLocation->begin, Position(8, 16));
        }
        if (AstStatLocalFunction* fn = stat->as<AstStatLocalFunction>(); fn && fn->name->name == "g")
        {
            AstStatMatch* match = fn->func->body->body.data[0]->as<AstStatMatch>();
            REQUIRE(match);
            CHECK(!match->armsEndLocation);
        }
    }
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_indented_match_arms_with_their_own_end")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    checkTypewriting(
        *this,
        kTypewriterBefore,
        "match maybe()\n    case is nil then\n        print(\"none\")\n    else\n        print(\"some\")\n    end\nend",
        kTypewriterAfter
    );
}

// A guard statement can start with a pattern, and narrows after it like the written-out test
TEST_CASE_FIXTURE(BuiltinsFixture, "a_guard_can_start_with_a_pattern")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        local function f(r: unknown): number
            r is string and return 1
            r is number or return 2
            return r
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_a_guard_that_starts_with_a_pattern")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuDoExpr, true},
    };

    checkTypewriting(
        *this,
        kTypewriterBefore,
        "local function f(r: unknown): number?\n    r is string and return 1\n    r is number or return nil\n    return r\nend",
        kTypewriterAfter
    );
}

// `is` with alternatives narrows like the `or` of its tests, and `is not` like the `and` of their negations
TEST_CASE_FIXTURE(ClassesFixture, "is_with_alternatives_narrows_the_value")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        class Point(public x: number)
        end
        class Circle(public radius: number)
        end
        local function f(v: Point | Circle | string, kind: "join" | "leave" | "kick")
            if v is Point | Circle then
                local shape: Point | Circle = v
            end
            if v is not Point | Circle then
                local text: string = v
            end
            if kind is "join" | "leave" then
                local k: "join" | "leave" = kind
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "a_leave_first_guard_condition_can_be_a_pattern")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        local function f(r: unknown): number
            return 1 if r is string
            return 2 if r is not number
            return r
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_a_leave_first_guard_whose_condition_is_a_pattern")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuDoExpr, true},
    };

    checkTypewriting(
        *this,
        kTypewriterBefore,
        "local function f(r: unknown): number?\n    return 1 if r is string\n    return nil if r is not number\n    return r\nend",
        kTypewriterAfter
    );
}

// A `case` name binds the subject's value narrowed by the arm's pattern, and a last `case name` is the `else`
TEST_CASE_FIXTURE(BuiltinsFixture, "case_bindings_narrow_and_a_last_catch_all_is_the_else")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuDoExpr, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function describe(v: string | number | boolean): string
            return match v
                case s is string then s
                case n is number when n > 10 then tostring(n + 1)
                case other then tostring(other)
        end
        local function first(xs: { number }): number?
            for _, x in xs do
                const r: number = match x
                    case is 0 then continue
                    case n when n < 0 then return nil
                    case n then n
                return r
            end
            return nil
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_case_bindings")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuDoExpr, true},
    };

    checkTypewriting(
        *this,
        kTypewriterBefore,
        "local label = match maybe()\n    case n is number when n > 0 then \"positive\"\n    case other then tostring(other)",
        kTypewriterAfter
    );
}

TEST_SUITE_END();
