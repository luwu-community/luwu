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
        local function describe(value: string | number | boolean): string
            return match const v = value
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
                 "A 'match' expression must end with an 'else' arm, so it has a value when no 'case' matches",
             },
             Case{
                 "local x, limit = 1, 2\nlocal y = match x\n    case limit then 'one'\n    else 'other'\n",
                 "A 'case' pattern is a literal, 'is Type' or 'implements Trait'; to compare with a variable, name the subject "
                 "('match const v = ...') and use 'when v == x'",
             },
             Case{
                 "local x = 1\nlocal y = match x\nprint(y)\n",
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

// Valid Luau that looks like the new syntax keeps its parse in a Luau file
TEST_CASE_FIXTURE(Fixture, "match_and_is_stay_names_in_luau_files")
{
    ScopedFastFlag matchExpressions{FFlag::DebugLuwuMatchExpressions, true};

    ParseOptions options;
    options.language = Language::Luau;
    ParseResult result = tryParse("local match, is, x, b = 1, 2, 3, 4\nlocal a = x is = 1\nlocal c = match b = 2\n", options);
    CHECK_EQ(result.errors.size(), 0);
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

TEST_SUITE_END();
