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

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): a type test no value can pass is reported once, at the
// test, and the code it guards adds nothing about the tested value being 'never'
TEST_CASE_FIXTURE(ClassesFixture, "a_type_test_that_never_passes_is_one_error")
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
             Case{
                 "local x = 1\nmatch x\n    case is 1 then print('one')\n    case is 2 then print('two')\nprint(x)\n",
                 "'match' only supports expressions, use an 'if' statement instead",
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

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): the class after `is` is a name or a dotted path, and
// `is integer` tests for an integer
TEST_CASE_FIXTURE(ClassesFixture, "type_tests_name_their_class_by_path")
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
    checkTypewriting(*this, kTypewriterBefore, "local n = maybe()\nlocal ok = n is number", kTypewriterAfter);
    checkTypewriting(*this, kTypewriterBefore, "local n = maybe()\nlocal ok = n is 1 | 2 | nil", kTypewriterAfter);
}

// A guard statement can start with a type test, and narrows after it like the written-out test
TEST_CASE_FIXTURE(BuiltinsFixture, "a_guard_can_start_with_a_type_test")
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

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_a_guard_that_starts_with_a_type_test")
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

TEST_CASE_FIXTURE(BuiltinsFixture, "a_leave_first_guard_condition_can_be_a_type_test")
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

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_a_leave_first_guard_whose_condition_is_a_type_test")
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
