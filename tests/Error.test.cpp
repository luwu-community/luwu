// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Error.h"

#include "Fixture.h"
#include "doctest.h"

using namespace Luau;

LUAU_FASTFLAG(DebugLuauForceOldSolver)

TEST_SUITE_BEGIN("ErrorTests");

TEST_CASE("TypeError_code_should_return_nonzero_code")
{
    auto e = TypeError{{{0, 0}, {0, 1}}, UnknownSymbol{"Foo"}};
    CHECK_GE(e.code(), 1000);
}

TEST_CASE_FIXTURE(Fixture, "identical_type_names_from_one_module_are_not_qualified_twice")
{
    // Two same-named extern types from the same module stringify identically. Qualifying both sides
    // with that one module produced "Expected this to be 'Widget' from 'game.luau', but got
    // 'Widget' from 'game.luau'", which tells the reader nothing. With nothing that separates them,
    // the qualifier is dropped rather than repeated.
    TypeArena arena;

    TypeId a = arena.addType(ExternType{"Widget", {}, std::nullopt, std::nullopt, {}, nullptr, "game.luau", Location{{0, 0}, {0, 1}}});
    TypeId b = arena.addType(ExternType{"Widget", {}, std::nullopt, std::nullopt, {}, nullptr, "game.luau", Location{{9, 0}, {9, 1}}});

    TypeError e{Location{{0, 0}, {0, 1}}, TypeMismatch{a, b}};
    CHECK_EQ("Expected this to be 'Widget', but got 'Widget'", toString(e));
}

TEST_CASE_FIXTURE(Fixture, "identical_type_names_from_different_modules_are_qualified")
{
    TypeArena arena;

    TypeId a = arena.addType(ExternType{"Widget", {}, std::nullopt, std::nullopt, {}, nullptr, "a.luau", Location{{0, 0}, {0, 1}}});
    TypeId b = arena.addType(ExternType{"Widget", {}, std::nullopt, std::nullopt, {}, nullptr, "b.luau", Location{{0, 0}, {0, 1}}});

    TypeError e{Location{{0, 0}, {0, 1}}, TypeMismatch{a, b}};
    CHECK_EQ("Expected this to be 'Widget' from 'a.luau', but got 'Widget' from 'b.luau'", toString(e));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "metatable_names_show_instead_of_tables")
{
    getFrontend().options.retainFullTypeGraphs = false;

    CheckResult result = check(R"(
--!strict
local Account = {}
Account.__index = Account
function Account.deposit(self: Account, x: number)
	self.balance += x
end
type Account = typeof(setmetatable({} :: { balance: number }, Account))
local x: Account = 5
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    CHECK_EQ("Expected this to be 'Account', but got 'number'", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "binary_op_type_function_errors")
{
    getFrontend().options.retainFullTypeGraphs = false;

    CheckResult result = check(R"(
        --!strict
        local x = 1 + "foo"
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    if (!FFlag::DebugLuauForceOldSolver)
        CHECK_EQ(
            "Operator '+' could not be applied to operands of types number and string; there is no corresponding overload for __add",
            toString(result.errors[0])
        );
    else
        CHECK_EQ("Expected this to be 'number', but got 'string'", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "unary_op_type_function_errors")
{
    getFrontend().options.retainFullTypeGraphs = false;

    CheckResult result = check(R"(
        --!strict
        local x = -"foo"
    )");


    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(2, result);
        CHECK_EQ(
            "Operator '-' could not be applied to operand of type string; there is no corresponding overload for __unm", toString(result.errors[0])
        );

        CHECK_EQ("Expected this to be 'number', but got 'string'", toString(result.errors[1]));
    }
    else
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK_EQ("Expected this to be 'number', but got 'string'", toString(result.errors[0]));
    }
}

TEST_SUITE_END();
