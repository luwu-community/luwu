// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Fixture.h"

#include "doctest.h"

#include "ScopedFlags.h"

LUAU_FASTFLAG(LuwuIfLocal)
LUAU_FASTFLAG(LuwuDestructuring)
LUAU_FASTFLAG(DebugLuauForceOldSolver)

using namespace Luau;

// Luwu If Local (rfcs/if-local.md)
TEST_SUITE_BEGIN("TypeInferIfLocal");

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_binding_is_narrowed_to_truthy")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    CheckResult result = check(R"(
        --!strict
        local function maybe(): number?
            return nil
        end

        if local n = maybe() then
            local x: number = n
        end

        if local n: number? = maybe() when n > 3 then
            local y: number = n + 1
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_binding_type_comes_from_its_value")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    CheckResult result = check(R"(
        --!strict
        local function maybe(): number?
            return nil
        end

        if local n = maybe() then
            local wrong: string = n
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 7);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_annotation_is_checked_against_the_value")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    CheckResult result = check(R"(
        --!strict
        local function maybe(): number?
            return nil
        end

        if local s: string? = maybe() then
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_plain_clauses_refine_the_clauses_after_them_and_the_branch")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    CheckResult result = check(R"(
        --!strict
        local function maybe(): number?
            return nil
        end

        local function f(s: string?)
            if local n = maybe() when s then
                local a: string = s
                local b: number = n
            end

            if s when #s > 0 when local n = maybe() then
                local c: string = s
                local d: number = n
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_binding_is_not_visible_in_the_else_branch")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    CheckResult result = check(R"(
        local function maybe(): number?
            return nil
        end

        if local n = maybe() then
        else
            print(n)
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    UnknownSymbol* unknown = get<UnknownSymbol>(result.errors[0]);
    REQUIRE(unknown);
    CHECK_EQ(unknown->name, "n");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_expression_type")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    CheckResult result = check(R"(
        --!strict
        local function maybe(): number?
            return nil
        end

        local r = if const n = maybe() then n * 2 else 0
        local s = if const n = maybe() when n > 1 then tostring(n) else "none"
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("r")), "number");
    CHECK_EQ(toString(requireType("s")), "string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_chain_refinements_hold_after_the_if_when_the_else_branch_leaves")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    CheckResult result = check(R"(
        --!strict
        local function f(x: string?)
            if x when #x > 0 then
            else
                return
            end

            local y: string = x
        end

        local function maybe(): number?
            return nil
        end

        local function g(x: string?)
            if local n = maybe() when x then
                print(n)
            else
                return
            end

            local y: string = x
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_destructuring_names_are_narrowed")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDestructuring, true},
    };

    CheckResult result = check(R"(
        --!strict
        local t: { a: number?, b: string? } = { a = 1, b = nil }

        if const .{a, b} = t then
            local x: number = a
            local y: string = b
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "if_local_in_the_old_solver")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuIfLocal, true},
        {FFlag::DebugLuauForceOldSolver, true},
    };

    CheckResult result = check(R"(
        local function maybe(): number?
            return nil
        end

        if local n = maybe() when n then
            print(n)
        end

        local r = if const n = maybe() then n else 0
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_SUITE_END();
