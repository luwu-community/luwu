// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/AstQuery.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Scope.h"
#include "Luau/TypeInfer.h"
#include "Luau/Type.h"
#include "Luau/VisitType.h"

#include "Fixture.h"

#include "doctest.h"

using namespace Luau;

LUAU_FASTFLAG(DebugLuauForceOldSolver)
LUAU_FASTFLAG(LuauIndexingIntoErrorGivesError)

TEST_SUITE_BEGIN("TypeInferAnyError");

TEST_CASE_FIXTURE(Fixture, "for_in_loop_iterator_returns_any")
{
    CheckResult result = check(R"(
        function bar(): any
            return true
        end

        local a
        for b in bar do
            a = b
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    if (!FFlag::DebugLuauForceOldSolver)
        CHECK("(*error-type* | ~nil)?" == toString(requireType("a")));
    else
        CHECK(getBuiltins()->anyType == requireType("a"));
}

TEST_CASE_FIXTURE(Fixture, "for_in_loop_iterator_returns_any2")
{
    CheckResult result = check(R"(
        function bar(): any
            return true
        end

        local a
        for b in bar() do
            a = b
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    if (!FFlag::DebugLuauForceOldSolver)
        CHECK("(*error-type* | ~nil)?" == toString(requireType("a")));
    else
        CHECK("any" == toString(requireType("a")));
}

TEST_CASE_FIXTURE(Fixture, "for_in_loop_iterator_is_any")
{
    CheckResult result = check(R"(
        local bar = nil :: any

        local a
        for b in bar do
            a = b
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    if (!FFlag::DebugLuauForceOldSolver)
        CHECK("(*error-type* | ~nil)?" == toString(requireType("a")));
    else
        CHECK("any" == toString(requireType("a")));
}

TEST_CASE_FIXTURE(Fixture, "for_in_loop_iterator_is_any2")
{
    CheckResult result = check(R"(
        local bar = nil :: any

        local a
        for b in bar() do
            a = b
        end
    )");

    if (!FFlag::DebugLuauForceOldSolver)
        CHECK("(*error-type* | ~nil)?" == toString(requireType("a")));
    else
        CHECK("any" == toString(requireType("a")));
}

TEST_CASE_FIXTURE(Fixture, "for_in_loop_iterator_is_any_pack")
{
    CheckResult result = check(R"(
        function bar(): ...any end

        local a
        for b in bar() do
            a = b
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    if (!FFlag::DebugLuauForceOldSolver)
        CHECK("(*error-type* | ~nil)?" == toString(requireType("a")));
    else
        CHECK("any" == toString(requireType("a")));
}

TEST_CASE_FIXTURE(Fixture, "for_in_loop_iterator_is_error")
{
    CheckResult result = check(R"(
        local a
        for b in bar do
            a = b
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);


    if (!FFlag::DebugLuauForceOldSolver)
    {
        // Bug: We do not simplify at the right time
        CHECK_EQ("*error-type*?", toString(requireType("a")));
    }
    else
    {
        CHECK_EQ("*error-type*", toString(requireType("a")));
    }
}

TEST_CASE_FIXTURE(Fixture, "for_in_loop_iterator_is_error2")
{
    CheckResult result = check(R"(
        function bar(c) return c end

        local a
        for b in bar() do
            a = b
        end
    )");

    if (!FFlag::DebugLuauForceOldSolver)
    {
        // CLI-97375(awe): `bar()` is returning `nil` here, which isn't wrong necessarily,
        // but then we're signaling an additional error for the access on `nil`.
        LUAU_REQUIRE_ERROR_COUNT(2, result);

        // Bug: We do not simplify at the right time
        CHECK_EQ("*error-type*?", toString(requireType("a")));
    }
    else
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK_EQ("*error-type*", toString(requireType("a")));
    }
}

TEST_CASE_FIXTURE(Fixture, "length_of_error_type_does_not_produce_an_error")
{
    CheckResult result = check(R"(
        local l = #this_is_not_defined
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(Fixture, "indexing_error_type_does_not_produce_an_error")
{
    CheckResult result = check(R"(
        local originalReward = unknown.Parent.Reward:GetChildren()[1]
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(Fixture, "dot_on_error_type_does_not_produce_an_error")
{
    CheckResult result = check(R"(
        local foo = (true).x
        foo.x = foo.y
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(Fixture, "any_type_propagates")
{
    CheckResult result = check(R"(
        local foo: any
        local bar = foo:method("argument")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ("any", toString(requireType("bar")));
}

TEST_CASE_FIXTURE(Fixture, "can_subscript_any")
{
    CheckResult result = check(R"(
        local foo: any
        local bar = foo[5]
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ("any", toString(requireType("bar")));
}

// Not strictly correct: metatables permit overriding this
TEST_CASE_FIXTURE(Fixture, "can_get_length_of_any")
{
    CheckResult result = check(R"(
        local foo = ({} :: any)
        local bar = #foo
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ(PrimitiveType::Number, getPrimitiveType(requireType("bar")));
}

TEST_CASE_FIXTURE(Fixture, "assign_prop_to_table_by_calling_any_yields_any")
{
    CheckResult result = check(R"(
        local f: any
        local T = {}

        T.prop = f()

        return T
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    TableType* ttv = getMutable<TableType>(requireType("T"));
    REQUIRE(ttv);
    REQUIRE(ttv->props.count("prop"));

    REQUIRE(ttv->props["prop"].readTy);
    CHECK_EQ("any", toString(*ttv->props["prop"].readTy));
}

TEST_CASE_FIXTURE(Fixture, "quantify_any_does_not_bind_to_itself")
{
    CheckResult result = check(R"(
        local A : any
        function A.B() end
        A:C()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    TypeId aType = requireType("A");
    CHECK_EQ(aType, getBuiltins()->anyType);
}

TEST_CASE_FIXTURE(Fixture, "calling_error_type_yields_error")
{
    ScopedFastFlag _{FFlag::LuauIndexingIntoErrorGivesError, true};

    CheckResult result = check(R"(
        local a = unknown.Parent.Reward.GetChildren()
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    UnknownSymbol* err = get<UnknownSymbol>(result.errors[0]);
    REQUIRE(err != nullptr);

    CHECK_EQ("unknown", err->name);
    CHECK_EQ("*error-type*", toString(requireType("a")));
}

TEST_CASE_FIXTURE(Fixture, "chain_calling_error_type_yields_error")
{
    ScopedFastFlag _{FFlag::LuauIndexingIntoErrorGivesError, true};

    CheckResult result = check(R"(
        local a = Utility.Create "Foo" {}
    )");

    CHECK_EQ("*error-type*", toString(requireType("a")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "replace_every_free_type_when_unifying_a_complex_function_with_any")
{
    CheckResult result = check(R"(
        local a: any
        local b
        for _, i in pairs(a) do
            b = i
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("any", toString(requireType("b")));
}

TEST_CASE_FIXTURE(Fixture, "call_to_any_yields_any")
{
    CheckResult result = check(R"(
        local a: any
        local b = a()
    )");

    REQUIRE_EQ("any", toString(requireType("b")));
}

TEST_CASE_FIXTURE(Fixture, "CheckMethodsOfAny")
{
    CheckResult result = check(R"(
local x: any = {}
function x:y(z: number)
    local s: string = z
end
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(Fixture, "CheckMethodsOfError")
{
    CheckResult result = check(R"(
local x = (true).foo
function x:y(z: number)
    local s: string = z
end
)");

    LUAU_REQUIRE_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "metatable_of_any_can_be_a_table")
{
    CheckResult result = check(R"(
--!strict
local T: any
T = {}
T.__index = T
function T.new(...)
    local self = {}
    setmetatable(self, T)
    self:construct(...)
    return self
end
function T:construct(index)
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "type_error_addition")
{
    CheckResult result = check(R"(
--!strict
local foo = makesandwich()
local bar = foo.nutrition + 100
    )");

    // Luwu: and the first use of `foo`, which has no type because of it (NoTypeUse). Upstream reports only the first.
    size_t expectedErrors = FFlag::DebugLuauForceOldSolver ? 1 : 2;
    LUAU_REQUIRE_ERROR_COUNT(expectedErrors, result);

    // We should definitely get this error
    CHECK_EQ("Unknown global 'makesandwich'; consider assigning to it first", toString(result.errors[0]));
    if (!FFlag::DebugLuauForceOldSolver)
        CHECK(get<NoTypeUse>(result.errors[1]));
    // We get this error if makesandwich() returns a free type
    // CHECK_EQ("Unknown type used in + operation; consider adding a type annotation to 'foo'", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(Fixture, "prop_access_on_any_with_other_options")
{
    CheckResult result = check(R"(
        local function f(thing: any | string)
            local foo = thing.SomeRandomKey
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "union_of_types_regression_test")
{
    CheckResult result = check(R"(
--!strict
local stat
stat = stat and tonumber(stat) or stat
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "table_of_any_calls")
{
    CheckResult result = check(R"(
        local function testFunc(input: {any})
        end

        local v = {true}

        testFunc(v)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "intersection_of_any_can_have_props")
{
    ScopedFastFlag _{FFlag::LuauIndexingIntoErrorGivesError, true};

    // *blocked-130* ~ hasProp any & ~(false?), "_status"
    CheckResult result = check(R"(
function foo(x: any, y)
    if x then
        return x._status
    end
    return y
end
)");

    if (!FFlag::DebugLuauForceOldSolver)
    {
        // This is an artifact of formalizing `any = unknown | *error-type*`.
        // We refine `any` to `*error-type* | ~(false?)`, and then index
        // into it, versus the old solver path where we just claim refining
        // `any` means `any`.
        CHECK("(any, *error-type*) -> *error-type*" == toString(requireType("foo")));
    }
    else
    {
        CHECK("(any, any) -> any" == toString(requireType("foo")));
    }
}

TEST_CASE_FIXTURE(Fixture, "cast_to_table_of_any")
{
    CheckResult result = check(R"(
        local v = {true} :: {any}
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu: the first call or index of a local with no type is an error pointing at why it has none; every other use is a
// note, not an error
TEST_CASE_FIXTURE(BuiltinsFixture, "using_a_local_with_no_type_points_at_the_error_behind_it")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    CheckResult result = check(R"(
        local w = nope
        w.call()
        w()
        local copy = w
    )");

    // `nope` is unknown, and `w` has no type because of it
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<UnknownSymbol>(result.errors[0]));
    CHECK_EQ(Location{{2, 8}, {2, 9}}, result.errors[1].location);
    CHECK_EQ(
        "Attempt to use 'w', which has no type because of the error on line 2\n\n"
        "Help (no type):\n"
        "  • Fix the error on line 2 first; until then, uses of 'w' aren't checked",
        toString(result.errors[1])
    );

    ModulePtr module = getMainModule();
    REQUIRE_EQ(2, module->notes.size());
    CHECK_EQ(Location{{3, 8}, {3, 9}}, module->notes[0].location);
    CHECK_EQ(Location{{4, 21}, {4, 22}}, module->notes[1].location);
    CHECK_EQ("'w' has no type because of the error on line 2", module->notes[0].message);
    REQUIRE(module->notes[0].origin);
    CHECK_EQ(result.errors[0].location, *module->notes[0].origin);
}

// Luwu: locals with no type because of the same one are one cascade: one error at the first improper use of any of
// them, and notes pointing at the first error for every other use
TEST_CASE_FIXTURE(BuiltinsFixture, "locals_with_no_type_from_the_same_one_get_one_error")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    CheckResult result = check(R"(
        local w = nope
        local alias = w
        alias.call()
        w.call()
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<UnknownSymbol>(result.errors[0]));
    const NoTypeUse* use = get<NoTypeUse>(result.errors[1]);
    REQUIRE(use);
    CHECK_EQ("alias", use->name);
    CHECK_EQ(result.errors[0].location, use->origin);

    // The use of `w` in `alias`'s declaration, and in `w.call()`
    ModulePtr module = getMainModule();
    REQUIRE_EQ(2, module->notes.size());
    CHECK_EQ(Location{{4, 8}, {4, 9}}, module->notes[1].location);
    CHECK_EQ("'w' has no type because of the error on line 2", module->notes[1].message);
}

// Luwu: an error type that isn't a local's comes from an error in the same expression, which is the only error
TEST_CASE_FIXTURE(BuiltinsFixture, "an_error_type_in_the_expression_itself_reports_nothing_more")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    CheckResult result = check(R"(
        nope.call()
        nope.field.call()
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<UnknownSymbol>(result.errors[0]));
    CHECK(get<UnknownSymbol>(result.errors[1]));
    CHECK(getMainModule()->notes.empty());
}

// Luwu: the error behind a local with no type can be in an alias its type goes through
TEST_CASE_FIXTURE(BuiltinsFixture, "a_local_with_no_type_through_an_alias_points_at_the_alias")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    CheckResult result = check(R"(
        type Result = Nope
        local function get(): Result
            return 1 :: any
        end
        local result = get()
        result.call()
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<UnknownSymbol>(result.errors[0]));
    const NoTypeUse* use = get<NoTypeUse>(result.errors[1]);
    REQUIRE(use);
    CHECK_EQ(result.errors[0].location, use->origin);
}

TEST_SUITE_END();
