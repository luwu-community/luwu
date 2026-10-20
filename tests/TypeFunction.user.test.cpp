// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/Error.h"

#include "ClassFixture.h"
#include "Fixture.h"
#include "Typewriter.h"

#include "doctest.h"

using namespace Luau;

LUAU_FASTFLAG(DebugLuauForceOldSolver)
LUAU_FASTFLAG(LuauTypeFunctionSupportsFrozen)
LUAU_FASTFLAG(LuauTypeFunctionStructuredErrors)
LUAU_FASTFLAG(LuauTypeFunctionSerializeArgNames)
LUAU_FASTFLAG(LuauSubtypingMissingPropertiesAsNil)
LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(LuauUdtfTypeIsSubtypeOf)
LUAU_FASTFLAG(LuauTypeFunctionTableIndexerIsReadOnly)
LUAU_DYNAMIC_FASTINT(LuauTypeFunctionSerdeIterationLimit)
LUAU_FASTFLAG(LuauUdtfCreateSingletonFixErrorMessage)
LUAU_FASTFLAG(LuauUdtfTypeToStringMetamethod)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTINT(LuwuTypeFunctionTimeLimitMs)
LUAU_FASTINT(LuwuTypeFunctionMemoryLimitMb)
LUAU_FASTFLAG(LuwuGenericNominals)
LUAU_FASTFLAG(LuwuTraits)
LUAU_FASTFLAG(LuwuLiteralTypes)
LUAU_FASTFLAG(LuwuClasses)

TEST_SUITE_BEGIN("UserDefinedTypeFunctionTests");

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_nil_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_nil(arg)
            return arg
        end
        type type_being_serialized = nil
        local function ok(idx: serialize_nil<type_being_serialized>): nil return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_nil_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getnil()
            local ty = types.singleton(nil)
            if ty:is("nil") then
                return ty
            end
            -- this should never be returned
            return types.string
        end
        local function ok(idx: getnil<>): nil return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_unknown_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_unknown(arg)
            return arg
        end
        type type_being_serialized = unknown
        local function ok(idx: serialize_unknown<type_being_serialized>): unknown return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_unknown_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getunknown()
            local ty = types.unknown
            if ty:is("unknown") then
                return ty
            end
            -- this should never be returned
            return types.string
        end
        local function ok(idx: getunknown<>): unknown return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_never_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_never(arg)
            return arg
        end
        type type_being_serialized = never
        local function ok(idx: serialize_never<type_being_serialized>): never return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_never_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getnever()
            local ty = types.never
            if ty:is("never") then
                return ty
            end
            -- this should never be returned
            return types.string
        end
        local function ok(idx: getnever<>): never return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_any_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_any(arg)
            return arg
        end
        type type_being_serialized = any
        local function ok(idx: serialize_any<type_being_serialized>): any return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_any_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getany()
            local ty = types.any
            if ty:is("any") then
                return ty
            end
            -- this should never be returned
            return types.string
        end
        local function ok(idx: getany<>): any return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_boolean_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_bool(arg)
            return arg
        end
        type type_being_serialized = boolean
        local function ok(idx: serialize_bool<type_being_serialized>): boolean return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_boolean_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getboolean()
            local ty = types.boolean
            if ty:is("boolean") then
                return ty
            end
            -- this should never be returned
            return types.string
        end
        local function ok(idx: getboolean<>): boolean return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_number_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_num(arg)
            return arg
        end
        type type_being_serialized = number
        local function ok(idx: serialize_num<type_being_serialized>): number return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_number_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getnumber()
            local ty = types.number
            if ty:is("number") then
                return ty
            end
            -- this should never be returned
            return types.string
        end
        local function ok(idx: getnumber<>): number return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "thread_and_buffer_and_none_types")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        type function work_with_thread(x)
            if x:is("thread") then
                return types.thread
            end
            return types.string
        end
        type X = thread
        local function ok(idx: work_with_thread<X>): thread return idx end
    )"));

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        type function work_with_buffer(x)
            if x:is("buffer") then
                return types.buffer
            end
            return types.string
        end
        type X = buffer
        local function ok(idx: work_with_buffer<X>): buffer return idx end
    )"));

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        type function work_with_none(x)
            if x:is("none") then
                return types.none
            end
            return types.string
        end
        type X = none
        local function ok(idx: work_with_none<X>): none return idx end
    )"));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_string_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_str(arg)
            return arg
        end
        type type_being_serialized = string
        local function ok(idx: serialize_str<type_being_serialized>): string return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_string_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getstring()
            local ty = types.string
            if ty:is("string") then
                return ty
            end
            -- this should never be returned
            return types.boolean
        end
        local function ok(idx: getstring<>): string return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_boolsingleton_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_boolsingleton(arg)
            return arg
        end
        type type_being_serialized = true
        local function ok(idx: serialize_boolsingleton<type_being_serialized>): true return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_boolsingleton_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getboolsingleton()
            local ty = types.singleton(true)
            if ty:is("singleton") and ty:value() then
                return ty
            end
            -- this should never be returned
            return types.string
        end
        local function ok(idx: getboolsingleton<>): true return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_strsingleton_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_strsingleton(arg)
            return arg
        end
        type type_being_serialized = "popcorn and movies!"
        local function ok(idx: serialize_strsingleton<type_being_serialized>): "popcorn and movies!" return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_strsingleton_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getstrsingleton()
            local ty = types.singleton("hungry hippo")
            if ty:is("singleton") and ty:value() == "hungry hippo" then
                return ty
            end
            -- this should never be returned
            return types.number
        end
        local function ok(idx: getstrsingleton<>): "hungry hippo" return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_union_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_union(arg)
            return arg
        end
        type type_being_serialized = number | string | boolean
        -- forcing an error here to check the exact type of the union
        local function ok(idx: serialize_union<type_being_serialized>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "boolean | number | string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_optional_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function numberhuh()
            return types.optional(types.number)
        end
        -- forcing an error here to check the exact type of the union
        local function ok(idx: numberhuh<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "number?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_optional_works_on_unions")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foobar()
            local ty = types.unionof(types.string, types.number, types.boolean)
            return types.optional(ty)
        end
        -- forcing an error here to check the exact type of the union
        local function ok(idx: foobar<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "(boolean | number | string)?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_union_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getunion()
            local ty = types.unionof(types.string, types.number, types.boolean)
            if ty:is("union") then
                -- creating a copy of `ty`
                -- Annotated: inside a type function, `table.unpack` of an unannotated array filled
                -- in a loop infers as `...unknown`, which isn't a `...type`.
                local arr: { type } = {}
                for _, value in ty:components() do
                    table.insert(arr, value)
                end
                return types.unionof(table.unpack(arr))
            end
            -- this should never be returned
            return types.number
        end
        -- forcing an error here to check the exact type of the union
        local function ok(idx: getunion<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "boolean | number | string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_flatten_on_unionof")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foobar()
            local tys = { types.string, types.number, types.never, types.boolean, types.singleton(nil) }
            local result = types.never
            for _, ty in tys do
                result = types.unionof(result, ty)
            end
            return result
        end
        -- forcing an error here to check the exact type of the union
        local function ok(idx: foobar<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "(boolean | number | string)?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_flatten_on_unionof_empty")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foobar()
            return types.unionof()
        end

        local f: foobar<>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK(toString(requireType("f")) == "never");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_flatten_on_unionof_two_things")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foobar()
            return types.unionof(types.string, types.never)
        end
        -- forcing an error here to check the exact type of the union
        local function ok(idx: foobar<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_flatten_on_intersectionof")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foobar()
            local tys = { types.string, types.number, types.unknown, types.boolean }
            local result = types.unknown
            for _, ty in tys do
                result = types.intersectionof(result, ty)
            end
            return result
        end

        local f: foobar<>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK(toString(requireType("f")) == "boolean & number & string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_flatten_on_intersectionof_empty")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foobar()
            return types.intersectionof()
        end

        local f: foobar<>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK(toString(requireType("f")) == "unknown");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_flatten_on_intersectionof_two_things")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foobar()
            return types.intersectionof(types.unknown, types.string)
        end
        -- forcing an error here to check the exact type of the union
        local function ok(idx: foobar<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_intersection_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_intersection(arg)
            return arg
        end
        type type_being_serialized = { boolean: boolean, number: number } & { boolean: boolean, string: string }
        -- forcing an error here to check the exact type of the intersection
        local function ok(idx: serialize_intersection<type_being_serialized>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ boolean: boolean, number: number } & { boolean: boolean, string: string }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_intersection_methods_work")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function getintersection()
            local tbl1 = types.newtable(nil, nil, nil)
            tbl1:setproperty(types.singleton("boolean"), types.boolean) -- {boolean: boolean}
            tbl1:setproperty(types.singleton("number"), types.number) -- {boolean: boolean, number: number}
            local tbl2 = types.newtable(nil, nil, nil)
            tbl2:setproperty(types.singleton("boolean"), types.boolean) -- {boolean: boolean}
            tbl2:setproperty(types.singleton("string"), types.string) -- {boolean: boolean, string: string}
            local ty = types.intersectionof(tbl1, tbl2)
            if ty:is("intersection") then
                -- creating a copy of `ty`
                -- Annotated: inside a type function, `table.unpack` of an unannotated array filled
                -- in a loop infers as `...unknown`, which isn't a `...type`.
                local arr: { type } = {}
                for index, value in ty:components() do
                    table.insert(arr, value)
                end
                return types.intersectionof(table.unpack(arr))
            end
            -- this should never be returned
            return types.string
        end
        -- forcing an error here to check the exact type of the intersection
        local function ok(idx: getintersection<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ boolean: boolean, number: number } & { boolean: boolean, string: string }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_negation_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getnegation()
            local ty = types.negationof(types.string)
            if ty:is("negation") then
                return ty
            end
            -- this should never be returned
            return types.number
        end

        -- forcing an error here to check the exact type of the negation
        local function ok(idx: getnegation<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "~string");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_negation_inner")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(t)
    return types.negationof(t):inner()
end

type function fail(t)
    return t:inner()
end

local function ok(idx: pass<number>): number return idx end
local function notok(idx: fail<number>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(
        toString(result.errors[0]) ==
        R"('fail' type function errored at runtime: [string "fail"]:7: type.inner: cannot call inner method on non-negation type: `number` type)"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_table_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_table(arg)
            return arg
        end
        type type_being_serialized = { boolean: boolean, number: number, [string]: number }
        -- forcing an error here to check the exact type of the table
        local function ok(idx: serialize_table<type_being_serialized>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    // Luwu: a type the type function passed through unchanged is still the alias it was
    CHECK(toString(tm->givenType) == "type_being_serialized");
    ToStringOptions expanded;
    expanded.alwaysExpandRootAlias = true;
    CHECK(toString(tm->givenType, expanded) == "{ [string]: number, boolean: boolean, number: number }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_newtable_can_do_readonly_or_writeonly_types")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function gettable()
            return types.newtable{[types.singleton("foo")] = { read = types.number }, [types.singleton("bar")] = { write = types.string }}
        end

        -- forcing an error here to check the exact type of the table
        local function ok(idx: gettable<>): never return idx end
    )");

    // Luwu: upstream's FIXME(CLI-178738) said a first error shouldn't exist; `newtable` takes read-only tables, so it doesn't
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ write bar: string, read foo: number }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_table_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function gettable()
            local indexer = {
                index = types.number,
                readresult = types.boolean,
                writeresult = types.boolean,
            }
            local ty = types.newtable(nil, indexer, nil) -- {[number]: boolean}
            ty:setproperty(types.singleton("string"), types.number) -- {string: number, [number] = boolean}
            ty:setproperty(types.singleton("number"), types.string) -- {string: number, number: string, [number] = boolean}
            ty:setproperty(types.singleton("string"), nil) -- {number: string, [number] = boolean}
            local ret = types.newtable(nil, nil, nil) -- {}
            -- creating a copy of `ty`
            for k, v in ty:properties() do
                ret:setreadproperty(k, v.read)
                ret:setwriteproperty(k, v.write)
            end
            if ret:is("table") then
                ret:setindexer(types.boolean, types.string) -- {number: string, [boolean] = string}
                return ret -- {number: string, [boolean] = string}
            end
            -- this should never be returned
            return types.number
        end
        -- forcing an error here to check the exact type of the table
        local function ok(idx: gettable<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ [boolean]: string, number: string }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_metatable_methods_work")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getmetatable()
            local indexer = {
                index = types.number,
                readresult = types.boolean,
                writeresult = types.boolean,
            }
            local ty = types.newtable(nil, indexer, nil) -- {[number]: boolean}
            ty:setproperty(types.singleton("string"), types.number) -- {string: number, [number]: boolean}
            local metatbl = types.newtable(nil, nil, ty) -- { {  }, @metatable { [number]: boolean, string: number } }
            metatbl:setmetatable(types.newtable(nil, indexer, nil)) -- { {  }, @metatable { [number]: boolean } }
            local ret = metatbl:metatable()
            if metatbl:is("table") and metatbl:metatable() then
                return ret -- { @metatable { [number]: boolean } }
            end
            -- this should never be returned
            return types.number
        end
        -- forcing an error here to check the exact type of the metatable
        local function ok(idx: getmetatable<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{boolean}");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_function_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_func(arg)
            return arg
        end
        type type_being_serialized = (boolean, number, nil) -> (...string)
        local function ok(idx: serialize_func<type_being_serialized>): (boolean, number, nil) -> (...string) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_function_methods_work")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function getfunction()
            local ty = types.newfunction(nil, nil) -- () -> ()
            ty:setparameters({types.string, types.number}, nil) -- (string, number) -> ()
            ty:setreturns(nil, types.boolean) -- (string, number) -> (...boolean)
            if ty:is("function") then
                -- creating a copy of `ty` parameters
                local arr: {type} = {}
                local args = ty:parameters().head
                if args then
                    for index, val in args do
                        table.insert(arr, val)
                    end
                end
                return types.newfunction({head = arr}, ty:returns()) -- (string, number) -> (...boolean)
            end
            -- this should never be returned
            return types.number
        end
        local function ok(idx: getfunction<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "(string, number) -> (...boolean)");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_class_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_class(arg)
            return arg
        end
        local function ok(idx: serialize_class<BaseClass>): BaseClass return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_class_serialization_works2")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_class(arg)
            return arg
        end
        local function ok(idx: serialize_class<typeof(confusingBaseClassInstance)>): typeof(confusingBaseClassInstance) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_class_methods_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getclass(arg)
            local props = arg:properties()
            local indexer = arg:indexer()
            local metatable = arg:metatable()
            return types.newtable(props, indexer, metatable)
        end
        -- forcing an error here to check the exact type of the metatable
        local function ok(idx: getclass<BaseClass>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ BaseField: number, read BaseMethod: (BaseClass, number) -> (), read Touched: Connection }");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "write_of_readonly_is_nil")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getclass(arg)
            local props = arg:properties()
            local table = types.newtable(props)
            local singleton = types.singleton("BaseMethod")

            if table:writeproperty(singleton) then
                return types.singleton(true)
            else
                return types.singleton(false)
            end
        end
        -- forcing an error here to check the exact type of the metatable
        local function ok(idx: getclass<BaseClass>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "false");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_check_mutability")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function checkmut()
            local indexer = {
                index = types.number,
                readresult = types.boolean,
                writeresult = types.boolean,
            }
            local ty = types.newtable(nil, indexer, nil) -- {[number]: boolean}
            ty:setproperty(types.singleton("string"), types.number) -- {string: number, [number]: boolean}
            local metatbl = types.newtable(nil, nil, ty) -- { {  }, @metatable { [number]: boolean, string: number } }
            -- mutate the table
            ty:setproperty(types.singleton("string"), nil) -- {[number]: boolean}
            if metatbl:is("table") and metatbl:metatable() then
                return metatbl -- { @metatable { [number]: boolean }, { } }
            end
            -- this should never be returned
            return types.number
        end
        local function ok(idx: checkmut<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ @metatable {boolean}, {  } }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_copy_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function getcopy()
            local indexer = {
                index = types.number,
                readresult = types.boolean,
                writeresult = types.boolean,
            }
            local ty = types.newtable(nil, indexer, nil) -- {[number]: boolean}
            ty:setproperty(types.singleton("string"), types.number) -- {string: number, [number]: boolean}
            local metaty = types.newtable(nil, nil, ty) -- { {  }, @metatable { [number]: boolean, string: number } }
            local copy = types.copy(metaty)
            -- mutate the table
            ty:setproperty(types.singleton("string"), nil) -- {[number]: boolean}
            if copy:is("table") and copy:metatable() then
                return copy -- { {  }, @metatable { [number]: boolean, string: number } }
            end
            -- this should never be returned
            return types.number
        end
        local function ok(idx: getcopy<>): never return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ @metatable { [number]: boolean, string: number }, {  } }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_simple_cyclic_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_cycle(arg)
            return arg
        end
        type basety = {
            first: basety2
        }
        type basety2 = {
            second: basety
        }
        local function ok(idx: serialize_cycle<basety>): basety return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_createtable_bad_metatable")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function badmetatable()
            return types.newtable(nil, nil, types.number)
        end
        local function bad(arg: badmetatable<>) end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    UserDefinedTypeFunctionError* e = get<UserDefinedTypeFunctionError>(result.errors[0]);
    REQUIRE(e);
    CHECK(
        e->message == "'badmetatable' type function errored at runtime: [string \"badmetatable\"]:3: types.newtable: expected to be given a table "
                      "type as a metatable, but got number instead"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_complex_cyclic_serialization_works")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function serialize_cycle2(arg)
            return arg
        end
        type Employee = {
            name: string,
            department: Department?
        }
        type Department = {
            name: string,
            manager: Employee?,
            employees: { Employee },
            company: Company?
        }
        type Company = {
            name: string,
            departments: { Department }
        }
        local function ok(idx: serialize_cycle2<Company>): Company return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_user_error_is_reported")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function errors_if_string(arg)
            if arg:is("string") then
                local a = 1
                error("We are in a math class! not english")
            end
            return arg
        end
        local function ok(idx: errors_if_string<string>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    UserDefinedTypeFunctionError* e = get<UserDefinedTypeFunctionError>(result.errors[0]);
    REQUIRE(e);
    CHECK(e->message == "'errors_if_string' type function errored at runtime: [string \"errors_if_string\"]:5: We are in a math class! not english");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_type_overrides_call_metamethod")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function hello(arg)
            error(type(arg))
        end
        local function ok(idx: hello<string>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    UserDefinedTypeFunctionError* e = get<UserDefinedTypeFunctionError>(result.errors[0]);
    REQUIRE(e);
    CHECK(e->message == "'hello' type function errored at runtime: [string \"hello\"]:3: userdata");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_type_overrides_eq_metamethod")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function hello()
            local p1 = types.string
            local p2 = types.string
            local t1 = types.newtable(nil, nil, nil)
            t1:setproperty(types.singleton("string"), types.boolean)
            t1:setmetatable(t1)
            local t2 = types.newtable(nil, nil, nil)
            t2:setproperty(types.singleton("string"), types.boolean)
            t1:setmetatable(t1)
            if p1 == p2 and t1 == t2 then
                return types.number
            end
            return types.unknown
        end
        local function ok(idx: hello<>): number return idx end
    )");

    LUAU_CHECK_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_function_type_cant_call_get_props")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function hello(arg)
            local arr = arg:properties()
        end
        local function ok(idx: hello<() -> ()>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    UserDefinedTypeFunctionError* e = get<UserDefinedTypeFunctionError>(result.errors[0]);
    REQUIRE(e);
    CHECK(
        e->message == "'hello' type function errored at runtime: [string \"hello\"]:3: type.properties: expected self to be either a table or class, "
                      "but got function instead"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_calling_each_other")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foo()
            return "hi"
        end
        type function bar()
            return types.singleton(foo())
        end
        local function ok(idx: bar<>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "\"hi\"");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_calling_each_other_2")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function first(arg: string)
            return arg
        end
        type function second(arg)
            return types.singleton(first(arg))
        end
        type function third()
            return second("hi")
        end
        local function ok(idx: third<>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "\"hi\"");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_calling_each_other_3")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        -- this function should not see 'fourth' function when invoked from 'third' that sees it
        type function first(arg)
            return fourth(arg)
        end
        type function second(arg)
            return types.singleton(first(arg))
        end

        do
            type function fourth(arg)
                return arg
            end
            type function third()
                return second("hi")
            end
            local function ok(idx: third<>): nil return idx end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK(toString(result.errors[0]) == R"(Unknown global 'fourth'; consider assigning to it first)");
    CHECK(toString(result.errors[1]) == R"('third' type function errored at runtime: [string "first"]:4: attempt to call a nil value)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_calling_each_other_unordered")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function bar()
            return types.singleton(foo())
        end
        type function foo()
            return "hi"
        end
        local function ok(idx: bar<>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "\"hi\"");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_no_shared_state")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foo()
            if not glob then
                glob = 'a'
            else
                glob ..= 'b'
            end

            return glob
        end
        type function bar(prefix)
            return types.singleton(tostring(prefix:value()) .. foo())
        end
        local function ok1(idx: bar<'x'>): nil return idx end
        local function ok2(idx: bar<'y'>): nil return idx end
    )");

    // We are only checking first errors, others are mostly duplicates
    LUAU_REQUIRE_ERROR_COUNT(5, result);
    CHECK(toString(result.errors[0]) == R"(Unknown global 'glob'; consider assigning to it first)");
    CHECK(toString(result.errors[1]) == R"('bar' type function errored at runtime: [string "foo"]:4: attempt to modify a readonly table)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_math_reset")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function foo(x)
            return types.singleton(tostring(math.random(1, 100)))
        end
        local x: foo<'a'> = ('' :: any) :: foo<'b'>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_optionify")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function optionify(tbl)
            if not tbl:is("table") then
                error("Argument is not a table")
            end
            for k, v in tbl:properties() do
                tbl:setproperty(k, types.unionof(v.read, types.singleton(nil)))
            end
            return tbl
        end
        type Person = {
            name: string,
            age: number,
            alive: boolean
        }
        local function ok(idx: optionify<Person>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ age: number?, alive: boolean?, name: string? }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_calling_illegal_global")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function illegal(arg)
            gcinfo() -- this should error

            return arg -- this should not be reached
        end

        local function ok(idx: illegal<number>): nil return idx end
    )");

    // We are only checking first errors, others are mostly duplicates
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK(toString(result.errors[0]) == R"(Unknown global 'gcinfo'; consider assigning to it first)");
    CHECK(
        toString(result.errors[1]) ==
        R"('illegal' type function errored at runtime: [string "illegal"]:3: this function is not supported in type functions)"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_recursion_and_gc")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function foo(tbl)
            local count = 0
            for k,v in tbl:properties() do count += 1 end
            if count < 100 then
                tbl:setproperty(types.singleton(`m{count}`), types.string)
                foo(tbl)
            end
            for i = 1,100 do table.create(10000) end
            return tbl
        end
        type Test = {}
        local function ok(idx: foo<Test>): nil return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_recovery_no_upvalues")
{
    ScopedFastFlag solverV2{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        local var

        type function save_upvalue(arg)
            var = 1
            return arg
        end

        type test = "test"
        local function ok(idx: save_upvalue<test>): "test"
            return idx
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]) == R"(Type function cannot reference outer local 'var')");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_follow")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type t0 = any
        type function t0()
            return types.any
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]) == R"(Redefinition of type 't0', previously defined at line 2)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_strip_indexer")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function stripindexer(tbl)
            if not tbl:is("table") then
                error("can only strip the indexer on a table!")
            end
            tbl:setindexer(types.never, types.never)
            return tbl
        end

        type map = { [number]: string, foo: string }
        -- forcing an error here to check the exact type
        local function ok(tbl: stripindexer<map>): never return tbl end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK(toString(tm->givenType) == "{ foo: string }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "no_type_methods_on_types")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function test(x)
            return if (types :: any).is(x, "number") then types.string else types.boolean
        end
        local function ok(tbl: test<number>): never return tbl end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"('test' type function errored at runtime: [string "test"]:3: attempt to call a nil value)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "no_types_functions_on_type")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function test(x)
            return x.singleton("a")
        end
        local function ok(tbl: test<number>): never return tbl end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"('test' type function errored at runtime: [string "test"]:3: attempt to call a nil value)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "no_metatable_writes")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function test(x)
            local a = x.__index
            a.is = function() return false end
            return types.singleton(x.is("number"))
        end
        local function ok(tbl: test<number>): never return tbl end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"('test' type function errored at runtime: [string "test"]:4: attempt to index nil with 'is')");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "no_eq_field")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function test(x)
            return types.singleton(x.__eq(x, types.number))
        end
        local function ok(tbl: test<number>): never return tbl end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"('test' type function errored at runtime: [string "test"]:3: attempt to call a nil value)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "tag_field")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS();
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function test(x)
            return types.singleton(x.tag)
        end

        local function ok1(tbl: test<number>): never return tbl end
        local function ok2(tbl: test<string>): never return tbl end
        local function ok3(tbl: test<{}>): never return tbl end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);

    CHECK("Expected this to be unreachable, but got '\"number\"'" == toString(result.errors[0]));
    CHECK("Expected this to be unreachable, but got '\"string\"'" == toString(result.errors[1]));
    CHECK("Expected this to be unreachable, but got '\"table\"'" == toString(result.errors[2]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "metatable_serialization")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS()
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function makemttbl()
            local metaprops = {
                [types.singleton("ma")] = types.boolean
            }
            local mt = types.newtable(metaprops)

            local props = {
                [types.singleton("a")] = types.number
            }
            return types.newtable(props, nil, mt)
        end

        type function id(x)
            return x
        end

        local a: number = {} :: id<makemttbl<>>
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]) == R"(Expected this to be 'number', but got '{ @metatable { ma: boolean }, { a: number } }')");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "nonstrict_mode")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
--!nonstrict
type function foo() return types.string end
local a: foo<> = "a"
    )");
    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "implicit_export")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    fileResolver.source["game/A"] = R"(
type function concat(a: type, b: type)
    assert(a:is("singleton") and b:is("singleton"))
    local as = a:value()
    local bs = b:value()
    assert(typeof(as) == "string")
    assert(typeof(bs) == "string")
    return types.singleton(as .. bs)
end
export type Concat<T, U> = concat<T, U>
local a: concat<'first', 'second'>
return {}
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CHECK(toString(requireType("game/A", "a")) == R"("firstsecond")");

    CheckResult bResult = check(R"(
local Test = require(game.A);
local b: Test.Concat<'third', 'fourth'>
    )");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    CHECK(toString(requireType("b")) == R"("thirdfourth")");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "local_scope")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function foo()
    return "hi"
end
local function test()
    type function bar()
        return types.singleton(foo())
    end

    return ("" :: any) :: bar<>
end
local a = test()
    )");
    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("a")) == R"("hi")");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "explicit_export")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    fileResolver.source["game/A"] = R"(
export type function concat(a: type, b: type)
    assert(a:is("singleton") and b:is("singleton"))
    local as = a:value()
    local bs = b:value()
    assert(typeof(as) == "string")
    assert(typeof(bs) == "string")
    return types.singleton(as .. bs)
end
local a: concat<'first', 'second'>
return {}
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CHECK(toString(requireType("game/A", "a")) == R"("firstsecond")");

    CheckResult bResult = check(R"(
local Test = require(game.A);
local b: Test.concat<'third', 'fourth'>
    )");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    CHECK(toString(requireType("b")) == R"("thirdfourth")");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "print_to_error")
{
    ScopedFastFlag solverV2{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function t0(a)
            print("Where does this go")
            print(a.tag)
            return types.any
        end
        local a: t0<string>
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"(Where does this go)");
    CHECK(toString(result.errors[1]) == R"(string)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "print_to_error_plus_error")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        type function t0(a)
            print("Where does this go")
            print(a.tag)
            error("test")
        end
        local a: t0<string>
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK(toString(result.errors[0]) == R"(Where does this go)");
    CHECK(toString(result.errors[1]) == R"(string)");
    CHECK(toString(result.errors[2]) == R"('t0' type function errored at runtime: [string "t0"]:5: test)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "print_to_error_plus_no_result")
{
    ScopedFastFlag solverV2{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function t0(a)
            print("Where does this go")
            print(a.tag)
        end
        local a: t0<string>
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK(toString(result.errors[0]) == R"(Where does this go)");
    CHECK(toString(result.errors[1]) == R"(string)");
    CHECK(toString(result.errors[2]) == R"('t0' type function: returned a non-type value)");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_serialization_1")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    return arg
end

type test = <T, U>(T, { x: <T>(y: T) -> (), y: U }, U) -> ()

local function ok(idx: pass<test>): test return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_serialization_2")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    return arg
end

type test = <T, U...>(T) -> (T, U...)

local function ok(idx: pass<test>): test return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_serialization_3")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    return arg
end

local function m(a, b)
    return {x = a, y = b}
end

type test = typeof(m)

local function ok(idx: pass<test>): test return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_cloning_1")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    return types.copy(arg)
end

type test = <T, U>(T, { x: <T>(y: T) -> (), y: U }, U) -> ()

local function ok(idx: pass<test>): test return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_cloning_2")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    return types.copy(arg)
end

type test = <T, U...>(T) -> (T, U...)

local function ok(idx: pass<test>): test return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_equality")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    return types.singleton(types.copy(arg) == arg)
end

type test = <T, U...>(T) -> (T, U...)

local function ok(idx: pass<test>): true return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_1")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    local generics = arg:generics()
    local T = generics[1]
    return types.newfunction({ head = {T} }, { head = {T} }, {T})
end

type test = <T, U>(T, { x: <T>(y: T) -> (), y: U }, U) -> ()

local function ok(idx: pass<test>): <T>(T) -> (T) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_2")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    local generics = arg:generics()
    local T = generics[1]
    local f = types.newfunction()
    f:setparameters({T, T});
    f:setreturns({T});
    f:setgenerics({T});
    return f
end

type test = <T, U>(T, { x: <T>(y: T) -> (), y: U }, U) -> ()

local function ok(idx: pass<test>): <T>(T, T) -> (T) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_3")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
type function pass()
    local T = types.generic("T")
    assert(T.tag == "generic")
    assert(T:name() == "T")
    assert(T:ispack() == false)

    local Us, Vs = types.generic("U", true), types.generic("V", true)
    assert(Us.tag == "generic")
    assert(Us:name() == "U")
    assert(Us:ispack() == true)

    local f = types.newfunction()
    f:setparameters({T}, Us);
    f:setreturns({T}, Vs);
    f:setgenerics({T, Us, Vs});
    return f
end

local function ok(idx: pass<>): <T, U..., V...>(T, U...) -> (T, V...) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_4")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass()
    local T, U = types.generic("T"), types.generic("U")

    -- <T>(T) -> ()
    local func = types.newfunction({ head = {T} }, {}, {T});

    -- { x: <T>(T) -> (), y: U }
    local tbl = types.newtable({ [types.singleton("x")] = func, [types.singleton("y")] = U })

    -- <T, U>(T, { x: <T>(T) -> (), y: U }, U) -> ()
    return types.newfunction({ head = {T, tbl, U } }, {}, {T, U})
end

type test = <T, U>(T, { x: <T>(y: T) -> (), y: U }, U) -> ()

local function ok(idx: pass<>): test return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_5")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass()
    local T = types.generic("T")
    return types.newfunction({ head = {T} }, {}, {types.copy(T)})
end

local function ok(idx: pass<>): <T>(T) -> () return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_6")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    local generics = arg:generics()
    local T, U = generics[1], generics[2]
    local f = types.newfunction()
    f:setparameters({T});
    f:setreturns({U});
    f:setgenerics({T, U});
    return f
end

local function m(a, b)
    return {x = a, y = b}
end

type test = typeof(m)

local function ok(idx: pass<test>): <T, U>(T) -> (U) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_7")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    local p, r = arg:parameters(), arg:returns()
    local f = types.newfunction()
    f:setparameters(p.head, p.tail);
    f:setreturns(r.head, r.tail);
    f:setgenerics(arg:generics());
    return f
end

type test = <T, U...>(T, U...) -> (T, U...)

local function ok(idx: pass<test>): <T, U...>(T, U...) -> (T, U...) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_8")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    local p, r = arg:parameters(), arg:returns()
    local f = types.newfunction()
    f:setparameters(p.head, p.tail);
    f:setreturns(r.head, r.tail);
    f:setgenerics(arg:generics());
    return f
end

type test = <U...>(U...) -> (U...)

local function ok(idx: pass<test>): <T>(T, T) -> (T, T) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_equality_2")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    local T, Us = types.generic("T"), types.generic("U", true)

    local tbl1 = types.newtable({ [types.singleton("x")] = T })
    local tbl2 = types.newtable({ [types.singleton("x")] = Us }) -- it is possible to have invalid types in-flight

    return types.singleton(tbl1 == tbl2)
end

local function ok(idx: get<>): false return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_error_1")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    local T, Us = types.generic("T"), types.generic("U", true)
    return types.newfunction({}, {}, {Us, T})
end
local function ok(idx: get<>): false return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(
        toString(result.errors[0]) ==
        R"('get' type function errored at runtime: [string "get"]:4: types.newfunction: generic type cannot follow a generic pack)"
    );
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_error_2")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    local T, Us = types.generic("T"), types.generic("U", true)
    return types.newfunction({ head = {T} }, {}, {})
end
local function ok(idx: get<>): false return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"(Generic type 'T' is not in a scope of the active generic function)");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_error_3")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    local T, U = types.generic("T"), types.generic("U")

    -- <U>(U) -> ()
    local func = types.newfunction({ head = {U} }, {}, {U});

    -- broken: <T>(T, <U>(U) -> (), U) -> ()
    return types.newfunction({ head = {T, func, U } }, {}, {T})
end
local function ok(idx: get<>): false return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"(Generic type 'U' is not in a scope of the active generic function)");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_error_4")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    local T, Us = types.generic("T"), types.generic("U", true)
    return types.newfunction({ head = {T} }, { tail = Us }, {T, T})
end
local function ok(idx: get<>): false return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"(Duplicate type parameter 'T')");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_error_5")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    local T, Ts = types.generic("T"), types.generic("T", true)
    return types.newfunction({ head = {T} }, { tail = Ts }, {T, Ts})
end
local function ok(idx: get<>): false return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"(Duplicate type parameter 'T')");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_error_6")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    local T, Us = types.generic("T"), types.generic("U", true)
    return types.newfunction({ head = {Us} }, {}, {T, Us})
end
local function ok(idx: get<>): false return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"(Generic type pack 'U...' cannot be placed in a type position)");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_generic_api_error_7")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    local T, Us = types.generic("T"), types.generic("U", true)
    return types.newfunction({ tail = Us }, {}, {T})
end
local function ok(idx: get<>): false return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"(Generic type pack 'U...' is not in a scope of the active generic function)");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_variadic_api")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function pass(arg)
    local p, r = arg:parameters(), arg:returns()
    local f = types.newfunction()
    f:setparameters({p.tail}, p.head[1]);
    f:setreturns({r.tail}, r.head[1]);
    return f
end

type test = (string, ...number) -> (number, ...string)

local function ok(idx: pass<test>): (number, ...string) -> (string, ...number) return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_singleton_equality_bool")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    if (false) // FFlag::LuauEagerGeneralization4)
    {
        // FIXME: CLI-151985
        // This test breaks because we can't see that eq<type?, b> is already fully reduced.
        return;
    }

    CheckResult result = check(R"(
type function compare(arg)
    return types.singleton(types.singleton(false) == arg)
end

local function ok1(idx: compare<false>): true return idx end
local function ok2(idx: compare<true>): false return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_singleton_equality_string")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    if (false) // FFlag::LuauEagerGeneralization4)
    {
        // FIXME: CLI-151985
        // This test breaks because we can't see that eq<type?, b> is already fully reduced.
        return;
    }

    CheckResult result = check(R"(
type function compare(arg)
    return types.singleton(types.singleton("") == arg)
end

local function ok(idx: compare<"">): true return idx end
local function ok(idx: compare<"a">): false return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typeof_type_userdata_returns_type")
{
    ScopedFastFlag solverV2{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function test(t)
    print(typeof(t))
    return t
end

local _:test<number>
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]) == R"(type)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_print_tab_char_fix")
{
    ScopedFastFlag solverV2{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function test(t)
            print(1,2)

            return t
        end

        local _:test<number>
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    // It should be \t and not \x1
    CHECK_EQ("1\t2", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(ExternTypeFixture, "udtf_class_parent_ops")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function readparentof(arg)
            return arg:readparent()
        end

        type function writeparentof(arg)
            return arg:writeparent()
        end

        local function ok1(idx: readparentof<ChildClass>): BaseClass return idx end
        local function ok2(idx: writeparentof<ChildClass>): BaseClass return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typecheck_success")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function foo(x: type)
    return types.singleton(x.tag)
end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typecheck_failure")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function foo()
    return types.singleton({1})
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "outer_generics_irreducible")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function func(t)
    return t
end

type wrap<T> = { a: func<T?> }

local x: wrap<string> = nil :: any
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "{ a: string? }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "inner_generics_reducible")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function func(t)
    return t
end

type wrap<T> = { a: func<<T>(T) -> number>, b: T }

local x: wrap<string> = nil :: any
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "{ a: <T>(T) -> number, b: string }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "blocking_nested_pending_expansions")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
type function func(t)
    return t
end

type test<T> = { x: T, y: T? }
type wrap<T> = { a: func<(string, keyof<test<T>>) -> number>, b: T }
local x: wrap<string>
local y: keyof<typeof(x)>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == R"({ a: (string, "x" | "y") -> number, b: string })");
    CHECK(toString(requireType("y"), ToStringOptions{true}) == R"("a" | "b")");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "blocking_nested_pending_expansions_2")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function foo(t)
    return types.unionof(t, types.singleton(nil))
end

local x: foo<{a: foo<string>, b: foo<number>}> = nil
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "{ a: string?, b: number? }?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "irreducible_pending_expansions")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
type function foo(t)
    return types.unionof(t, types.singleton(nil))
end

type table<T> = { a: index<T, "a"> }
type wrap<T> = foo<table<T>>

local x: wrap<{a: number}> = { a = 2 }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "{ a: number }?");
}

TEST_CASE_FIXTURE(Fixture, "typeof_is_not_a_valid_type_function_name")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function typeof(t)
	        return t
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    CHECK("typeof cannot be used as an identifier for a type function or alias" == toString(result.errors[0]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_call")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type Test<T> = T?

type function foo(t)
    return Test(t)
end

local x: foo<{a: number}> = { a = 2 }
local y: foo<{b: number}> = { b = 2 }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "{ a: number }?");
    CHECK(toString(requireType("y"), ToStringOptions{true}) == "{ b: number }?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_call_indirect")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type Test<T> = T?

type function foo(t)
    return Test(t)
end

type function bar(t)
    return foo(t)
end

local x: bar<{a: number}> = { a = 2 }
local y: bar<{b: number}> = { b = 2 }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "{ a: number }?");
    CHECK(toString(requireType("y"), ToStringOptions{true}) == "{ b: number }?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_call_indirect_levels")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type Test<T> = T?

type function foo(t)
    return Test(t)
end

do
    type function bar(t)
        return foo(t)
    end

    local x: bar<{a: number}> = { a = 2 }
    local y: bar<{b: number}> = { b = 2 }

    print(x, y)
end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireTypeAtPosition(Position{15, 10}), ToStringOptions{true}) == "{ a: number }?");
    CHECK(toString(requireTypeAtPosition(Position{15, 13}), ToStringOptions{true}) == "{ b: number }?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_values")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type Test = { a: number }

type function foo(t)
    return types.unionof(Test, t)
end

local x: foo<nil> = { a = 2 }
local y: foo<string> = "a"
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "{ a: number }?");
    CHECK(toString(requireType("y"), ToStringOptions{true}) == "string | { a: number }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_unordered")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function foobar(ty)
    if ty:is("number") then
        return ty
    end
    return TableOf(ty)
end

type TableOf<T> = { prop: T }

type ShouldBeNumber = foobar<number>
type ShouldBeTableOfString = foobar<string>

local x: ShouldBeNumber = 2
local y: ShouldBeTableOfString = { prop = "a" }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "number");
    CHECK(toString(requireType("y"), ToStringOptions{true}) == "{ prop: string }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_call_with_reduction")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
type Test<T> = rawget<T, "a">

type function foo(t)
    return Test(t)
end

local x: foo<{ a: number }> = 2
local y: foo<{ a: string }> = "x"
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "number");
    CHECK(toString(requireType("y"), ToStringOptions{true}) == "string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_implicit_export")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    fileResolver.source["game/A"] = R"(
type Test<T> = rawget<T, "a">

export type function foo(t)
    return Test(t)
end
local x: foo<{ a: number }> = 2
return {}
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CHECK(toString(requireType("game/A", "x")) == R"(number)");

    CheckResult bResult = check(R"(
local Test = require(game.A);
local y: Test.foo<{ a: string }> = "x"
    )");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    CHECK(toString(requireType("y")) == R"(string)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_implicit_export_indirect")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    fileResolver.source["game/A"] = R"(
type Test<T> = rawget<T, "a">

type function foo(t)
    return Test(t)
end

export type function bar(t)
    return foo(t)
end

local x: bar<{ a: number }> = 2
return {}
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CHECK(toString(requireType("game/A", "x")) == R"(number)");

    CheckResult bResult = check(R"(
local Test = require(game.A);
local y: Test.bar<{ a: string }> = "x"
    )");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    CHECK(toString(requireType("y")) == R"(string)");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "type_alias_not_too_many_globals")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function get()
    return number
end
local function ok(idx: get<>): number return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK(toString(result.errors[0]) == R"(Unknown global 'number'; consider assigning to it first)");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "type_alias_not_enough_arguments")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type Test<A, B> = (a: A, b: B) -> A

type function get()
    return Test(types.number)
end

local function ok(idx: get<>): number return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]) == R"('get' type function errored at runtime: [string "get"]:5: not enough arguments to call)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_can_call_packs")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type Test<T, U...> = (U...) -> T

type function foo(t)
    return Test(types.number, types.string, t)
end

local x: foo<boolean>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "(string, boolean) -> number");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "type_alias_reduction_errors")
{
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
type Test<T, U> = setmetatable<T, U>

type function get()
    return Test(types.number, types.string)
end

local function ok(idx: get<>): number return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(
        toString(result.errors[0]) ==
        R"('get' type function errored at runtime: [string "get"]:5: failed to reduce type function with: Type function instance setmetatable<number, string> is uninhabited)"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_alias_unreferenced_do_not_block")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function foo(t)
    return types.unionof(types.number, t)
end

type Test = foo<string>

local x: foo<boolean>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK(toString(requireType("x"), ToStringOptions{true}) == "boolean | number");
}

TEST_CASE_FIXTURE(Fixture, "udtf_type_alias_registration_follows")
{
    LUAU_REQUIRE_ERRORS(check(R"(
export type t110 = ""type--"
function _<t32...,t0...,t0...,t0...>(...):(any)&(any)
end
if _ then
else
    export type t110 = ""type--"
    function _<t32...,t0...,t0...,t0...>(...):(any)&(any)
    end
end
    )"));
}

TEST_CASE_FIXTURE(Fixture, "udtf_metatable_serialization_follows")
{
    LUAU_REQUIRE_ERRORS(check(R"(
_ = setmetatable(_(),_),(_) or _ == _ or f
while _() do
export type function t0<O,I>(l0,l0)
end
type t39<A> = t0<{write [Vector3]:any},typeof(_),l0.t0,any>
end
    )"));
}

TEST_CASE_FIXTURE(Fixture, "udtf_double_definition")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function t0<A>()
end
type function t0<A>()
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]) == R"(Redefinition of type 't0', previously defined at line 2)");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_fuzz_environment_scope_crash")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
local _, running = ...
type function t255() end
if _ then
    type function t1() end
    type function t6(l0,...) end
    type function t255<A...>() end
    export type function t0<A>() end
else
    type function t1(...) end
    type function t66<A...>(...) end
    type function t255() end
    if running then
        export type function t255() end
        type function t0(l0) end
    end
end
type function t0(l0,...) end
export type function t66(...)
    export type function t255() end
end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "oss_1887_udtf_with_optional_missing")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult results = check(R"(
        type function create_table_with_key()
            local tbl = types.newtable()
            tbl:setproperty(types.singleton "key", types.unionof(types.string, types.singleton(nil)))
            return tbl
        end
        local a: create_table_with_key = {}
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, results);
    LUAU_REQUIRE_ERROR(results, UnappliedTypeFunction);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "oss_1887_udtf_with_optional_present")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult results = check(R"(
        type function create_table_with_key()
            local tbl = types.newtable()
            tbl:setproperty(types.singleton "key", types.unionof(types.string, types.singleton(nil)))
            return tbl
        end
        local a: create_table_with_key = { key = "123" }
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, results);
    LUAU_REQUIRE_ERROR(results, UnappliedTypeFunction);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "oss_1887_udtf_table_mismatch")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult results = check(R"(
        type function create_table_with_key()
            local tbl = types.newtable()
            tbl:setproperty(types.singleton "key", types.optional(types.number))
            return tbl
        end
        local my_tbl: create_table_with_key = {key = "123"}
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, results);
    CHECK(get<UnappliedTypeFunction>(results.errors[0]));
    auto err = get<TypeMismatch>(results.errors[1]);
    REQUIRE(err);
    CHECK_EQ("string", toString(err->givenType));
    CHECK_EQ("number?", toString(err->wantedType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "oss_1887_basic_mismatch")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult results = check(R"(
        type function foo()
            return types.number
        end
        local f: foo = "123"
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, results);
    CHECK(get<UnappliedTypeFunction>(results.errors[0]));
    auto err = get<TypeMismatch>(results.errors[1]);
    REQUIRE(err);
    CHECK_EQ("string", toString(err->givenType));
    CHECK_EQ("number", toString(err->wantedType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "oss_1887_basic_match")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult results = check(R"(
        type function foo()
            return types.string
        end
        local f: foo = "123"
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, results);
    LUAU_REQUIRE_ERROR(results, UnappliedTypeFunction);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typeof_into_type_function_should_not_crash")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};
    CheckResult results = check(R"(
        type function identity(t: type)
            return t
        end

        type func<parameters...> = typeof(function(...: parameters...) end)
        local whomp: <T>(arg1: T) -> identity<T>
        whomp(function(...) end :: func<any>)
    )");

    LUAU_REQUIRE_NO_ERRORS(results);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "externs_are_extern")
{
    ScopedFastFlag _ = {FFlag::DebugLuauForceOldSolver, false};

    loadDefinition(R"(
        declare extern type Bar with
        end
    )");

    CheckResult results = check(R"(
        type function foo(t: type)
            assert(t.tag == "extern")
            return t
        end

        type T = foo<Bar>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_functions_cannot_try_to_mutate_type_aliases")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag frozen{FFlag::LuauTypeFunctionSupportsFrozen, true};
    CheckResult result = check(R"(
        type myType = {}

        type function create_table_with_key()
            myType:setproperty(types.singleton "key", types.optional(types.number))
            return myType
        end
        local my_tbl: create_table_with_key<> = {key = "123"}
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(
        toString(result.errors[0]) ==
        R"('create_table_with_key' type function errored at runtime: [string "create_table_with_key"]:5: type.setproperty: cannot be called to mutate a frozen type, use `types.copy` to make a copy)"
    );
    auto err = get<TypeMismatch>(result.errors[1]);
    REQUIRE(err);
    CHECK_EQ("{ key: string }", toString(err->givenType));
    CHECK_EQ("create_table_with_key<>", toString(err->wantedType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_functions_can_mutate_cloned_type_aliases")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag frozen{FFlag::LuauTypeFunctionSupportsFrozen, true};

    CheckResult result = check(R"(
        type myType = { woof: string }

        type function create_table_with_key()
            local tbl = types.copy(myType)
            tbl:setproperty(types.singleton "key", types.optional(types.number))
            return tbl
        end
        local my_tbl: create_table_with_key<> = { key = 123, woof = "woof" }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "oss2164_table_subtyping_bug")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag fix{FFlag::LuauSubtypingMissingPropertiesAsNil, true};

    CheckResult results = check(R"(
        export type function tblpartial(tbl: type)
            assert(tbl:is("table"), "tblpartial can only be applied to tables")
            local new = types.newtable()

            for k, v in tbl:properties() do
                local read = assert(v.read, "properties cannot be write-only")
                new:setreadproperty(k, types.optional(read))
            end

            return new
        end

        local function tblmerge<T>(base: T, override: tblpartial<T>): T error("unimplemented") end
        tblmerge({ a = 1 }, {}) -- Type '{  }' could not be converted into '{ read a: number? }'
    )");

    LUAU_REQUIRE_NO_ERRORS(results);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_functions_many_arguments")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function many(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af, ag, ah, ai, aj, ak)
    return a0
end

local x: many<number, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any, any> = 1
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_unsupported_type_function_application_error")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag structuredErrors{FFlag::LuauTypeFunctionStructuredErrors, true};

    CheckResult result = check(R"(
local function f<D, L>(data: D & {}, index: L | "Test"): index<D, L>
    return data[index]
end

local test = f :: test<typeof(f)>
type function test(t: type) return t end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]) == "Type functions do not currently support types of the form 'index<D, L>'");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_identity_preserves_parameter_names")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag serializeArgNames{FFlag::LuauTypeFunctionSerializeArgNames, true};

    CheckResult result = check(R"(
type function identity(t)
    return t
end

type baz = string
type foo = (foo: number, bar: baz) -> baz
type bar = identity<foo>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    std::optional<TypeId> barTy = lookupType("bar");
    REQUIRE(barTy);

    const FunctionType* ftv = get<FunctionType>(follow(*barTy));
    REQUIRE(ftv);
    REQUIRE(ftv->argNames.size() == 2);

    REQUIRE(ftv->argNames[0].has_value());
    CHECK(ftv->argNames[0]->name == "foo");

    REQUIRE(ftv->argNames[1].has_value());
    CHECK(ftv->argNames[1]->name == "bar");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_serialize_iteration_limit_null_deref")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastInt luauTypeFunctionSerdeIterationLimit{DFInt::LuauTypeFunctionSerdeIterationLimit, 10};

    CheckResult result = check(R"(
        type function pass(arg)
            return arg
        end

        type Complex = {
            a: number,
            b: string,
            c: boolean,
            d: nil,
            e: buffer,
            f: thread,
            g: (number, string) -> (boolean, nil),
        }

        local function ok(idx: pass<Complex>): Complex return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK("Complexity limit reached when passing a type to a type function" == toString(result.errors[0]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_type_alias_call_serialize_null_deref")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastInt luauTypeFunctionSerdeIterationLimit{DFInt::LuauTypeFunctionSerdeIterationLimit, 10};

    CheckResult result = check(R"(
        type Big<T> = {
            a: T,
            b: string,
            c: boolean,
            d: nil,
            e: buffer,
            f: thread,
            g: (T, string) -> (boolean, nil),
        }

        type function apply(arg)
            return Big(arg)
        end

        local function ok(idx: apply<number>): Big<number> return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(
        "'apply' type function errored at runtime: [string \"apply\"]:13: Complexity limit reached when passing a type to a type alias" ==
        toString(result.errors[0])
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_env_alias_serialize_null_deref")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastInt luauTypeFunctionSerdeIterationLimit{DFInt::LuauTypeFunctionSerdeIterationLimit, 10};

    CheckResult result = check(R"(
        type Alias = {
            a: number,
            b: string,
            c: boolean,
            d: nil,
            e: buffer,
            f: thread,
            g: (number, string) -> (boolean, nil),
        }

        type function use_alias()
            return Alias
        end

        local function ok(idx: use_alias<>): Alias return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK("'use_alias' type function: returned a non-type value" == toString(result.errors[0]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_deep_copy_iteration_limit_null_deref")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastInt serdeLimit{DFInt::LuauTypeFunctionSerdeIterationLimit, 10};

    CheckResult result = check(R"(
        type function copy_complex(arg)
            local t = types.newtable()
            t:setproperty(types.singleton("a"), types.number)
            t:setproperty(types.singleton("b"), types.string)
            t:setproperty(types.singleton("c"), types.boolean)
            t:setproperty(types.singleton("d"), types.buffer)
            t:setproperty(types.singleton("e"), types.thread)
            t:setproperty(types.singleton("f"), types.newtable())
            t:setproperty(types.singleton("g"), types.newfunction())
            local c = types.copy(t)
            return c
        end

        local function ok(idx: copy_complex<number>): number return idx end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(
        "'copy_complex' type function errored at runtime: [string \"copy_complex\"]:11: types.copy: complexity limit reached during type copy" ==
        toString(result.errors[0])
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_areequal_stack_overflow_on_deep_types")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function deep_eq()
            local depth = 50000
            local function build()
                local t = types.newtable()
                for i = 1, depth do
                    local outer = types.newtable()
                    outer:setproperty(types.singleton("x"), t)
                    t = outer
                end
                return t
            end
            local a = build()
            local b = build()
            if a == b then
                return types.boolean
            end
            return types.string
        end

        local x: deep_eq<> = true
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK("'deep_eq' type function errored at runtime: Internal recursion counter limit exceeded in areEqual" == toString(result.errors[0]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_setmetatable_wrong_error_tag")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function foo()
            local t = types.newtable()
            t:setmetatable(types.number)
            return t
        end

        local x: foo<> = nil
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(
        "'foo' type function errored at runtime: [string \"foo\"]:4: type.setmetatable: expected the argument to be a table, but got number "
        "instead" == toString(result.errors[0])
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_cloner_missing_integer_crashes_copy")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag integerType{FFlag::LuauIntegerType2, true};

    CheckResult result = check(R"(
        type function copy_int(arg)
            local c = types.copy(arg)
            return c
        end

        local function ok(idx: copy_int<integer>): integer return idx end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_setgenerics_wrong_argcount_check")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        type function extra_arg()
            local f = types.newfunction()
            local g = types.generic("T")
            f:setgenerics({g}, "extra")
            return f
        end

        local x: extra_arg<> = nil
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK("Argument count mismatch. Function expects 1 to 2 arguments, but 3 are specified" == toString(result.errors[0]));
    CHECK(
        "'extra_arg' type function errored at runtime: [string \"extra_arg\"]:5: type.setgenerics: expected 2 arguments, but got 3" ==
        toString(result.errors[1])
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "issubtypeof")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sff{FFlag::LuauUdtfTypeIsSubtypeOf, true};

    CheckResult results = check(R"(
        type function checksubtype(a, b)
            if not a:issubtypeof(b) then
                error("Not a subtype!")
            end
            return a
        end

        local x: checksubtype<nil, nil>                          -- T
        local y: checksubtype<nil, string?>                      -- T
        local z: checksubtype<"Hello", string>                   -- T
        local x1: checksubtype<add<number, number>, number | vector> -- T
        local w: checksubtype<string | vector | number, number>  -- F
        local a: checksubtype<boolean, number>                   -- F
        local b: checksubtype<false, nil>                        -- F
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, results);

    CHECK(get<UserDefinedTypeFunctionError>(results.errors[0]));
    CHECK(get<UserDefinedTypeFunctionError>(results.errors[1]));
    CHECK(get<UserDefinedTypeFunctionError>(results.errors[2]));

    CHECK_EQ(results.errors[0].location.begin.line, 12);
    CHECK_EQ(results.errors[1].location.begin.line, 13);
    CHECK_EQ(results.errors[2].location.begin.line, 14);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "issubtypeof_top_and_bottom")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sff{FFlag::LuauUdtfTypeIsSubtypeOf, true};

    CheckResult results = check(R"(
        type function issub(a, b)
            return types.singleton(a:issubtypeof(b))
        end

        local a: issub<never, number>
        local b: issub<never, string>
        local c: issub<never, never>
        local d: issub<number, unknown>
        local e: issub<string, unknown>
        local f: issub<unknown, unknown>
        local g: issub<never, unknown>
        local h: issub<unknown, number>
        local i: issub<number, never>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);

    CHECK(toString(requireType("a")) == "true");
    CHECK(toString(requireType("b")) == "true");
    CHECK(toString(requireType("c")) == "true");
    CHECK(toString(requireType("d")) == "true");
    CHECK(toString(requireType("e")) == "true");
    CHECK(toString(requireType("f")) == "true");
    CHECK(toString(requireType("g")) == "true");
    CHECK(toString(requireType("h")) == "false");
    CHECK(toString(requireType("i")) == "false");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "issubtypeof_any")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sff{FFlag::LuauUdtfTypeIsSubtypeOf, true};

    CheckResult results = check(R"(
        type function issub(a, b)
            return types.singleton(a:issubtypeof(b))
        end

        local a: issub<any, number>
        local b: issub<number, any>
        local c: issub<any, any>
        -- This is a special case: any <: unknown
        local d: issub<any, unknown>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);

    CHECK(toString(requireType("a")) == "false");
    CHECK(toString(requireType("b")) == "true");
    CHECK(toString(requireType("c")) == "true");
    CHECK(toString(requireType("d")) == "true");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "issubtypeof_table_structural")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sff{FFlag::LuauUdtfTypeIsSubtypeOf, true};

    CheckResult results = check(R"(
        type function issub(a, b)
            return types.singleton(a:issubtypeof(b))
        end

        type Wide = { a: number, b: string }
        type Narrow = { a: number }
        type Different = { a: string }
        type TablesSubtypeInvariantly = { a: string | number }
        type ReadNum1 = { read a: number }
        type ReadNumOrStr = { read a: number | string }

        local a: issub<Wide, Narrow>
        local b: issub<Narrow, Wide>
        local c: issub<Wide, Wide>
        local d: issub<Wide, Different>
        local e: issub<Narrow, TablesSubtypeInvariantly>
        local f: issub<ReadNum1, ReadNumOrStr>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);

    CHECK(toString(requireType("a")) == "true");
    CHECK(toString(requireType("b")) == "false");
    CHECK(toString(requireType("c")) == "true");
    CHECK(toString(requireType("d")) == "false");
    CHECK(toString(requireType("e")) == "false");
    CHECK(toString(requireType("f")) == "true");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "issubtypeof_function")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sff{FFlag::LuauUdtfTypeIsSubtypeOf, true};

    CheckResult results = check(R"(
        type function issub(a, b)
            return types.singleton(a:issubtypeof(b))
        end

        type F1 = (number) -> string
        type F2 = (number) -> string
        type F3 = (unknown) -> string
        type F4 = (number) -> unknown

        local a: issub<F1, F2>
        local b: issub<F3, F1>
        local c: issub<F1, F3>
        local d: issub<F1, F4>
        local e: issub<F4, F1>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);

    CHECK(toString(requireType("a")) == "true");
    CHECK(toString(requireType("b")) == "true");
    CHECK(toString(requireType("c")) == "false");
    CHECK(toString(requireType("d")) == "true");
    CHECK(toString(requireType("e")) == "false");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "issubtypeof_union")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sff{FFlag::LuauUdtfTypeIsSubtypeOf, true};

    CheckResult results = check(R"(
        type function issub(a, b)
            return types.singleton(a:issubtypeof(b))
        end

        local a: issub<number, number | string>
        local b: issub<number | string, number | string | boolean>
        local c: issub<number | string, number>
        local d: issub<number | string, number | string>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);

    CHECK(toString(requireType("a")) == "true");
    CHECK(toString(requireType("b")) == "true");
    CHECK(toString(requireType("c")) == "false");
    CHECK(toString(requireType("d")) == "true");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "issubtypeof_intersection")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sff{FFlag::LuauUdtfTypeIsSubtypeOf, true};

    CheckResult results = check(R"(
        type function issub(a, b)
            return types.singleton(a:issubtypeof(b))
        end

        type A = { a: number }
        type B = { b: string }

        local a: issub<A & B, A>
        local b: issub<A & B, B>
        local c: issub<A, A & B>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);

    CHECK(toString(requireType("a")) == "true");
    CHECK(toString(requireType("b")) == "true");
    CHECK(toString(requireType("c")) == "false");
}

TEST_CASE_FIXTURE(ExternTypeFixture, "issubtypeof_extern_type_hierarchy")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sff{FFlag::LuauUdtfTypeIsSubtypeOf, true};

    CheckResult results = check(R"(
        type function issub(a, b)
            return types.singleton(a:issubtypeof(b))
        end

        local a: issub<ChildClass, BaseClass>
        local b: issub<BaseClass, ChildClass>
        local c: issub<BaseClass, BaseClass>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);

    CHECK(toString(requireType("a")) == "true");
    CHECK(toString(requireType("b")) == "false");
    CHECK(toString(requireType("c")) == "true");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "issubtypeof_table_indexer")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag sffs[] = {
        {FFlag::LuauUdtfTypeIsSubtypeOf, true}, {FFlag::LuauTypeFunctionTableIndexerIsReadOnly, true}
    };

    CheckResult results = check(R"(
        type function issub(a, b)
            return types.singleton(a:issubtypeof(b))
        end

        type Arr = { [number]: string }
        type Map = { [string]: number }
        type NumStrArray = { [number]: string | number }
        type ReadArr = { read [number]: string }
        type ReadNumStrArray = { read [number]: string | number }

        local a: issub<Arr, Map>
        local b: issub<{ [number]: string }, NumStrArray>
        local c: issub<ReadArr, ReadNumStrArray>
    )");

    LUAU_REQUIRE_NO_ERRORS(results);

    CHECK(toString(requireType("a")) == "false");
    CHECK(toString(requireType("b")) == "false");
    CHECK(toString(requireType("c")) == "true");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_tostring")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag tostringMetamethod{FFlag::LuauUdtfTypeToStringMetamethod, true};

    CheckResult results = check(R"(
        type function foo(ty)
            error(tostring(ty))
        end

        type T<U> = {
            read absoluteHina: true,
            [number]: string,
            t: T<U>
        }

        local x: foo<T<vector>>
    )");
    LUAU_REQUIRE_ERROR_COUNT(1, results);

    CHECK_EQ(
        toString(results.errors[0]),
        "'foo' type function errored at runtime: [string \"foo\"]:3: { [number]: string, read absoluteHina: true, t: t1 }"
            " where t1 = { [number]: string, read absoluteHina: true, t: t1 }"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "types_singleton_error_message")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
    ScopedFastFlag fixErrorMessage{FFlag::LuauUdtfCreateSingletonFixErrorMessage, true};

    CheckResult results = check(R"(
        type alias = {}
        type function meow()
            return types.singleton(alias :: any)
        end

        type test = meow<>
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, results);
    CHECK_EQ(
        toString(results.errors[0]),
        "'meow' type function errored at runtime: [string \"meow\"]:4: types.singleton: can't create a singleton from a type"
    );
}

// Luwu: `types.error(message)` returned from a type function is reported as `message`
TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_types_error_reports_its_message")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function NotNumber(t: type): type
    if t:is("number") then
        return types.error("numbers aren't allowed here")
    end
    return t
end

type function Nested(t: type): type
    return types.unionof(types.string, types.error("nope"))
end

type function Tag(t: type): type
    return types.singleton(types.error("x").tag)
end

local a: NotNumber<string> = "fine"
local b: NotNumber<number> = nil :: any
local c: Nested<number> = nil :: any
local d: Tag<number> = "error"
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(result.errors[0].location.begin.line, 17);
    CHECK_EQ(toString(result.errors[0]), "numbers aren't allowed here");
    CHECK_EQ(result.errors[1].location.begin.line, 18);
    CHECK_EQ(toString(result.errors[1]), "types.error(...) can only be returned from a type function, not used inside another type");
}

// Luwu: a generic function's `Row<C>` is left unreduced by its module. A caller in another module used to re-push the
// argument into it until the solver gave up ("Code is too complex", "failed to complete").
TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_of_a_generic_parameter_from_another_module")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    fileResolver.source["game/A"] = R"(
export type function Row(columns: type): type
    return types.newtable({ [types.singleton("x")] = types.string })
end
local function with_rows<C>(rows: { Row<C> }) end
return { with_rows = with_rows }
    )";

    CheckResult result = check(R"(
local A = require(game.A)
A.with_rows({ { x = "a" } })
A.with_rows({ { y = "a" } })
    )");

    // The mismatch is reported twice, in one module as well: both errors are the same one
    REQUIRE_EQ(result.errors.size(), 2);
    for (const TypeError& error : result.errors)
    {
        CHECK_EQ(error.location.begin.line, 3);
        CHECK(get<TypeMismatch>(error));
    }
}

// Luwu: an imported generic class's member that holds a type function of the class's generics (`Row<C>`,
// `Append<Headings, N>`) is built as it is: nothing in the importing module will reduce it in the template.
TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_of_class_generics_from_another_module")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag genericNominals{FFlag::LuwuGenericNominals, true};

    fileResolver.source["game/A"] = R"(
type function Append(prev: type, new: type): type
    return new
end
export type function Row(columns: type): type
    return types.newtable({ [types.singleton("x")] = types.string })
end
class Header<Headings = "">()
    function column<N>(self, name: N | ""): Header<Append<Headings, N>>
        return self
    end
end
class Grid<C>
    function with_rows(self, rows: { Row<C> }) end
end
return { Header = Header, Grid = Grid }
    )";

    CheckResult result = check(R"(
local A = require(game.A)
local h = A.Header():column("a"):column("b")
A.Grid<<"x">>():with_rows({ { x = "a" } })
A.Grid<<"x">>():with_rows({ { y = "a" } })
    )");

    REQUIRE_EQ(result.errors.size(), 1);
    CHECK_EQ(result.errors[0].location.begin.line, 4);
    CHECK_EQ(toString(requireType("h")), R"(Header<"b">)");
}

// Luwu: a generic call waits for a type function in its argument (`Box<Same<'a>>`) to reduce, so `T` is what it reduces
// to rather than `unknown`
TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_in_an_argument_infers_the_generic")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag genericNominals{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
type function Same(t: type): type
    return t
end

class Box<T>()
    function retag<N>(self, tag: N | ""): Box<Same<N>>
        return self
    end
end

class Wrap<T>(box: Box<T>) end

local function wrap<T>(box: Box<T>): Wrap<T>
    return Wrap(box)
end

local b = Box():retag("x")
local constructed = Wrap(b)
local called = wrap(Box():retag("y"))
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("constructed")), R"(Wrap<"x">)");
    CHECK_EQ(toString(requireType("called")), R"(Wrap<"y">)");
}

// Luwu: `types.error` in a method's return type is reported at the call, once: not again where the value is used
TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_types_error_in_a_method_return_type")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag genericNominals{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
type function Reject(t: type): type
    return types.error("rejected")
end

class Box<T>()
    function retag<N>(self, tag: N | ""): Box<Reject<N>>
        return self
    end

    function use(self) end
end

Box():retag("x")
local used = Box():retag("y")
used:use()
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(result.errors[0].location.begin.line, 13);
    CHECK_EQ(toString(result.errors[0]), "rejected");
    CHECK_EQ(result.errors[1].location.begin.line, 14);
    CHECK_EQ(toString(result.errors[1]), "rejected");
}

// Luwu: an imported generic function's `keyof<T>` is irreducible in the caller too, so a literal argument keeps its
// literal type rather than widening while it waits for it
TEST_CASE_FIXTURE(BuiltinsFixture, "keyof_of_a_generic_parameter_from_another_module")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    fileResolver.source["game/A"] = R"(
local function pick<T>(rows: { T }, column: keyof<T>) end
local function picks<T>(rows: { T }, columns: { keyof<T> }) end
return { pick = pick, picks = picks }
    )";

    CheckResult result = check(R"(
local A = require(game.A)
type Pet = { name: string, age: number }
local pets: { Pet } = {}
A.pick(pets, "name")
A.picks(pets, { "name", "age" })
A.pick(pets, "nam")
    )");

    REQUIRE_EQ(result.errors.size(), 1);
    CHECK_EQ(result.errors[0].location.begin.line, 6);
}

// Luwu: a type function that loops or allocates without end (easy to have half-typed in an editor) stops at its own
// limits, as a type function error, whether or not the embedder set a time limit for the module
TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_runaway_loops_and_allocations_stop")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastInt timeLimit{FInt::LuwuTypeFunctionTimeLimitMs, 50};
    ScopedFastInt memoryLimit{FInt::LuwuTypeFunctionMemoryLimitMb, 16};

    CheckResult result = check(R"(
type function Loop(t: type): type
    while true do
    end
    return t
end

type function Hog(t: type): type
    local all = {}
    while true do
        table.insert(all, table.create(1000, 1))
    end
    return t
end

local a: Loop<string> = "a"
local b: Hog<string> = "b"
    )");

    // Each runtime error comes with the mismatch every type function error has ("Expected 'a' to be 'Loop<string>'")
    REQUIRE_EQ(result.errors.size(), 4);
    CHECK_EQ(result.errors[0].location.begin.line, 15);
    CHECK(toString(result.errors[0]).find("type function ran for more than 50 ms") != std::string::npos);
    CHECK_EQ(result.errors[2].location.begin.line, 16);
    CHECK(toString(result.errors[2]).find("not enough memory") != std::string::npos);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_functions_keep_docs_and_parameter_names")
{
    ScopedFastFlag serializeArgNames{FFlag::LuauTypeFunctionSerializeArgNames, true};

    CheckResult result = check(R"(
type function Partial(t: type): type
    local out = types.copy(t)
    if not out:is("table") then
        return out
    end
    for key, prop in out:properties() do
        out:setproperty(key, types.optional(prop.read :: type))
    end
    return out
end

type function Same(t: type): type
    return t
end

type Config = {
    name: string,
}

local function greet(who: string, times: number): string
    return who
end

type P = Partial<Config>
type G = Same<typeof(greet)>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    const TableType* config = get<TableType>(follow(requireTypeAlias("Config")));
    const TableType* partial = get<TableType>(follow(requireTypeAlias("P")));
    REQUIRE(config);
    REQUIRE(partial);
    // the rebuilt table and its property point at where the original was written, which is how docs are found
    CHECK_EQ(partial->definitionLocation, config->definitionLocation);
    REQUIRE(partial->props.count("name"));
    CHECK_EQ(partial->props.at("name").location, config->props.at("name").location);
    REQUIRE(partial->props.at("name").readTy);
    CHECK_EQ(toString(*partial->props.at("name").readTy), "string?");

    const FunctionType* greet = get<FunctionType>(follow(requireTypeAlias("G")));
    REQUIRE(greet);
    REQUIRE(greet->definition);
    CHECK_EQ(greet->definition->definitionLocation.begin.line, 20);
    REQUIRE_EQ(greet->argNames.size(), 2);
    REQUIRE(greet->argNames[0]);
    CHECK_EQ(greet->argNames[0]->name, "who");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_functions_name_parameters")
{
    ScopedFastFlag serializeArgNames{FFlag::LuauTypeFunctionSerializeArgNames, true};

    CheckResult result = check(R"(
type function Rename(f: type): type
    local out = types.copy(f)
    if not out:is("function") then
        return out
    end
    local names = out:getparameternames()
    out:setparametername(1, names[2])
    out:setparametername(2, nil)
    return out
end

type function OutOfRange(f: type): type
    local out = types.copy(f)
    if out.tag ~= "function" then
        return out
    end
    out:setparametername(3, "c")
    return out
end

type F = Rename<(a: number, b: string) -> ()>
type G = OutOfRange<(a: number, b: string) -> ()>
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const FunctionType* renamed = get<FunctionType>(follow(requireTypeAlias("F")));
    REQUIRE(renamed);
    REQUIRE_EQ(renamed->argNames.size(), 2);
    REQUIRE(renamed->argNames[0]);
    CHECK_EQ(renamed->argNames[0]->name, "b");
    CHECK_FALSE(renamed->argNames[1]);
    CHECK(
        toString(result.errors[0]).find("type.setparametername: index 3 is out of range; the function has 2 parameters before its tail") !=
        std::string::npos
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_function_metadata_outlives_the_type_function")
{
    CheckResult result = check(R"(
type function Tag(t: type): type
    local out = types.copy(t)
    local metadata = out:metadata()
    metadata.count = (metadata.count or 0) + 1
    metadata.original = t
    metadata.nested = { flag = true, list = { "a", "b" } }
    out:setmetadata(metadata)
    return out
end

type function Count(t: type): type
    local metadata = t:metadata()
    local original = metadata.original
    return types.singleton(`{metadata.count} {metadata.nested.list[2]} {original.tag}`)
end

type A = { x: number }
type B = Tag<Tag<A>>
type C = Count<B>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireTypeAlias("C")), "\"2 b table\"");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_function_metadata_holds_only_what_outlives_it")
{
    CheckResult result = check(R"(
type function Function(t: type): type
    local out = types.copy(t)
    out:setmetadata({ f = function() end })
    return out
end

type function Cycle(t: type): type
    local out = types.copy(t)
    local m = {}
    m.self = m
    out:setmetadata(m)
    return out
end

type function Shared(t: type): type
    types.number:setmetadata({})
    return t
end


type A = Function<{}>
type B = Cycle<{}>
type C = Shared<{}>
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    auto mentions = [&](size_t i, const char* text)
    {
        std::string message = toString(result.errors[i]);
        INFO(message);
        CHECK(message.find(text) != std::string::npos);
    };
    mentions(0, "metadata can only hold booleans, numbers, strings, types and tables of those, not a function");
    mentions(1, "a metadata table can't contain itself");
    mentions(2, "a number type is shared by every use of it, so it can't have metadata of its own");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "declared_types_carry_their_comments_in_metadata")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
type function Comments(t: type): type
    local out = {}
    for _, c in t:metadata().comments or {} do
        table.insert(out, `{c.location} {c.prefix} {c.message}`)
    end
    return types.singleton(table.concat(out, "|"))
end

type function Same(t: type): type
    return t
end

local unrelated = 1 -- not about Config

--- Config docs
-- second line
--[==[ block ]==]
type Config = { -- the side
    name: string,
}

-- a function
local function f(a: number): string -- returns a string
    return ""
end

-- separated by a blank line

local value = { y = 1 }

--!nolint LocalUnused
-- the class
class Cat
end

type A = Comments<Config>
type B = Comments<typeof(f)>
type C = Comments<typeof(value)>
type D = Comments<Cat>
type E = Comments<Same<Config>>
type F = Comments<number>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    const char* configComments = "\"above --- Config docs|above -- second line|above --[==[ block|side -- the side\"";
    CHECK_EQ(toString(requireTypeAlias("A")), configComments);
    CHECK_EQ(toString(requireTypeAlias("B")), "\"above -- a function|side -- returns a string\"");
    CHECK_EQ(toString(requireTypeAlias("C")), "\"\"");
    CHECK_EQ(toString(requireTypeAlias("D")), "\"above -- the class\"");
    // a type a type function returns keeps the original's metadata
    CHECK_EQ(toString(requireTypeAlias("E")), configComments);
    CHECK_EQ(toString(requireTypeAlias("F")), "\"\"");
}

// the text of a string singleton type, or "" when `ty` isn't one
static std::string stringSingletonOf(TypeId ty)
{
    const SingletonType* singleton = get<SingletonType>(follow(ty));
    const StringSingleton* string = singleton ? get<StringSingleton>(singleton) : nullptr;
    return string ? string->value : "";
}

TEST_CASE_FIXTURE(BuiltinsFixture, "a_type_passed_through_a_type_function_keeps_its_alias_names")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
type function Show(t: type): type
    return types.singleton(types.format(t))
end
type function Copied(t: type): type
    return types.copy(t)
end
type function Edited(t: type): type
    local out = types.copy(t) :: any
    local q = out:readproperty(types.singleton("q"))
    q:setproperty(types.singleton("b"), types.number)
    out:setproperty(types.singleton("q"), q)
    return out
end

type P = string | number
type Q = { a: P }
type R = { q: Q, p: P }

local shown: Show<R> = nil :: any
local copied: Copied<R> = nil :: any
local edited: Edited<R> = nil :: any
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    // the type itself prints as its structure, like a hover, and what's inside it by name
    CHECK_EQ(stringSingletonOf(requireType("shown")), "{ p: P, q: Q }");
    // a type a type function didn't change is still the alias it was
    CHECK_EQ(toString(requireType("copied")), "R");
    // one it changed isn't, and neither is anything that reaches the change; what it didn't touch keeps its name
    CHECK_EQ(toString(requireType("edited")), "{ p: P, q: { a: P, b: number } }");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "types_format_prints_a_type_with_options")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag argNames{FFlag::LuauTypeFunctionSerializeArgNames, true};

    CheckResult result = check(R"(
type function None(t: type): type
    return types.singleton(types.format(t, { expand = "none" }))
end
type function All(t: type): type
    return types.singleton(types.format(t, { expand = "all", parameternames = false }))
end
type function Multi(t: type): type
    return types.singleton(types.format(t, { multiline = true, indent = 2, comments = 16 }))
end
type function Bad(t: type): type
    return types.singleton(types.format(t, { colour = true } :: any))
end

type P = string | number
type R = {
    --- The path to a file that is read
    path: P,
    -- not a doc comment
    read: (path: P) -> string,
}

local none: None<R> = nil :: any
local all: All<R> = nil :: any
local multi: Multi<R> = nil :: any
local bad: Bad<R> = nil :: any
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(toString(result.errors[0]).find("types.format: 'colour' isn't an option") != std::string::npos);
    CHECK_EQ(stringSingletonOf(requireType("none")), "R");
    CHECK_EQ(stringSingletonOf(requireType("all")), "{ path: number | string, read: (number | string) -> string }");
    // the comment above `path` is cut to 16 characters; a plain `--` comment isn't a doc comment
    CHECK_EQ(stringSingletonOf(requireType("multi")), "{\n  --- The path to a f…\n  path: P,\n  read: (path: P) -> string\n}");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "types_format_shows_the_comments_of_functions_declared_into_a_table")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag argNames{FFlag::LuauTypeFunctionSerializeArgNames, true};

    CheckResult result = check(R"(
local str = {}
--- check if a string starts with `prefix`
function str.startswith(s: string, prefix: string): boolean
    return true
end
str.plain = function() end

type function Show(t: type): type
    return types.singleton(types.format(t, { multiline = true, comments = true }))
end
local shown: Show<typeof(str)> = nil :: any
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    // the property has no comments of its own; the function declared into it does
    CHECK_EQ(
        stringSingletonOf(requireType("shown")),
        "{\n    plain: (...any) -> (),\n    --- check if a string starts with `prefix`\n    startswith: (s: string, prefix: string, ...any) -> boolean\n}"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "a_block_comment_message_loses_its_source_indentation")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
type function Message(t: type): type
    local comments = t:metadata().comments :: { { message: string } }
    return types.singleton(comments[1].message)
end

--[=[
    A filesystem library.

        indented further
    the end
]=]
type Fs = { x: number }

local message: Message<Fs> = nil :: any
local x: never = message
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const TypeMismatch* mismatch = get<TypeMismatch>(result.errors[0]);
    REQUIRE(mismatch);
    CHECK_EQ(toString(mismatch->givenType), "\"A filesystem library.\\n\\n    indented further\\nthe end\"");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "luwu_type_kinds_have_only_their_own_methods")
{
    CheckResult result = check(R"(
type function Keys(t: type): type
    local unnarrowed = t:properties()
    if t:is("table") then
        local names: { type } = {}
        for key in t:properties() do
            table.insert(names, key)
        end
        return types.unionof(table.unpack(names))
    elseif t.tag == "function" then
        local params = t:parameters()
        return types.never
    else
        local notATable = t:properties()
        return types.never
    end
end

type function Fill(t: type): type
    local out = types.newtable()
    out:setproperty(types.singleton("a"), types.number)
    local tag: "union" | "table" = if t.tag == "union" then "union" else "table"
    if t:is(tag) then
        local unknownTag = t:components()
    end
    return out
end

type K = Keys<{ a: number, b: string }>
type F = Fill<number>
    )");

    // `t:properties()` before narrowing, in the branch left with extern types (which have properties) and the kinds
    // without, and `components` when the tag could be one of several
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK_EQ(result.errors[0].location.begin.line, 2);
    CHECK_EQ(result.errors[1].location.begin.line, 13);
    CHECK_EQ(result.errors[2].location.begin.line, 23);
    CHECK_EQ(toString(requireTypeAlias("K")), "\"a\" | \"b\"");
    const TableType* filled = get<TableType>(follow(requireTypeAlias("F")));
    REQUIRE(filled);
    CHECK(filled->props.count("a"));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_functions_see_objects_classes_and_traits")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag genericNominals{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
trait Greeter
    expect public name: string
    expect public function speak?(self): string
    public function greet(self): string
        return self.name
    end
end

class Cat(public name: string, private secret: number) implements Greeter
    public const legs = 4
    public function speak(self): string
        return "meow"
    end
    private function purr(self) end
    public function __tostring(self): string
        return self.name
    end
end

class Box<T>(public value: T)
end

type function Describe(t: type): type
    local out: { string } = { t.tag }
    if t:is("object") then
        local cls = t:class()
        table.insert(out, if cls then "class=" .. cls:name() else "noclass")
        for _, trait in t:traits() do
            table.insert(out, "trait=" .. trait:name())
        end
        local fields: { string } = {}
        for name, field in t:fields() do
            table.insert(fields, `{name}{if field.private then ":private" else ""}{if field.const then ":const" else ""}{if field.expected then ":expected" else ""}`)
        end
        table.sort(fields)
        table.insert(out, "fields=" .. table.concat(fields, ","))
        local functions: { string } = {}
        for name, fn in t:functions() do
            table.insert(functions, `{name}{if fn.private then ":private" else ""}{if fn.expected then ":expected" else ""}{if fn.optional then ":optional" else ""}`)
        end
        table.sort(functions)
        table.insert(out, "functions=" .. table.concat(functions, ","))
        for _, arg in t:typearguments() do
            table.insert(out, "arg=" .. arg.tag)
        end
    elseif t:is("class") then
        local ctor = t:constructor()
        local params: { string } = {}
        if ctor and ctor.type:is("function") then
            for _, param in ctor.type:parameters().head or {} do
                table.insert(params, param.tag)
            end
        end
        table.insert(out, `object={t:object():name()} ctor=({table.concat(params, ",")})`)
    elseif t:is("trait") then
        table.insert(out, `object={t:object():name()} needs={#t:needs()}`)
    end
    return types.singleton(table.concat(out, " "))
end

type A = Describe<Cat>
type B = Describe<Greeter>
type C = Describe<Box<number>>
type D = Describe<class<Cat>>
type E = Describe<trait<Greeter>>
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(
        toString(requireTypeAlias("A")),
        R"x("object class=Cat trait=Greeter fields=legs:const,name,secret:private functions=__tostring,greet,purr:private,speak")x"
    );
    // a value of a trait type is an object, with no one class
    CHECK_EQ(toString(requireTypeAlias("B")), R"x("object noclass trait=Greeter fields=name:expected functions=greet,speak:expected:optional")x");
    CHECK_EQ(toString(requireTypeAlias("C")), R"x("object class=Box fields=value functions= arg=number")x");
    CHECK_EQ(toString(requireTypeAlias("D")), R"x("class object=Cat ctor=(string,number)")x");
    CHECK_EQ(toString(requireTypeAlias("E")), R"x("trait object=Greeter needs=0")x");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_functions_make_classes_and_traits")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
type function Named(): Trait
    return types.newtrait("Named", {
        fields = { name = { type = types.string, expected = true } },
        functions = { describe = { type = types.newfunction({}, { head = { types.string } }), method = true } },
    })
end

type function Point(withZ: type): type
    local fields: { [string]: FieldDefinition } = { x = types.number, y = { type = types.number, const = true }, name = types.string }
    if withZ:is("singleton") and withZ:value() == true then
        fields.z = types.number
    end
    return types.newclass("Point", {
        fields = fields,
        functions = { length = { type = types.newfunction({}, { head = { types.number } }), method = true } },
        implements = { Named() },
        constructor = types.newfunction({ head = { types.number, types.number } }),
    })
end

type function Nameless(): type
    return types.newclass("Nameless", { implements = { Named() } })
end

type function Object(t: type): type
    assert(t:is("class") or t:is("trait"))
    return t:object()
end

type P = Object<Point<false>>
type P2 = Object<Point<false>>
type P3 = Object<Point<true>>
type N = Object<Named<>>

local PointClass = (nil :: any) :: Point<false>
local p: P = PointClass(1, 2)
local again: P2 = p
local len: number = p:length()
local text: string = p:describe()
local named: N = p
p.y = 5
local other: P3 = p
type Broken = Nameless<>
    )");

    // `y` is const; a `Point` with a `z` field is another class, though both are named `Point`; `Nameless` lacks the
    // `name` its trait expects
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK_EQ(result.errors[0].location.begin.line, 46);
    CHECK_EQ(result.errors[1].location.begin.line, 47);
    CHECK_EQ(result.errors[2].location.begin.line, 48);
    CHECK(toString(result.errors[2]).find("class 'Nameless' doesn't have 'name', which trait 'Named' expects") != std::string::npos);
    CHECK_EQ(toString(requireType("p")), "Point");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "classes_type_functions_make_are_the_same_across_modules")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    fileResolver.source["game/A"] = R"(
        export type function Vec(): type
            return types.newclass("Vec", { fields = { x = types.number, y = types.number } })
        end
        export type V = Vec<>
        return { make = function(): V return nil :: any end }
    )";
    fileResolver.source["game/B"] = R"(
        type function Vec(): type
            return types.newclass("Vec", { fields = { x = types.number, y = types.number } })
        end
        type function Object(t: type): type
            assert(t:is("class"))
            return t:object()
        end
        local a = require(game.A)
        local mine: Object<Vec<>> = (a.make() :: any) :: Object<a.Vec<>>
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "an_error_inside_a_type_functions_argument_is_reported_once")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    // the unknown type is the only error: the call is an error type quietly, as any use of an error type is
    CheckResult result = check(R"(
        type function Same(t: type): type
            return t
        end
        type Lib = Same<{ file: { read: (path: Pathlike) -> string } }>
        type Top = Same<Pathlike>
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    for (const TypeError& error : result.errors)
    {
        const UnknownSymbol* unknown = get<UnknownSymbol>(error);
        REQUIRE(unknown);
        CHECK_EQ(unknown->name, "Pathlike");
    }
}

// Luwu literal types: a validator sees each argument as written and accepts or rejects it at the call
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_run_the_validator_on_each_argument")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
type function ShortFlag(alias: type): type
    if alias:is("singleton") then
        local name = alias:value()
        if typeof(name) == "string" and string.sub(name, 1, 1) == "-" and string.sub(name, 1, 2) ~= "--" then
            return alias
        end
        return types.error(`bad alias {name}`)
    end
    return types.error(`not a literal: {alias.tag}`)
end

local function aliases(first: ShortFlag<literal<string>>, ...: ShortFlag<literal<string>>): string
    local s: string = first
    return s
end

aliases("-f")
aliases("-f", "-g", ("-h"))
aliases("--f")
aliases("-f", "-g", "x")
local s: string = "-f"
aliases(s)
    )");

    REQUIRE_EQ(result.errors.size(), 3);
    CHECK_EQ(result.errors[0].location.begin.line, 19);
    CHECK_EQ(toString(result.errors[0]), "bad alias --f");
    CHECK_EQ(result.errors[1].location.begin.line, 20);
    CHECK_EQ(toString(result.errors[1]), "bad alias x");
    CHECK_EQ(result.errors[2].location.begin.line, 22);
    CHECK_EQ(toString(result.errors[2]), "expected a literal of type string, but got string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_rejected_by_never")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
type function NoDashes(s: type): type
    if s:is("singleton") and string.find(s:value() :: string, "-", 1, true) == nil then
        return s
    end
    return types.never
end

type Opts = {
    set: (self: Opts, key: NoDashes<literal<string>>) -> (),
}
local o: Opts = nil :: any
o:set("abc")
o:set("a-b")

local function generic<T>(x: T, key: NoDashes<literal<string>>) end
generic(1, "ok")
generic(1, "n-o")
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(result.errors[0].location.begin.line, 13);
    CHECK_EQ(toString(result.errors[0]), "Expected this to be unreachable, but got '\"a-b\"'");
    CHECK_EQ(result.errors[1].location.begin.line, 17);
    CHECK_EQ(toString(result.errors[1]), "Expected this to be unreachable, but got '\"n-o\"'");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_on_class_methods")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
type function Short(s: type): type
    if s:is("singleton") and #(s:value() :: string) <= 2 then
        return s
    end
    return types.never
end

class Flag(public name: string)
    public function alias(self, short: Short<literal<string>>): Flag
        return self
    end
end

local ok: Flag = Flag("verbose"):alias("-v")
local bad = Flag("verbose"):alias("--verbose")
    )");

    REQUIRE_EQ(result.errors.size(), 1);
    CHECK_EQ(result.errors[0].location.begin.line, 15);
    CHECK_EQ(toString(result.errors[0]), "Expected this to be unreachable, but got\n    '\"--verbose\"'");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_locals_keep_the_literal")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalTypes{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
local a: literal<string> = "a"
local a2: "a" = a
local c: literal<boolean> = true
local c2: true = c
local s: string = "x"
local b: literal<string> = s
local wrong: "b" = a
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(result.errors[0].location.begin.line, 6);
    CHECK_EQ(toString(result.errors[0]), "expected a literal of type string, but got string");
    CHECK_EQ(result.errors[1].location.begin.line, 7);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_generics_infer_the_literal")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalTypes{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
type Symbol<S> = { name: S }
local function sym<S>(n: literal<S>): Symbol<S>
    return { name = n }
end
local function name(n: literal<string>): string
    return n
end

local foo = sym("foo")
local foo2: Symbol<"foo"> = foo
local s: string = "x"
local notLiteral = sym(s)
name("ok")
name(s)
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(result.errors[0].location.begin.line, 12);
    CHECK_EQ(toString(result.errors[0]), "expected a literal of type string, but got string");
    CHECK_EQ(result.errors[1].location.begin.line, 14);
    CHECK_EQ(toString(result.errors[1]), "expected a literal of type string, but got string");
    CHECK_EQ(toString(requireType("foo")), "Symbol<\"foo\">");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_outside_parameters_locals_and_fields")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalTypes{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
local function ret(): literal<string> return "a" end
local function many(): { literal<string> } return {} end
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    for (const TypeError& error : result.errors)
        CHECK_EQ(
            toString(error),
            "literal<T> only works in the annotation of a function parameter, a local, a table field or a type alias, or as the "
            "argument of a type function there (`name: Validate<literal<string>>`)"
        );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_through_type_aliases")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalTypes{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
type function NoDashes(alias: type): type
    if alias:is("singleton") and string.sub(alias:value() :: string, 1, 1) == "-" then
        return types.error(`no dashes: {alias:value()}`)
    end
    return alias
end

type Unprefixed = NoDashes<literal<string>>
type AnyLiteral = literal<string>

local function aliases(first: Unprefixed, ...: Unprefixed)
    local s: string = first
end
local function sym(n: AnyLiteral) end
type Fn = (first: Unprefixed) -> ()
local fn: Fn = nil :: any

aliases("f", "g")
aliases("-f", "--g")
sym("ok")
local str: string = "x"
sym(str)
fn("-x")
    )");

    REQUIRE_EQ(result.errors.size(), 4);
    CHECK_EQ(toString(result.errors[0]), "no dashes: -f");
    CHECK_EQ(toString(result.errors[1]), "no dashes: --g");
    CHECK_EQ(toString(result.errors[2]), "expected a literal of type string, but got string");
    CHECK_EQ(toString(result.errors[3]), "no dashes: -x");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_in_primary_constructors")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalTypes{FFlag::LuwuLiteralTypes, true};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
type function NoDashes(alias: type): type
    if alias:is("singleton") and string.sub(alias:value() :: string, 1, 1) == "-" then
        return types.error(`no dashes: {alias:value()}`)
    end
    return alias
end
type Unprefixed = NoDashes<literal<string>>

class Positional(public name: Unprefixed, public help: string) end
class Tagged(public tag: literal<string>) end

local p = Positional("file", "the file")
local name = p.name
Positional("-f", "dashes")
Tagged("x")
local s: string = "y"
Tagged(s)
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(toString(result.errors[0]), "no dashes: -f");
    CHECK_EQ(toString(result.errors[1]), "expected a literal of type string, but got string");
    CHECK_EQ("string", toString(requireType("name")));
}

// Luwu literal types: a function's own literal parameter, passed on unchanged, was checked where that function was called
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_forward")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalTypes{FFlag::LuwuLiteralTypes, true};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
type function NoDashes(alias: type): type
    return alias
end
type function Short(alias: type): type
    return alias
end
type Unprefixed = NoDashes<literal<string>>

class Positional(public name: Unprefixed) end

local function same(name: Unprefixed)
    return Positional(name)
end
local function bare(name: literal<string>)
    return Positional(name)
end
local function other(name: Short<literal<string>>)
    return Positional(name)
end
local function plainString(name: string)
    return Positional(name)
end
return { same = same, bare = bare, other = other, plainString = plainString }
    )");

    REQUIRE_EQ(result.errors.size(), 3);
    CHECK_EQ(
        toString(result.errors[0]),
        "'name' isn't checked by 'NoDashes'; annotate 'name' with 'NoDashes' (or an alias of it), or pass a literal here"
    );
    CHECK_EQ(
        toString(result.errors[1]),
        "'name' was checked by 'Short', not 'NoDashes'; annotate 'name' with 'NoDashes' (or an alias of it), or pass a literal here"
    );
    CHECK_EQ(toString(result.errors[2]), "expected a literal of type string, but got string");
}

// Luwu literal types: a cast to a literal type passes as a literal, unchecked, like a cast to `any`
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_casts_pass_as_literals")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalTypes{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
type function NoDashes(alias: type): type
    return types.error("never reached for a cast")
end
type Unprefixed = NoDashes<literal<string>>
local function named(name: Unprefixed) end
local function tag(t: literal<string>) end

local s: string = "file"
named(s :: literal<string>)
named(s :: Unprefixed)
tag((s :: literal<string>))
local l: literal<string> = s :: literal<string>
local back: string = s :: literal<string>
tag(s)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 14);
    CHECK_EQ(toString(result.errors[0]), "expected a literal of type string, but got string");
}

// Luwu: an argument that is an error was reported where it's written; the type function adds nothing about it
TEST_CASE_FIXTURE(BuiltinsFixture, "udtf_error_type_argument_reports_once")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
type function Id(t: type): type
    return t
end
local x: Id<Nope> = 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(toString(result.errors[0]), "Unknown type 'Nope'");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_yield_to_a_type_named_literal")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
type literal<T> = T
type function Id(s: type): type
    return s
end

local function f(key: Id<literal<string>>): string
    return key
end
local s: string = "x"
f(s)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_need_a_type_with_literals")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
type function Id(s: type): type
    return s
end

local function strings(a: Id<literal<string>>, b: Id<literal<boolean>>, c: Id<literal<string | false>>, d: Id<literal<"x" | "y">>) end
local function numbers(n: Id<literal<number>>) end
    )");

    REQUIRE_EQ(result.errors.size(), 1);
    CHECK_EQ(result.errors[0].location.begin.line, 6);
    CHECK_EQ(toString(result.errors[0]), "there are no literals of type number");
}

// Luwu literal types: a validator is part of the module's public interface, so a call from another module runs it
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_validators_from_another_module")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    fileResolver.source["game/A"] = R"(
type function HexColor(hex: type): type
    if hex:is("singleton") and string.sub(hex:value() :: string, 1, 1) == "#" then
        return hex
    end
    return types.error(`hex color is missing its '#'`)
end

class Color(r: number)
    function from_hex(hex: HexColor<literal<string>>): Color
        return Color(0)
    end
end

local function from_hex(hex: HexColor<literal<string>>) end
return { Color = Color, from_hex = from_hex }
    )";

    CheckResult result = check(R"(
local A = require(game.A)
local style = { Color = A.Color }
A.Color.from_hex("#ff")
A.Color.from_hex("ff")
style.Color.from_hex("ff")
A.from_hex("ff")
    )");

    REQUIRE_EQ(result.errors.size(), 3);
    for (size_t i = 0; i < result.errors.size(); ++i)
    {
        CHECK_EQ(result.errors[i].location.begin.line, i + 4);
        CHECK_EQ(toString(result.errors[i]), "hex color is missing its '#'");
    }
}

// Luwu literal types: `literal<B>?` may be left out or nil; a given argument is checked like `literal<B>`'s
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_can_be_optional")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(R"(
type function Short(s: type): type
    if s:is("singleton") and #(s:value() :: string) <= 2 then
        return s
    end
    return types.error("too long")
end

local function columns<A, B>(a: literal<A>, b: literal<B>?): B?
    return b
end
local function flag(short: Short<literal<string>>?) end

local named = columns("a", "b")
columns("a")
columns("a", nil)
flag()
flag("-v")
local s: string = "q"
columns("a", s)
flag("--verbose")
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(result.errors[0].location.begin.line, 19);
    CHECK_EQ(toString(result.errors[0]), "expected a literal of type string, but got string");
    CHECK_EQ(result.errors[1].location.begin.line, 20);
    CHECK_EQ(toString(result.errors[1]), "too long");
    CHECK_EQ(toString(requireType("named")), R"("b"?)");
}

// Luwu literal types: a nonstrict file knows `literal<B>` too
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_parameters_in_nonstrict_files")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(Mode::Nonstrict, R"(
local function name<N>(n: literal<N>) end
local function flag(short: literal<string>) end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu literal types: the CSS length validator the table field tests share
static const char* const kLengthValidator = R"(
type function Length(value: type): type
    if not value:is("singleton") then
        return types.error("expected a literal length")
    end
    local text = value:value()
    if typeof(text) ~= "string" or not string.match(text, "^%-?[%d%.]+[a-z%%]*$") then
        return types.error(`'{tostring(text)}' isn't a CSS length`)
    end
    return value
end
)";

// Luwu literal types: a `literal<B>` field is checked wherever a value flows into it. A cast passes, and so does a value
// already checked by the same validator; a table that wasn't checked against the field doesn't.
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_fields_are_checked_where_values_flow_in")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    CheckResult result = check(std::string(kLengthValidator) + R"(
type CssLength = Length<literal<string>>
type Props = { padding: CssLength?, margin: Length<literal<string>>? }
type Nested = { inner: Props }
local function rule(_props: Props) end
local function nested(_n: Nested) end

rule({ padding = "1rem", margin = "2px" })
rule({})
local p: CssLength = "3px"
local q: Length<literal<string>> = "3px"
local s: string = "1px"
rule({ padding = s :: CssLength })
rule({ padding = p, margin = q })
local checked: Props = { padding = "4px" }
rule(checked)
rule({ margin = checked.padding })
checked.padding = "6px"
nested({ inner = { padding = "9px" } })
local function make(): Props
    return { padding = "1px" }
end

rule({ padding = "1rx!" })
local bad: CssLength = "3pq!"
rule({ padding = s })
checked.padding = "6zz!"
nested({ inner = { padding = "9!" } })
local function remake(): Props
    return { margin = "x!" }
end
local raw: { padding: string } = { padding = "5px" }
rule(raw)
    )");

    // `rule(raw)` also gets the read/write mismatch for `padding`, which isn't optional in `raw`'s type
    REQUIRE_EQ(result.errors.size(), 9);
    const std::vector<std::pair<int, std::string>> expected = {
        {34, "'1rx!' isn't a CSS length"},
        {35, "'3pq!' isn't a CSS length"},
        {36, "expected a literal of type string, but got string"},
        {37, "'6zz!' isn't a CSS length"},
        {38, "'9!' isn't a CSS length"},
        {40, "'x!' isn't a CSS length"},
        {43,
         "'raw' wasn't checked against 'padding: Length<literal<string>>?'; pass a table literal here, or annotate 'raw' with a type "
         "that has that field"},
    };
    for (size_t i = 0; i < expected.size(); ++i)
    {
        CHECK_EQ(result.errors[i].location.begin.line, expected[i].first);
        CHECK_EQ(toString(result.errors[i]), expected[i].second);
    }
}

// Luwu literal types: a table type's literal fields are part of its module's public interface
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_fields_from_another_module")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    fileResolver.source["game/A"] = std::string(kLengthValidator) + R"(
export type CssLength = Length<literal<string>>
export type Props = { padding: CssLength?, margin: Length<literal<string>>? }
local function rule(props: Props): Props
    return props
end
return { rule = rule }
    )";

    CheckResult result = check(R"(
local A = require(game.A)
A.rule({ padding = "1px", margin = "2em" })
A.rule({ padding = "1p!" })
local p: A.Props = { margin = "q!" }
local l: A.CssLength = "z!"
    )");

    REQUIRE_EQ(result.errors.size(), 3);
    CHECK_EQ(result.errors[0].location.begin.line, 3);
    CHECK_EQ(toString(result.errors[0]), "'1p!' isn't a CSS length");
    CHECK_EQ(result.errors[1].location.begin.line, 4);
    CHECK_EQ(toString(result.errors[1]), "'q!' isn't a CSS length");
    CHECK_EQ(result.errors[2].location.begin.line, 5);
    CHECK_EQ(toString(result.errors[2]), "'z!' isn't a CSS length");
}

// Luwu literal types: a union with a literal member checks only what the rest of the union doesn't take
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_union_members")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    CheckResult result = check(std::string(kLengthValidator) + R"(
class Token(const name: string) end
type CssLength = Length<literal<string>>
type Props = { padding: (CssLength | Token | number)? }
local function rule(_props: Props) end
local function pad(_x: CssLength | number | "auto") end

rule({ padding = "1rem" })
rule({ padding = 0 })
rule({ padding = Token("x") })
rule({})
pad("2em")
pad(3)
pad("auto")

rule({ padding = "1q!" })
local s: string = "1px"
rule({ padding = s })
pad("bad!")
type Two = { x: CssLength | Length<literal<string>> }
    )");

    REQUIRE_EQ(result.errors.size(), 4);
    CHECK_EQ(result.errors[0].location.begin.line, 30);
    CHECK_EQ(toString(result.errors[0]), "a union can have only one literal<T> member");
    CHECK_EQ(result.errors[1].location.begin.line, 26);
    CHECK_EQ(toString(result.errors[1]), "'1q!' isn't a CSS length");
    CHECK_EQ(result.errors[2].location.begin.line, 28);
    CHECK_EQ(toString(result.errors[2]), "expected a literal of type string, but got string");
    CHECK_EQ(result.errors[3].location.begin.line, 29);
    CHECK_EQ(toString(result.errors[3]), "'bad!' isn't a CSS length");
}

// Luwu literal types: an alias of a union with a literal member checks like the union written out, `?` included
TEST_CASE_FIXTURE(BuiltinsFixture, "literal_union_aliases")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    fileResolver.source["game/A"] = std::string(kLengthValidator) + R"(
export type CssLength = Length<literal<string>>
return {}
    )";

    CheckResult result = check(std::string(kLengthValidator) + R"(
local A = require(game.A)
class Token(const css: string) end
type LengthU = A.CssLength | Token | number
type Direct = Length<literal<string>> | number
local function b(_p: { padding: LengthU? }) end
local function d(_x: LengthU) end
local function e(_x: Direct?) end

b({ padding = "1rem" })
b({ padding = 0 })
b({ padding = Token("x") })
b({})
d("1rem")
e(2)
e()

b({ padding = "1x!" })
d("1x!")
e("1x!")
type Bad = A.CssLength | Length<literal<string>>
    )");

    REQUIRE_EQ(result.errors.size(), 4);
    CHECK_EQ(result.errors[0].location.begin.line, 31);
    CHECK_EQ(toString(result.errors[0]), "a union can have only one literal<T> member");
    for (size_t i = 1; i < result.errors.size(); ++i)
    {
        CHECK_EQ(result.errors[i].location.begin.line, 27 + i);
        CHECK_EQ(toString(result.errors[i]), "'1x!' isn't a CSS length");
    }
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_literal_fields")
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};
    ScopedFastFlag literalParameters{FFlag::LuwuLiteralTypes, true};

    std::string before = std::string(kLengthValidator) + "type Props = { padding: Length<literal<string>>? }\nlocal function rule(_p: Props) end\n";
    const char* after = "\nlocal unrelated = 1\nprint(unrelated + 1)\n";
    checkTypewriting(*this, before, "rule({ padding = \"1rem\" })", after);
    checkTypewriting(*this, before, "local checked: Props = { padding = \"2px\" }", after);
    checkTypewriting(*this, before, "local function pad(_x: Length<literal<string>> | number) end", after);
}

TEST_SUITE_END();
