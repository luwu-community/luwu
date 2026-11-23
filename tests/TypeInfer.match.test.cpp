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
LUAU_FASTFLAG(LuwuDestructuring)
LUAU_FASTFLAG(DebugLuwuTryExpressions)
LUAU_FASTFLAG(LuwuDeclareStatements)
LUAU_FASTFLAG(LuwuNonePrimitive)
LUAU_FASTFLAG(DebugLuwuBuiltinClasses)
LUAU_FASTFLAG(DebugLuwuBundles)
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
                 "Patterns cannot have generic parameters (types are erased at runtime); try an 'is' test on a binding instead: "
                 "'b is Box(inner is string)'",
             },
             Case{
                 "class Box<T>(inner: T)\nend\nconst b = Box('hi')\nlocal n = match b\n    case is Box<string> then 1\n    else 2\n",
                 "Patterns cannot have generic parameters (types are erased at runtime); try an 'is' test on a binding instead: "
                 "'is Box(inner is string)'",
             },
             // a field the arguments don't type is skipped with `_`
             Case{
                 "class Pair<K, V>(label: string, key: K, value: V)\nend\nconst p = Pair('a', 1, 'b')\nlocal ok = p is Pair<number, string>\n",
                 "Patterns cannot have generic parameters (types are erased at runtime); try an 'is' test on a binding instead: "
                 "'p is Pair(_, key is number, value is string)'",
             },
             // a class without a primary constructor is tested by name
             Case{
                 "class Box<T>\n    inner: T\nend\nconst b = Box { inner = 1 }\nlocal ok = b is Box<number>\n",
                 "Patterns cannot have generic parameters (types are erased at runtime); try an 'is' test on a binding instead: "
                 "'b is Box {inner is number}'",
             },
             // `is` can't test a table type, and `is not` can't bind
             Case{
                 "class Box<T>(inner: T)\nend\nconst b = Box({})\nlocal ok = b is Box<{ number }>\n",
                 "Patterns cannot have generic parameters (types are erased at runtime); try an 'is' test on a binding instead",
             },
             Case{
                 "class Box<T>(inner: T)\nend\nconst b = Box('hi')\nlocal ok = b is not Box<string>\n",
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
            if p is Point {x, y} then
                return x + y
            end
            return #p
        end

        local function g(v: Point | Flag): string
            return match v
                case is Point {x, y as py} when x > 0 then tostring(x + py)
                case is Flag {on} then tostring(on)
                else "other"
        end

        local function make(): Point | string
            return "s"
        end

        local function h(): number
            if make() is Point {x} then
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
                 "local ok, p, Point\nif ok and p is Point {x} then end\n",
                 "A '{...}' after a type test binds names only when the test is the whole condition; join other conditions with 'when'",
             },
             Case{
                 "local p, Point\nlocal b = p is Point {x}\n",
                 "Can't bind names in this position; a type test's '{...}' binds only as the condition of an 'if', 'while', 'when' or 'case'",
             },
             Case{
                 "local p, Point\nif p is not Point {x} then end\n",
                 "'is not' has no fields to bind; remove the '{...}'",
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

    checkTypewriting(*this, before, "local v = get()\nif v is Point {x, y as py} then\n    print(x + py)\nend", kTypewriterAfter);
    checkTypewriting(
        *this,
        before,
        "local v = get()\nlocal n = match v\n    case is Point {x} when x > 0 then x\n    else 0",
        kTypewriterAfter
    );
    checkTypewriting(*this, before, "local v = get()\nif v is Point(x, y) then\n    print(x + y)\nend", kTypewriterAfter);
    checkTypewriting(
        *this,
        before,
        "local v = get()\nlocal n = match v\n    case is Point(x) when x > 0 then x\n    else 0",
        kTypewriterAfter
    );
}

// Luwu Destructuring (rfcs/destructuring.md): `is Class(a, b)` binds a primary constructor's fields by position
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
            if p is Point(x, y) then
                return x + y
            end
            return #p
        end

        local function g(v: Point | Named): string
            return match v
                case is Point(x, py) when x > 0 then tostring(x + py)
                case is Named(name) then name
                else "other"
        end

        local function make(): Point | Named
            return Named("n")
        end

        local function h(): string
            if make() is Named(n) then
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
                 "local ok, p, Point\nif ok and p is Point(x) then end\n",
                 "A '(...)' after a type test binds names only when the test is the whole condition; join other conditions with 'when'",
             },
             Case{
                 "local p, Point\nlocal ok = p is Point(x)\n",
                 "Can't bind names in this position; a type test's '(...)' binds only as the condition of an 'if', 'while', 'when' or 'case'",
             },
             Case{
                 "local p, Point\nif p is not Point(x) then end\n",
                 "'is not' has no elements to bind; remove the '(...)'",
             },
             // destructuring's spelling still binds, so nothing below reports the names as unknown
             Case{
                 "local p, result\nif p is result.err.(e) then print(e) end\n",
                 "Remove the '.': a pattern after 'is' is written 'result.err(...)'",
             },
             Case{
                 "local p, Pod\nif p is Pod.{x} then print(x) end\n",
                 "Remove the '.': a pattern after 'is' is written 'Pod {...}'",
             },
             // an annotation in a pattern would only claim the type, so it asks for the test
             Case{
                 "local c, Cat\nif c is Cat(name: string) then print(name) end\n",
                 "Type annotations are not valid in this position, use 'name is string' here to perform a runtime check within this pattern",
             },
             Case{
                 "local p, Pod\nif p is Pod {x: number, y} then print(x, y) end\n",
                 "Type annotations are not valid in this position, use 'x is number' here to perform a runtime check within this pattern",
             },
             Case{
                 "local v\nif v is .(a, b: Point?) then print(a, b) end\n",
                 "Type annotations are not valid in this position, use 'b is Type' here to perform a runtime check within this pattern",
             },
         })
    {
        INFO(c.source);
        ParseResult result = tryParse(c.source);
        REQUIRE_EQ(result.errors.size(), 1);
        CHECK_EQ(result.errors[0].getMessage(), c.message);
    }

    // the class is a name or a dotted path, so the parentheses after it are always a pattern
    ParseResult pattern = tryParse("local p, result\nif p is result.err(e) then print(e) end\n");
    CHECK(pattern.errors.empty());
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): a binding in a pattern can be tested in turn, and the
// test narrows it
TEST_CASE_FIXTURE(ClassesFixture, "bindings_in_a_pattern_take_type_tests")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    CheckResult result = check(R"(
        --!strict
        class Cat(public name: string, public age: number?)
        end
        class Box(public inner: unknown)
        end

        local function age(c: Cat): number
            if c is Cat(name, age is number) then
                return age
            end
            return 0
        end

        local function unbox(b: Box): string
            if b is Box { inner as _ is Box(inner is string) } then
                return inner
            end
            return ""
        end

        local function kind(c: Cat): string
            return match c
                case is Cat(name is "Taz" | "Nanuk") then name
                case is Cat(age is nil) then "ageless"
                else "cat"
        end

        local function whole(n: unknown): boolean
            return n is integer
        end

        local function sum(v: vector | string): number
            if v is vector { x, y, z } then
                return x + y + z
            end
            return 0
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): `.(a, b)` after `is` tests for a bundle of exactly that
// many values and binds them by position; `..` allows more, and `.. as rest` binds them
TEST_CASE_FIXTURE(BuiltinsFixture, "bundle_patterns_bind_by_position")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuBundles, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function label(v: .(string, number) | string): string
            if v is .(name, count) then
                return name .. tostring(count)
            end
            return "none"
        end
        local function first(v: unknown)
            return match v
                case is .(a, b) when a is "a" | "b" then b
                case is .(a, .. as rest) then rest
                case other then other
        end
        local function rest(v: unknown): number
            if v is .(a, .. as others) then
                return #others
            end
            return 0
        end
        local l = label(.("x", 1))
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("l")), "string");
}

TEST_CASE_FIXTURE(Fixture, "bundle_pattern_mistakes_get_one_error")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    for (const char* source : {
             "local v\nlocal a = match v\n    case .(x, y) then 1\n    else 2\n",
             "local v\nlocal a = match v\n    case .(x) when x ~= nil then 1\n    else 2\n",
             "local v\nlocal c = v is not .(z)\n",
             "local v\nlocal d = v is .(w)\n",
             "local v\nif v is .(a, .., b) then end\n",
         })
    {
        INFO(source);
        ParseResult result = tryParse(source);
        CHECK_EQ(result.errors.size(), 1);
    }
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): alternatives may bind different names. A name gets the
// union of what the alternatives that bind it give, and is optional when one of them doesn't bind it.
TEST_CASE_FIXTURE(ClassesFixture, "alternatives_bind_names")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    CheckResult result = check(R"(
        --!strict
        class Cat(public name: string, public lives: number)
        end
        class Dog(public name: string)
        end
        class Box(public inner: unknown)
        end

        local function lives(v: Cat | Dog | string): number
            if v is Cat(name, lives) | Dog(name) then
                local n: string = name
                local l: number? = lives
                local wrong: number = lives
                return #n
            end
            return 0
        end

        local function unbox(b: Box): string
            if b is Box(inner is Cat(name, count) | Dog(name)) then
                local c: number? = count
                return name
            end
            return ""
        end
    )");

    // `lives` is `nil` when a `Dog` matched
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
    CHECK_EQ(result.errors[0].location.begin.line, 13);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): a bundle pattern narrows the value to the bundles of
// its size, so alternatives of different sizes read each position only from the bundles that have it
TEST_CASE_FIXTURE(BuiltinsFixture, "bundle_alternatives_narrow_by_size")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuBundles, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function either(v: .(string, number) | .(string, number, boolean)): string
            if v is .(x, y) | .(x, y, z) then
                local s: string = x
                local n: number = y
                local b: boolean? = z
                return s
            end
            return ""
        end
        local function exact(v: .(string, number) | .(string, number, boolean)): number
            if v is .(a, b) then
                local pair: .(string, number) = v
                return b
            end
            return 0
        end
        local function anything(v: unknown): string
            if v is .(k is string, n) then
                return k
            end
            return ""
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu Pattern Matching (rfcs/control-flow/pattern-matching.md): `_` in a pattern binds nothing, so the test that reads
// its value isn't a read of a placeholder
TEST_CASE_FIXTURE(Fixture, "a_placeholder_in_a_pattern_binds_nothing")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDestructuring, true},
    };

    LintResult result = lint(R"(
local Box, b = ...
if b is Box { inner as _ is Box(inner is string) } then
    print(inner)
end
if b is Box(_, count is number) then
    print(count)
end
)");

    for (const LintWarning& warning : result.warnings)
        CHECK_MESSAGE(warning.code != LintWarning::Code_PlaceholderRead, warning.text);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_bundle_patterns")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuBundles, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    const char* before = "local function pair(): .(number, string) | string\n    return \"x\"\nend\n";
    checkTypewriting(
        *this,
        before,
        "local label = match pair()\n    case is .(n, s) then tostring(n) .. s\n    case other then other",
        kTypewriterAfter
    );
    checkTypewriting(*this, before, "local v = pair()\nif v is .(n, .. as rest) then\n    print(n, rest)\nend", kTypewriterAfter);
}

TEST_CASE_FIXTURE(ClassesFixture, "typing_tested_bindings")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuMatchExpressions, true},
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDoExpr, true},
        {FFlag::LuwuDestructuring, true},
    };

    const char* before = "class Cat(public name: string, public age: number?)\nend\nlocal function get(): Cat | string\n    return \"x\"\nend\n";
    checkTypewriting(*this, before, "local v = get()\nif v is Cat(name, age) | \"x\" then\n    print(name, age)\nend", kTypewriterAfter);
    checkTypewriting(*this, before, "local v = get()\nif v is Cat(name, age is number) then\n    print(name, age + 1)\nend", kTypewriterAfter);
    checkTypewriting(*this, before, "local v = get()\nif v is Cat { name is \"Taz\" | \"Nanuk\" } then\n    print(name)\nend", kTypewriterAfter);
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
        {FFlag::DebugLuwuBuiltinClasses, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        local function read(path: string): string
            return path
        end
        local f = if try read("x") is Ok(content) then content else nil
        local s = "x"
        local g = if try s is .(whole, ..) then whole else nil
    )");

    // With the library default flags, BuiltinsFixture has no `class` global, which the test `Ok(...)` calls
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
        {FFlag::DebugLuwuBuiltinClasses, true},
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
                case is Ok(content) then content
                case other then nil
        end
        local loaded = load("x")
    )");

    // With the library default flags, BuiltinsFixture has no `class` global, which the test `Ok(...)` calls
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
