// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Common.h"
#include "Luau/Error.h"
#include "Luau/ExperimentalFlags.h"
#include "Luau/TypeInfer.h"
#include "Luau/Type.h"

#include "Fixture.h"
#include "ClassFixture.h"

#include "ScopedFlags.h"
#include "doctest.h"

#include <cstring>
#include <sstream>

using namespace Luau;
using std::nullopt;

LUAU_FASTFLAG(DebugLuauForceOldSolver)
LUAU_FASTFLAG(LuauDropUnionSubtypeReasoning)
LUAU_FASTFLAG(LuauExternTypeGenericMethods)
LUAU_FASTFLAG(LuauExternTypeUseDefinitionScope)
LUAU_FASTFLAG(LuauGenericNominals)
LUAU_FASTFLAG(LuauHigherOrderGenericInference)
LUAU_FASTFLAG(LuauSolverV2)
LUAU_FASTFLAG(LuauAllowIntersectionOfOneTableWithExtern)

TEST_SUITE_BEGIN("TypeInferExternTypes");

TEST_CASE_FIXTURE(ExternTypeFixture, "Luau.Analyze.CLI_crashes_on_this_test")
{
    CheckResult result = check(R"(
        local CircularQueue = {}
CircularQueue.__index = CircularQueue

function CircularQueue:new()
	local newCircularQueue = {
		head = nil,
	}
	setmetatable(newCircularQueue, CircularQueue)

	return newCircularQueue
end

function CircularQueue:push()
	local newListNode

	if self.head then
		newListNode = {
			prevNode = self.head.prevNode,
			nextNode = self.head,
		}
		newListNode.prevNode.nextNode = newListNode
		newListNode.nextNode.prevNode = newListNode
	end
end

return CircularQueue

    )");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "call_method_of_a_class")
{
    CheckResult result = check(R"(
        local m = BaseClass.StaticMethod()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    REQUIRE_EQ("number", toString(requireType("m")));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "call_method_of_a_child_class")
{
    CheckResult result = check(R"(
        local m = ChildClass.StaticMethod()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    REQUIRE_EQ("number", toString(requireType("m")));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "call_instance_method")
{
    CheckResult result = check(R"(
        local i = ChildClass.New()
        local result = i:Method()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ("string", toString(requireType("result")));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "call_base_method")
{
    CheckResult result = check(R"(
        local i = ChildClass.New()
        i:BaseMethod(41)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "cannot_call_unknown_method_of_a_class")
{
    CheckResult result = check(R"(
        local m = BaseClass.Nope()
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "cannot_call_method_of_child_on_base_instance")
{
    CheckResult result = check(R"(
        local i = BaseClass.New()
        i:Method()
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "we_can_infer_that_a_parameter_must_be_a_particular_class")
{
    CheckResult result = check(R"(
        function makeClone(o)
            return BaseClass.Clone(o)
        end

        local a = makeClone(ChildClass.New())
    )");

    CHECK_EQ("BaseClass", toString(requireType("a")));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "we_can_report_when_someone_is_trying_to_use_a_table_rather_than_a_class")
{
    CheckResult result = check(R"(
        function makeClone(o)
            return BaseClass.Clone(o)
        end

        type Oopsies = { BaseMethod: (Oopsies, number) -> ()}

        local oopsies: Oopsies = {
            BaseMethod = function (self: Oopsies, i: number)
                print('gadzooks!')
            end
        }

        makeClone(oopsies)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors.at(0));
    REQUIRE(tm != nullptr);

    CHECK_EQ("Oopsies", toString(tm->givenType));
    CHECK_EQ("BaseClass", toString(tm->wantedType));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "we_can_report_when_someone_is_trying_to_use_a_table_rather_than_a_class_using_new_solver")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        function makeClone(o)
            return BaseClass.Clone(o)
        end

        type Oopsies = { read BaseMethod: (Oopsies, number) -> ()}

        local oopsies: Oopsies = {
            BaseMethod = function (self: Oopsies, i: number)
                print('gadzooks!')
            end
        }

        makeClone(oopsies)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors.at(0));
    REQUIRE(tm != nullptr);

    CHECK_EQ("Oopsies", toString(tm->givenType));
    CHECK_EQ("BaseClass", toString(tm->wantedType));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "assign_to_prop_of_class")
{
    CheckResult result = check(R"(
        local v = Vector2.New(0, 5)
        v.X = 55
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "can_read_prop_of_base_class")
{
    CheckResult result = check(R"(
        local c = ChildClass.New()
        local x = 1 + c.BaseField
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "can_assign_to_prop_of_base_class")
{
    CheckResult result = check(R"(
        local c = ChildClass.New()
        c.BaseField = 444
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "can_read_prop_of_base_class_using_string")
{
    CheckResult result = check(R"(
        local c = ChildClass.New()
        local x = 1 + c["BaseField"]
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "can_assign_to_prop_of_base_class_using_string")
{
    CheckResult result = check(R"(
        local c = ChildClass.New()
        c["BaseField"] = 444
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "cannot_unify_class_instance_with_primitive")
{
    // This is allowed in the new solver
    DOES_NOT_PASS_NEW_SOLVER_GUARD();

    CheckResult result = check(R"(
        local v = Vector2.New(0, 5)
        v = 444
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "warn_when_prop_almost_matches")
{
    CheckResult result = check(R"(
        Vector2.new(0, 0)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    auto err = get<UnknownPropButFoundLikeProp>(result.errors.at(0));
    REQUIRE(err != nullptr);

    REQUIRE_EQ(1, err->candidates.size());
    CHECK_EQ("New", *err->candidates.begin());
}

TEST_CASE_FIXTURE(ExternTypeFixture, "extern_types_can_have_overloaded_operators")
{
    CheckResult result = check(R"(
        local a = Vector2.New(1, 2)
        local b = Vector2.New(3, 4)
        local c = a + b
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ("Vector2", toString(requireType("c")));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "extern_types_without_overloaded_operators_cannot_be_added")
{
    CheckResult result = check(R"(
        local a = BaseClass.New()
        local b = BaseClass.New()
        local c = a + b
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "function_arguments_are_covariant")
{
    CheckResult result = check(R"(
        function f(b: BaseClass) end

        f(ChildClass.New())
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "higher_order_function_arguments_are_contravariant")
{
    CheckResult result = check(R"(
        function apply(f: (BaseClass) -> ())
            f(ChildClass.New()) -- 2
        end

        apply(function (c: ChildClass) end) -- 5
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "higher_order_function_return_values_are_covariant")
{
    CheckResult result = check(R"(
        function apply(f: () -> BaseClass)
            return f()
        end

        apply(function ()
            return ChildClass.New()
        end)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "higher_order_function_return_type_is_not_contravariant")
{
    CheckResult result = check(R"(
        function apply(f: () -> BaseClass)
            return f()
        end

        apply(function ()
            return ChildClass.New()
        end)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "table_properties_are_invariant")
{
    CheckResult result = check(R"(
        function f(a: {foo: BaseClass})
            a.foo = AnotherChild.New()
        end

        local t: {foo: ChildClass}
        f(t) -- line 6.  Breaks soundness.

        function g(t: {foo: ChildClass})
        end

        local t2: {foo: BaseClass} = {foo=BaseClass.New()}
        t2.foo = AnotherChild.New()
        g(t2) -- line 13.  Breaks soundness
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(6, result.errors.at(0).location.begin.line);
    CHECK_EQ(13, result.errors[1].location.begin.line);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "table_indexers_are_invariant")
{
    CheckResult result = check(R"(
        function f(a: {[number]: BaseClass})
            a[1] = AnotherChild.New()
        end

        local t: {[number]: ChildClass}
        f(t) -- line 6.  Breaks soundness.

        function g(t: {[number]: ChildClass})
        end

        local t2: {[number]: BaseClass} = {BaseClass.New()}
        t2[1] = AnotherChild.New()
        g(t2) -- line 13.  Breaks soundness
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(6, result.errors.at(0).location.begin.line);
    CHECK_EQ(13, result.errors[1].location.begin.line);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "table_class_unification_reports_sane_errors_for_missing_properties")
{
    CheckResult result = check(R"(
        function foo(bar)
            bar.Y = 1 -- valid
            bar.x = 2 -- invalid, wanted 'X'
            bar.w = 2 -- invalid
        end

        local a: Vector2
        foo(a)
    )");

    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK("Expected this to be '{ Y: number, w: number, x: number }', but got 'Vector2'" == toString(result.errors[0]));
    }
    else
    {
        LUAU_REQUIRE_ERROR_COUNT(2, result);
        REQUIRE_EQ("Key 'w' not found in external type 'Vector2'", toString(result.errors.at(0)));
        REQUIRE_EQ("Key 'x' not found in external type 'Vector2'.  Did you mean 'X'?", toString(result.errors[1]));
    }
}

TEST_CASE_FIXTURE(ExternTypeFixture, "class_unification_type_mismatch_is_correct_order")
{
    CheckResult result = check(R"(
        local p: BaseClass
        local foo: number = p
        local foo2: BaseClass = 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);

    REQUIRE_EQ("Expected this to be 'number', but got 'BaseClass'", toString(result.errors.at(0)));
    REQUIRE_EQ("Expected this to be 'BaseClass', but got 'number'", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "optional_class_field_access_error")
{
    CheckResult result = check(R"(
local b: Vector2? = nil
local a = b.X + b.Z

b.X = 2 -- real Vector2.X is also read-only
    )");

    LUAU_REQUIRE_ERROR_COUNT(4, result);
    CHECK_EQ("Value of type 'Vector2?' could be nil", toString(result.errors.at(0)));
    CHECK_EQ("Value of type 'Vector2?' could be nil", toString(result.errors[1]));
    CHECK_EQ("Key 'Z' not found in external type 'Vector2'", toString(result.errors[2]));
    CHECK_EQ("Value of type 'Vector2?' could be nil", toString(result.errors[3]));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "detailed_class_unification_error")
{
    CheckResult result = check(R"(
local function foo(v)
    return v.X :: number + string.len(v.Y)
end

local a: Vector2
local b = foo
b(a)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);


    if (!FFlag::DebugLuauForceOldSolver)
    {
        const std::string expected = "Expected this to be '{ read X: unknown, read Y: string }', but got 'Vector2'; \n"
                                     "accessing `Y` results in `number` in the latter type and `string` in the former type, "
                                     "and `number` is not a subtype of `string`";
        CHECK_EQ(expected, toString(result.errors.at(0)));
    }
    else
    {
        const std::string expected =
            R"(Expected this to be '{- X: number, Y: string -}', but got 'Vector2'
caused by:
  Property 'Y' is not compatible.
Expected this to be 'string', but got 'number')";

        CHECK_EQ(expected, toString(result.errors.at(0)));
    }
}

TEST_CASE_FIXTURE(ExternTypeFixture, "class_type_mismatch_with_name_conflict")
{
    CheckResult result = check(R"(
local i = ChildClass.New()
type ChildClass = { x: number }
local a: ChildClass = i
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Expected this to be 'ChildClass' from 'MainModule', but got 'ChildClass' from 'Test'", toString(result.errors.at(0)));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "intersections_of_unions_of_extern_types")
{
    CheckResult result = check(R"(
        local x : (BaseClass | Vector2) & (ChildClass | AnotherChild)
        local y : (ChildClass | AnotherChild)
        x = y
        y = x
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "unions_of_intersections_of_extern_types")
{
    CheckResult result = check(R"(
        local x : (BaseClass & ChildClass) | (BaseClass & AnotherChild) | (BaseClass & Vector2)
        local y : (ChildClass | AnotherChild)
        x = y
        y = x
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "index_instance_property")
{
    CheckResult result = check(R"(
        local function execute(object: BaseClass, name: string)
            print(object[name])
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Attempting a dynamic property access on type 'BaseClass' is unsafe and may cause exceptions at runtime", toString(result.errors.at(0)));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "index_instance_property_nonstrict")
{
    CheckResult result = check(R"(
        --!nonstrict

        local function execute(object: BaseClass, name: string)
            print(object[name])
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "type_mismatch_invariance_required_for_error")
{
    CheckResult result = check(R"(
type A = { x: ChildClass }
type B = { x: BaseClass }

local a: A = { x = ChildClass.New() }
local b: B = a
    )");

    LUAU_REQUIRE_ERRORS(result);

    if (!FFlag::DebugLuauForceOldSolver)
    {
        CHECK(
            "Expected this to be 'B', but got 'A'; \n"
            "accessing `x` results in `ChildClass` in the latter type and `BaseClass` in the former type, and `ChildClass` is not "
            "exactly `BaseClass`" == toString(result.errors.at(0))
        );
    }
    else
    {
        const std::string expected =
            R"(Expected this to be exactly 'B', but got 'A'
caused by:
  Property 'x' is not compatible.
Expected this to be exactly 'BaseClass', but got 'ChildClass')";
        CHECK_EQ(expected, toString(result.errors.at(0)));
    }
}

TEST_CASE_FIXTURE(ExternTypeFixture, "optional_class_casts_work_in_new_solver")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type A = { x: ChildClass }
        type B = { x: BaseClass }

        local a = { x = ChildClass.New() } :: A
        local opt_a = a :: A?
        local b = { x = BaseClass.New() } :: B
        local opt_b = b :: B?
        local b_from_a = a :: B
        local b_from_opt_a = opt_a :: B
        local opt_b_from_a = a :: B?
        local opt_b_from_opt_a = opt_a :: B?
        local a_from_b = b :: A
        local a_from_opt_b = opt_b :: A
        local opt_a_from_b = b :: A?
        local opt_a_from_opt_b = opt_b :: A?
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "callable_extern_types")
{
    CheckResult result = check(R"(
        local x : CallableClass
        local y = x("testing")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("number", toString(requireType("y")));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "indexable_extern_types")
{
    ScopedFastFlag _{FFlag::LuauDropUnionSubtypeReasoning, true};
    // Test reading from an index
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            local y = x.stringKey
        )");
        LUAU_REQUIRE_NO_ERRORS(result);
    }
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            local y = x["stringKey"]
        )");
        LUAU_REQUIRE_NO_ERRORS(result);
    }
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            local str : string
            local y = x[str]            -- Index with a non-const string
        )");
        LUAU_REQUIRE_NO_ERRORS(result);
    }
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            local y = x[7]              -- Index with a numeric key
        )");
        LUAU_REQUIRE_NO_ERRORS(result);
    }

    // Test writing to an index
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            x.stringKey = 42
        )");
        LUAU_REQUIRE_NO_ERRORS(result);
    }
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            x["stringKey"] = 42
        )");
        LUAU_REQUIRE_NO_ERRORS(result);
    }
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            local str : string
            x[str] = 42                 -- Index with a non-const string
        )");
        LUAU_REQUIRE_NO_ERRORS(result);
    }
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            x[1] = 42                   -- Index with a numeric key
        )");
        LUAU_REQUIRE_NO_ERRORS(result);
    }

    // Try to index the class using an invalid type for the key (key type is 'number | string'.)
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            local y = x[true]
        )");

        if (!FFlag::DebugLuauForceOldSolver)
        {
            const std::string expected = "Expected this to be 'number | string', but got 'boolean'";
            CHECK_LONG_STRINGS_EQ(expected, toString(result.errors[0]));
        }
        else
            CHECK_EQ(
                toString(result.errors.at(0)), "Expected this to be 'number | string', but got 'boolean'; none of the union options are compatible"
            );
    }
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            x[true] = 42
        )");

        if (!FFlag::DebugLuauForceOldSolver)
        {
            const std::string expected = "Expected this to be 'number | string', but got 'boolean'";
            CHECK_LONG_STRINGS_EQ(expected, toString(result.errors[0]));
        }
        else
            CHECK_EQ(
                toString(result.errors.at(0)), "Expected this to be 'number | string', but got 'boolean'; none of the union options are compatible"
            );
    }

    // Test type checking for the return type of the indexer (i.e. a number)
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            x.key = "string value"
        )");

        if (!FFlag::DebugLuauForceOldSolver)
        {
            // Disabled for now.  CLI-115686
        }
        else
            CHECK_EQ(toString(result.errors.at(0)), "Expected this to be 'number', but got 'string'");
    }
    {
        CheckResult result = check(R"(
            local x : IndexableClass
            local str : string = x.key
        )");

        CHECK_EQ(toString(result.errors.at(0)), "Expected this to be 'string', but got 'number'");
    }

    // Check that we string key are rejected if the indexer's key type is not compatible with string
    {
        CheckResult result = check(R"(
            local x : IndexableNumericKeyClass
            x.key = 1
        )");
        CHECK_EQ(toString(result.errors.at(0)), "Key 'key' not found in external type 'IndexableNumericKeyClass'");
    }
    {
        CheckResult result = check(R"(
            local x : IndexableNumericKeyClass
            x["key"] = 1
        )");
        if (!FFlag::DebugLuauForceOldSolver)
            CHECK_EQ(toString(result.errors.at(0)), "Key 'key' not found in external type 'IndexableNumericKeyClass'");
        else
            CHECK_EQ(toString(result.errors.at(0)), "Expected this to be 'number', but got 'string'");
    }
    {
        CheckResult result = check(R"(
            local x : IndexableNumericKeyClass
            local str : string
            x[str] = 1                  -- Index with a non-const string
        )");

        CHECK_EQ(toString(result.errors.at(0)), "Expected this to be 'number', but got 'string'");
    }
    {
        CheckResult result = check(R"(
            local x : IndexableNumericKeyClass
            local y = x.key
        )");
        CHECK_EQ(toString(result.errors.at(0)), "Key 'key' not found in external type 'IndexableNumericKeyClass'");
    }
    {
        CheckResult result = check(R"(
            local x : IndexableNumericKeyClass
            local y = x["key"]
        )");
        if (!FFlag::DebugLuauForceOldSolver)
            CHECK(toString(result.errors.at(0)) == "Key 'key' not found in external type 'IndexableNumericKeyClass'");
        else
            CHECK_EQ(toString(result.errors.at(0)), "Expected this to be 'number', but got 'string'");
    }
    {
        CheckResult result = check(R"(
            local x : IndexableNumericKeyClass
            local str : string
            local y = x[str]            -- Index with a non-const string
        )");

        CHECK_EQ(toString(result.errors.at(0)), "Expected this to be 'number', but got 'string'");
    }
}

TEST_CASE_FIXTURE(Fixture, "read_write_class_properties")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    TypeArena& arena = getFrontend().globals.globalTypes;

    unfreeze(arena);

    TypeId instanceType = arena.addType(ExternType{"Instance", {}, nullopt, nullopt, {}, {}, "Test", {}});
    getMutable<ExternType>(instanceType)->props = {{"Parent", Property::rw(instanceType)}};

    //

    TypeId workspaceType = arena.addType(ExternType{"Workspace", {}, nullopt, nullopt, {}, {}, "Test", {}});

    TypeId scriptType =
        arena.addType(ExternType{"Script", {{"Parent", Property::rw(workspaceType, instanceType)}}, instanceType, nullopt, {}, {}, "Test", {}});

    TypeId partType = arena.addType(
        ExternType{
            "Part",
            {{"BrickColor", Property::rw(getBuiltins()->stringType)}, {"Parent", Property::rw(workspaceType, instanceType)}},
            instanceType,
            nullopt,
            {},
            {},
            "Test",
            {}
        }
    );

    getMutable<ExternType>(workspaceType)->props = {{"Script", Property::readonly(scriptType)}, {"Part", Property::readonly(partType)}};

    getFrontend().globals.globalScope->bindings[getFrontend().globals.globalNames.names->getOrAdd("script")] = Binding{scriptType};

    freeze(arena);

    CheckResult result = check(R"(
        script.Parent.Part.BrickColor = 0xFFFFFF
        script.Parent.Part.Parent = script
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    CHECK(Location{{1, 40}, {1, 48}} == result.errors[0].location);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(getBuiltins()->stringType == tm->wantedType);
    CHECK(getBuiltins()->numberType == tm->givenType);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "cannot_index_a_class_with_no_indexer")
{
    CheckResult result = check(R"(
        local a = BaseClass.New()

        local c = a[1]
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    CHECK_MESSAGE(
        get<DynamicPropertyLookupOnExternTypesUnsafe>(result.errors[0]),
        "Expected DynamicPropertyLookupOnExternTypesUnsafe but got " << result.errors[0]
    );

    CHECK(getBuiltins()->errorType == requireType("c"));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "cyclic_tables_are_assumed_to_be_compatible_with_extern_types")
{
    /*
     * This is technically documenting a case where we are intentionally
     * unsound.
     *
     * Our builtins are essentially defined like so:
     *
     * declare extern type BaseClass with
     *     BaseField: number
     *     function BaseMethod(self, number): ()
     *     read Touched: Connection
     * end
     *
     * declare extern type Connection with
     *     Connect: (Connection, (BaseClass) -> ()) -> ()
     * end
     *
     * The type we infer for `onTouch` is
     *
     * (t1) -> () where t1 = { read BaseField: unknown, read BaseMethod: (t1, number) -> () }
     *
     * In order to validate that onTouch can be passed to Connect, we must
     * verify the following relation:
     *
     * BaseClass <: t1 where t1 = { read BaseField: unknown, read BaseMethod: (t1, number) -> () }
     *
     * However, the cycle between the table and the function gums up the works
     * here and the worst thing is that it's perfectly reasonable in principle.
     * Just from these types, we cannot see that BaseMethod will only be passed
     * t1.  Without that guarantee, BaseClass cannot be used as a subtype of t1.
     *
     * I think the theoretically-correct way to untangle this would be to infer
     * t1 as a bounded existential type.
     *
     * For now, we have a subtyping has a rule that provisionally substitutes
     * the table for the class type when performing the subtyping test.  We
     * essentially assume that, for all cyclic functions, that the table and the
     * class are mutually subtypes of one another.
     *
     * For more information, read uses of Subtyping::substitutions.
     */

    CheckResult result = check(R"(
        local c = BaseClass.New()

        function requiresNothing() end

        function onTouch(other)
            requiresNothing(other:BaseMethod(0))
            print(other.BaseField)
        end

        c.Touched:Connect(onTouch)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "ice_while_checking_script_due_to_scopes_not_being_solver_agnostic")
{
    // This is intentional - if LuauSolverV2 is false, but we elect the new solver, we should still follow
    // new solver code paths.
    // This is necessary to repro an ice that can occur in studio
    ScopedFastFlag luauSolverOff{FFlag::DebugLuauForceOldSolver, true};
    getFrontend().setLuauSolverMode(SolverMode::New);

    auto result = check(R"(
local function ExitSeat(player, character, seat, weld)
    --Find vehicle model
    local model
    local newParent = seat
    repeat
        model = newParent
        newParent = model.Parent
    until newParent.ClassName ~= "Model"
    local part, _ = Raycast(seat.Position, dir, dist, {character, model})
end
)");
    LUAU_REQUIRE_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "extern_type_check_missing_key")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type Foobar with
            Enabled: boolean
            function Disable(self): ()
        end
    )");

    CheckResult results = check(R"(
        local isUsingGamepad = false
        local isModalVisible = false

        local function updateGamepadCursor(foo: Foobar)
            local shouldEnableCursor = isUsingGamepad and isModalVisible

            if foo.IsEnabled == shouldEnableCursor then
                return
            end

            if not shouldEnableCursor then
                foo:Disable()
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, results);
    auto err = get<UnknownProperty>(results.errors[0]);
    CHECK_EQ("IsEnabled", err->key);
}

TEST_CASE_FIXTURE(Fixture, "extern_type_check_present_key_in_superclass")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type FoobarParent with
            IsEnabled: boolean
        end
        declare extern type Foobar extends FoobarParent with
            function Disable(self): ()
        end
    )");

    CheckResult results = check(R"(
        local isUsingGamepad = false
        local isModalVisible = false

        local function updateGamepadCursor(foo: Foobar)
            local shouldEnableCursor = isUsingGamepad and isModalVisible

            if foo.IsEnabled == shouldEnableCursor then
                return
            end

            if not shouldEnableCursor then
                foo:Disable()
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(results);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_check_key_becomes_never")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type Foobar with
            IsEnabled: string
        end

        declare extern type Bing with
            IsEnabled: number
        end
    )");

    CheckResult results = check(R"(
        local function update(foo: Foobar | Bing)
            assert(type(foo.IsEnabled) == "number")
            return foo
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(results);
    CHECK_EQ("(Bing | Foobar) -> Bing", toString(requireType("update")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_check_key_becomes_intersection")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type Foobar with
            IsEnabled: string | boolean
        end
    )");

    CheckResult results = check(R"(
        local function update(foo: Foobar)
            assert(type(foo.IsEnabled) == "string")
            return foo
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(results);
    CHECK_EQ("(Foobar) -> Foobar & { read IsEnabled: string }", toString(requireType("update")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_check_key_superset")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type Foobar with
            IsEnabled: string
        end
    )");

    CheckResult results = check(R"(
        local function update(foo: Foobar)
            assert(type(foo.IsEnabled) == "string" or type(foo.IsEnabled) == "number")
            return foo
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(results);
    CHECK_EQ("(Foobar) -> Foobar", toString(requireType("update")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_check_key_idempotent")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type Foobar with
            IsEnabled: string
        end
    )");

    CheckResult results = check(R"(
        local function update(foo: Foobar)
            assert(type(foo.IsEnabled) == "string")
            return foo
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(results);
    CHECK_EQ("(Foobar) -> Foobar", toString(requireType("update")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_intersect_with_table_indexer")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        local function f(obj: { [any]: any }, functionName: string)
            if typeof(obj) == "userdata" then
                local _ = obj[functionName]
            end
        end
    )"));

    CHECK_EQ("userdata & { [any]: any }", toString(requireTypeAtPosition({3, 28})));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_with_indexer_intersect_table")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type Foobar with
            [string]: unknown
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        local function update(obj: Foobar)
            assert(typeof(obj.Baz) == "number")
            return obj
        end
    )"));

    CHECK_EQ("(Foobar) -> Foobar & { read Baz: number }", toString(requireType("update")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_is_not_subtype_of_table")
{
    loadDefinition(R"(
        declare extern type Color3 with
        end
    )");

    CheckResult result = check(R"(
        local function f(c: Color3): { Color3 }
            return c
        end
    )");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<TypeMismatch>(result.errors[0]);
    CHECK_EQ("Color3", toString(err->givenType));
    CHECK_EQ("{Color3}", toString(err->wantedType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_overload")
{
    loadDefinition(R"(
        declare extern type Color3 with
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        local f : ((Color3) -> ()) & (({Color3}) -> ())
        local c: Color3
        f(c)
    )"));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_indexer_interactions")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type Container with
            [string | number]: boolean | string
        end

        declare extern type Point with
            X: number
            Y: number
        end
    )");

    CheckResult result = check(R"(
        local c: Container
        local p: Point
        local _: { [ string | number ]: boolean | string } = c -- OK
        local _: { [string]: boolean | string } = c -- not OK
        local _: { [ string | number ]: boolean } = c -- not OK
        local _: { [string]: number } = p -- not OK
    )");
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    for (const auto& err : result.errors)
        CHECK(get<TypeMismatch>(err));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_intersection_with_table_type_1")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
    };

    loadDefinition(R"(
        declare extern type Instance with
            name: string
        end

        declare extern type WithBrushes extends Instance with
            brushes: Instance
        end
    )");

    CheckResult result = check(R"(
        function take(thing: WithBrushes & { brushes: Instance })
            print(thing)
            print(thing.brushes.name)
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    // These two types are entirely coincident, so we could imagine a world where this becomes simply `WithBrushes`, but
    // the principal here is that the user wrote the annotation in this way, and so we're propagating that without normalizing.
    CHECK_EQ("WithBrushes & { brushes: Instance }", toString(requireTypeAtPosition({2, 18})));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_intersection_with_table_type_2")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
    };

    loadDefinition(R"(
        declare extern type Instance with
            name: string
        end

        declare extern type WithBrushes extends Instance with
            brushes: Instance
        end
    )");

    CheckResult result = check(R"(
        function take(thing: Instance & { brushes: Instance })
            print(thing)
            print(thing.brushes.name)
        end
    )");

    LUAU_CHECK_NO_ERRORS(result);

    CHECK_EQ("Instance & { brushes: Instance }", toString(requireTypeAtPosition({2, 18})));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generic_method_property_syntax_resolves_without_leak")
{
    // Regression test: type references inside an extern type's body (here, a generic
    // method's own type parameter, declared using the pre-existing colon-property syntax)
    // must resolve against the extern type's own definition scope. Before
    // LuauExternTypeUseDefinitionScope, the per-method generic scope was created as a
    // sibling of the (unused) extern type definition scope rather than a child of it, so
    // TypeChecker2's location-based scope lookup could never find it, and `T` was reported
    // as an unresolved global.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
    };

    loadDefinition(R"(
        declare extern type Cat with
            meow: <T>(self: Cat, whatever: T) -> T
        end
    )");

    CheckResult result = check(R"(
        local c: Cat = nil :: any
        local x = c:meow(5)
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("number", toString(requireType("x")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generic_method_property_syntax_still_broken_without_flag")
{
    // Without the fix, `T` fails to resolve while checking the definition file itself
    // (not the code that uses Cat), so loading the definition file is where this fails.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, false},
        {FFlag::LuauGenericNominals, false},
    };

    unfreeze(getFrontend().globals.globalTypes);
    LoadDefinitionFileResult loadResult = getFrontend().loadDefinitionFile(
        getFrontend().globals, getFrontend().globals.globalScope, R"(
        declare extern type Cat with
            meow: <T>(self: Cat, whatever: T) -> T
        end
    )",
        "@test",
        /* captureComments */ false,
        /* typecheckForAutocomplete */ false
    );
    freeze(getFrontend().globals.globalTypes);

    CHECK(!loadResult.success);
    REQUIRE(loadResult.module);
    CHECK(!loadResult.module->errors.empty());
}

TEST_CASE_FIXTURE(Fixture, "extern_type_function_sugar_generic_method_resolves")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauExternTypeGenericMethods, true},
    };

    loadDefinition(R"(
        declare extern type Cat with
            function meow<T>(self, whatever: T): T
        end
    )");

    CheckResult result = check(R"(
        local c: Cat = nil :: any
        local x = c:meow(5)
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("number", toString(requireType("x")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generic_method_multiple_params")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauExternTypeGenericMethods, true},
    };

    loadDefinition(R"(
        declare extern type Cat with
            function pair<T, U>(self, a: T, b: U): (T, U)
        end
    )");

    CheckResult result = check(R"(
        local c: Cat = nil :: any
        local x, y = c:pair(5, "hello")
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("number", toString(requireType("x")));
    CHECK_EQ("string", toString(requireType("y")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generic_method_infers_union_return_from_argument")
{
    // Method that takes T as a parameter and returns T | string: T should be inferred
    // from the argument, and the call's result type should reflect the full union.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauExternTypeGenericMethods, true},
    };

    loadDefinition(R"(
        declare extern type Cat with
            function meowOrName<T>(self, whatever: T): T | string
        end
    )");

    CheckResult result = check(R"(
        local c: Cat = nil :: any
        local result = c:meowOrName(5)
    )");

    LUAU_CHECK_NO_ERRORS(result);

    TypeId resultTy = requireType("result");
    const UnionType* ut = get<UnionType>(follow(resultTy));
    REQUIRE_MESSAGE(ut, "Expected a union type, got " << toString(resultTy));

    bool hasNumber = false;
    bool hasString = false;
    for (TypeId option : ut)
    {
        option = follow(option);
        if (get<PrimitiveType>(option) && toString(option) == "number")
            hasNumber = true;
        if (get<PrimitiveType>(option) && toString(option) == "string")
            hasString = true;
    }

    CHECK(hasNumber);
    CHECK(hasString);
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generic_method_explicit_instantiation")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauExternTypeGenericMethods, true},
    };

    loadDefinition(R"(
        declare extern type HttpResponse with
            function try_json<T>(self): T?
        end
    )");

    CheckResult result = check(R"(
        local response: HttpResponse = nil :: any
        local data = response:try_json<<{ name: string }>>()
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("{ name: string }?", toString(requireType("data")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generic_method_explicit_instantiation_mismatch")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauExternTypeGenericMethods, true},
    };

    loadDefinition(R"(
        declare extern type HttpResponse with
            function try_json<T>(self): T?
        end
    )");

    CheckResult result = check(R"(
        local response: HttpResponse = nil :: any
        local data: number = response:try_json<<string>>()
    )");

    LUAU_REQUIRE_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_instantiate")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
    };

    loadDefinition(R"(
        declare extern type Box<T> with
            value: T
        end

        declare Box: {
            new: <T>(value: T) -> Box<T>
        }
    )");

    CheckResult result = check(R"(
        local b = Box.new(5)
        local x = b.value
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("Box<number>", toString(requireType("b")));
    CHECK_EQ("number", toString(requireType("x")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_nested_instantiation")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
    };

    loadDefinition(R"(
        declare extern type Box<T> with
            value: T
        end

        declare Box: {
            new: <T>(value: T) -> Box<T>
        }
    )");

    CheckResult result = check(R"(
        type Nested = Box<Box<string>>
        local n = (nil :: any) :: Nested
        local inner = n.value
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("Box<Box<string>>", toString(requireType("n")));
    CHECK_EQ("Box<string>", toString(requireType("inner")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_nested_instantiation_with_unused_param")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
    };

    // `T` does not appear anywhere in Result's own body, so the type argument used to
    // instantiate the outer Result<...> is the only place a nested, still-unresolved
    // generic instantiation (like `Result<string>`) can be found.
    loadDefinition(R"(
        declare extern type Result<T> with
            cats: string
        end
    )");

    CheckResult result = check(R"(
        type meow = Result<Result<string>>
        local meo = (nil :: any) :: meow
        local cats = meo.cats
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("Result<Result<string>>", toString(requireType("meo")));
    CHECK_EQ("string", toString(requireType("cats")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_two_params_with_method")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
    };

    loadDefinition(R"(
        declare extern type Result<Ok, Err> with
            inner: Ok | Err
            function is_ok(self): boolean
            function expect(self, assertion: string?): Ok
        end

        declare function try_<T, E>(f: () -> T): Result<T, E>
    )");

    CheckResult result = check(R"(
        local function get_sketchy(): string
            return "x"
        end

        local r = try_(get_sketchy)
        if r:is_ok() then
            local response = r:expect()
        end
    )");

    LUAU_CHECK_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_explicit_partial_instantiation_KNOWN_BUG_produces_cyclic_type")
{
    // KNOWN BUG, not yet fixed: explicitly instantiating only a leading subset of a function's
    // generics (e.g. `try_<<string>>(...)`, pinning E and leaving Args/T to be inferred) while its
    // return type expands a generic nominal type (`declare extern type Result<Ok, Err> with ...`)
    // produces a corrupted, self-referential result type (toString shows `Result<Result<*CYCLE*>,
    // string>` -- guarded against hanging/unbounded output by the cycle check in ToString.cpp, but
    // the underlying type is still wrong) and an "outstanding free or blocked type" internal error
    // from TypeChecker2 on any subsequent method call. Traced to a single TypeAliasExpansionConstraint
    // dispatch for `Result<T, E>` whose own `T` argument ends up bound back to the result type itself
    // -- likely a Unifier2/instantiate2 issue unifying a still-pending return type against the fresh
    // inferred return pack, not something in the substitution/display work this test file otherwise
    // covers. The identical explicit-partial-instantiation pattern against a plain table return type
    // works correctly, so this is specific to generic nominal (extern type) expansion.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
    };

    loadDefinition(R"(
        declare extern type Result<Ok, Err> with
            inner: Ok | Err
            function is_ok(self): boolean
            function expect(self, assertion: string?): Ok
        end

        declare function try_<E, Args, T>(f: (...Args) -> T, ...: Args): Result<T, E>
    )");

    CheckResult result = check(R"(
        local function get_sketchy(url: string): string
            return "x"
        end

        local r = try_<<string>>(get_sketchy, "someurl")
        if r:is_ok() then
            local response = r:expect()
        end
    )");

    // Once fixed, this should be LUAU_CHECK_NO_ERRORS(result) with r typed as Result<string, string>.
    CHECK(!result.errors.empty());
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_independent_instantiations_are_compatible")
{
    // Two independently-produced clones of the same generic nominal instantiation (one from
    // resolving the `List<string>` annotation, one from substituting the vararg constructor's
    // return type) are different TypeIds but must still be recognized as the same nominal type.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
        {FFlag::LuauHigherOrderGenericInference, true},
    };

    loadDefinition(R"(
        declare extern type List<T> with
            function get(self, idx: number): T?
        end

        declare list: {
            new: <T>(...T) -> List<T>,
        }
    )");

    CheckResult result = check(R"(
        local listy: List<string> = list.new("cats", "dogs")
        local res = listy:get(1)
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("List<string>", toString(requireType("listy")));
    CHECK_EQ("string?", toString(requireType("res")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "extern_type_generics_typeof_refines_generic_instantiation")
{
    // `typeof(x) == "ClassName"` refinement already narrows unions for non-generic extern
    // types; it used to silently do nothing for generic ones because the lookup that finds the
    // class by name required `typeFun->typeParams.empty()`, which is never true for a generic
    // extern type's TypeFun.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
        {FFlag::LuauHigherOrderGenericInference, true},
    };

    loadDefinition(R"(
        declare extern type Exception<Data> with
            inner: Data
        end
    )");

    CheckResult result = check(R"(
        local function can_fail(): string | Exception<{kind: string}>
            return nil :: any
        end

        local ress = can_fail()
        if typeof(ress) == "Exception" then
            local inner = ress.inner
            local k = inner.kind
        end
    )");

    LUAU_CHECK_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_unconstrained_generic_resolved_from_expected_type")
{
    // A generic that only appears in the return type, never in any argument (e.g. `E` in
    // `function ok<T, E>(value: T): Result<T, E>`), has nothing to bind it at the call site,
    // so it used to default to `unknown` -- which then failed a subtype check against whatever
    // the call's surrounding context actually expected. If the call site has a known expected
    // type shaped like the same generic nominal type, that should resolve the otherwise-free
    // generic instead.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
        {FFlag::LuauHigherOrderGenericInference, true},
    };

    loadDefinition(R"(
        declare extern type List<T> with
            function front(self): T?
        end

        declare list: {
            new: <T>(...T) -> List<T>,
        }

        declare extern type Result<T, E> with
            function is_ok(self): boolean
        end

        declare result: {
            ok: <T, E>(ok: T) -> Result<T, E>,
        }
    )");

    CheckResult result = check(R"(
        local list_res: Result<List<string>, string> = result.ok(list.new("cats", "dogs"))
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("Result<List<string>, string>", toString(requireType("list_res")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_inferred_from_union_argument_member")
{
    // `expect<T, E>(r: T | Exception<E>): T` called with an argument of type
    // `string | Exception<FileIoError>` must bind T = string and E = FileIoError from matching
    // up the union members positionally by shape, not by unifying every argument-union member
    // against every parameter-union member indiscriminately. Two bugs used to conspire here:
    // (1) Unifier2 had no ExternType-vs-ExternType case at all, so `Exception<E>`'s own type
    // argument could never be bound from a concrete `Exception<FileIoError>` argument; and (2)
    // union-member unification tried every (subOption, superOption) pair whose `areCompatible`
    // check passed, and a bare free type (standing in for `T`) is trivially "compatible" with
    // anything -- so `Exception<FileIoError>` also got unified against `T`, polluting T's lower
    // bound to `string | Exception<FileIoError>` instead of leaving it as plain `string`.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
        {FFlag::LuauHigherOrderGenericInference, true},
    };

    loadDefinition(R"(
        declare extern type Exception<Data> with
            inner: Data
        end

        declare function expect<T, E>(r: T | Exception<E>): T
    )");

    CheckResult result = check(R"(
        type FileIoError = { kind: string }

        local function get(): string | Exception<FileIoError>
            return "x" :: any
        end

        local content = expect(get())
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("string", toString(requireType("content")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_mismatch_reasoning_is_terse_and_context_aware")
{
    // Regression test for a confusing TypeMismatch explanation. Two problems used to compound
    // here: (1) when the sub type's reasoning path drilled further into a union than the super
    // type's path did, the old phrasing repeated their shared "it returns the 1st entry in the
    // type pack" lead-in twice back-to-back; and (2) even fixed, that lead-in is jargon ("type
    // pack", "component of the union") that doesn't help readers and is redundant with the
    // wanted/got types already printed above it. The mismatch is entirely about the function's
    // return type, so the whole explanation should read as a single short, plain-English line:
    // a context-aware preamble ("Expected this function to return") instead of the generic
    // "Expected this to be", and a bare leaf comparison with no path narration at all.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
        {FFlag::LuauHigherOrderGenericInference, true},
        {FFlag::LuauDropUnionSubtypeReasoning, true},
    };

    loadDefinition(R"(
        declare extern type Exception<Data> with
            inner: Data
        end
    )");

    CheckResult result = check(R"(
        type Wrapped = { x: number }

        local function bad(x: string): Exception<Wrapped> | number
            return 1 :: any
        end

        local f: (string) -> Exception<string> | number = bad
    )");

    REQUIRE_EQ(1, result.errors.size());
    std::string message = toString(result.errors[0]);

    CHECK_EQ(
        "Expected this function to return\n"
        "\t'(string) -> Exception<string> | number'\n"
        "but got\n"
        "\t'(string) -> Exception<Wrapped> | number'; \n"
        "`Exception<Wrapped>` is not a subtype of `Exception<string> | number`",
        message
    );
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_independent_instantiations_are_compatible_when_nested")
{
    // Same idea as extern_type_generics_independent_instantiations_are_compatible, but the
    // mismatched clones are *nested inside another generic nominal type's type arguments*
    // (`Result<string, Option<T>>`), so isSameGenericNominalInstantiation must compare those
    // type arguments recursively (via nominal identity) rather than by raw TypeId/follow()
    // equality, since two `Option<T>` instantiations built at different call sites are
    // different TypeIds.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
        {FFlag::LuauHigherOrderGenericInference, true},
    };

    loadDefinition(R"(
        declare extern type List<T> with
            function front(self): T?
        end

        declare list: {
            new: <T>(...T) -> List<T>,
        }

        declare extern type Option<T> with
            function try_some(self): T?
        end

        declare option: {
            some: <T>(T) -> Option<T>,
        }

        declare extern type Result<T, E> with
            function is_ok(self): boolean
        end

        declare result: {
            err: <T, E>(err: E) -> Result<T, E>,
        }
    )");

    CheckResult result = check(R"(
        local function get(): Result<string, Option<List<string>>>
            local listy = list.new("cats", "dogs")
            return result.err<<string, Option<List<string>>>>(option.some(listy))
        end
    )");

    LUAU_CHECK_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_vararg_constructor_method_only_generic")
{
    // Covers a combination the other tests in this file don't: a vararg generic constructor
    // (`<T>(...T) -> List<T>`), a forward reference (`list` declared before `List<T>` itself,
    // matching how real-world definition files are usually written), and a generic that only
    // appears inside a method's signature (`function get(self, idx): T?`), never in a plain
    // field. All three needed their own fix:
    //  - Tarjan::visitChildren (Substitution.cpp) never walked ExternType::instantiatedTypeParams,
    //    so dirty-detection could miss a generic that's otherwise only reachable through it.
    //  - Clone.cpp's cloneChildren(ExternType*) never remapped instantiatedTypeParams/PackParams
    //    through shallowClone, so a deep-cloned instance (e.g. at a call site) kept pointing at
    //    the pre-clone generic instead of the fresh one used everywhere else in the clone.
    // Also enables every FFlag the way luau-lsp's --flag-enabled-by-default does (see
    // Luau::isAnalysisFlagExperimental), since a narrower hand-picked flag set didn't reproduce
    // this; something else enabled by that broader set (not yet identified) was also load-bearing.
    std::vector<ScopedFastFlag> allFlags;
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
    {
        if (strncmp(flag->name, "Luau", 4) == 0 && !Luau::isAnalysisFlagExperimental(flag->name))
            allFlags.emplace_back(*flag, true);
    }
    ScopedFastFlag solverV2{FFlag::LuauSolverV2, true};

    loadDefinition(R"(
        declare list: {
            new: <T>(...T) -> List<T>,
        }

        declare extern type List<T> with
            function get(self, idx: number): T?
        end
    )");

    CheckResult result = check(R"(
        local listy: List<string> = list.new("cats", "dogs")
        local res = listy:get(1)
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("List<string>", toString(requireType("listy")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_method_with_own_generic_does_not_corrupt_constructors")
{
    // Regression test: a method with its own generic (`map<To>(self): Option<To>`) on a generic
    // extern type used to make InfiniteTypeFinder mistake the method's own generic `To` for a
    // recursive/infinite expansion of `Option`, silently binding `Option<T>` to *error-type*
    // with no diagnostics anywhere, which broke every constructor call (`option.some(...)`).
    std::vector<ScopedFastFlag> allFlags;
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
    {
        if (strncmp(flag->name, "Luau", 4) == 0 && !Luau::isAnalysisFlagExperimental(flag->name))
            allFlags.emplace_back(*flag, true);
    }
    ScopedFastFlag solverV2{FFlag::LuauSolverV2, true};

    auto defResult = loadDefinition(R"(
        declare option: {
            some: <T>(T) -> Option<T>,
            none: <T>() -> Option<T>
        }

        declare extern type Option<T> with
            inner: T?
            function map<To>(self): Option<To>
            function unwrap_or(self, default: T): T
        end
    )");

    REQUIRE(defResult.module != nullptr);
    CHECK(defResult.module->errors.empty());

    CheckResult result = check(R"(
        local somee = option.some("hello")
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("Option<string>", toString(requireType("somee")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_method_self_type_is_parameterized")
{
    // A generic extern type's methods implicitly take `self`. `self` needs to be typed as
    // `ListGivesOption<T>` (applied to the type's own generics), not the bare, unparameterized
    // `ListGivesOption` -- otherwise calling a method on a properly-instantiated `ListGivesOption<string>`
    // fails to match against `self`, since the two would be nominally unrelated.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
        {FFlag::LuauHigherOrderGenericInference, true},
    };

    loadDefinition(R"(
        declare extern type Option<T> with
            inner: T?
            function unwrap_or(self, default: T): T
        end

        declare extern type ListGivesOption<T> with
            function get(self, idx: number): Option<T>
        end

        declare list: {
            new_returns_option: <T>(...T) -> ListGivesOption<T>,
        }
    )");

    CheckResult result = check(R"(
        local listyy: ListGivesOption<string> = list.new_returns_option("cats", "dogs")
        local opt = listyy:get(1)
        local val = opt:unwrap_or("default")
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("string", toString(requireType("val")));
}

TEST_CASE_FIXTURE(Fixture, "extern_type_generics_do_not_crash_old_solver")
{
    // The old solver never reads AstStatDeclareExternType::generics/genericPacks, so it should
    // just silently treat a generic extern type as non-generic (and referencing it with type
    // arguments should produce an ordinary user-facing type error, not a crash). This test only
    // asserts we don't crash/assert; it deliberately does not assert on the resulting types,
    // since the old solver isn't expected to actually support this feature. Confirmed by hand
    // that this currently produces ordinary UnknownSymbol/IncorrectGenericParameterCount errors,
    // not a crash.
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, true},
        {FFlag::LuauExternTypeUseDefinitionScope, true},
        {FFlag::LuauGenericNominals, true},
        {FFlag::LuauExternTypeGenericMethods, true},
    };

    unfreeze(getFrontend().globals.globalTypes);
    LoadDefinitionFileResult loadResult = getFrontend().loadDefinitionFile(
        getFrontend().globals, getFrontend().globals.globalScope, R"(
        declare extern type Box<T> with
            function map<U>(self, f: (T) -> U): Box<U>
        end

        declare Box: {
            new: <T>(value: T) -> Box<T>
        }
    )",
        "@test",
        /* captureComments */ false,
        /* typecheckForAutocomplete */ false
    );
    freeze(getFrontend().globals.globalTypes);

    if (!loadResult.success)
        return;

    CheckResult result = check(R"(
        local b = Box.new(5)
        local x = b:map(function(v) return tostring(v) end)
    )");

    (void)result;
}

TEST_CASE_FIXTURE(BuiltinsFixture, "table_intersected_against_extern_type_1")
{
    ScopedFastFlag _{FFlag::LuauAllowIntersectionOfOneTableWithExtern, true};

    loadDefinition(R"(
        declare extern type Frame with
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        type BIG_FRAME = {something: Frame} & Frame
        type context<O> = {_object: O}

        local big_context: context<BIG_FRAME>

        local function fn<O>(p: context<O>)
        end

        fn(big_context)
    )"));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "table_intersected_against_extern_type_2")
{
    ScopedFastFlag _{FFlag::LuauAllowIntersectionOfOneTableWithExtern, true};

    loadDefinition(R"(
        declare extern type Folder with
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        local World : { [number]: { PlayerData: { Settings: { Audio: {} & Folder } } } }

        local function Spread(Id: number)
            local Ownership = World[Id]
            assert(Ownership)
            return Ownership
        end
    )"));

    CHECK_EQ("(number) -> { PlayerData: { Settings: { Audio: Folder & {  } } } }", toString(requireType("Spread")));
}

TEST_SUITE_END();
