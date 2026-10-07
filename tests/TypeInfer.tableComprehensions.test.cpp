// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Fixture.h"

#include "doctest.h"

#include "ScopedFlags.h"

LUAU_FASTFLAG(LuwuTableComprehensions)
LUAU_FASTFLAG(LuwuIfLocal)
LUAU_FASTFLAG(DebugLuauForceOldSolver)

using namespace Luau;

// Luwu Table Comprehensions (rfcs/table-comprehensions.md)
TEST_SUITE_BEGIN("TypeInferTableComprehensions");

TEST_CASE_FIXTURE(BuiltinsFixture, "table_comprehension_types_come_from_the_item")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuTableComprehensions, true},
        {FFlag::LuwuIfLocal, true},
    };

    CheckResult result = check(R"(
        --!strict
        local nums = {1, 2, 3}
        local doubled = { for _, v in nums give v * 2 }
        local names = { for i, v in nums give [tostring(i)] = v }
        local squares = { for i = 1, 10 when i % 2 == 0 give i * i }
        local people: { { name: string, age: number? } } = {}
        local adults = { for _, p in people when const age = p.age when age > 18 give p.name }
        local grid: { { number } } = {}
        local flat = { for _, row in grid for _, c in row give tostring(c) }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CHECK_EQ(toString(requireType("doubled")), "{number}");
    CHECK_EQ(toString(requireType("names")), "{ [string]: number }");
    CHECK_EQ(toString(requireType("squares")), "{number}");
    CHECK_EQ(toString(requireType("adults")), "{string}");
    CHECK_EQ(toString(requireType("flat")), "{string}");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "a_nil_item_adds_nothing_so_the_element_type_excludes_nil")
{
    ScopedFastFlag comprehensions{FFlag::LuwuTableComprehensions, true};

    CheckResult result = check(R"(
        --!strict
        local function maybe(n: number): string?
            return if n > 1 then "x" else nil
        end
        local nums = {1, 2, 3}
        local found = { for _, v in nums give maybe(v) }
        local evens = { for _, v in nums give if v % 2 == 0 then v else nil }
        local typed: { string } = { for _, v in nums give maybe(v) }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CHECK_EQ(toString(requireType("found")), "{string}");
    CHECK_EQ(toString(requireType("evens")), "{number}");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "accumulating_items_keep_the_value_type")
{
    ScopedFastFlag comprehensions{FFlag::LuwuTableComprehensions, true};

    CheckResult result = check(R"(
        --!strict
        local words = {"a", "b", "a"}
        local counts = { for _, w in words give [w] += 1 }
        local joined = { for i, w in words give [w] ..= i }
        local typed: { [string]: number } = { for _, w in words give [w] += 1 }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CHECK_EQ(toString(requireType("counts")), "{ [string]: number }");
    CHECK_EQ(toString(requireType("joined")), "{ [string]: number | string }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "an_annotation_checks_the_item")
{
    ScopedFastFlag comprehensions{FFlag::LuwuTableComprehensions, true};

    CheckResult result = check(R"(
        --!strict
        local nums = {1, 2, 3}
        local bad: { string } = { for _, v in nums give v }
        local badKey: { [string]: number } = { for i, v in nums give [i] = v }
        local fine: { [string]: number }? = { for i, v in nums give [tostring(i)] = v }
    )");

    if (FFlag::DebugLuauForceOldSolver)
        return;

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(result.errors[0].location.begin.line, 3);
    const TypeMismatch* mismatch = get<TypeMismatch>(result.errors[0]);
    REQUIRE(mismatch);
    CHECK_EQ(toString(mismatch->wantedType), "string");
    CHECK_EQ(toString(mismatch->givenType), "number");
    CHECK_EQ(result.errors[1].location.begin.line, 4);
}

TEST_SUITE_END();
