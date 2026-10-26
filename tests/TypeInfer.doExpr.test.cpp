// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Fixture.h"
#include "Typewriter.h"

#include "doctest.h"

#include "ScopedFlags.h"

LUAU_FASTFLAG(LuwuDoExpr)
LUAU_FASTFLAG(DebugLuauForceOldSolver)

using namespace Luau;

// Luwu Do Expressions (rfcs/do-expressions.md)
TEST_SUITE_BEGIN("TypeInferDoExpr");

TEST_CASE_FIXTURE(BuiltinsFixture, "do_expression_type_is_the_union_of_its_gives")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        local os: string = "Windows"
        local a = do
            if os == "Windows" then
                give "C:/Temp"
            end
            give 5
        local b = do give true
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("a")), "number | string");
    CHECK_EQ(toString(requireType("b")), "boolean");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "a_do_expression_that_never_gives_is_never")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        local function maybe(): number?
            return nil
        end

        local function f(): number?
            local n = maybe() or do return nil
            local m = maybe() or return nil
            local x: number = n + m
            return x
        end

        for i = 1, 3 do
            local q = maybe() or break
            local r: number = q
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "do_expression_returns_are_the_function_s_returns")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        local function f(x: number?): string
            local n = x or return 5
            return tostring(n)
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 3);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "an_early_give_refines_the_rest_of_the_block")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        local function f(s: string?)
            local len = do
                if not s then
                    give 0
                end
                give #s
            local l: number = len
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "assignments_on_the_way_to_a_give_are_seen_after_the_expression")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        local function f(c: boolean)
            local v: string | number = 1
            local _ = do
                if c then
                    v = "s"
                    give 1
                end
                give 2
            local n: number = v
        end
    )");

    // `v` may be the string assigned on the first give's path
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 10);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "the_expected_type_flows_into_give")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        type Point = { x: number, y: number }
        local p: Point = do give { x = 1, y = 2 }
        local q: "a" | "b" = do
            if p.x > 0 then
                give "a"
            end
            give "b"
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "do_expression_locals_are_scoped_to_the_block")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        local v = do
            local inner = 1
            give inner
        print(inner)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    UnknownSymbol* unknown = get<UnknownSymbol>(result.errors[0]);
    REQUIRE(unknown);
    CHECK_EQ(unknown->name, "inner");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "do_expression_in_the_old_solver")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuDoExpr, true},
        {FFlag::DebugLuauForceOldSolver, true},
    };

    CheckResult result = check(R"(
        local function f(x)
            local v = x or return 1
            local w = do
                local a = 1
                give a
            return v, w
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// An assignment in a `do` in one branch of an if-else expression is joined with the other branch for the code after
// it, as after an `if` statement
TEST_CASE_FIXTURE(BuiltinsFixture, "assigning_in_an_if_else_expression_branch_joins_after_it")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        local function f(s: string, c: boolean)
            local ok = (if c then do s = "x" give true else true) and s
            if (if typeof(s) ~= "string" then do s = tostring(s) give true else true) and s == "meow" then
                print(s)
            end
            return ok
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// A `do` that always returns, inside a statement that always runs it, ends the code path
TEST_CASE_FIXTURE(BuiltinsFixture, "a_statement_running_a_returning_do_ends_the_code_path")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        local function meow(s: string): string
            if s == "a" then
                return s
            end
            print(do return "meow")
        end

        local function purr(s: string): string
            if s == "a" and (do return "a") then
                print(s)
            end
            return "b"
        end

        local function bark(s: string?): string
            print(s and do return "x")
        end
    )");

    // only `bark`: its `do` doesn't run when `s` is nil
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(result.errors[0].location.begin.line, 18);
    CHECK_EQ(result.errors[1].location.begin.line, 17);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "an_or_leave_statement_narrows_its_operand_for_the_rest_of_the_block")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    CheckResult result = check(R"(
        --!strict
        local function f(x: string?, y: number?, z: boolean?, t: { string? }): string
            x or return ""
            local s: string = x

            local n = y or return ""
            local m: number = y

            local w: boolean? = nil
            w = z or return ""
            local b: boolean = z

            for _, v in t do
                v or continue
                local u: string = v
            end

            return s
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "an_or_leave_inside_another_expression_narrows_nothing_after_it")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    // the `or` only runs when `c` is truthy, so `x` may still be nil after the statement
    CheckResult result = check(R"(
        --!strict
        local function f(x: string?, c: boolean): string
            local a = c and (x or return "")
            local s: string = x
            return s
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 4);
}

// Code after the typed text. It uses nothing the typed text declares, so it stays clean whatever is typed above it.
const char* const kTypewriterAfter = R"(
local unrelated = 1
print(unrelated + 1)
)";

const char* const kTypewriterBefore = R"(local function maybe(): number?
    return nil
end
local nums = {1, 2, 3}
)";
TEST_CASE_FIXTURE(BuiltinsFixture, "typing_do_expressions")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    checkTypewriting(*this, kTypewriterBefore, "local a = do give 5", kTypewriterAfter);
    checkTypewriting(
        *this,
        kTypewriterBefore,
        "local a = do\n    local n = maybe()\n    if n then\n        give n * 2\n    end\n    give 0",
        kTypewriterAfter
    );
    checkTypewriting(
        *this,
        kTypewriterBefore,
        "local function f(): number?\n    local n = maybe() or do\n        print(\"none\")\n        return nil\n    return n + 1\nend",
        kTypewriterAfter
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_or_leave")
{
    ScopedFastFlag doExpr{FFlag::LuwuDoExpr, true};

    checkTypewriting(*this, kTypewriterBefore, "local function f(): number?\n    local n = maybe() or return nil\n    return n + 1\nend", kTypewriterAfter);
    checkTypewriting(*this, kTypewriterBefore, "local function f(): number\n    local n = maybe()\n    n or return 0\n    return n\nend", kTypewriterAfter);
    checkTypewriting(*this, kTypewriterBefore, "for _, v in nums do\n    local n = maybe() or continue\n    print(v + n)\nend", kTypewriterAfter);
    checkTypewriting(*this, kTypewriterBefore, "for _, v in nums do\n    #nums >= v or continue\n    print(v)\nend", kTypewriterAfter);
    checkTypewriting(*this, kTypewriterBefore, "for _, v in nums do\n    v > 2 or v == 1 and continue\n    print(v)\nend", kTypewriterAfter);
}

TEST_SUITE_END();
