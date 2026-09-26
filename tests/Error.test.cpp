// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
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

TEST_CASE_FIXTURE(Fixture, "errors_that_render_differently_are_not_equal")
{
    // Errors are deduplicated with ==, so two that print different messages must not compare equal.
    TypeId number = getBuiltins()->numberType;
    TypeId string = getBuiltins()->stringType;
    TypePackId empty = getBuiltins()->emptyTypePack;

    TypeMismatch plain{number, string};
    TypeMismatch overridden{number, string};
    overridden.overrideMessage = "a bespoke explanation";
    CHECK_FALSE((plain == overridden));

    TypeMismatch named{number, string};
    named.givenName = "value";
    CHECK_FALSE((plain == named));

    // `luwuExplanation` only selects the rendering style, so it doesn't make these two different errors.
    TypeMismatch explained{number, string};
    explained.luwuExplanation = true;
    CHECK((plain == explained));

    TypePackMismatch plainPack{empty, empty};
    TypePackMismatch overriddenPack{empty, empty};
    overriddenPack.overrideMessage = "a bespoke explanation";
    CHECK_FALSE((plainPack == overriddenPack));

    FunctionExitsWithoutReturning plainExit{empty};
    FunctionExitsWithoutReturning overriddenExit{empty};
    overriddenExit.overrideMessage = "This loop may not run, so the code path that skips it doesn't return 'nil'.";
    CHECK_FALSE((plainExit == overriddenExit));

    MissingProperties plainMissing{number, string, {"a"}};
    MissingProperties namedMissing{number, string, {"a"}};
    namedMissing.givenName = "config";
    CHECK_FALSE((plainMissing == namedMissing));

    MissingProperties explainedMissing{number, string, {"a"}};
    explainedMissing.luwuExplanation = true;
    CHECK((plainMissing == explainedMissing));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "metatable_names_show_instead_of_tables")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS()
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

TEST_CASE_FIXTURE(BuiltinsFixture, "metatable_names_show_instead_of_tables_in_explained_errors")
{
    ScopedFastFlag helpfulErrors{FFlag::LuwuHelpfulSubtypingErrors, true};
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

    CHECK_EQ("Expected this to be 'Account', but was given 'number'\n\n'number' is unrelated to 'Account'.", toString(result.errors[0]));
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
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS()
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

TEST_CASE_FIXTURE(BuiltinsFixture, "unary_op_type_function_errors_in_explained_errors")
{
    ScopedFastFlag helpfulErrors{FFlag::LuwuHelpfulSubtypingErrors, true};
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

        CHECK_EQ("Expected this to be 'number', but was given 'string'", toString(result.errors[1]));
    }
    else
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK_EQ("Expected this to be 'number', but got 'string'", toString(result.errors[0]));
    }
}

TEST_SUITE_END();
