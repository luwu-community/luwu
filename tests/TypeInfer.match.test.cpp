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
LUAU_FASTFLAG(DebugLuwuTryExpressions)
LUAU_FASTFLAG(LuwuDeclareStatements)
LUAU_FASTFLAG(LuwuNonePrimitive)
LUAU_FASTFLAG(DebugLuwuCollections)
LUAU_FASTFLAG(LuwuGenericNominals)

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

// Luwu match expressions: a type test no value can pass is reported once, at the test, and the code it guards adds
// nothing about the tested value being 'never'
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

// Valid Luau that looks like the new syntax parses as before in a Luau file
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
                 "Can't bind names in this position; a type test's '.{...}' binds only as the condition of an 'if', 'while', 'when' or 'case'",
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
    checkTypewriting(*this, before, "local v = get()\nif v is Point.(x, y) then\n    print(x + y)\nend", kTypewriterAfter);
    checkTypewriting(
        *this,
        before,
        "local v = get()\nlocal n = match v\n    case is Point.(x) when x > 0 then x\n    else 0",
        kTypewriterAfter
    );
}

// Luwu Destructuring (rfcs/destructuring.md): `is Class.(a, b)` binds a primary constructor's fields by position
TEST_CASE_FIXTURE(ClassesFixture, "type_tests_bind_by_position_with_a_pattern")
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
        class Named(public name: string)
        end

        local function f(p: Point | string): number
            if p is Point.(x, y) then
                return x + y
            end
            return #p
        end

        local function g(v: Point | Named): string
            return match v
                case is Point.(x, py) when x > 0 then tostring(x + py)
                case is Named.(name) then name
                else "other"
        end

        local function make(): Point | Named
            return Named("n")
        end

        local function h(): string
            if make() is Named.(n) then
                return n
            end
            return ""
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "positional_type_test_mistakes_get_one_error")
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
                 "local ok, p, Point\nif ok and p is Point.(x) then end\n",
                 "A '.(...)' after a type test binds names only when the test is the whole condition; join other conditions with 'when'",
             },
             Case{
                 "local p, Point\nlocal ok = p is Point.(x)\n",
                 "Can't bind names in this position; a type test's '.(...)' binds only as the condition of an 'if', 'while', 'when' or 'case'",
             },
             Case{
                 "local p, Point\nif p is not Point.(x) then end\n",
                 "'is not' has no elements to bind; remove the '.(...)'",
             },
             // the names after a call are bound anyway, so nothing below reports them as unknown
             Case{
                 "local p, result\nif p is result.err(e) then print(e) end\n",
                 "Write 'result.err.(e)' to bind by position; without the '.', 'result.err(e)' is a call",
             },
         })
    {
        INFO(c.source);
        ParseResult result = tryParse(c.source);
        REQUIRE_EQ(result.errors.size(), 1);
        CHECK_EQ(result.errors[0].getMessage(), c.message);
    }

    // a call whose arguments are in scope is a class expression, not a pattern
    ParseResult call = tryParse("local p, classOf, Point\nif p is classOf(Point) then end\n");
    CHECK(call.errors.empty());
}

// Luwu match expressions: a bare `.(name)` binds the whole value and matches every value, so a `match` ending in one
// needs no `else`, and `x is .(name)` binds `x` in a condition
TEST_CASE_FIXTURE(BuiltinsFixture, "a_bare_positional_pattern_binds_the_whole_value")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function kind(v: unknown): string
            return match v
                case is string then "string"
                case .(other) then typeof(other)
        end
        local function label(n: number?)
            return match n
                case nil then "none"
                case .(value) then tostring(value)
        end
        local function field(t: { kind: string })
            return match t
                case .(.{kind}) then kind
        end
        local function bound(n: number?)
            if n is .(whole) then
                return whole
            end
            return 0
        end
        local s = label(1)
        local k = field({ kind = "x" })
        local b = bound(2)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("s")), "string");
    CHECK_EQ(toString(requireType("k")), "string");
    CHECK_EQ(toString(requireType("b")), "number?");
}

TEST_CASE_FIXTURE(Fixture, "bare_positional_pattern_mistakes_get_one_error")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    for (const char* source : {
             "local v\nlocal a = match v\n    case .(x, y) then 1\n    else 2\n",
             "local v\nlocal a = match v\n    case .(x) when x ~= nil then 1\n",
             "local v\nlocal c = v is not .(z)\n",
             "local v\nlocal d = v is .(w)\n",
         })
    {
        INFO(source);
        ParseResult result = tryParse(source);
        CHECK_EQ(result.errors.size(), 1);
    }
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_bare_positional_patterns")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    checkTypewriting(
        *this, kTypewriterBefore, "local label = match maybe()\n    case nil then \"none\"\n    case .(other) then tostring(other)", kTypewriterAfter
    );
    checkTypewriting(*this, kTypewriterBefore, "local n = maybe()\nif n is .(whole) then\n    print(whole)\nend", kTypewriterAfter);
}

// Luwu try expressions: `try` takes the whole type test after it, so its pattern can't bind; the one error says to put
// the `try` in parentheses, and the names it would have bound report nothing more
TEST_CASE_FIXTURE(BuiltinsFixture, "try_around_a_type_test_asks_for_parentheses")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
        {FFlag::DebugLuwuTryExpressions, true},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        local function read(path: string): string
            return path
        end
        local f = if try read("x") is Ok.(content) then content else nil
        local s = "x"
        local g = if try s is .(whole) then whole else nil
    )");

    // With the library default flags, BuiltinsFixture has no `class` global, which the test `Ok.(...)` calls
    size_t syntaxErrors = 0;
    for (const TypeError& error : result.errors)
    {
        INFO(toString(error));
        if (get<SyntaxError>(error))
        {
            syntaxErrors++;
            CHECK(toString(error).find("(try ") != std::string::npos);
        }
        else
        {
            const UnknownSymbol* unknown = get<UnknownSymbol>(error);
            CHECK((unknown && unknown->name == "class"));
        }
    }
    CHECK_EQ(syntaxErrors, 2);
}

// Luwu try expressions: `try e case ...` types like `match try e case ...`, its arms narrowing the result
TEST_CASE_FIXTURE(BuiltinsFixture, "try_with_case_arms_types_like_match")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
        {FFlag::DebugLuwuTryExpressions, true},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function read(path: string): string
            return path
        end
        local function load(path: string)
            return try read(path)
                case is Ok.(content) then content
                case .(other) then nil
        end
        local loaded = load("x")
    )");

    // With the library default flags, BuiltinsFixture has no `class` global, which the test `Ok.(...)` calls
    for (const TypeError& error : result.errors)
    {
        const UnknownSymbol* unknown = get<UnknownSymbol>(error);
        CHECK_MESSAGE((unknown && unknown->name == "class"), toString(error));
    }
    if (result.errors.empty())
        CHECK_EQ(toString(requireType("loaded")), "string?");
}

// Luwu try expressions: options and results destructure by position, and the payload of `Some`, `Ok` and `Err` is a
// public const field
TEST_CASE_FIXTURE(BuiltinsFixture, "options_and_results_destructure_by_position")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::LuwuDestructuring, true},
        {FFlag::DebugLuwuTryExpressions, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function read(fail: boolean): Result<string, number>
            return if fail then Err(404) else Ok("text")
        end

        const .(inner) = Some(5)
        const .(payload) = read(false)
        local field = Some(true).value
        local failure = Err("e").error
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("inner")), "number");
    CHECK_EQ(toString(requireType("payload")), "number | string");
    CHECK_EQ(toString(requireType("field")), "boolean");
    CHECK_EQ(toString(requireType("failure")), "string");
}

TEST_SUITE_END();
