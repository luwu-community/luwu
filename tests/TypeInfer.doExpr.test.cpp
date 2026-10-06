// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Fixture.h"

#include "doctest.h"

#include "ScopedFlags.h"

LUAU_FASTFLAG(DebugLuwuDoExpr)
LUAU_FASTFLAG(DebugLuauForceOldSolver)

using namespace Luau;

// Luwu Do Expressions (rfcs/do-expressions.md)
TEST_SUITE_BEGIN("TypeInferDoExpr");

TEST_CASE_FIXTURE(BuiltinsFixture, "do_expression_type_is_the_union_of_its_gives")
{
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

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
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

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
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

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
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

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
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

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
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

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
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

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
        {FFlag::DebugLuwuDoExpr, true},
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

TEST_SUITE_END();
