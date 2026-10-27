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
LUAU_FASTFLAG(LuwuDestructuring)

using namespace Luau;

// Luwu match expressions
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
                case "p" | "patch" then "patch"
                case "u" | "unpatch" then "unpatch"
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

TEST_CASE_FIXTURE(Fixture, "match_mistakes_get_one_error")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
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
                 "local x = 1\nlocal y = match x\n    case 1 then 'one'\n",
                 "A 'match' expression must be exhaustive; add an 'else' branch to handle all unhandled cases",
             },
             Case{
                 "local x, limit = 1, 2\nlocal y = match x\n    case limit then 'one'\n    else 'other'\n",
                 "A 'case' pattern is a literal, 'is Type' or 'implements Trait'; to compare with a variable, name the subject "
                 "('match const v = ...') and use 'when v == x'",
             },
             Case{
                 "local x = 1\nmatch x\n    case 1 then print('one')\n    case 2 then print('two')\nprint(x)\n",
                 "'match' only supports expressions, use an 'if' statement instead",
             },
             Case{
                 "local x = 1\nlocal y = match x\n    c\nprint(y)\n",
                 "Expected 'case' after the value 'match' tests",
             },
         })
    {
        INFO(c.source);
        ParseResult result = tryParse(c.source);
        REQUIRE_EQ(result.errors.size(), 1);
        CHECK_EQ(result.errors[0].getMessage(), c.message);
    }
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

// Valid Luau that looks like the new syntax keeps its parse in a Luau file
TEST_CASE_FIXTURE(Fixture, "match_and_is_stay_names_in_luau_files")
{
    ScopedFastFlag matchExpressions{FFlag::DebugLuwuMatchExpressions, true};

    ParseOptions options;
    options.language = Language::Luau;
    ParseResult result = tryParse("local match, is, x, b = 1, 2, 3, 4\nlocal a = x is = 1\nlocal c = match b = 2\n", options);
    CHECK_EQ(result.errors.size(), 0);
}

TEST_CASE_FIXTURE(ClassesFixture, "type_tests_bind_fields_with_a_pattern")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    CheckResult result = check(R"(
        class Point(public x: number, public y: number)
        end
        class Flag(public on: boolean)
        end

        local function f(p: Point | string): number
            if p is Point.{x, y} then
                return x + y
            end
            return #p
        end

        local function g(v: Point | Flag): string
            return match v
                case is Point.{x, y as py} when x > 0 then tostring(x + py)
                case is Flag.{on} then tostring(on)
                else "other"
        end

        local function make(): Point | string
            return "s"
        end

        local function h(): number
            if make() is Point.{x} then
                return x
            end
            return 0
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "a_pattern_binds_only_as_the_whole_condition")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDestructuring, true},
    };

    struct Case
    {
        const char* source;
        const char* message;
    };
    for (const Case& c : {
             Case{
                 "local ok, p, Point\nif ok and p is Point.{x} then end\n",
                 "A '.{...}' after a type test binds names only when the test is the whole condition; join other conditions with 'when'",
             },
             Case{
                 "local p, Point\nlocal b = p is Point.{x}\n",
                 "A '.{...}' after a type test binds names only when the test is the whole condition; join other conditions with 'when'",
             },
             Case{
                 "local p, Point\nif p is not Point.{x} then end\n",
                 "'is not' has no fields to bind; remove the '.{...}'",
             },
         })
    {
        INFO(c.source);
        ParseResult result = tryParse(c.source);
        REQUIRE_EQ(result.errors.size(), 1);
        CHECK_EQ(result.errors[0].getMessage(), c.message);
    }
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
        "local label = match const n = maybe()\n    case nil then \"none\"\n    case is number when n > 0 then \"positive\"\n    else \"other\"",
        kTypewriterAfter
    );
    checkTypewriting(*this, kTypewriterBefore, "local n = maybe()\nlocal ok = n is number", kTypewriterAfter);
}

TEST_CASE_FIXTURE(ClassesFixture, "typing_type_test_patterns")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    const char* before = R"(class Point(public x: number, public y: number)
end
local function get(): Point | string
    return "s"
end
)";

    checkTypewriting(*this, before, "local v = get()\nif v is Point.{x, y as py} then\n    print(x + py)\nend", kTypewriterAfter);
    checkTypewriting(
        *this,
        before,
        "local v = get()\nlocal n = match v\n    case is Point.{x} when x > 0 then x\n    else 0",
        kTypewriterAfter
    );
}

TEST_SUITE_END();
