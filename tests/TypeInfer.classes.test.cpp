// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "ClassesFixture.h"
#include "Typewriter.h"
#include "Fixture.h"
#include "Typewriter.h"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/Error.h"
#include "Luau/StringUtils.h"
#include "ScopedFlags.h"
#include "Typewriter.h"
#include "doctest.h"

#include <algorithm>

using namespace Luau;

LUAU_FASTFLAG(LuauHigherOrderGenericInference)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuDestructuring)
LUAU_FASTFLAG(LuwuDefaultArguments)
LUAU_FASTFLAG(LuwuGenericNominals)
LUAU_FASTFLAG(LuauAllowGlobalDeclarationToBeCalledClass);
LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(LuauExportValueSyntax)
LUAU_FASTFLAG(LuauExportValueTypecheck)
LUAU_FASTFLAG(LuwuHelpfulSubtypingErrors)
LUAU_FASTFLAG(LuwuDeclareStatements)
LUAU_FASTFLAG(LuwuTraits)
LUAU_FASTINT(LuauSolverConstraintLimit)
LUAU_FASTINT(LuauTarjanChildLimit)
LUAU_FASTFLAG(DebugLuwuUserDefinedRefinements)
LUAU_FASTFLAG(LuauCstAttr)
LUAU_FASTFLAG(DebugLuwuCollections)
LUAU_FASTFLAG(DebugLuwuTryExpressions)
LUAU_FASTFLAG(LuwuNonePrimitive)


TEST_SUITE_BEGIN("TypeInferClasses");

TEST_CASE_FIXTURE(ClassesFixture, "Point_tostring")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    auto result = check(R"(
class Point
    x
    y
    function __tostring(self)
        return `Point(x={self.x}, y={self.y})`
    end
end

local p = Point { x = 1, y = 2 }
local _ = tostring(p)
    )");
    LUAU_REQUIRE_NO_ERRORS(result);
}


TEST_CASE_FIXTURE(ClassesFixture, "Point_eq_mm")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Point
    x
    y

    function __eq(self, other)
        return self.x == other.x and self.y == other.y
    end
    function zero()
        return Point { x = 0, y = 0 }
    end
end

local p1 = Point { x = 1, y = 2 }
local p2 = Point { x = 1, y = 2 }
local _ = p1 == p2
local _ = p1 ~= Point.zero()
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "Box_Point_no_eq")
{
    auto result = check(R"(
class Point
    public x
    public y
end


class Box
    public x
end

local p1 = Point { x = 1, y = 2 }
local p2 = Box { x = 1 }
local _ = p1 == p1
-- This one too
local _ = p1 ~= p2
local _ = Box == Box
-- This line should error...
local _ = Point ~= Box
)");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    auto e1 = get<CannotCompareUnrelatedTypes>(result.errors[0]);
    auto e2 = get<CannotCompareUnrelatedTypes>(result.errors[1]);
    REQUIRE(e1);
    REQUIRE(e2);

    CHECK(result.errors[0].location.begin.line == 15);
    CHECK(result.errors[1].location.begin.line == 18);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_mm")
{
    auto result = check(R"(
class Point
    function __add(self, other)
        return self
    end
end

local p = Point {}
p:__add()
)");
    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_structure")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Point
    x
    y

    function magnitude(self)
        return sqrt(self.x * self.x + self.y * self.y)
    end

    function zero()
        return Point { x = 0, y = 0 }
    end

    function __tostring(self)
        return `Point(x={self.x}, y={self.y})`
    end

end

local p = Point
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    auto t = requireType("p");
    auto et = get<ExternType>(t);
    REQUIRE(et);
    CHECK(et->parent == builtinTypes->classType);
    REQUIRE(et->metatable);

    CHECK(et->props().find("zero") != et->props().end());

    auto cobjmeta = get<TableType>(*et->metatable);
    REQUIRE(cobjmeta);
    auto& cobjMetaProps = cobjmeta->props;
    CHECK(cobjMetaProps.find("__call") != cobjmeta->props.end());
}

TEST_CASE_FIXTURE(ClassesFixture, "class_with_no_fields_can_be_constructed_with_no_arguments")
{
    auto result = check(R"(
class Empty
    function greet(self)
        return "hi"
    end
end

local e = Empty()
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_with_fields_still_requires_argument_table")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Person
    name
    age

    function greet(self)
        return self.name
    end
end

local p = Person()
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<CountMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_property_default_value_infers_type_from_default")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Cat
    name: string
    age = 0

    function __init(self, t: { name: string, age: number? })
        self.name = t.name
        self.age = t.age or self.age
    end
end

local cat = Cat { name = "Taz", age = 32 }
local a = cat.age
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("number", toString(requireType("a")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_property_with_type_annotation_takes_priority_over_default_value_type")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // If the annotation were ignored in favor of inferring from the default value, `label`'s type
    // would be the narrower `string` (from `"unnamed"`) instead of the annotated `string?`.
    auto result = check(R"(
class Widget
    label: string? = "unnamed"

    function __init(self) end
end

local w = Widget()
local l = w.label
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("string?", toString(requireType("l")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_property_default_value_incompatible_with_annotation_is_an_error")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Cat
    age: string = 4

    function __init(self) end
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_default_value_expressions_are_typechecked")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // field defaults and primary constructor parameter defaults are ordinary expressions, not just a
    // type to compare against an annotation
    auto result = check(R"(
--!strict
class Item(public name: string = make_name())
    private const id = next_id()
    public price: number = compute_price()

    public function describe(self) return self.id end
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    const char* expectedNames[] = {"make_name", "next_id", "compute_price"};
    for (size_t i = 0; i < 3; ++i)
    {
        const UnknownSymbol* err = get<UnknownSymbol>(result.errors[i]);
        REQUIRE(err);
        CHECK_EQ(expectedNames[i], err->name);
    }
}

TEST_CASE_FIXTURE(ClassesFixture, "class_pod_constructor_argument_optional_when_all_properties_have_defaults")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
local last_id = 0
class Id
    const current = (function()
        local old = last_id
        last_id += 1
        return old
    end)()

    function __tostring(self)
        return `ID<{self.current}>`
    end
end

local a = Id()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_pod_constructor_argument_still_required_when_any_property_lacks_a_default")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Mixed
    public a = 0
    public b: string

    public function greet(self)
        return self.b
    end
end

local m = Mixed()
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<CountMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_custom_init_constructor_signature")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public name: string
    public age: number

    public function __init(self, name: string, age: number)
        self.name = name
        self.age = age
    end
end

local p = Thingy
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    auto t = requireType("p");
    auto et = get<ExternType>(t);
    REQUIRE(et);
    REQUIRE(et->metatable);

    auto cobjmeta = get<TableType>(*et->metatable);
    REQUIRE(cobjmeta);
    auto callProp = cobjmeta->props.find("__call");
    REQUIRE(callProp != cobjmeta->props.end());
    REQUIRE(callProp->second.readTy);
    CHECK_EQ("(unknown, string, number) -> Thingy", toString(*callProp->second.readTy));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_custom_init_constructor_call_is_checked")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public name: string
    public age: number

    public function __init(self, name: string, age: number)
        self.name = name
        self.age = age
    end
end

local good = Thingy("hi", 5)
local missingArgs = Thingy()
local wrongTypes = Thingy(5, "hi")
local wrongShape = Thingy({ name = "hi", age = 5 })
    )");

    LUAU_REQUIRE_ERROR_COUNT(5, result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_private_init_can_be_called_from_a_factory_function")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public const cat: string

    private function __init(self, cat: string)
        self.cat = cat
    end

    public function new(cat: string): Thingy
        return Thingy(cat)
    end
end

local thing = Thingy.new("meow")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_private_init_cannot_be_called_directly_from_outside")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public const cat: string

    private function __init(self, cat: string)
        self.cat = cat
    end

    public function new(cat: string): Thingy
        return Thingy(cat)
    end
end

local thing = Thingy("meow")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<PrivateConstructorAccess>(result.errors[0]);
    REQUIRE(err);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_public_init_can_be_called_from_outside")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public cat: string

    public function __init(self, cat: string)
        self.cat = cat
    end
end

local thing = Thingy("meow")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_const_property_can_be_assigned_from_init")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public const name: string

    public function __init(self, name: string)
        self.name = name
    end
end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_const_property_cannot_be_assigned_from_other_methods")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public const name: string

    public function __init(self, name: string)
        self.name = name
    end

    public function rename(self, name: string)
        self.name = name
    end
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<ConstPropertyAssignment>(result.errors[0]);
    REQUIRE(err);
    CHECK_EQ("name", err->key);
    CHECK_EQ(
        "Field 'name' of class 'Thingy' is constant; assigning to it outside of '__init' will raise a runtime error", toString(result.errors[0])
    );
}

TEST_CASE_FIXTURE(ClassesFixture, "class_const_property_cannot_be_assigned_from_outside_the_class")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public const name: string

    public function __init(self, name: string)
        self.name = name
    end
end

local t = Thingy("hi")
t.name = "bye"
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<ConstPropertyAssignment>(result.errors[0]);
    REQUIRE(err);
    CHECK_EQ("name", err->key);
    CHECK_EQ(
        "Field 'name' of class 'Thingy' is constant; assigning to it outside of '__init' will raise a runtime error", toString(result.errors[0])
    );
}

TEST_CASE_FIXTURE(ClassesFixture, "class_non_const_property_can_be_assigned_anywhere")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
class Thingy
    public name: string

    public function __init(self, name: string)
        self.name = name
    end

    public function rename(self, name: string)
        self.name = name
    end
end

local t = Thingy("hi")
t.name = "bye"
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_generic_parameter_is_inferred_from_constructor")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    auto result = check(R"(
class Box<T>
    value: T

    function __init(self, value: T)
        self.value = value
    end

    function get(self): T
        return self.value
    end
end

local a = Box(5)
local b: number = a:get()

local c = Box("hi")
local d: string = c:get()

local e = Box(5)
local f: string = e:get()
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK_EQ("string", toString(tm->wantedType));
    CHECK_EQ("number", toString(tm->givenType));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_generic_parameter_default_is_used_when_omitted")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    auto result = check(R"(
class Box<T = string>
    value: T
end

local a: Box = Box { value = "hi" }
local b: Box<number> = Box { value = 1 }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Box<string>", toString(requireType("a")));
    CHECK_EQ("Box<number>", toString(requireType("b")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_generic_parameter_default_is_checked_against_annotation")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    auto result = check(R"(
class Box<T = string>
    value: T
end

local bad: Box = Box { value = 1 }
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK_EQ("Box<string>", toString(tm->wantedType));
    CHECK_EQ("Box<number>", toString(tm->givenType));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_generic_parameter_default_can_reference_earlier_parameter")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    auto result = check(R"(
class Pair<A, B = A>
    first: A
    second: B
end

local p: Pair<number> = Pair { first = 1, second = 2 }
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Pair<number, number>", toString(requireType("p")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_generic_parameter_without_default_still_requires_an_argument")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    auto result = check(R"(
class Pair<A, B = A>
    first: A
    second: B
end

type Bad = Pair
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<IncorrectGenericParameterCount>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_generic_default_reports_unknown_type")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    auto result = check(R"(
class Box<T = ThisTypeDoesNotExist>
    value: T
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<UnknownSymbol>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_duplicate_generic_parameter_is_reported")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    auto result = check(R"(
class Dup<T, T>
    value: T
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<DuplicateGenericParameter>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_unknown_value")
{
    ScopedFastFlag sff{FFlag::LuauIntegerType2, true};
    CheckResult result = check(R"(
class Point
    public x
end

local function f(v: unknown)
    if class.isinstance(v, Point) then
        local s = v
    else
        local s = v
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Point", toString(requireTypeAtPosition({7, 18})));
    CHECK_EQ(
        "((object & ~Point) | boolean | buffer | function | integer | none | number | string | table | thread)?", toString(requireTypeAtPosition({9, 18}))
    );
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_union_value")
{
    CheckResult result = check(R"(
class Point
    public x
end

local function f(v: Point | string)
    if class.isinstance(v, Point) then
        local s = v
    else
        local s = v
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Point", toString(requireTypeAtPosition({7, 18})));
    CHECK_EQ("string", toString(requireTypeAtPosition({9, 18})));
}

TEST_CASE_FIXTURE(ClassesFixture, "not_isinstance_refines_union")
{
    CheckResult result = check(R"(
class Point
    public x
end

local function f(v: Point | string)
    if not class.isinstance(v, Point) then
        local s = v
    else
        local s = v
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("string", toString(requireTypeAtPosition({7, 18})));
    CHECK_EQ("Point", toString(requireTypeAtPosition({9, 18})));
}

TEST_CASE_FIXTURE(ClassesFixture, "typeof_object_refines_to_object_arm")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat
    name = "Taz"
    function __init(self)
    end
end

local cat = Cat()
local x = cat :: Cat | { [string]: string }

if typeof(x) == "object" then
    local a = x
else
    local b = x
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Cat", toString(requireTypeAtPosition({11, 14})));
    CHECK_EQ("{ [string]: string }", toString(requireTypeAtPosition({13, 14})));
}

TEST_CASE_FIXTURE(ClassesFixture, "userdata_is_namable_and_narrows_via_typeof")
{
    // `userdata` is a writable type annotation (like `object`/`class`), and `typeof(x) == "..."`
    // narrows it to the matching extern datatype (a direct child of the userdata root).
    CheckResult result = check(R"(
local function f(x: userdata)
    if typeof(x) == "Duration" then
        local a = x
    else
        local b = x
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Duration", toString(requireTypeAtPosition({3, 18})));
    CHECK_EQ("userdata & ~Duration", toString(requireTypeAtPosition({5, 18})));
}

TEST_CASE_FIXTURE(ClassesFixture, "not_isinstance_refines_unknown")
{
    CheckResult result = check(R"(
class Point
    public x
end

local function f(v: unknown)
    if not class.isinstance(v, Point) then
        local s = v
    else
        local s = v
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Point", toString(requireTypeAtPosition({9, 18})));
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_unknown_elseif_chain")
{
    CheckResult result = check(R"(
class Num
    public value
end

class Var
    public name
end

class Add
    public left
    public right
end

local function f(node: unknown)
    if class.isinstance(node, Num) then
        local a = node
    elseif class.isinstance(node, Var) then
        local b = node
    elseif class.isinstance(node, Add) then
        local c = node
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Num", toString(requireTypeAtPosition({16, 18})));
    CHECK_EQ("Var", toString(requireTypeAtPosition({18, 18})));
    CHECK_EQ("Add", toString(requireTypeAtPosition({20, 18})));
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_union_elseif_chain")
{
    CheckResult result = check(R"(
class Num
    public value
end

class Var
    public name
end

class Add
    public left
    public right
end

local function f(node: Num | Var | Add)
    if class.isinstance(node, Num) then
        local a = node
    elseif class.isinstance(node, Var) then
        local b = node
    elseif class.isinstance(node, Add) then
        local c = node
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Num", toString(requireTypeAtPosition({16, 18})));
    CHECK_EQ("Var", toString(requireTypeAtPosition({18, 18})));
    CHECK_EQ("Add", toString(requireTypeAtPosition({20, 18})));
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_optional_property")
{
    CheckResult result = check(R"(
class Point
    public x
end

local function f(t: { x: Point? })
    if t.x and class.isinstance(t.x, Point) then
        local s = t.x
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Point", toString(requireTypeAtPosition({7, 20})));
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_property_already_typed")
{
    CheckResult result = check(R"(
class Point
    public x
end

local function f(t: { x: Point })
    if class.isinstance(t.x, Point) then
        local s = t.x
    end
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Point", toString(requireTypeAtPosition({7, 20})));
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_imported_class")
{
    ScopedFastFlag _[2]{{FFlag::LuauExportValueSyntax, true}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        export class Point
            public x: number
        end
    )";

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)

        local x : unknown = A.Point({} :: any)
        if class.isinstance(x, A.Point) then
            local y = x
        end
    )";
    CheckResult modB = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(modB);
    CHECK_EQ("Point", toString(requireTypeAtPosition("game/B", {5, 22})));
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_imported_class_but_not_a_class")
{
    ScopedFastFlag _[2]{{FFlag::LuauExportValueSyntax, true}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        export class Point
            public x: number
        end

        export notAPoint = nil
    )";

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)

        local x : unknown = A.Point({} :: any)
        if class.isinstance(x, A.notAPoint) then
            local y = x
        end
    )";
    CheckResult modA = getFrontend().check("game/A");
    CheckResult modB = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, modB);
    // Theres an unknown property on A.foo, but
    LUAU_REQUIRE_ERROR(modB, TypeMismatch);
    auto err = get<TypeMismatch>(modB.errors[0]);
    CHECK_EQ("class", toString(err->wantedType));
    CHECK_EQ("nil", toString(err->givenType));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_fields_on_instance_reports_precise_field_types_and_complete_true")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Point
    x: number
    y: number

    function magnitude(self)
        return sqrt(self.x * self.x + self.y * self.y)
    end
end

local p = Point { x = 1, y = 2 }
local fields, complete = class.fields(p)
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("{ read x: number, read y: number }", toString(requireType("fields")));
    CHECK_EQ("true", toString(requireType("complete")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_fields_omits_private_fields_from_the_type_and_reports_complete_false")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class User
    public first_name: string
    private ssn: string?

    public function __init(self, first_name, ssn)
        self.first_name = first_name
        self.ssn = ssn
    end
end

local u = User("Taz", "126-222-1123")
local fields, complete = class.fields(u)
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("{ read first_name: string }", toString(requireType("fields")));
    CHECK_EQ("false", toString(requireType("complete")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_fields_omits_methods_from_the_type")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Point
    x: number
    y: number

    function magnitude(self)
        return sqrt(self.x * self.x + self.y * self.y)
    end
end

local p = Point { x = 1, y = 2 }
local fields = class.fields(p)
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("{ read x: number, read y: number }", toString(requireType("fields")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_fields_also_works_on_the_class_itself_not_just_an_instance")
{
    CheckResult result = check(R"(
class Point
    public x: number
    public y: number
end

local fields, complete = class.fields(Point)
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("{ read x: number, read y: number }", toString(requireType("fields")));
    CHECK_EQ("true", toString(requireType("complete")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_name_of_a_class_or_object_is_its_name_as_a_singleton")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat
    name: string
end

local c = Cat { name = "Taz" }
local ofObject = class.name(c)
local ofClass = class.name(Cat)
local annotated: "Cat" = class.name(c)
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("\"Cat\"", toString(requireType("ofObject")));
    CHECK_EQ("\"Cat\"", toString(requireType("ofClass")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_name_of_a_union_of_objects_is_a_union_of_singletons")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat end
class Dog end
class Walrus end

local function nameOf(pet: Cat | Dog | Walrus)
    return class.name(pet)
end

local n = nameOf(Dog())
local exhaustive: "Cat" | "Dog" | "Walrus" = n
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("\"Cat\" | \"Dog\" | \"Walrus\"", toString(requireType("n")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_name_collapses_a_class_and_its_objects_to_one_singleton")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat end
class Dog end

local function nameOf(x: Cat | class<Cat> | Dog)
    return class.name(x)
end

local n = nameOf(Cat)
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("\"Cat\" | \"Dog\"", toString(requireType("n")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_name_collapses_instantiations_of_a_generic_class")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    CheckResult result = check(R"(
class Box<T>
    value: T
end

local function nameOf(b: Box<number> | Box<string>)
    return class.name(b)
end

local n = nameOf(Box { value = 1 })
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("\"Box\"", toString(requireType("n")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_name_falls_back_to_string_when_the_class_is_not_known")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat end

local function ofAnyObject(o: object)
    return class.name(o)
end

local function ofAnyClass(c: class)
    return class.name(c)
end

local function ofOptional(c: Cat?)
    return class.name(c)
end

local a = ofAnyObject(Cat())
local b = ofAnyClass(Cat)
)");

    CHECK_EQ("string", toString(requireType("a")));
    CHECK_EQ("string", toString(requireType("b")));
    // `Cat?` is not a class or object, so the call is an error, but the magic function still leaves
    // the declared return type in place rather than claiming a singleton
    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(ClassesFixture, "typed_self_parameter_after_class_declaration")
{
    // Annotations on the self parameter are forbidden, but we still have to
    // parse this without crashing.
    CheckResult result = check(R"(
        class Q
            function f(self: number) end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    auto e0 = get<SyntaxError>(result.errors[0]);
    REQUIRE(e0);
    CHECK("The 'self' parameter cannot have a type annotation" == e0->message);

    auto e1 = get<TypeMismatch>(result.errors[1]);
    REQUIRE(e1);
    CHECK("number" == toString(e1->wantedType));
    CHECK("Q" == toString(e1->givenType));
}

TEST_CASE_FIXTURE(ClassesFixture, "typeof_class_prop_ice")
{
    LUAU_REQUIRE_NO_ERRORS(check(R"(
        local x = 1
        class Foo
            public bar: typeof(x)
        end
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "typeof_indexing_ice_in_class_prop_typeof")
{
    CheckResult results = check(R"(
local A = ""
class B
    public C: { _: typeof(A.D) }
end
    )");
    LUAU_REQUIRE_ERROR_COUNT(1, results);
    auto err = get<UnknownProperty>(results.errors[0]);
    REQUIRE(err);
    CHECK_EQ("D", err->key);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_refers_to_later_type_alias")
{
    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Foo
            public bar: BarType
        end

        type BarType = number | string

        local function getbar(f: Foo)
            return f.bar
        end
    )"));

    // BarType is a named type alias referenced (not expanded) in the return position.
    CHECK_EQ("(Foo) -> BarType", toString(requireType("getbar")));
}

TEST_CASE_FIXTURE(ClassesFixture, "accept_read_only_tables")
{
    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Foo
            public bar: number | string
        end

        local function ofnumbertbl(tbl: { bar: number })
            return Foo(tbl)
        end

        local function inference(tbl)
            return Foo(tbl)
        end
    )"));

    CHECK_EQ("({ bar: number }) -> Foo", toString(requireType("ofnumbertbl")));
    CHECK_EQ("({ read bar: number | string }) -> Foo", toString(requireType("inference")));
}


// Primary constructors (rfcs/classes): each parameter declares a public field, and the class is
// constructed positionally through the `__init` the parameter list implies.

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_declares_a_field_per_parameter")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Cat(name: string, age: number)
            function describe(self)
                return self.name
            end
        end

        local cat = Cat("Taz", 14)
        local name = cat.name
        local age = cat.age
    )"));

    CHECK_EQ("string", toString(requireType("name")));
    CHECK_EQ("number", toString(requireType("age")));
    CHECK_EQ("Cat", toString(requireType("cat")));
}

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_argument_types_are_checked")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    auto result = check(R"(
        class Cat(name: string, age: number)
        end

        local cat = Cat(12, 14)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_arity_is_checked")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    auto result = check(R"(
        class Cat(name: string, age: number)
        end

        local cat = Cat("Taz")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<CountMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_parameter_defaults")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Percentage(current: number, total = 100)
            value = (current / total) * 100
        end

        local a = Percentage(27, 42)
        local b = Percentage(27)
        local total = a.total
        local value = a.value
    )"));

    // a parameter with a default may be omitted at the call site, but the field it declares is never
    // nil -- the default fills it in
    CHECK_EQ("number", toString(requireType("total")));
    CHECK_EQ("number", toString(requireType("value")));
}

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_parameter_default_is_checked_against_its_annotation")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    auto result = check(R"(
        class Percentage(current: number, total: number = "one hundred")
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_parameters_are_visible_to_field_initializers")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Frame(public name: string, public size: number)
            public doubled = size * 2
            private label = name
        end

        local f = Frame("main", 10)
        local doubled = f.doubled
    )"));

    CHECK_EQ("number", toString(requireType("doubled")));
}

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_parameters_are_not_visible_to_methods")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // methods read the field through `self`; the parameter itself isn't in scope there
    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Symbol(public name: string)
            public function describe(self): string
                return self.name
            end
        end
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "qualified_parameters_declare_their_fields_access_and_constness")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // a parameter may carry its field's access specifier and `const` modifier directly, instead of
    // restating the field in the class body (rfcs/classes)
    auto result = check(R"(
        class SshKey(public const public_key: string, private const private_key: string)
            public function fingerprint(self): string
                return self.private_key
            end
        end

        local key = SshKey("pub", "priv")
        local leaked = key.private_key
        key.public_key = "other"
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<PrivatePropertyAccess>(result.errors[0]));
    CHECK(get<ConstPropertyAssignment>(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "bare_restatement_takes_the_parameters_type")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    auto result = check(R"(
        class Card(public userid: number, hash: string)
            private const hash

            public function same(self, other: Card): boolean
                return self.hash == other.hash
            end
        end

        local card = Card(1, "abc")
        local leaked = card.hash
    )");

    // the field is private, so reading it from outside the class is the only error here
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<PrivatePropertyAccess>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "field_that_can_never_be_initialized")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    auto result = check(R"(
        class Bottle()
            brand = "Coke"
            top: string
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<UninitializableClassField>(result.errors[0]);
    REQUIRE(err);
    CHECK_EQ("top", err->key);
    CHECK_EQ(
        "Field 'top' will always be initialized to `nil` but is not marked as optional; consider providing a default field value, adding a "
        "class parameter of the same name, or marking the field as optional with `?`",
        toString(result.errors[0])
    );
}

TEST_CASE_FIXTURE(ClassesFixture, "class_with_only_private_fields_and_no_functions_is_unusable")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    auto result = check(R"(
        class UseMe
            private please: string
            private uses: number
        end

        class Secret(private key: string) end

        class Card(hash: string)
            private const hash
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    for (const TypeError& err : result.errors)
        CHECK(get<UnusableClass>(err));
    CHECK_EQ("This class cannot be used because it only has private fields", toString(result.errors[0]));
    CHECK_EQ(1, result.errors[0].location.begin.line);
    CHECK_EQ(6, result.errors[1].location.begin.line);
    CHECK_EQ(8, result.errors[2].location.begin.line);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_with_a_private_constructor_it_never_calls_is_uninstantiable")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    auto result = check(R"(
        class Primary private (public name: string)
            public function hi(self): string
                return self.name
            end
        end

        class Init
            public name: string
            private function __init(self, name: string)
                self.name = name
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    for (const TypeError& err : result.errors)
        CHECK(get<UninstantiableClass>(err));
    CHECK_EQ(
        "This class can never be instantiated because its constructor is private and is never called; did you mean to return an instance "
        "of this class from a `public function` instead? Call the constructor to silence",
        toString(result.errors[0])
    );
    CHECK_EQ(1, result.errors[0].location.begin.line);
    CHECK_EQ(7, result.errors[1].location.begin.line);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_with_a_private_constructor_called_from_its_body_is_instantiable")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Factory private (public name: string)
            public function make(): Factory
                return Factory("x")
            end
        end

        class Nested private (public name: string)
            public function maker(): () -> Nested
                return function()
                    return Nested("x")
                end
            end
        end

        class Pod
            public name: string = ""
            private function __init(self)
            end

            public function make(): Pod
                return Pod()
            end
        end
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_argument_count_excludes_the_class")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // the constructor is the class's `__call`, which receives the class first; the count reports the call as written
    auto result = check(R"(
        class Point(public x: number, public y: number) end
        local _ = Point(1)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Argument count mismatch. Function expects 2 arguments, but only 1 is specified", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_with_private_fields_is_usable_through_a_function_or_a_public_field")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Empty end

        class WithFunction
            private please: string
            public function get(self): string
                return self.please
            end
        end

        class WithMetamethod
            private please: string
            public function __tostring(self): string
                return self.please
            end
        end

        class WithPublicField
            private please: string
            public uses: number
        end

        class WithPublicParam(public name: string, private key: string) end
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "private_member_access_error_names_fields_and_functions")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    auto result = check(R"(
        class User
            private ssn: string
            public function new(ssn: string): User
                return User { ssn = ssn }
            end
            private function terminate(self) end
        end

        local user = User.new("126-222-1123")
        local leaked = user.ssn
        user:terminate()
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);

    auto field = get<PrivatePropertyAccess>(result.errors[0]);
    REQUIRE(field);
    CHECK_FALSE(field->isFunction);
    CHECK_EQ("Field 'ssn' of class 'User' is private; accessing it here will raise a runtime error", toString(result.errors[0]));

    auto function = get<PrivatePropertyAccess>(result.errors[1]);
    REQUIRE(function);
    CHECK(function->isFunction);
    CHECK_EQ("Function 'terminate' of class 'User' is private; calling it here will raise a runtime error", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "field_that_can_never_be_initialized_is_fine_when_optional")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // ...and a field a parameter names, or one with a default, is initialized after all
    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Bottle(size: number)
            brand = "Coke"
            top: string?
            size: number
        end
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "a_class_with_a_table_constructor_may_leave_fields_uninitialized")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // the rule is specific to primary constructors: a POD class's table constructor can still supply
    // the field
    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Bottle
            top: string
        end

        local b = Bottle { top = "cap" }
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "private_primary_constructor")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    auto result = check(R"(
        class Account private (public holder: string)
            public function open(h: string): Account
                return Account(h)
            end
        end

        local a = Account("taz")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<PrivateConstructorAccess>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_table_argument_is_positional")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // a primary constructor takes away the table constructor: the table is just argument one
    auto result = check(R"(
        class Package(owner: string, contents: number)
        end

        local pkg = Package { owner = "x", contents = 1 }
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
    CHECK(get<CountMismatch>(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_with_a_primary_constructor")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
        {FFlag::LuwuGenericNominals, true},
    };

    auto result = check(R"(
        class Box<T>(public inner: T)
            public function get(self): T
                return self.inner
            end
        end

        local box = Box(5)
        local unwrapped: number = box:get()
        local mistyped: string = box:get()
    )");

    // the class's generic is inferred from the constructor argument, so `get` returns a number here
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
    CHECK_EQ("Box<number>", toString(requireType("box")));
}

TEST_CASE_FIXTURE(ClassesFixture, "bare_restatement_is_checked_against_the_parameters_type")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // `private breed: number` still initializes the field from the parameter -- the `= breed` is
    // implicit, not absent -- so the parameter's type has to fit the annotation
    auto result = check(R"(
        type CatBreed = "Orange" | "AmericanShorthair" | "Void"

        class Cat(public name: string, breed: CatBreed = "AmericanShorthair")
            private breed: number

            public function describe(self): string
                return self.name
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "bare_restatement_with_a_compatible_annotation")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // widening the annotation is fine; the parameter's type still fits
    LUAU_REQUIRE_NO_ERRORS(check(R"(
        class Cat(name: string, age: number)
            private const name: string
            private age: number | string

            public function getName(self): string
                return self.name
            end
        end
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "missing_key_error_names_the_class_or_object_not_an_external_type")
{
    CheckResult result = check(R"(
        class Dog
            public name: string
        end

        local d = Dog { name = "rex" }
        local a = d.age
        local b = Dog.age
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("Member 'age' not found in object 'Dog'", toString(result.errors[0]));
    CHECK_EQ("Member 'age' not found in class 'Dog'", toString(result.errors[1]));
}

// A field read off its class instead of an object gets the runtime's explanation as help, under the short message
// A trait value against an object typed by that trait reads like the class version, in words
TEST_CASE_FIXTURE(ClassesFixture, "type_errors_tell_a_trait_value_from_its_objects")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag helpful{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
trait Colored
    function color(self): string
        return "default"
    end
end

class Paint implements Colored
end

local function paint(c: Colored, t: trait<Colored>)
    local shape: trait<Colored> = c
    local colored: Colored = t
end
)");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("Expected 'c' to be the trait 'Colored', but got an object that implements 'Colored' instead", toString(result.errors[0]));
    CHECK_EQ("Expected 't' to be an object that implements 'Colored', but got the trait 'Colored' itself", toString(result.errors[1]));
}

// `object<C>` is the inverse of `class<T>`: an object type is its own `object<...>`, so the two round-trip
TEST_CASE_FIXTURE(ClassesFixture, "object_of_a_class_value_round_trips")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
trait Colored
    function color(self): string
        return "default"
    end
end

class Dog end
class Cat end

type RoundTrip = object<class<object<Dog>>>
type FromUnion = object<class<Cat> | class<Dog>>
type FromTrait = object<trait<Colored>>
type FromAnyClass = object<class>
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Dog", toString(requireTypeAlias("RoundTrip")));
    CHECK_EQ("Cat | Dog", toString(requireTypeAlias("FromUnion")));
    CHECK_EQ("Colored", toString(requireTypeAlias("FromTrait")));
    CHECK_EQ("object", toString(requireTypeAlias("FromAnyClass")));
}

// A generic over class values can name the objects they make
TEST_CASE_FIXTURE(ClassesFixture, "object_of_a_generic_class_value_types_a_factory")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag helpful{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
class Paint
    public gloss: number = 0
end

local function make<C>(cls: C): object<C>
    return (cls :: any)()
end

local made = make(Paint)
local wrong: string = made.gloss
)");

    // `made` is a `Paint`, so its `gloss` is a number
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Expected 'made.gloss' to be 'string', but was given 'number'", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "object_of_something_that_makes_no_objects_is_an_error")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
type Bad = object<number>
type Empty = object<>
)");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("object<C> requires exactly one type argument, a class or trait value", toString(result.errors[0]));
    CHECK_EQ("Type function instance object<number> is uninhabited", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "field_read_off_its_class_has_help")
{
    const char* source = R"(
        class Paint
            public gloss: number = 0
        end

        local g = Paint.gloss
    )";

    CheckResult result = check(source);
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        "Member 'gloss' not found in class 'Paint'\n\n"
        "Help (object field):\n"
        "  • Field 'gloss' is a member of objects of class 'Paint'; did you mean to access it on an object of 'Paint' instead of "
        "the class?",
        toString(result.errors[0])
    );

    getFrontend().options.showHelp = false;
    result = check(source);
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Member 'gloss' not found in class 'Paint'", toString(result.errors[0]));
}

// A compound assignment checks its target as a write and as a read; the typo is reported once.
TEST_CASE_FIXTURE(ClassesFixture, "misspelled_field_is_one_typo")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        class Player(name: string, level: number)
            function promote(self)
                self.levle += 1
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<Typo>(result.errors[0]));
    CHECK_EQ(toString(result.errors[0]), "Did you mean 'self.level' instead of 'self.levle'?");
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_instantiated_from_inside_its_own_body")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // `Box<number>` here is resolved while `Box`'s own members are still unsolved, so the
    // instantiation has to wait for them rather than sharing them uninstantiated.
    LUAU_REQUIRE_NO_ERRORS(check(R"(
class Box<T>
    public value: T
    public function makenum(v: number): Box<number>
        return Box { value = v }
    end
    public function get(self): T
        return self.value
    end
end

local b = Box.makenum(1)
local n: number = b:get()
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_instantiated_by_a_forward_reference")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // Same as above, but the unsolved class is a different one, declared further down the file.
    LUAU_REQUIRE_NO_ERRORS(check(R"(
class Box<T>
    public value: T
    public function mk(v: number): Other<number>
        return Other { v = v }
    end
end

class Other<U>
    public v: U
    public function get(self): U
        return self.v
    end
end

local o = Box.mk(1)
local n: number = o:get()
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_instantiated_through_a_static_method_generic")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // `Box<N>` is resolved inside the body and then instantiated again at the call site, so both
    // substitutions have to land on the members.
    LUAU_REQUIRE_NO_ERRORS(check(R"(
class Box<T>
    public value: T
    public function with<N>(v: N): Box<N>
        return Box { value = v }
    end
    public function get(self): T
        return self.value
    end
end

local b = Box.with<<number>>(1)
local n: number = b:get()
    )"));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_referenced_after_a_branch_that_also_references_it")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // Regression test. A class is referenced as a global, and the join at the end of the `if` produces a
    // phi def that nothing binds a type to. `prepopulateGlobalScope` maps every global reference's def onto
    // its binding up front, but it runs before the class prepass creates the class's binding. So every
    // reference to the class after the branch silently came back as *error-type*.
    //
    // *error-type* swallows real errors instead of reporting new ones. That is why the annotations here
    // are deliberately wrong: they are the errors the bug made disappear.
    auto result = check(R"(
class Prefix(prefix: string)
    function is_dot(self)
        return self.prefix == "."
    end
end

local function parse(s: string)
    if s == "./" then
        local inside: number = Prefix(".")
    end
    local after: number = Prefix("..")
    return after
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    for (size_t i = 0; i < 2; ++i)
    {
        const TypeMismatch* err = get<TypeMismatch>(result.errors[i]);
        REQUIRE(err);
        CHECK_EQ("Prefix", toString(err->givenType));
    }
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_function_calling_itself_in_a_loop_with_classes_enabled")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuClasses, true},
    };

    // Regression test for the class fallback exercised by the test above, which resolves a global read
    // through a phi def to the global's binding. A type function that calls itself in a loop reads its own
    // name through the loop's phi. The fallback handed back the type function's binding, so the call saw
    // the function's own, still unsolved type. Every call made with the result then failed with
    // "outstanding free or blocked type in function call". Definition files using this pattern
    // (PhoenixEngine's `phoenix.d.luau`) failed to load in luwu-lsp, which always enables classes.
    auto result = check(R"(
type function flatten(un: type): { type }
    local results = {}
    for _, inner in flatten(un) do
        table.insert(results, inner)
    end
    return results
end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "constructor_argument_that_is_itself_a_constructor_call")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // Construction dispatches through the class's `__call` metamethod. A final argument that is itself a
    // call contributes a type *pack*. A pack skips the per-argument check in TypeChecker2, so overload
    // resolution is the only thing that checks it.
    //
    // Regression test: the pack the resolver tests has the forwarded callee at the front, but its report
    // was indexed one slot short of that pack. For a one-argument constructor the report resolved against
    // nothing, and the mismatch went unreported. Every other spelling of the same wrong argument (a
    // literal, a local, a parenthesized call) was caught, which is why the bug stayed hidden.
    auto result = check(R"(
class Comp(x: string) end
class Other(y: string) end
class Path(inner: Comp) end

local viaCall = Path(Other("x"))
local viaLocal = Path((nil :: any) :: Other)
local viaParens = Path((Other("x")))
local fine = Path(Comp("x"))
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    for (size_t i = 0; i < 3; ++i)
    {
        const TypeMismatch* err = get<TypeMismatch>(result.errors[i]);
        REQUIRE(err);
        CHECK_EQ("Comp", toString(err->wantedType));
        CHECK_EQ("Other", toString(err->givenType));
    }
}

TEST_CASE_FIXTURE(ClassesFixture, "class_value_type_and_object_type_are_not_interchangeable")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // A class declaration builds two extern types from the same AstStatClass, so they share name,
    // definition module and definition location; only their nominal root tells them apart. Without
    // that check the nominal comparison in Type.cpp treats them as the same type.
    CheckResult result = check(R"(
class Cat
    name: string
end

local inst = Cat { name = "a" }
local objIntoClass: class<Cat> = inst
local classIntoObj: Cat = Cat
)");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
    CHECK(get<TypeMismatch>(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_type_function_names_the_class_value")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat
    name: string
    function make(n: string): Cat
        return Cat { name = n }
    end
end

local C: class<Cat> = Cat
local made = C.make("x")
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Cat", toString(requireType("made")));
}

TEST_CASE_FIXTURE(ClassesFixture, "bare_class_is_still_the_top_type_of_all_classes")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // Adding the applied form `class<T>` must not disturb the zero-parameter `class` binding.
    CheckResult result = check(R"(
class Cat end
class Dog end

local a: class = Cat
local b: class = Dog
local c: class<Cat> = Cat
local d: class = c
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_type_function_rejects_a_non_class_argument")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
type NotAClass = { x: number }
local a: class<number>
local b: class<NotAClass>
)");

    REQUIRE(!result.errors.empty());
    CHECK_EQ(
        "Type 'number' is not the object type of a class, so 'class<number>' is invalid",
        toString(result.errors[0])
    );
}

TEST_CASE_FIXTURE(ClassesFixture, "class_type_function_works_on_a_generic_class")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    CheckResult result = check(R"(
class List<T>
    inner: { T }
end

local L: class<List<number>> = List
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "typeof_of_a_class_suggests_the_class_type_function")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat
    name: string
end

local c: typeof(Cat) = Cat
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<GenericError>(result.errors[0]);
    REQUIRE(err);
    CHECK_EQ("Use 'class<Cat>' instead of 'typeof(Cat)' to get the class of 'Cat'", err->message);
}

TEST_CASE_FIXTURE(ClassesFixture, "typeof_of_an_object_is_still_allowed")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // Only a class value earns the suggestion; `typeof` of an instance is an ordinary object type.
    CheckResult result = check(R"(
class Cat
    name: string
end

local inst = Cat { name = "a" }
local same: typeof(inst) = inst
)");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Cat", toString(requireType("same")));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_of_the_object_top_type_is_the_class_top_type")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // `object` is the top of the object lattice, so the class that produced such a value could be
    // any class: the top of the class lattice.
    CheckResult result = check(R"(
class Cat end
class Dog end

local a: class<object> = Cat
local b: class<object> = Dog
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_of_the_object_top_type_still_rejects_an_object")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat
    name: string
end

local inst = Cat { name = "a" }
local a: class<object> = inst
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<TypeMismatch>(result.errors[0]);
    REQUIRE(err);
    CHECK_EQ("class", toString(err->wantedType));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_distributes_over_a_union")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Cat end
class Dog end
class Walrus end

local ok: class<Cat | Dog> = Cat
local bad: class<Cat | Dog> = Walrus
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<TypeMismatch>(result.errors[0]);
    REQUIRE(err);
    CHECK_EQ("Cat | Dog", toString(err->wantedType));
    CHECK_EQ("Walrus", toString(err->givenType));
}

// A class value and its objects' type both print as `Paint`. Type errors tell them apart: an object passed as its own
// class (or the reverse) is said in words, and any other mismatch prints a class value as `class<Paint>`.
TEST_CASE_FIXTURE(ClassesFixture, "type_errors_tell_a_class_value_from_its_objects")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag helpful{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
class Paint end
class Ink end

local paint = Paint
local wrongClass: class<Paint> = Ink
local objectForClass: class<Paint> = Ink()
local ownObject: class<Paint> = Paint()
local classForObject: Paint = Paint
)");

    CHECK_EQ("Paint", toString(requireType("paint")));

    LUAU_REQUIRE_ERROR_COUNT(4, result);
    CHECK_EQ("Expected 'wrongClass' to be 'class<Paint>', but got 'class<Ink>'", toString(result.errors[0]));
    CHECK_EQ("Expected 'objectForClass' to be 'class<Paint>', but got 'Ink'", toString(result.errors[1]));
    CHECK_EQ("Expected 'ownObject' to be the class 'Paint', but got an object of class 'Paint' instead", toString(result.errors[2]));
    CHECK_EQ("Expected 'classForObject' to be an object of class 'Paint', but got the class 'Paint' itself", toString(result.errors[3]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_of_an_uninhabited_object_type_is_never")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Item end

local a: class<never> = nil :: any
local bad: class<never> = Item
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<TypeMismatch>(result.errors[0]);
    REQUIRE(err);
    CHECK_EQ("never", toString(err->wantedType));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_still_rejects_unknown")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // `unknown` is not an object type, so it stays a user error rather than reducing to the top.
    CheckResult result = check(R"(
local a: class<unknown>
)");

    REQUIRE(!result.errors.empty());
    CHECK_EQ(
        "Type 'unknown' is not the object type of a class, so 'class<unknown>' is invalid",
        toString(result.errors[0])
    );
}

TEST_CASE_FIXTURE(ClassesFixture, "class_object_mismatch_says_which_side_is_the_class")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS();
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // Both extern types stringify as the bare name, which used to produce the long-standing
    // "Expected this to be 'Item' from '<file>', but got 'Item' from '<file>'".
    CheckResult result = check(R"(
class Item
    name: string
end

local inst = Item { name = "a" }
local a: class<Item> = inst
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Expected this to be the class 'Item', but got an object of class 'Item' instead", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "object_class_mismatch_says_which_side_is_the_class")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS();
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Item
    name: string
end

local a: Item = Item
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Expected this to be an object of class 'Item', but got the class 'Item' itself", toString(result.errors[0]));
}

// The class's lines in its own module say nothing about the same lines of another module.
TEST_CASE_FIXTURE(ClassesFixture, "private_checks_compare_modules_not_just_positions")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
    };

    fileResolver.source["game/A"] = R"(
        export class Secret
            private key: string
            public const id: number
            private function __init(self, k: string)
                self.key = k
                self.id = 1
            end
            public function make(k: string): Secret return Secret(k) end
        end
    )";

    // Every access below sits inside `Secret`'s line range in game/A.
    fileResolver.source["game/B"] = R"(
        local A = require(game.A)
        local s = A.Secret.make("x")
        local k = s.key
        local t = A.Secret("y")
        s.id = 2
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK(get<PrivatePropertyAccess>(result.errors[0]));
    CHECK(get<PrivateConstructorAccess>(result.errors[1]));
    CHECK(get<ConstPropertyAssignment>(result.errors[2]));
}

TEST_CASE_FIXTURE(ClassesFixture, "const_fields_are_assigned_only_by_init_itself_on_its_own_self")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // The runtime authorizes the running closure, which must be `__init` itself, and only for the
    // object in its `self`.
    CheckResult result = check(R"(
        class K
            public const id: number
            public function __init(self, id: number)
                self.id = id
                local function later() self.id = 3 end
                later()
                local other = K(1)
                other.id = 4
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    REQUIRE(get<ConstPropertyAssignment>(result.errors[0]));
    CHECK_EQ(result.errors[0].location.begin.line, 5);
    REQUIRE(get<ConstPropertyAssignment>(result.errors[1]));
    CHECK_EQ(result.errors[1].location.begin.line, 8);
}

TEST_CASE_FIXTURE(ClassesFixture, "const_field_written_through_an_alias_of_self_in_init")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // The runtime checks the object, not the register holding it, so `__init` may write a const field
    // through a local that always holds `self`. A local that is ever reassigned might not, so it is
    // still reported.
    CheckResult result = check(R"(
        class K
            public const id: number
            public const tag: string
            public function __init(self, id: number)
                local s = self
                local t = s
                s.id = id
                t.tag = "k"
                local u = self
                u.id = 2
                u = K(1)
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<ConstPropertyAssignment>(result.errors[0]));
    CHECK_EQ(result.errors[0].location.begin.line, 10);
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_referring_to_itself_with_a_wrapped_parameter_is_reported")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // Each expansion of `A<{T}>` mentions a new instantiation, so it can't be expanded eagerly.
    CheckResult result = check(R"(
        class A<T>
            v: T
            nest: A<{T}>?
        end
        local a: A<number> = A { v = 1, nest = nil }
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<RecursiveRestraintViolation>(result.errors[0]));
    CHECK_EQ(result.errors[0].location.begin.line, 3);
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_referring_to_itself_with_other_arguments_expands")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // Finitely many instantiations: another argument, or the parameters in another order.
    CheckResult result = check(R"(
        class List<T>
            inner: {T}
            other: List<string>?
        end
        class Pair<A, B>
            a: A
            b: B
            flipped: Pair<B, A>?
        end
        local l: List<number> = List { inner = {1}, other = nil }
        local o = l.other
        local p: Pair<number, string> = Pair { a = 1, b = "x", flipped = nil }
        local f = p.flipped
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("List<string>?", toString(requireType("o")));
    CHECK_EQ("Pair<string, number>?", toString(requireType("f")));
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_member_display_arguments_are_expanded")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // `R` never uses its parameter, so `A<T>` survives only in `R<A<T>>`'s type arguments.
    CheckResult result = check(R"(
        type R<T> = { R<T> }
        class A<T>
            r: R<A<T>>
        end
        local a: A<string> = A { r = {} }
        local r = a.r
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("R<A<string>>", toString(requireType("r")));
}

TEST_CASE_FIXTURE(ClassesFixture, "reading_init_by_name_is_reported")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
        class Cat
            public name: string
            public function __init(self, name: string)
                self.name = name
            end
            public function reset(self)
                self:__init("x")
            end
        end
        class Dog
            public name: string
        end
        local c = Cat("a")
        local a = Cat.__init
        local b = c.__init
        local d = Cat["__init"]
        local e = Dog.__init
    )");

    LUAU_REQUIRE_ERROR_COUNT(5, result);
    const size_t lines[] = {7, 14, 15, 16, 17};
    for (size_t i = 0; i < 5; ++i)
    {
        const ConstructorReadByName* err = get<ConstructorReadByName>(result.errors[i]);
        REQUIRE(err);
        CHECK_EQ(result.errors[i].location.begin.line, lines[i]);
    }
    CHECK_EQ(
        "Cannot read '__init' of class 'Dog'; constructing the class with 'Dog(...)' is the only way to run it, and reading it here will raise "
        "a runtime error",
        toString(result.errors[4])
    );
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_instantiated_with_any_is_compatible")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    CheckResult result = check(R"(
        class A<T>
            v: T
        end
        class P<T, U>
            t: T
            u: U
        end
        local function f(x: A<any>, y: A<string>)
            local s: A<string> = x
            local t: A<any> = y
        end
        local a: A<string> = A { v = nil :: any }
        local function g(z: A<number>, p: P<any, number>)
            local w: A<string> = z
            local q: P<string, string> = p
        end
    )");

    // `any` suppresses only its own argument's mismatch.
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    REQUIRE(get<TypeMismatch>(result.errors[0]));
    CHECK_EQ(result.errors[0].location.begin.line, 14);
    REQUIRE(get<TypeMismatch>(result.errors[1]));
    CHECK_EQ(result.errors[1].location.begin.line, 15);
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_value_against_its_object_is_said_in_words")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS();
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    CheckResult result = check(R"(
        class Box<T>
            v: T
        end
        local b = Box { v = 1 }
        local c: class<Box<number>> = b
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Expected this to be the class 'Box', but got an object of class 'Box<number>' instead", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "uninstantiable_class_is_not_constructed_by_another_class_of_the_same_name")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
    };

    fileResolver.source["game/A"] = R"(
        export class Node
            public v: number
        end
    )";

    fileResolver.source["game/B"] = R"(
        class Node
            public v: number
            private function __init(self, v: number)
                self.v = v
            end
            public function other(self)
                local Other = require(game.A).Node
                local Node = Other
                return Node { v = 1 }
            end
        end
        return Node
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<UninstantiableClass>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "typeof_class_hint_only_names_a_type_that_resolves_here")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
    };

    fileResolver.source["game/A"] = R"(
        export class Node
            public v: number
        end
    )";

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)
        local x: typeof(A.Node) = A.Node
        class Local
            public v: number
        end
        local L = Local
        local z: typeof(L) = L
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("Use 'class<T>', where T is the type of 'Node' objects, instead of 'typeof(A.Node)' to get the class of 'Node'", toString(result.errors[0]));
    CHECK_EQ("Use 'class<Local>' instead of 'typeof(L)' to get the class of 'Local'", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "annotate_writes_generic_class_type_arguments")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    std::string annotated = decorateWithTypes(R"(
class A<T>
    v: T
end
local a: A<number> = A { v = 1 }
local n = a
)");

    CHECK_MESSAGE(annotated.find("local n:A<number>=") != std::string::npos, annotated);
}

TEST_CASE_FIXTURE(ClassesFixture, "instance_method_through_the_class_value")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
        class Bag
            public n: number
            public function add(self, k: number): number return self.n + k end
            private function secret(self): number return self.n end
            public function peek(b: Bag): number return Bag.secret(b) end
        end
        class Dog
            public n: number
        end
        local b = Bag { n = 1 }
        local x = Bag.add(b, 2)
        local f = Bag.add
        local y: number = f(b, 3)
        local wrong = Bag.add(Dog { n = 1 }, 2)
        local hidden = Bag.secret(b)
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("number", toString(requireType("x")));
    CHECK_EQ("(Bag, number) -> number", toString(requireType("f")));

    const TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK_EQ(result.errors[0].location.begin.line, 14);
    CHECK_EQ("Bag", toString(tm->wantedType));
    CHECK_EQ("Dog", toString(tm->givenType));

    REQUIRE(get<PrivatePropertyAccess>(result.errors[1]));
    CHECK_EQ(result.errors[1].location.begin.line, 15);
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_instance_method_through_the_class_value")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // Nothing instantiates the class's `T` when the method is read through the class value, so the
    // method is generic over it.
    CheckResult result = check(R"(
        class Box<T>
            public v: T
            public function get(self): T return self.v end
        end
        local b: Box<number> = Box { v = 1 }
        local s: Box<string> = Box { v = "x" }
        local n = Box.get(b)
        local str = Box.get(s)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("number", toString(requireType("n")));
    CHECK_EQ("string", toString(requireType("str")));
}

TEST_CASE_FIXTURE(ClassesFixture, "table_shape_intersected_with_nominal_types_provides_its_properties")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // Each nominal part is checked for the property, but a table shape intersected with the parts gives it to all of them
    auto result = check(R"(
class Cat(public name: string) end
class Dog(public age: number) end

local function label(d: Duration & { label: string }): string
    return d.label
end

local function extra(p: (Cat | Dog) & { extra: number }): number
    return p.extra
end

local function name(p: Cat | Dog)
    return p.name
end
    )");

    // Only `name` reports: without a shape, every member of a union of objects needs the property
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const MissingUnionProperty* missing = get<MissingUnionProperty>(result.errors[0]);
    REQUIRE(missing);
    CHECK_EQ(missing->key, "name");
    REQUIRE_EQ(missing->missing.size(), 1);
    CHECK_EQ(toString(missing->missing[0]), "Dog");
}

// Luwu Classes (rfcs/classes): a field holding `nil` shows up as `none` in `class.fields`, so `nil` in its type is `none`
TEST_CASE_FIXTURE(ClassesFixture, "class_fields_types_nil_as_none")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    CheckResult result = check(R"(
class Profile
    name = "Ada"
    nickname: string? = nil
    gone: nil = nil
end
local fields = class.fields(Profile())
local nickname = fields.nickname
local name = fields.name
local gone = fields.gone
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("nickname")), "none | string");
    CHECK_EQ(toString(requireType("name")), "string");
    CHECK_EQ(toString(requireType("gone")), "none");
}

// Luwu: a table in the union that the class can't fit (its `display` method against a `display` field) doesn't take the
// class out of the true branch
TEST_CASE_FIXTURE(ClassesFixture, "isinstance_keeps_the_class_beside_a_disjoint_table")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    CheckResult result = check(R"(
type Input = { padding: string?, display: ("flex" | "grid")? }
type Other = { margin: string? }
class Box
    names: { string } = {}
    function display(self): string
        return "box"
    end
end
local function merge(v: Box | Input)
    if class.isinstance(v, Box) then
        local x = v
    end
end
local function merge_many(v: Box | Input | Other)
    if class.isinstance(v, Box) then
        local y = v
    end
end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireTypeAtPosition({11, 18})), "Box");
    CHECK_EQ(toString(requireTypeAtPosition({16, 18})), "Box");
}

// Luwu: a default is checked like a local's value, so `{}` fits a table type whose fields are all optional
TEST_CASE_FIXTURE(ClassesFixture, "empty_table_default_for_an_all_optional_record")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag defaultArguments{FFlag::LuwuDefaultArguments, true};
    CheckResult result = check(R"(
type Input = { padding: string?, margin: string? }
local function plain(_x: Input = {}) end
class Box() end
local function either(_x: Box | Input = {}) end
class Rule(public selector: string, public style: Box | Input = {}) end
local function wrong(_x: Input = { padding = 1 }) end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 6);
}

// Luwu Destructuring (rfcs/destructuring.md): a positional pattern on an object reads its primary constructor's fields
TEST_CASE_FIXTURE(ClassesFixture, "positional_destructuring_binds_the_primary_constructors_fields")
{
    ScopedFastFlag sffs[] = {{FFlag::LuwuClasses, true}, {FFlag::LuwuDestructuring, true}, {FFlag::LuwuGenericNominals, true}};
    CheckResult result = check(R"(
class Cat(name: string, age: number) end
class Box<T>(item: T, count: number) end
class Dog(barks: boolean) end
class Frame(name, size)
    public size: number
    public name: string
end

const .(cat_name, cat_age) = Cat("Taz", 12)
const .(item) = Box("hello", 2)
const .(frame_name, frame_size) = Frame("f", 2)
const .(a, b) = { "hi", "there" }
local function first(v: Cat | Dog)
    const .(f) = v
    return f
end
local either = first(Dog(true))
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("cat_name")), "string");
    CHECK_EQ(toString(requireType("cat_age")), "number");
    CHECK_EQ(toString(requireType("item")), "string");
    CHECK_EQ(toString(requireType("frame_name")), "string");
    CHECK_EQ(toString(requireType("frame_size")), "number");
    CHECK_EQ(toString(requireType("a")), "string");
    CHECK_EQ(toString(requireType("either")), "boolean | string");
}

TEST_CASE_FIXTURE(ClassesFixture, "positional_destructuring_needs_a_primary_constructor_with_that_position")
{
    ScopedFastFlag sffs[] = {{FFlag::LuwuClasses, true}, {FFlag::LuwuDestructuring, true}};
    CheckResult result = check(R"(
class Pod
    x: number = 0
end
class Custom
    x: number
    function __init(self, x: number)
        self.x = x
    end
end
class Cat(name: string, age: number) end

const .(p1, p2) = Pod()
const .(c1) = Custom(1)
const .(n, a, extra) = Cat("Taz", 12)
    )");

    // one error per pattern on a class without a primary constructor, at its first element
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    for (const TypeError& error : result.errors)
        REQUIRE(get<PositionalDestructuring>(error));

    CHECK_EQ(result.errors[0].location.begin.line, 12);
    CHECK_FALSE(get<PositionalDestructuring>(result.errors[0])->parameters);
    CHECK_EQ(result.errors[1].location.begin.line, 13);
    CHECK_EQ(result.errors[2].location.begin.line, 14);
    CHECK_EQ(get<PositionalDestructuring>(result.errors[2])->position, 2);
    CHECK_EQ(get<PositionalDestructuring>(result.errors[2])->parameters->size(), 2);
}

TEST_CASE_FIXTURE(ClassesFixture, "positional_destructuring_respects_private_fields")
{
    ScopedFastFlag sffs[] = {{FFlag::LuwuClasses, true}, {FFlag::LuwuDestructuring, true}};
    CheckResult result = check(R"(
class Key(public label: string, private secret: string)
    public function both(self): string
        const .(l, s) = self
        return l .. s
    end
end

const .(label, secret) = Key("a", "b")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<PrivatePropertyAccess>(result.errors[0]));
    CHECK_EQ(result.errors[0].location.begin.line, 8);
}

TEST_CASE_FIXTURE(ClassesFixture, "typing_positional_destructuring")
{
    ScopedFastFlag sffs[] = {{FFlag::LuwuClasses, true}, {FFlag::LuwuDestructuring, true}};
    const char* before = "class Cat(name: string, age: number) end\nlocal cats = { Cat(\"Taz\", 12) }\n";
    const char* after = "\nlocal unrelated = 1\nprint(unrelated + 1)\n";

    checkTypewriting(*this, before, "const .(name, age) = Cat(\"Taz\", 12)", after);
    checkTypewriting(*this, before, "local cat.(name, age: number) = Cat(\"Taz\", 12)", after);
    checkTypewriting(*this, before, "for _, .(name, age) in cats do\n    print(name, age)\nend", after);
}

// Luwu: a callee with one `__call` signature is called like a function type, so a call with a wrong argument reports
// that argument and still gives the signature's return type. Upstream resolves it like an overload set.
TEST_CASE_FIXTURE(ClassesFixture, "a_misargued_single_call_signature_keeps_its_return_type")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    auto result = check(R"(
class Cat
    name: string
    function __init(self, name: string)
        self.name = name
    end
end

local cat = Cat(1)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(Location{{8, 16}, {8, 17}}, result.errors[0].location);
    CHECK_EQ("Cat", toString(requireType("cat")));
}

// Luwu: the same for a table with a single `__call`
TEST_CASE_FIXTURE(BuiltinsFixture, "a_misargued_single_call_metamethod_keeps_its_return_type")
{
    auto result = check(R"(
type Dog = { name: string }
local Dogs = setmetatable({}, { __call = function(_, name: string): Dog return { name = name } end })
local dog = Dogs(2)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(Location{{3, 17}, {3, 18}}, result.errors[0].location);
    CHECK_EQ("Dog", toString(requireType("dog")));
}

TEST_CASE_FIXTURE(ClassesFixture, "declared_class_types")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    CheckResult result = check(R"(
        --!strict
        local function name(p: Path): string
            return p:join("x").raw
        end

        local Path = (nil :: any) :: class<Path>
        local p = Path("a")
        local q = Path("a", "/")
        local r = Path.cwd()
        local s: string = tostring(q)

        do
            class type Path(public raw: string, public sep = string)
                public function join(self, other: string): Path
                public function cwd(): Path
                public function __tostring(self): string
                public function parent(self, levels = number): Path
            end
        end

        local up = p:parent()
        local further = p:parent(2)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("p")), "Path");
    CHECK_EQ(toString(requireType("r")), "Path");
}

TEST_CASE_FIXTURE(ClassesFixture, "declare_class_declares_the_class_value")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    // `declare` declares a value: the class an embedder provides, along with its type
    CheckResult result = check(R"(
        --!strict
        declare class Semver(public const major: number, private minor: number)
            public function bump(self): Semver
        end

        local version = Semver(1, 2)
        local major: number = version.major
        local next: Semver = version:bump()
        version.major = 3
        local wrong = Semver("a", 2)
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]).find("constant") != std::string::npos);
    CHECK(result.errors[1].location.begin.line == 10);
}

TEST_CASE_FIXTURE(ClassesFixture, "declare_trait_declares_the_trait_value")
{
    ScopedFastFlag flags[] = {
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::LuwuTraits, true},
    };

    // `declare trait` is the trait and its type; `trait type` is the type alone
    CheckResult result = check(R"(
        --!strict
        declare trait Named
            expect name: string
        end

        trait type Titled
            expect title: string
        end

        local named: trait<Named> = Named
        local titled = Titled
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<UnknownSymbol>(result.errors[0]));
    CHECK(result.errors[0].location.begin.line == 11);
}

TEST_CASE_FIXTURE(ClassesFixture, "export_declare_trait_exports_the_trait_and_its_type")
{
    ScopedFastFlag flags[] = {
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::LuwuTraits, true},
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
    };

    fileResolver.source["game/A"] = R"(
        --!strict
        export declare trait Named
            expect name: string
        end

        export trait type Titled
            expect title: string
        end
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local traits = require(game.A)
        local named: trait<traits.Named> = traits.Named
        local titled = traits.Titled
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(result.errors[0].location.begin.line == 4);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_type_declares_no_value")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    // `class type` is the type alone: objects come from elsewhere, and there's no class to call
    CheckResult result = check(R"(
        --!strict
        class type Path(public raw: string)
        end

        local function name(p: Path): string
            return p.raw
        end

        local p = Path("a")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<UnknownSymbol>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "export_declare_class_exports_the_class_and_its_type")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
    };

    fileResolver.source["game/A"] = R"(
        --!strict
        export declare class Semver(public const major: number, public const minor: number)
        end

        export class type Path(public raw: string)
        end
    )";

    // `export declare class` exports the class and its type; `export class type` only the type
    fileResolver.source["game/B"] = R"(
        --!strict
        local versions = require(game.A)
        local version: versions.Semver = versions.Semver(1, 2)
        local major: number = version.major
        local path: versions.Path? = nil
        local wrong = versions.Semver("a", 2)
        local missing = versions.Path
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(result.errors[0].location.begin.line == 6);
    CHECK(result.errors[1].location.begin.line == 7);
}

TEST_CASE_FIXTURE(ClassesFixture, "declaring_classes_is_typed_without_cascades")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    const char* after = "\nlocal unrelated = 1\nprint(unrelated)\n";
    checkTypewriting(*this, "", "declare class Semver(public major: number)\n    public function bump(self): Semver\nend", after);
    checkTypewriting(*this, "", "class type Path(public raw: string)\nend", after);
}

TEST_CASE_FIXTURE(ClassesFixture, "declared_class_construction_and_access")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    CheckResult result = check(R"(
        --!strict
        class type Point
            x: number
            y = number
        end

        class type Counter
            private count: number
            public function __init(self, start: number)
            public function bump(self): number
        end

        local P = (nil :: any) :: class<Point>
        local a = P({ x = 1 })
        local b = P({ x = 1, y = 2 })

        local C = (nil :: any) :: class<Counter>
        local c = C(1)
        local n: number = c:bump()

        local missing = P({ y = 2 })
        local wrong = C("one")
        local hidden = c.count
    )");

    // `x` has no default, `start` is a number, and `count` is private
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK_EQ(result.errors[0].location.begin.line, 21);
    CHECK_EQ(result.errors[1].location.begin.line, 22);
    CHECK_EQ(result.errors[2].location.begin.line, 23);
    CHECK(get<TypeMismatch>(result.errors[1]));
    CHECK_EQ(toString(result.errors[2]), "Field 'count' of class 'Counter' is private; accessing it here will raise a runtime error");
}

TEST_CASE_FIXTURE(ClassesFixture, "declared_class_has_no_value")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    CheckResult result = check(R"(
        --!strict
        class type Path
            raw: string
        end
        local p = Path({ raw = "a" })
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    UnknownSymbol* unknown = get<UnknownSymbol>(result.errors[0]);
    REQUIRE(unknown);
    CHECK_EQ(unknown->name, "Path");
}

TEST_CASE_FIXTURE(ClassesFixture, "declared_classes_are_scoped_like_types")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    fileResolver.source["game/A"] = R"(
        --!strict
        export class type Path(raw: string)
            function join(self, other: string): Path
        end
        class type Hidden
            name: string
        end
        return {} :: { Path: class<Path>, cwd: () -> Path }
    )";
    fileResolver.source["game/B"] = R"(
        --!strict
        local path = require(game.A)
        local p: path.Path = path.Path("a")
        local q: path.Path = path.cwd():join("b")
        type H = path.Hidden
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    UnknownSymbol* hidden = get<UnknownSymbol>(result.errors[0]);
    REQUIRE(hidden);
    CHECK_EQ(hidden->name, "path.Hidden");

    LUAU_REQUIRE_NO_ERRORS(getFrontend().check("game/A"));
}TEST_CASE_FIXTURE(ClassesFixture, "declared_generic_class")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};
    ScopedFastFlag luwuGenericNominals{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        --!strict
        class type List<T>(items: { T })
            function get(self, index: number): T
            function push(self, value: T)
            function map<U>(self, f: (T) -> U): List<U>
            function empty<V>(): List<V>
        end

        local List = (nil :: any) :: class<List>
        local numbers = List({ 1, 2, 3 })
        local first = numbers:get(1)
        numbers:push(4)
        local strings = numbers:map(function(n) return tostring(n) end)
        local s = strings:get(1)
        local annotated: List<string> = strings
        local none = List.empty()

        numbers:push("five")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
    CHECK_EQ(result.errors[0].location.begin.line, 18);
    CHECK_EQ(toString(requireType("numbers")), "List<number>");
    CHECK_EQ(toString(requireType("first")), "number");
    CHECK_EQ(toString(requireType("strings")), "List<string>");
    CHECK_EQ(toString(requireType("s")), "string");
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_to_a_declared_class")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    CheckResult result = check(R"(
        --!strict
        class type Path
            raw: string
        end

        local Path = (nil :: any) :: class<Path>

        local function describe(x: Path | string): string
            if class.isinstance(x, Path) then
                return x.raw
            end
            return x
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "isinstance_refines_to_an_imported_declared_class")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    fileResolver.source["game/A"] = R"(
        --!strict
        export class type Path
            raw: string
        end
        return {} :: { Path: class<Path> }
    )";
    fileResolver.source["game/B"] = R"(
        --!strict
        local path = require(game.A)

        local function describe(x: path.Path | string): string
            if class.isinstance(x, path.Path) then
                return x.raw
            end
            return x
        end

        local function onlyPaths(x: unknown): string?
            if class.isinstance(x, path.Path) then
                return x.raw
            end
            return nil
        end
    )";

    LUAU_REQUIRE_NO_ERRORS(getFrontend().check("game/B"));
}

TEST_CASE_FIXTURE(ClassesFixture, "declared_trait_types")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        --!strict
        trait type Named
            expect public name: string
        end

        trait type Greeter needs Named
            expect public function speak?(self): string
            private secret: number
            public function greet(self): string
        end

        local function hello(g: Greeter): string
            local n: string = g.name
            local s = g.speak
            local hidden = g.secret
            return g:greet()
        end

        local Greeter = (nil :: any) :: trait<Greeter>
        local greet = Greeter.greet
    )");

    // `secret` is private
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 15);
    // an implementing class may leave `speak` out
    CHECK_EQ(toString(requireTypeAtPosition({14, 25})), "((Greeter) -> string)?");
}

TEST_CASE_FIXTURE(ClassesFixture, "optional_expected_functions_may_be_nil_through_the_trait")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        --!strict
        trait Greeter
            expect function speak?(self): string
            function greet(self): string
                if self.speak then
                    return self:speak()
                end
                return "..."
            end
            function shout(self): string
                return self:speak()
            end
        end

        class Cat implements Greeter
            function speak(self): string return "meow" end
        end
        class Rock implements Greeter end

        local function f(g: Greeter, c: Cat, r: Rock)
            local a = g.speak
            local b = c.speak
            local d = r.speak
        end
    )");

    // `shout` calls `speak` without checking that the class has it
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 11);
    CHECK_EQ(toString(requireTypeAtPosition({21, 25})), "((Greeter) -> string)?");
    CHECK_EQ(toString(requireTypeAtPosition({22, 25})), "(Cat) -> string");
    CHECK_EQ(toString(requireTypeAtPosition({23, 25})), "((Greeter) -> string)?");
}

TEST_CASE_FIXTURE(ClassesFixture, "classes_implement_declared_traits")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        --!strict
        trait type Greeter
            expect public name: string
            public function greet(self): string
        end

        class type Robot implements Greeter
            public name: string
            public serial: number
        end

        local Greeter = (nil :: any) :: trait<Greeter>

        class Cat implements Greeter
            public name = "c"
        end

        class Rock implements Greeter
            public weight = 1
        end

        local function robot(r: Robot): Greeter
            local s: string = r:greet()
            return r
        end

        local cat: Greeter = Cat()

        local function f(x: unknown)
            if class.implements(x, Greeter) then
                local refined = x
            end
        end

        -- the annotation names the trait, whatever the local is called
        const G: trait<Greeter> = Greeter
        class Dog implements G end
    )");

    // Rock doesn't have the `name` Greeter expects, and neither does Dog
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(result.errors[0].location.begin.line, 18);
    CHECK_EQ(result.errors[1].location.begin.line, 37);
    CHECK_EQ(toString(requireTypeAtPosition({31, 32})), "Greeter");
}

TEST_CASE_FIXTURE(ClassesFixture, "declared_traits_are_scoped_like_types")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    fileResolver.source["game/A"] = R"(
        --!strict
        export trait type Greeter
            expect public name: string
            public function greet(self): string
        end
        trait type Hidden
            public function hide(self)
        end
        return {} :: { Greeter: trait<Greeter> }
    )";
    fileResolver.source["game/B"] = R"(
        --!strict
        local a = require(game.A)
        local function hello(g: a.Greeter): string
            return g:greet() .. g.name
        end
        class Cat implements a.Greeter
            public name = "c"
        end
        local g: a.Greeter = Cat()
        type H = a.Hidden
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    UnknownSymbol* hidden = get<UnknownSymbol>(result.errors[0]);
    REQUIRE(hidden);
    CHECK_EQ(hidden->name, "a.Hidden");
}

TEST_CASE_FIXTURE(ClassesFixture, "class_of_a_generic_class_without_type_arguments")
{
    ScopedFastFlag luwuGenericNominals{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        --!strict
        class Box<T>(public value: T)
            public function get(self): T
                return self.value
            end
        end

        local B: class<Box> = Box
        local b = B("s")
        local s = b:get()
        local specific: class<Box<number>> = Box
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("b")), "Box<string>");
    CHECK_EQ(toString(requireType("s")), "string");
}

// Luwu Traits (rfcs/classes/traits.md)

TEST_CASE_FIXTURE(ClassesFixture, "trait_members_are_the_implementing_class_members")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Greeter
            expect name: string
            greeting = "hello"
            function greet(self): string
                return self.greeting .. ", " .. self.name
            end
        end

        class Cat implements Greeter
            name = "whiskers"
        end

        local function welcome(g: Greeter): string
            return g:greet()
        end

        local c = Cat()
        local a = c:greet()
        local b = c.greeting
        local w = welcome(c)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("string", toString(requireType("a")));
    CHECK_EQ("string", toString(requireType("b")));
    CHECK_EQ("string", toString(requireType("w")));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_needs_are_implied_and_private_members_stay_private")
{
    ScopedFastFlag _[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuDefaultArguments, true}};

    CheckResult result = check(R"(
        trait Item(category: string, count = 0)
            expect name: string
            function is_weapon(self): boolean
                return self.category == "Weapon"
            end
        end

        trait Tool needs Item
            expect private model: string
            expect public function equipped?(self)
            private is_equipped = false
            public function toggle(self): boolean
                self.is_equipped = not self.is_equipped
                if self.equipped then
                    self:equipped()
                end
                return self.is_equipped
            end
        end

        trait Gun needs Tool
            expect public const beamtype: "Las" | "Particle"
        end

        class Rifle implements Gun, Item("Weapon", 1)
            public name = "rifle"
            private model: string = "m"
            public const beamtype = "Particle"
            public function peek(self): boolean
                return self.is_equipped
            end
        end

        local r = Rifle()
        local w = r:is_weapon()
        local t = r:toggle()
        local e = r.equipped
        local hidden = r.is_equipped
    )");

    // the only error is reading the trait's private field from outside the class
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<PrivatePropertyAccess>(result.errors[0]));
    CHECK_EQ("boolean", toString(requireType("w")));
    CHECK_EQ("boolean", toString(requireType("t")));
    // an optional expected function keeps the trait's signature, `self` included
    CHECK_EQ("((Tool) -> ())?", toString(requireType("e")));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_function_overrides_keep_their_access")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait T
            public function shown(self) end
            private function hidden(self) end
            public function make() end
            public function kept(self) end
        end

        class A implements T
            private function shown(self) end
            public function hidden(self) end
            private function make() end
            public function kept(self) end
        end
    )");

    // each on the trait in the `implements` list, then on the class's own function
    LUAU_REQUIRE_ERROR_COUNT(6, result);
    CHECK_EQ("'hidden' must be private for 'A' to implement 'T'", toString(result.errors[0]));
    CHECK_EQ("'hidden' must be private for 'A' to implement 'T'", toString(result.errors[1]));
    CHECK_EQ(result.errors[0].location.begin.line, 8);
    CHECK_EQ(result.errors[1].location.begin.line, 10);
    CHECK_EQ("'make' must be public for 'A' to implement 'T'", toString(result.errors[2]));
    CHECK_EQ(result.errors[3].location.begin.line, 11);
    CHECK_EQ("'shown' must be public for 'A' to implement 'T'", toString(result.errors[4]));
    CHECK_EQ(result.errors[5].location.begin.line, 9);
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_final_fields_are_never_assigned")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Tagged
            final tag = "tagged"
        end

        class Owned implements Tagged
            n: number
            function __init(self)
                self.n = 1
                self.tag = "mine"
            end
        end

        local o = Owned()
        local t: string = o.tag
        o.tag = "x"
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("'tag' is a final field of 'Owned' and can't be assigned", toString(result.errors[0]));
    CHECK_EQ("'tag' is a final field of 'Owned' and can't be assigned", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_arguments_are_checked")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag defaultArguments{FFlag::LuwuDefaultArguments, true};

    CheckResult result = check(R"(
        trait Item(category: "Weapon" | "Tool" | "Currency", max = 10)
            expect name: string
        end

        class Package(contents: { Item }) implements Item
        end

        class TooMany implements Item("Tool", 1, 2)
            name = "many"
        end

        class WrongType implements Item("Food")
            name = "food"
        end

        class Fine(n: number) implements Item("Weapon", n)
            name = "fine"
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(4, result);
    CHECK_EQ("This trait must be called: 'Item(category, max)'", toString(result.errors[0]));
    CHECK_EQ("Trait 'Item' takes 1 to 2 arguments, but 3 were given", toString(result.errors[1]));
    CHECK_EQ("'Package' is missing a field to implement 'Item':\n  • 'name: string'", toString(result.errors[2]));
    CHECK(get<TypeMismatch>(result.errors[3]));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_expectations_are_checked")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait T
            expect public x: number
            expect private y: number
            expect public function f(self): number
            public final function h(self) end
        end

        class A implements T
            public x = "no"
            public y = 1
            public function h(self) end
        end
    )");

    // a member-level error is on the trait in the `implements` list, and on the class's own member
    LUAU_REQUIRE_ERROR_COUNT(6, result);
    CHECK_EQ("'h' is final in trait 'T' and can't be overridden", toString(result.errors[0]));
    CHECK_EQ("'h' is final in trait 'T' and can't be overridden", toString(result.errors[1]));
    CHECK_EQ(result.errors[1].location.begin.line, 11);
    CHECK_EQ("'y' must be private for 'A' to implement 'T'", toString(result.errors[2]));
    CHECK_EQ(result.errors[3].location.begin.line, 10);
    CHECK(get<TypeMismatch>(result.errors[4]));
    CHECK_EQ("'A' is missing a function to implement 'T':\n  • 'public function f(self: T): number'", toString(result.errors[5]));
}

TEST_CASE_FIXTURE(ClassesFixture, "traits_may_expect_the_same_member")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait A
            expect name: string
            expect function speak(self): string
            function a(self): string return self:speak() end
        end

        trait B
            expect name: string
            expect function speak(self): string
            function b(self): string return self.name end
        end

        class C implements A, B
            name = "c"
            function speak(self): string return "hi" end
        end

        local c = C()
        local x = c:a() .. c:b()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "traits_expecting_one_field_with_different_types")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait A
            expect x: number
        end

        trait B
            expect x: string
        end

        class C implements A, B
            x = 1
        end
    )");

    // no field is both a number and a string
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
}

// A class, or a trait that needs another, may override a non-final field a trait provides: the override keeps the field's
// type, access and constness, and a class's has to give the field a value.
TEST_CASE_FIXTURE(ClassesFixture, "trait_field_overrides")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Enemy
            name = "Enemy"
            final kind = "enemy"
            count: number = 0
        end

        class Bot implements Enemy
            name = "Bot"
        end

        trait Boss(name: string) needs Enemy
        end

        class Karmelita implements Boss("Karmelita")
        end

        local bot: string = Bot().name
        local boss: string = Karmelita().name

        class WrongType implements Enemy
            name = 10
        end

        class Final implements Enemy
            kind = "mine"
        end

        class NoValue implements Enemy
            count: number
        end

        trait WrongBoss(name: number) needs Enemy
        end
    )");

    // the class-level errors are reported on the `implements` entry and on the member, the type errors on the member
    LUAU_REQUIRE_ERROR_COUNT(6, result);
    CHECK_EQ("'kind' is final in trait 'Enemy' and can't be overridden", toString(result.errors[0]));
    CHECK_EQ("'kind' is final in trait 'Enemy' and can't be overridden", toString(result.errors[1]));
    CHECK_EQ("Class 'NoValue' overrides 'count' of trait 'Enemy', so it has to give 'count' a value", toString(result.errors[2]));
    CHECK_EQ("Class 'NoValue' overrides 'count' of trait 'Enemy', so it has to give 'count' a value", toString(result.errors[3]));
    CHECK(get<TypeMismatch>(result.errors[4]));
    CHECK_EQ(result.errors[4].location.begin.line, 21);
    CHECK(get<TypeMismatch>(result.errors[5]));
    CHECK_EQ(result.errors[5].location.begin.line, 32);
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_member_clashes")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait A
            shared = 1
            function f(self) end
        end

        trait B
            function f(self) end
        end

        class C implements A, B
            shared = 2
        end

        class D implements A, B
            function f(self) end
        end
    )");

    // C overrides A's `shared`, which a class may do; D settles the clash of `f` by defining it
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Traits 'A' and 'B' both provide 'f' and neither needs the other; define 'f' in class 'C' to choose", toString(result.errors[0]));
}

// A trait's function overrides the function of a trait it needs: the provider that needs every other one wins, and
// the override is checked where it is written
TEST_CASE_FIXTURE(ClassesFixture, "a_trait_overrides_a_function_of_a_trait_it_needs")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Path
            function join(self, s: string): Path return self end
            final function locked(self) return 1 end
            function narrow(self): string return "" end
            y = 1
        end
        trait FilesystemPath needs Path
            function join(self, s: string): FilesystemPath return self end
            function exists(self): boolean return true end
            function locked(self) return 2 end
            function narrow(self): number return 0 end
            function y(self) end
        end
        class FilePath implements FilesystemPath end
        local exists: boolean = FilePath():join("x"):exists()

        trait Base function g(self) return 0 end end
        trait Left needs Base function g(self) return 1 end end
        trait Right needs Base function g(self) return 2 end end
        trait Both needs Left, Right function g(self) return 3 end end
        class Diamond implements Left, Right end
        class Settled implements Left, Right function g(self) return 4 end end
        class Resolved implements Both end
    )");

    // the clash comes from constraint generation, the trait-level checks after it; the narrowed signature is fine
    LUAU_REQUIRE_ERROR_COUNT(4, result);
    CHECK_EQ(
        "Traits 'Left' and 'Right' both provide 'g' and neither needs the other; define 'g' in class 'Diamond' to choose",
        toString(result.errors[0])
    );
    CHECK_EQ("'locked' is final in trait 'Path' and can't be overridden", toString(result.errors[1]));
    CHECK_EQ(result.errors[1].location.begin.line, 10);
    // `narrow` returns a number where Path's returns a string
    CHECK_EQ(result.errors[2].location.begin.line, 11);
    CHECK_EQ("Trait 'FilesystemPath' can't redefine 'y' as a function: it is a field in trait 'Path'", toString(result.errors[3]));
    CHECK_EQ(result.errors[3].location.begin.line, 12);
}

TEST_CASE_FIXTURE(ClassesFixture, "traits_that_need_each_other")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait A needs B end
        trait B needs A end
        trait Base end
        trait Left needs Base end
        trait Right needs Base end
        trait X needs Y end
        trait Y needs Z end
        trait Z needs X end
        trait Me needs Me end
    )");

    // the diamond through Base is fine; each trait in a cycle reports it from its own `needs` entry, starting with itself
    LUAU_REQUIRE_ERROR_COUNT(6, result);
    CHECK_EQ(
        "Traits 'A' and 'B' are codependent. This is an unhealthy relationship; consider merging these traits or factoring out "
        "common members into a new trait",
        toString(result.errors[0])
    );
    CHECK_EQ(
        "Traits 'B' and 'A' are codependent. This is an unhealthy relationship; consider merging these traits or factoring out "
        "common members into a new trait",
        toString(result.errors[1])
    );
    CHECK_EQ(
        "Traits 'X', 'Y' and 'Z' are codependent. This is an unhealthy relationship; consider merging these traits or factoring out "
        "common members into a new trait",
        toString(result.errors[2])
    );
    CHECK_EQ(
        "Traits 'Y', 'Z' and 'X' are codependent. This is an unhealthy relationship; consider merging these traits or factoring out "
        "common members into a new trait",
        toString(result.errors[3])
    );
    CHECK_EQ(
        "Traits 'Z', 'X' and 'Y' are codependent. This is an unhealthy relationship; consider merging these traits or factoring out "
        "common members into a new trait",
        toString(result.errors[4])
    );
    CHECK_EQ("Trait 'Me' needs itself", toString(result.errors[5]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_implements_refines")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Greeter
            expect name: string
            function greet(self): string return "hi " .. self.name end
        end
        trait Other
            function other(self): number return 1 end
        end
        class Cat implements Greeter
            name = "c"
        end
        class Dog implements Other end

        local function f(x: unknown)
            if class.implements(x, Greeter) then
                local s = x:greet()
                local refined = x
            end
        end

        local function g(x: Cat | Dog)
            if class.implements(x, Greeter) then
                local yes = x
            else
                local no = x
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Greeter", toString(requireTypeAtPosition({16, 32})));
    CHECK_EQ("Cat", toString(requireTypeAtPosition({22, 28})));
    CHECK_EQ("Dog", toString(requireTypeAtPosition({24, 27})));
}

// Luwu Traits (rfcs/classes/traits.md): with no specific trait known, a value that passes `class.implements` is still
// what it was (an object), not an error type that silences everything after
TEST_CASE_FIXTURE(ClassesFixture, "class_implements_with_any_trait_keeps_the_value")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Expect
            private function expect_some(self, expected: trait)
                if class.implements(self, expected) then
                    local probe: number = self
                end
            end
        end
    )");

    // `self` is still `Expect` in the branch, so the probe is an error instead of silently passing
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 4);
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_functions_through_the_trait")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Greeter
            expect name: string
            expect function speak(self): string
            function greet(self): string return self.name end
            function make(): number return 1 end
        end
        class Cat implements Greeter
            name = "c"
            function speak(self): string return "meow" end
        end
        class Unrelated end
        local a: string = Greeter.greet(Cat())
        local b: number = Greeter.make()
        local c = Greeter.greet(Unrelated())
        local d = Greeter.speak
    )");

    // a method's `self` must implement the trait, and an expected function isn't part of the trait's value
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
    CHECK(get<UnknownProperty>(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_factory")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Path
            expect raw: string
            function __create(raw: string)
                if raw == "@" then
                    return RequirePath(raw)
                end
                return RelativePath(raw)
            end
        end

        class RelativePath private (public raw: string) implements Path end
        class RequirePath private (public raw: string) implements Path end

        trait Parsed
            function __create(n: number): string?
                return nil
            end
        end

        trait Plain end

        local p = Path("./x")
        local s = Parsed(1)
        local bad = Plain()
        local direct = RelativePath("y")
    )");

    // calling a trait without `__create`, and a private constructor outside the class and its traits
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(toString(result.errors[0]).find("Cannot call a value of type Plain") != std::string::npos);
    CHECK(get<PrivateConstructorAccess>(result.errors[1]));
    CHECK_EQ("Path", toString(requireType("p")));
    CHECK_EQ("string?", toString(requireType("s")));
}

TEST_CASE_FIXTURE(ClassesFixture, "unannotated_trait_factory_returns_the_trait")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait T
            function __create()
                return 1
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_expected_constructor_and_class_of_trait")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        type Props = { text: string }

        const registry: { [string]: class<Element> } = {}

        trait Element
            expect function __init(self, props: Props)
            expect function render(self): string
            function __create(kind: string, props: Props)
                return registry[kind](props)
            end
        end

        class Button private (private props: Props) implements Element
            public function render(self): string return self.props.text end
        end

        class Heading(props: Props, level: number?) implements Element
            function render(self): string return "" end
        end

        registry.button = Button
        registry.h1 = Heading

        local made = Element("button", { text = "ok" })
        local cls: class<Element> = Heading
        local fromCls = cls({ text = "x" })
        local traitValue: trait<Element> = Element
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Element", toString(requireType("made")));
    CHECK_EQ("Element", toString(requireType("fromCls")));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_expected_constructor_errors")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        type Props = { text: string }
        trait Element
            expect function __init(self, props: Props)
        end
        class Pod implements Element end
        class WrongType(n: number) implements Element end
        class Extra(props: Props, required: number) implements Element end
        class NotAnElement end
        local a: class<Element> = NotAnElement
        local b: trait<NotAnElement> = nil :: any
    )");

    // Pod has no constructor; WrongType's takes a number; Extra needs an argument the trait doesn't pass;
    // NotAnElement is no class<Element>; trait<> needs a trait
    LUAU_REQUIRE_ERROR_COUNT(5, result);
}

TEST_CASE_FIXTURE(ClassesFixture, "intersection_of_traits_is_inhabited")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Mid function mid(self): string return "m" end end
        trait Other function other(self): number return 1 end end
        class A implements Mid, Other end
        class B implements Other end

        local z: Mid & Other = A()
        local m: string = z:mid()
        local o: number = z:other()
        local n: never = z
        local bad: Mid & Other = B()
    )");

    // `z` isn't never; B implements only Other
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
    CHECK(get<TypeMismatch>(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "implements_refinements_intersect")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Mid function mid(self): string return "m" end end
        trait Other function other(self): number return 1 end end
        trait Base end
        trait Needy needs Base end
        class A implements Mid, Other end
        class B implements Other end

        local function f(x: unknown)
            if class.implements(x, Mid) and class.implements(x, Other) then
                local both = x
            end
            if class.implements(x, Mid) then
                if class.implements(x, Other) then
                    local nested = x
                end
            end
            if class.implements(x, Needy) and class.implements(x, Base) then
                local needy = x
            end
        end

        local function g(x: A | B)
            if class.implements(x, Mid) and class.implements(x, Other) then
                local a = x
            elseif class.implements(x, Other) then
                local b = x
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Mid & Other", toString(requireTypeAtPosition({10, 29})));
    CHECK_EQ("Mid & Other", toString(requireTypeAtPosition({14, 35})));
    CHECK_EQ("Needy", toString(requireTypeAtPosition({18, 30})));
    CHECK_EQ("A", toString(requireTypeAtPosition({24, 26})));
    CHECK_EQ("B", toString(requireTypeAtPosition({26, 26})));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_values_have_their_needed_traits_members")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Base
            expect name: string
            function hello(self): string return "hi " .. self.name end
        end
        trait Needy needs Base
            function twice(self): string return self:hello() .. self:hello() end
        end
        class Good implements Needy
            name = "g"
        end
        class Missing implements Needy end
        local function f(n: Needy): string
            return n:hello() .. n.name .. n:twice()
        end
    )");

    // Base's expectation is still Base's: Missing is told, Good isn't accused of redeclaring `name`
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("'Missing' is missing a field to implement 'Needy':\n  • 'name: string' (from 'Base')", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "missing_trait_members_are_listed_in_one_error")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait HelpInfo
            expect public const name: string
            expect private help: string
        end
        trait ArgInProgress needs HelpInfo
            expect phrase: string
            expect function parse(self, s: string): boolean
        end
        trait Other
            expect other: number
        end
        class Flag implements ArgInProgress, Other
        end
    )");

    // one error per `implements` entry; a member a needed trait expects says which trait that is
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(
        "'Flag' is missing 4 members to implement 'ArgInProgress':\n"
        "  • 'function parse(self: ArgInProgress, s: string): boolean'\n"
        "  • 'phrase: string'\n"
        "  • 'private help: string' (from 'HelpInfo')\n"
        "  • 'public const name: string' (from 'HelpInfo')",
        toString(result.errors[0])
    );
    CHECK_EQ(result.errors[0].location.begin.line, 12);
    CHECK_EQ("'Flag' is missing a field to implement 'Other':\n  • 'other: number'", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_needs_graph_is_walked_once")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    // 30 levels of diamonds, about 2^30 paths from the top to the bottom; each trait has to be visited once
    std::string source;
    constexpr int kLevels = 30;
    for (int i = 0; i < kLevels; ++i)
    {
        if (i + 1 < kLevels)
        {
            source += format("trait L%d needs L%d, R%d end\n", i, i + 1, i + 1);
            source += format("trait R%d needs L%d, R%d end\n", i, i + 1, i + 1);
        }
        else
        {
            source += format("trait L%d function bottom(self): string return '' end end\n", i);
            source += format("trait R%d end\n", i);
        }
    }
    source += "class Top implements L0 end\n";
    source += "local function f(v: L0): string return v:bottom() end\n";
    source += "local s: string = f(Top())\n";
    source += "local r: R29 = Top()\n";

    CheckResult result = check(source);
    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_from_another_module")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::LuauExportValueTypecheck, true}, {FFlag::LuwuTraits, true}};

    fileResolver.source["game/A"] = R"(
        export trait Named
            expect name: string
            function hello(self): string
                return "hi " .. self.name
            end
        end
    )";

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)

        class Dog(name: string) implements A.Named
        end

        local h = Dog("rex"):hello()
        local n: A.Named = Dog("rex")
    )";

    CheckResult modB = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(modB);
    CHECK_EQ("string", toString(requireType("game/B", "h")));
}

// A trait needing one from another module is linked to it, so what that trait provides reaches the classes implementing
// it, and a trait implemented alongside that needs the same root sees it provided
TEST_CASE_FIXTURE(ClassesFixture, "trait_needing_a_trait_from_another_module")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::LuauExportValueTypecheck, true}, {FFlag::LuwuTraits, true}};

    fileResolver.source["game/Core"] = R"(
        export trait Markup
            expect function render(self): string
            function child_count(self): number
                return 0
            end
        end

        export trait Child needs Markup end

        export trait Element needs Markup
            expect const tag: string
            function render(self): string
                return `<{self.tag}>`
            end
        end
    )";

    fileResolver.source["game/Lists"] = R"(
        local core = require(game.Core)

        trait List needs core.Element
            items: { string } = { "a", "b" }
            function child_count(self): number
                return #self.items
            end
        end

        class UnorderedList implements List, core.Child
            const tag = "ul"
        end

        local rendered = UnorderedList():render()
        local count = UnorderedList():child_count()
        local element: core.Element = UnorderedList()
    )";

    CheckResult result = getFrontend().check("game/Lists");
    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("string", toString(requireType("game/Lists", "rendered")));
    CHECK_EQ("number", toString(requireType("game/Lists", "count")));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_values_are_traits_not_classes")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait T
            function f(self): number return 1 end
        end
        class C implements T end

        local o = C()
        local a = class.isinstance(o, T)
        local b = class.implements(o, C)
        local c = class.implements(o, T)
        local n = class.name(T)
        local t: trait = T
        local k: class = C
    )");

    // `class.isinstance` never matches a trait, and `class.implements` takes only traits
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(result.errors[0].location.begin.line, 7);
    CHECK_EQ(result.errors[1].location.begin.line, 8);
    CHECK_EQ("\"T\"", toString(requireType("n")));
}

TEST_CASE_FIXTURE(ClassesFixture, "type_guards_refine_class_object_and_trait")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait T end
        class C end

        local function f(x: class | trait, o: object | number)
            if typeof(x) == "trait" then
                local _t: trait = x
            else
                local _c: class = x
            end

            -- parentheses around the call keep it a type guard
            if (typeof(x)) == "class" then
                local _c: class = x
            end

            if typeof(o) == "object" then
                local _o: object = o
            end
        end

        f(T, 1)
        f(C, C())
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_of_narrows_to_the_class")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait T
            function again(self): T
                local Cls = class.of(self)
                return Cls()
            end
            expect function __init(self)
        end
        class A() implements T end
        class B() end

        local cond: boolean = true
        local a = class.of(A())
        local ab = class.of(if cond then A() else B())
        local u = class.of(1 :: unknown)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    // a class value prints as its bare name
    CHECK_EQ(follow(requireType("a")), follow(requireType("A")));
    CHECK_EQ("A | B", toString(requireType("ab")));
    CHECK_EQ("class?", toString(requireType("u")));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_values_have_their_needed_traits_metamethods")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Base
            function __div(self, other: string): Base return self end
        end
        trait Fs needs Base end
        class P implements Fs end

        local function f(p: Base)
            if class.implements(p, Fs) then
                local q = p / "x"
            end
        end
        f(P())
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "implementing_a_generic_trait_with_type_arguments")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};
    ScopedFastFlag defaultArguments{FFlag::LuwuDefaultArguments, true};

    CheckResult result = check(R"(
        trait Listable<T>
            expect public inner: { T }
            public function last(self): T
                return self.inner[#self.inner]
            end
            public function push(self, item: T)
                self.inner[#self.inner + 1] = item
            end
        end

        class Names(inner: { string } = {}) implements Listable<string> end

        class Gen<T = string>(inner: { T } = {}) implements Listable<T> end

        local n = Names()
        n:push("x")
        local s = n:last()
        local l: Listable<string> = n

        local g = Gen()
        g:push("y")
        local gs = g:last()
        local gl: Listable<string> = g
        local gn: Listable<number> = Gen<<number>>({ 1 })

        n:push(1)
    )");

    // only the last push, of a number where the trait's `T` is a string
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 26);
    CHECK_EQ("string", toString(requireType("s")));
    CHECK_EQ("string", toString(requireType("gs")));
    CHECK_EQ("Gen<string>", toString(requireType("g")));
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_trait_needs_pass_their_type_arguments_on")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        trait Root<T>
            expect public inner: { T }
            public function root(self): T?
                return self.inner[1]
            end
        end
        trait Base<T> needs Root<T>
            public function first(self): T?
                return self.inner[1]
            end
        end
        trait Mut<T> needs Base<T>
            public function push(self, item: T)
                self.inner[#self.inner + 1] = item
            end
        end
        trait Wrapped<T> needs Root<{ T }> end
        trait Concrete needs Root<string> end

        class Strs(inner: { string }) implements Mut<string> public inner end
        class Gen<T>(inner: { T }) implements Mut<T> public inner end
        class W(inner: { { number } }) implements Wrapped<number> public inner end
        class C(inner: { string }) implements Concrete public inner end

        local s = Strs({ "a" })
        local r = s:root()
        local g = Gen({ 1 })
        local gf = g:first()
        local function viaTrait(m: Mut<string>)
            return m:root()
        end
        local m: Mut<string> = s
        local b: Base<string> = m
        local o: Root<string> = m

        s:push(1)
    )");

    // only the push of a number onto strings
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 36);
    CHECK_EQ("string?", toString(requireType("r")));
    CHECK_EQ("number?", toString(requireType("gf")));
    CHECK_EQ("(Mut<string>) -> string?", toString(requireType("viaTrait")));
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_trait_needs_must_agree_with_the_implemented_instantiation")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        trait Root<T>
            expect public inner: { T }
        end
        trait Base<T> needs Root<T> end
        trait Mut<T> needs Base<T> end

        class Listed(inner: { string }) implements Base<string>, Mut<number> public inner end
        class Transitive(inner: { string }) implements Root<string>, Mut<number> public inner end
        class Agrees(inner: { string }) implements Base<string>, Mut<string> public inner end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("'Listed' implements 'Base<string>', but 'Mut<number>' needs 'Base<number>'", toString(result.errors[0]));
    CHECK_EQ(result.errors[0].location.begin.line, 7);
    CHECK_EQ("'Transitive' implements 'Root<string>', but 'Base<number>' needs 'Root<number>'", toString(result.errors[1]));
    CHECK_EQ(result.errors[1].location.begin.line, 8);
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_trait_needs_across_modules")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuTraits, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
    };

    fileResolver.source["game/A"] = R"(
        export trait Root<T>
            expect public inner: { T }
            public function root(self): T?
                return self.inner[1]
            end
        end
        export trait Base<T> needs Root<T> end
        export trait Mut<T> needs Base<T> end
    )";

    fileResolver.source["game/B"] = R"(
        local t = require(game.A)
        class Strs(inner: { string }) implements t.Mut<string> public inner end
        class Mismatch(inner: { string }) implements t.Root<string>, t.Mut<number> public inner end
        local r = Strs({ "a" }):root()
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("'Mismatch' implements 'Root<string>', but 'Base<number>' needs 'Root<number>'", toString(result.errors[0]));

    ModulePtr module = getFrontend().moduleResolver.getModule("game/B");
    REQUIRE(module);
    std::optional<TypeId> r = lookupName(module->getModuleScope(), "r");
    REQUIRE(r);
    CHECK_EQ("string?", toString(*r));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_type_argument_defaults_fill_unconstrained_arguments")
{
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};
    ScopedFastFlag defaultArguments{FFlag::LuwuDefaultArguments, true};

    CheckResult result = check(R"(
        class Box<T = string>(value: T? = nil) end

        local a = Box()
        local b = Box(1)
        local c: Box<boolean> = Box()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Box<string>", toString(requireType("a")));
    CHECK_EQ("Box<number>", toString(requireType("b")));
}

TEST_CASE_FIXTURE(ClassesFixture, "explicit_type_arguments_on_a_class_or_trait_call")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        trait Shape
            function __create<T>(n: number): T
                return (nil :: any)
            end
        end
        class Box<T>(v: T) end

        local s = Shape<<number>>(1)
        local b = Box<<string>>("x")
        local bad = Box<<string>>(5)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 10);
    CHECK_EQ("number", toString(requireType("s")));
    CHECK_EQ("Box<string>", toString(requireType("b")));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_expectation_mismatches_are_reported_on_the_member")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Named
            expect public name: string
            expect public function __init(self, n: string)
            expect public function greet(self, other: string): string
        end
        class A implements Named
            public name: number = 1
            public function __init(self, n: number)
            end
            public function greet(self, other: number): number
                return other
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    // `__init`, `greet` and `name`, each on its own declaration rather than on the class's name
    std::vector<unsigned> lines;
    for (const TypeError& error : result.errors)
    {
        CHECK(get<TypeMismatch>(error));
        lines.push_back(error.location.begin.line);
    }
    std::sort(lines.begin(), lines.end());
    CHECK_EQ(lines, std::vector<unsigned>{7, 8, 10});
}

TEST_CASE_FIXTURE(ClassesFixture, "expected_function_signatures_are_checked_where_they_are_written")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        trait Listable<T>
            expect public inner: { T }
            expect public function clone(self): Listable
        end

        class List<T>(inner: { T }) implements Listable<T>
            public inner
            public function clone(self): List<T>
                return List(self.inner)
            end
        end
    )");

    // only the missing type argument, where it is written: an implementation isn't compared with a broken signature
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 3);
    CHECK_EQ("Generic type 'Listable<T>' expects 1 type argument, but none are specified", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "a_generic_trait_expected_field_is_compared_with_the_class_type_arguments")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        trait Holder<T>
            expect value: T?
            function get(self): T? return self.value end
        end
        class Box<T> implements Holder<T>
            value: T?
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "generic_classes_sharing_a_parameter_name_get_their_own_generics")
{
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        class A<T>
            public value: T?
        end
        class B<T>
            public value: T?
            public function f(self, x: T): T?
                local y: T? = x
                return y
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "a_missing_member_of_a_needed_trait_is_reported_on_the_implements_entry")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Named
            expect name: string
        end
        trait Greeter needs Named
            function greet(self): string return self.name end
        end
        class Cat implements Greeter
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location, Location{{7, 29}, {7, 36}});
}

TEST_CASE_FIXTURE(ClassesFixture, "implements_and_needs_entries_that_are_not_traits_are_reported")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Known
            function a(self) return 1 end
        end
        class Cls end
        trait T needs Nope, Known
        end
        trait V needs Cls
        end
        class C implements Nope2
        end
        class D implements Known, Cls
        end
        trait W needs none
        end
        class E implements none
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(6, result);
    CHECK_EQ("Expected this to be a trait, but got 'nil'", toString(result.errors[0]));
    CHECK_EQ(result.errors[0].location, Location{{5, 22}, {5, 26}});
    CHECK_EQ("This is a class, traits are not allowed to depend on classes (only other traits)", toString(result.errors[1]));
    CHECK_EQ("Expected this to be a trait, but got 'nil'", toString(result.errors[2]));
    CHECK_EQ("This is a class, classes can only implement traits (not other classes)", toString(result.errors[3]));
    CHECK_EQ("Expected this to be a trait, but got 'none'", toString(result.errors[4]));
    CHECK_EQ("Expected this to be a trait, but got 'none'", toString(result.errors[5]));
}

// Every expression in an `implements` or `needs` entry needs a def in the data flow graph: the name, an expression in a
// type argument, and a trait argument
TEST_CASE_FIXTURE(ClassesFixture, "expressions_anywhere_in_trait_entries_are_checked")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag generics{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        trait Box<T>
            function f(self) return 1 end
        end
        local v = 1
        class A implements Box<typeof(v)> end
        class B implements Box<typeof(sqrt)> end
        trait N needs Box(sqrt) end
    )");

    // `needs` takes no arguments; the parse error is the only one
    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(ClassesFixture, "a_trait_where_a_class_is_expected_and_the_other_way_round")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Walker end
        class Cat implements Walker end
        local c = Cat()
        local a = class.isinstance(c, Walker)
        local b = class.implements(c, Cat)
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("Expected this to be a class, got trait 'Walker'", toString(result.errors[0]));
    CHECK_EQ("Expected this to be a trait, got class 'Cat'", toString(result.errors[1]));
}

// The checks refine by the callee's type, not its spelling (MagicClassInstanceCheck), so an alias refines like the
// original, as it compiles like it
TEST_CASE_FIXTURE(ClassesFixture, "aliased_class_checks_refine")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Named
            expect name: string
        end
        class Cat(public name: string, public lives: number) implements Named end

        const is = class.isinstance
        local implements = class.implements

        local function f(x: unknown, check: typeof(class.isinstance))
            if is(x, Cat) then
                local lives: number = x.lives
            end
            if implements(x, Named) then
                local name: string = x.name
            end
            if not is(x, Cat) then
                return
            end
            local afterGuard: number = x.lives
        end

        local function h(x: unknown, check: typeof(class.isinstance))
            -- a parameter typed as the function refines too: it's the same function type
            if check(x, Cat) then
                local viaParameter: number = x.lives
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// The same through the real builtin definitions, where registerBuiltinGlobals attaches the refinement
TEST_CASE_FIXTURE(BuiltinsFixture, "aliased_class_checks_refine_with_the_builtin_class_library")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag classGlobal{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};

    CheckResult result = check(R"(
        class Cat(public lives: number) end
        const is = class.isinstance

        local function g(x: unknown)
            assert(is(x, Cat))
            local asserted: number = x.lives
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu user-defined refinements

TEST_CASE_FIXTURE(ClassesFixture, "truthy_attribute_refines_its_parameter_when_the_call_is_truthy")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag refinements{FFlag::DebugLuwuUserDefinedRefinements, true};
    ScopedFastFlag cstAttr{FFlag::LuauCstAttr, true};

    CheckResult result = check(R"(
        trait Path
            expect public kind: string
            @[truthy(self, FilePath)]
            public function is_file(self): boolean
                return self.kind == "file"
            end
        end
        class FilePath(public kind: string) implements Path
            public function read(self): string return "" end
        end

        @[truthy(p, FilePath)]
        local function isFile(p: Path): boolean
            return p.kind == "file"
        end

        local function use(pathy: Path)
            if pathy:is_file() then
                local a: string = pathy:read()
            end
            if isFile(pathy) then
                local b: string = pathy:read()
            end
            if Path.is_file(pathy) then
                local c: string = pathy:read()
            end
            if not pathy:is_file() then
                local e: string = pathy:read()
                return
            end
            local d: string = pathy:read()
        end
    )");

    // a falsy result says nothing, so inside `if not ...` it is still a Path; past the guard it was truthy
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 28);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "truthy_attribute_targets_can_be_type_functions_of_the_argument")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag classGlobal{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};
    ScopedFastFlag refinements{FFlag::DebugLuwuUserDefinedRefinements, true};
    ScopedFastFlag cstAttr{FFlag::LuauCstAttr, true};

    CheckResult result = check(R"(
        class FilePath(public kind: string)
            public function read(self): string return "" end
        end
        class DirPath(public kind: string) end

        type function narrowTo(given, target)
            return types.intersectionof(given, target)
        end

        -- a generic that annotates a parameter is the argument's type at each call
        @[truthy(p, narrowTo<T, FilePath>)]
        local function isFile<T>(p: T): boolean
            return true
        end

        local function use(pathy: FilePath | DirPath)
            if isFile(pathy) then
                local s: string = pathy:read()
            end
            local either: FilePath | DirPath = pathy
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "truthy_attribute_errors")
{
    ScopedFastFlag refinements{FFlag::DebugLuwuUserDefinedRefinements, true};
    ScopedFastFlag cstAttr{FFlag::LuauCstAttr, true};

    CheckResult result = check(R"(
        class FilePath end
        @[truthy(q, FilePath)]
        local function isFile(p: unknown): boolean
            return true
        end
        @truthy
        local function bare(p: unknown): boolean
            return true
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("'truthy' takes a parameter and a type: write it as @[truthy(param, Type)]", toString(result.errors[0]));
    CHECK_EQ("'q' is not a parameter of this function", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "exact_checks_still_refine_when_false_with_user_defined_refinements_on")
{
    ScopedFastFlag refinements{FFlag::DebugLuwuUserDefinedRefinements, true};
    ScopedFastFlag cstAttr{FFlag::LuauCstAttr, true};

    CheckResult result = check(R"(
        class Cat(public lives: number) end
        class Dog(public bark: string) end
        local function f(x: Cat | Dog)
            if not class.isinstance(x, Cat) then
                local d: string = x.bark
                return
            end
            local lives: number = x.lives
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "a_class_with_private_fields_is_usable_through_its_traits")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Shape
            expect private sides: number
            public function describe(self): string
                return `{self.sides} sides`
            end
        end
        class Tri implements Shape
            private sides: number = 3
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_members_are_named_after_the_trait_in_errors")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait P
            expect private secret: number
            expect public const k: number
        end
        class A implements P
            private secret: number = 1
            public const k: number = 2
        end
        local function f(p: P)
            local _ = p.secret
            p.k = 3
        end
        f(A())
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("Field 'secret' of trait 'P' is private; accessing it here will raise a runtime error", toString(result.errors[0]));
    CHECK_EQ(
        "Field 'k' of trait 'P' is constant; assigning to it outside of '__init' will raise a runtime error", toString(result.errors[1])
    );
}


TEST_CASE_FIXTURE(ClassesFixture, "a_generic_trait_can_return_a_generic_class_declared_below_it")
{
    ScopedFastFlag _[4]{
        {FFlag::LuwuTraits, true},
        {FFlag::LuwuDefaultArguments, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuauExportValueSyntax, true},
    };
    // A class implementing a generic trait copies every `List<...>` its members mention. Each copy used to take a
    // constraint of its own, and the user's 60-member list went past the default limit of 1000. This one needs well
    // under 100.
    ScopedFastInt constraintLimit{FInt::LuauSolverConstraintLimit, 100};

    // The trait's `List<T>` is written before `List` exists, so instantiating the trait's members into `List` copied it
    // unexpanded, and the copy escaped the module.
    CheckResult result = check(R"(
        export trait Listable<T>
            expect public inner: { T }
            public function derive<U>(self, inner: { U }): List<U>
                return List(inner)
            end
            public function map<U>(self, mapper: (T) -> U): List<U>
                const mapped = {}
                for index, value in self.inner do
                    mapped[index] = mapper(value)
                end
                return self:derive(mapped)
            end
            public function filter(self, predicate: (T) -> boolean): List<T>
                const kept = {}
                for _, value in self.inner do
                    if predicate(value) then
                        kept[#kept + 1] = value
                    end
                end
                return self:derive(kept)
            end
            public function first(self): T?
                return self.inner[1]
            end
        end

        export class List<T>(
            inner: { T } = {}
        ) implements Listable<T>
            public inner
        end

        local lengths = List({ "a", "bb" }):map(function(s: string) return #s end)
        local long = lengths:filter(function(n: number) return n > 1 end)
        local n = long:first()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("number?", toString(requireType("n")));
}


TEST_CASE_FIXTURE(ClassesFixture, "implementing_a_trait_that_failed_to_export_is_reported_at_the_entry")
{
    ScopedFastFlag _[3]{{FFlag::LuwuTraits, true}, {FFlag::LuauExportValueSyntax, true}, {FFlag::LuauExportValueTypecheck, true}};
    // Loads the builtins first, which the limit below is too small for
    getFrontend();
    // Too small for the trait to be exported, so `A.Shown` is the error type in B
    ScopedFastInt childLimit{FInt::LuauTarjanChildLimit, 2};

    fileResolver.source["game/A"] = R"(
        export trait Shown
            public function show(self): string
                return "shown"
            end
        end
    )";

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)
        class Thing implements A.Shown
        end
    )";

    getFrontend().check("game/A");
    CheckResult result = getFrontend().check("game/B");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        "This trait failed to typecheck, so class 'Thing' gets none of its members; check the errors where it is defined",
        toString(result.errors[0])
    );
}


TEST_CASE_FIXTURE(ClassesFixture, "a_generic_class_with_many_generic_methods_checks_in_linear_time")
{
    ScopedFastFlag _[3]{{FFlag::LuwuDefaultArguments, true}, {FFlag::LuwuGenericNominals, true}, {FFlag::LuauExportValueSyntax, true}};

    // Each `m<U>(): List<U>` names its own instantiation of `List`. Building every instantiation's members up front made
    // n instantiations of n members each, which reported "too complex" when the module was exported at about 70 methods,
    // and walking them took time n^4.
    std::string source = "export class List<T>(inner: { T } = {})\n    public inner\n";
    for (int i = 0; i < 300; ++i)
        source += format("    public function m%d<U>(self, f: (T) -> U): List<U>\n        return List({})\n    end\n", i);
    source += "end\nlocal lengths = List({ \"a\" }):m0(function(s: string) return #s end)\nlocal n = lengths.inner\n";

    CheckResult result = check(source);

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("{number}", toString(requireType("n")));
}

TEST_CASE_FIXTURE(ClassesFixture, "a_class_implements_a_large_generic_trait_from_another_module")
{
    ScopedFastFlag _[5]{
        {FFlag::LuwuTraits, true},
        {FFlag::LuwuDefaultArguments, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
    };

    // The trait's module used to fail to export (see above), so `A.Listable` was the error type in B and `Components`
    // got none of its members
    std::string library = "export trait Listable<T>\n    expect public inner: { T }\n";
    for (int i = 0; i < 80; ++i)
        library += format("    public function m%d<U>(self, f: (T) -> U): List<U>\n        return List({})\n    end\n", i);
    library += R"(
    public function push_back(self, item: T)
        table.insert(self.inner, item)
        return self
    end
end

export class List<T>(inner: { T } = {}) implements Listable<T>
    public inner
end
)";
    fileResolver.source["game/A"] = library;

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)
        class Components(inner: { string } = {}) implements A.Listable<string>
            public inner
        end
        local c = Components():push_back("a")
        local first = c.inner[1]
    )";

    getFrontend().check("game/A");
    CheckResult result = getFrontend().check("game/B");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("string", toString(requireType("game/B", "first")));
}


TEST_CASE_FIXTURE(ClassesFixture, "a_table_literal_argument_takes_the_expected_type_argument")
{
    ScopedFastFlag _[2]{{FFlag::LuwuDefaultArguments, true}, {FFlag::LuwuGenericNominals, true}};

    // The literal's own type is `{ kind: "NotFound", path: string }`, and type arguments are invariant, so `T` has to be
    // `Data` for the call to produce the annotated type. A literal that doesn't fit `Data` gets its own type.
    CheckResult result = check(R"(
        type Data = { kind: "NotFound" | "Denied", path: string }

        class Pod<T>
            public data: T
        end
        class WithInit<T>
            public data: T
            public function __init(self, data: T)
                self.data = data
            end
        end
        class Primary<T>(public data: T)
        end

        local a: Pod<Data> = Pod { data = { kind = "NotFound", path = "/a" } }
        local b: WithInit<Data> = WithInit({ kind = "Denied", path = "/b" })
        local c: Primary<Data> = Primary({ kind = "NotFound", path = "/c" })
        local function fail(): Primary<Data>
            return Primary({ kind = "Denied", path = "/d" })
        end
        local wrong: Primary<Data> = Primary({ kind = "Nope", path = "/e" })
    )");

    // the last line: "Nope" is not a kind
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<TypeMismatch>(result.errors[0]));
    CHECK_EQ(21, result.errors[0].location.begin.line);
}


TEST_CASE_FIXTURE(ClassesFixture, "a_generic_class_static_is_generic_over_the_class_generics")
{
    ScopedFastFlag _[2]{{FFlag::LuwuDefaultArguments, true}, {FFlag::LuwuGenericNominals, true}};

    // Nothing instantiates `T` when a static is read through the class value, however the class value was reached
    CheckResult result = check(R"(
        class Box<T>(public value: T)
            public function make(v: T): Box<T>
                return Box(v)
            end
        end
        local direct = Box.make("str")
        local b: Box<number> = Box(5)
        local viaOf = class.of(b).make("str")
        local typed: class<Box<number>> = Box
        local viaAnnotation = typed.make("str")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Box<string>", toString(requireType("direct")));
    CHECK_EQ("Box<string>", toString(requireType("viaOf")));
    CHECK_EQ("Box<string>", toString(requireType("viaAnnotation")));
}

TEST_CASE_FIXTURE(ClassesFixture, "an_alias_of_class_of_a_generic_class_is_not_an_unapplied_type_function")
{
    ScopedFastFlag _[2]{{FFlag::LuwuDefaultArguments, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        class Box<T>(public value: T)
        end
        type BoxClass = class<Box<number>>
        local c: BoxClass = Box
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}


TEST_CASE_FIXTURE(BuiltinsFixture, "type_functions_take_classes_and_traits_as_types")
{
    ScopedFastFlag _[4]{
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuTraits, true},
        {FFlag::LuwuDefaultArguments, true},
        {FFlag::LuwuGenericNominals, true},
    };

    // A type function sees a class's members as code outside the class does: no private members, no constructor. A
    // generic class's `__init` is generic over the class's generics, so passing `List<number>` used to fail to read it
    // back ("Encountered unexpected generic").
    CheckResult result = check(R"(
        class Account(public owner: string, private balance: number)
            public function deposit(self, n: number)
                self.balance += n
            end
        end
        class List<T>(public inner: { T })
            public function map<U>(self, f: (T) -> U): List<U>
                return List({})
            end
        end
        trait Named
            expect public name: string
        end
        class Dog(public name: string) implements Named
        end

        type function keysOf(t)
            local names = {}
            for k in t:properties() do
                table.insert(names, k)
            end
            return types.unionof(table.unpack(names))
        end
        type function boxed(t)
            return types.newtable({ [types.singleton("item")] = { read = t, write = t } })
        end

        local deposit: keysOf<Account> = "deposit"
        local balance: keysOf<Account> = "balance"
        local map: keysOf<List<number>> = "map"
        local name: keysOf<Named> = "name"
        local item: boxed<List<string>> = { item = List({ "a" }) }
        local wrongItem: boxed<List<string>> = { item = List({ 1 }) }
        local named: boxed<Named> = { item = Dog("rex") }
    )");

    // `balance` is private, and a `List<number>` is not a `List<string>`
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(29, result.errors[0].location.begin.line);
    CHECK_EQ(33, result.errors[1].location.begin.line);
}

TEST_CASE_FIXTURE(ClassesFixture, "misspelled_field_write_reports_the_field_as_not_found")
{
    CheckResult result = check(R"(
        class Cat(name: string)
            function rename(self, n: string)
                self.nmae = n
            end
        end

        Cat.nmae = 1
    )");

    // not "Property nmae of type 'Cat' is read-only", which is what an extern type gets
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    // Luwu (helpful subtyping errors): the object's `nmae` is one typo from `name`; the class has no `name`
    if (FFlag::LuwuHelpfulSubtypingErrors)
        CHECK_EQ("Did you mean 'self.name' instead of 'self.nmae'?", toString(result.errors[0]));
    else
        CHECK_EQ("Member 'nmae' not found in object 'Cat'", toString(result.errors[0]));
    CHECK_EQ("Member 'nmae' not found in class 'Cat'", toString(result.errors[1]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "type_of_an_object_class_or_trait_value_compared_with_another_kind")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    ScopedFastFlag classGlobal{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};

    CheckResult result = check(R"(
        trait Named end
        class Cat(name: string) end
        class Dog(name: string) implements Named end

        local function f(c: Cat, n: Named, u: Cat | Dog)
            local a = type(c) == "table"
            local b = typeof(c) ~= "Cat"
            local d = type(Cat) == "table"
            local e = typeof(n) == "Named"
            local g = type(u) == "table"

            -- the right kind, a plain table, and a local named `type` are all fine
            local ok1 = type(c) == "object"
            local ok2 = typeof(Cat) == "class"
            local ok3 = type({}) == "table"
            local type = function(_: Cat): string return "table" end
            local ok4 = type(c) == "table"
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(5, result);
    CHECK_EQ(
        "'type' of an object of class 'Cat' is always \"object\", so comparing it with \"table\" is always false; to check whether a value "
        "is an object of class 'Cat', use 'class.isinstance(value, Cat)'",
        toString(result.errors[0])
    );
    CHECK_EQ(
        "'typeof' of an object of class 'Cat' is always \"object\", so comparing it with \"Cat\" is always true; to check whether a value "
        "is an object of class 'Cat', use 'class.isinstance(value, Cat)'",
        toString(result.errors[1])
    );
    CHECK_EQ("'type' of class 'Cat' is always \"class\", so comparing it with \"table\" is always false", toString(result.errors[2]));
    CHECK_EQ(
        "'typeof' of an object implementing 'Named' is always \"object\", so comparing it with \"Named\" is always false; to check whether a "
        "value implements 'Named', use 'class.implements(value, Named)'",
        toString(result.errors[3])
    );
    CHECK_EQ("'type' of 'Cat | Dog' is always \"object\", so comparing it with \"table\" is always false", toString(result.errors[4]));
}

TEST_CASE_FIXTURE(ClassesFixture, "comparing_an_object_with_a_class_explains_and_suggests_narrowing")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Named end
        class Cat(name: string) implements Named end
        class Dog(name: string) end

        local function f(d: Dog, o: object, u: Cat | Dog, n: Named)
            local a = d == Cat
            local b = o == Cat
            local c = u == Cat
            local e = n == Named
            local g = d == Named
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(5, result);
    CHECK_EQ(
        "Cannot compare unrelated types 'Dog' and 'class<Cat>' with '=='\n\n"
        "Help (comparing class with object of class):\n"
        "  - the type annotation 'Dog' refers to an object of 'class<Dog>'\n"
        "  - 'class<Cat>' is a class itself (what you call to make 'Cat' objects)\n"
        "  - A 'Dog' cannot be a 'Cat'",
        toString(result.errors[0])
    );
    CHECK_EQ(
        "Cannot compare unrelated types 'object' and 'class<Cat>' with '=='\n\n"
        "Help (comparing class with object of class):\n"
        "  - An object cannot be a class\n"
        "  - To narrow 'o' into a 'Cat', use 'class.isinstance(o, Cat)'",
        toString(result.errors[1])
    );
    CHECK_EQ(
        "Cannot compare unrelated types 'Cat | Dog' and 'class<Cat>' with '=='\n\n"
        "Help (comparing class with object of class):\n"
        "  - the object 'u' could be a 'Cat', but you're comparing it with the class itself (did you mean to narrow it instead?)\n"
        "  - To narrow 'u' into a 'Cat', use 'class.isinstance(u, Cat)'",
        toString(result.errors[2])
    );
    CHECK_EQ(
        "Cannot compare unrelated types 'Named' and 'trait<Named>' with '=='\n\n"
        "Help (comparing trait with object):\n"
        "  - the object 'n' implements 'Named', but you're comparing it with the trait itself (did you mean to narrow it instead?)\n"
        "  - To narrow 'n' into a 'Named', use 'class.implements(n, Named)'",
        toString(result.errors[3])
    );
    CHECK_EQ(
        "Cannot compare unrelated types 'Dog' and 'trait<Named>' with '=='\n\n"
        "Help (comparing trait with object):\n"
        "  - the type annotation 'Dog' refers to an object of 'class<Dog>'\n"
        "  - 'trait<Named>' is a trait itself, not an object that implements it\n"
        "  - A 'Dog' doesn't implement 'Named'",
        toString(result.errors[4])
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "objects_passed_where_the_table_library_wants_a_table")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag classGlobal{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};

    CheckResult result = check(R"(
        class Cat(name: string) end

        local function f(c: Cat)
            setmetatable(c, {})
            local a = table.clone(c)
            rawset(c, "name", "b")
            local b = next(c)
            local d = rawget(c, "name")
            table.freeze(c)
            return a, b, d
        end

        local function length(x: string | { number }): number
            return #x
        end
        local function h(c: Cat)
            return length(c)
        end

        -- generic code passing a `T` along is not affected
        local function g<T>(t: T): T
            return table.clone(t)
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(7, result);
    const std::string expected = "Expected this to be a table, but got an object of class 'Cat'; objects are not tables";
    // setmetatable, table.clone, rawset, next, rawget
    for (size_t i = 0; i < 5; ++i)
        CHECK_EQ(expected, toString(result.errors[i]));
    // table.freeze: `setmetatable(c, {})` above left `c` an object, not a metatable wrapped around one
    CHECK_EQ(expected, toString(result.errors[5]));
    CHECK_EQ("Expected this to be a string or a table, but got an object of class 'Cat'; objects are not tables", toString(result.errors[6]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "getmetatable_of_an_object_or_class_is_nil")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag classGlobal{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};

    CheckResult result = check(R"(
        class Cat(name: string) end

        local function f(c: Cat)
            return getmetatable(c), getmetatable(Cat)
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("(Cat) -> (nil, nil)", toString(requireType("f")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "iterating_an_object_without_iter_names_the_missing_metamethod")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};
    ScopedFastFlag classGlobal{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};
    ScopedFastFlag genericNominals{FFlag::LuwuGenericNominals, true};

    CheckResult result = check(R"(
        class Bag(items: { number }) end
        class Box<T>(item: T) end

        local function f(b: Bag, x: Box<number>)
            for k, v in b do end
            for k, v in x do end
            for k, v in Bag do end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK_EQ("Cannot iterate over an object of class 'Bag': its class doesn't define '__iter'", toString(result.errors[0]));
    CHECK_EQ("Cannot iterate over an object of class 'Box<number>': its class doesn't define '__iter'", toString(result.errors[1]));
    CHECK_EQ("Cannot iterate over class 'Bag' itself; only objects can be iterated, when their class defines '__iter'", toString(result.errors[2]));
}

// Luwu Traits (rfcs/classes/traits.md): `Self` in a trait method is the class it is called on
TEST_CASE_FIXTURE(ClassesFixture, "trait_self_is_the_implementing_class")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        trait Aliased
            private _aliases: { string } = {}
            public function aliases(self, alias: string): Self
                self._aliases[#self._aliases + 1] = alias
                return self
            end
        end

        trait Unprefixed needs Aliased
            public function aliases(self, alias: string): Self
                self._aliases[#self._aliases + 1] = alias
                return self
            end
        end

        class Command(public name: string) implements Unprefixed
            public function run(self): Self
                return self
            end
        end

        local c = Command("run"):aliases("r"):run()
        local viaTrait: Unprefixed = c
        local back = viaTrait:aliases("x")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Command", toString(requireType("c")));
    CHECK_EQ("Unprefixed", toString(requireType("back")));
}

// Luwu Traits (rfcs/classes/traits.md): a trait field naming `Self` has each implementing class's own type in that class
TEST_CASE_FIXTURE(ClassesFixture, "trait_self_fields_are_the_implementing_class")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        trait Linked
            public next: Self? = nil
            public children: { Self } = {}
        end

        class Node(public name: string) implements Linked
        end
        class Other() implements Linked
        end
        class Overrides() implements Linked
            public next: number? = nil
        end

        local n = Node("a")
        local next = n.next
        local kids = n.children
        local viaTrait: Linked = n
        local traitNext = viaTrait.next
        n.next = Other()
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    // the override has to fit `Self?` as Overrides has it
    CHECK_EQ(result.errors[0].location.begin.line, 11);
    // `next` is a `Node?` on a Node
    CHECK_EQ(result.errors[1].location.begin.line, 19);
    CHECK_EQ("Node?", toString(requireType("next")));
    CHECK_EQ("{Node}", toString(requireType("kids")));
    CHECK_EQ("Linked?", toString(requireType("traitNext")));
}

// Luwu Traits (rfcs/classes/traits.md): a trait function returning only `self` or `class.of(self)(...)` returns `Self`, and an
// implementing class's copy has the class for `Self`
TEST_CASE_FIXTURE(ClassesFixture, "trait_self_returns_infer_self")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        trait Aliased
            expect public function __init(self)
            public function chain(self)
                return self
            end
            public function copy(self)
                return class.of(self)()
            end
        end

        class Command() implements Aliased
            public function run(self) end
        end

        local c = Command():chain():copy()
        c:run()
        local viaTrait: Aliased = Command()
        local back = viaTrait:chain()
        local inClass = Command().chain
        local inTrait = viaTrait.chain
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Command", toString(requireType("c")));
    CHECK_EQ("Aliased", toString(requireType("back")));
    CHECK_EQ("(Command) -> Command", toString(requireType("inClass")));
    CHECK_EQ("(Self) -> Self", toString(requireType("inTrait")));
}

// Luwu Traits (rfcs/classes/traits.md): an override returning the trait where the overridden function returns `Self`
TEST_CASE_FIXTURE(ClassesFixture, "trait_self_mismatch_explains_self")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        trait Aliased
            public function aliases(self, first: string): Self
                return self
            end
        end
        trait Unprefixed needs Aliased
            public function aliases(self, first: string): Unprefixed
                return self
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this function to return 'Self', but got 'Unprefixed'\n"
        "`Self` is the class this is called on, and a `Unprefixed` may be a different class, so this has to be `Self` too: "
        "`self`, or `class.of(self)(...)` for a new object of its class"
    );
}

// Luwu Traits (rfcs/classes/traits.md): an override takes the parameter and return types it leaves out from the function it
// overrides, in a trait that needs the overridden one's trait and in a class implementing it
TEST_CASE_FIXTURE(ClassesFixture, "trait_overrides_infer_their_signature")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        trait Aliased
            public function aliases(self, first: string, ...: string): Self
                return self
            end
            public function describe(self, verbose: boolean): string
                return ""
            end
        end
        trait Unprefixed needs Aliased
            public function aliases(self, first, ...)
                return self
            end
        end
        class Command() implements Unprefixed
            public function describe(self, verbose)
                return "cmd"
            end
        end

        local inTrait = Unprefixed.aliases
        local inClass = Command().describe
        local c = Command():aliases("a", "b")
        c:describe(5)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 23);
    CHECK_EQ("(Self, string, ...string) -> Self", toString(requireType("inTrait")));
    CHECK_EQ("(Command, boolean) -> string", toString(requireType("inClass")));
    CHECK_EQ("Command", toString(requireType("c")));
}

// Luwu Traits (rfcs/classes/traits.md): `Self` in a class is the class, with its own generics
TEST_CASE_FIXTURE(ClassesFixture, "class_self_is_the_class")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        class Box<T>(public value: T)
            public function with(self, value: T): Self
                self.value = value
                return self
            end
        end

        local b = Box(1):with(2)
        local wrong: Box<string> = Box(1):with(2)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 9);
    CHECK_EQ("Box<number>", toString(requireType("b")));
}

// Luwu Traits (rfcs/classes/traits.md): the value `Self` constructs the class in a class, and the receiver's class in a
// trait method (through the trait's expected constructor)
TEST_CASE_FIXTURE(ClassesFixture, "self_class_value_constructs")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        trait Cloner
            expect value: number
            expect function __init(self, value: number)
            function clone(self)
                return Self(self.value)
            end
            function wrong(self)
                return Self("one")
            end
        end

        class Plain(value: number) implements Cloner
            function twin(self)
                return Self(self.value)
            end
        end

        local cloned = Plain(1):clone()
        local twin = Plain(1):twin()
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 8);
    CHECK_EQ("Plain", toString(requireType("cloned")));
    CHECK_EQ("Plain", toString(requireType("twin")));
}

// Luwu Traits (rfcs/classes/traits.md): a type the code names `Self` itself, like a trait parameter, isn't replaced
TEST_CASE_FIXTURE(ClassesFixture, "trait_parameter_named_self_wins")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};

    CheckResult result = check(R"(
        trait Fluent<Self>
            public function again(self): Self
                return self :: any
            end
        end

        class Thing() implements Fluent<Thing>
        end

        local t = Thing():again()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("Thing", toString(requireType("t")));
}

// Luwu Traits (rfcs/classes/traits.md): typing traits, classes that implement them, and their uses
const char* const kTraitTypewriterAfter = R"(
local unrelated = 1
print(unrelated + 1)
)";

TEST_CASE_FIXTURE(ClassesFixture, "typing_trait_declarations")
{
    ScopedFastFlag _[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuDefaultArguments, true}};
    const char* before = "local a = 1\n";

    const char* members = "trait Greeter\n"
                          "    expect name: string\n"
                          "    greeting = \"hello\"\n"
                          "\n"
                          "    function greet(self): string\n"
                          "        return self.greeting .. \", \" .. self.name\n"
                          "    end\n"
                          "end";
    checkTypewriting(*this, before, members, kTraitTypewriterAfter);

    const char* parameters = "trait Item(category: string, count = 0)\n"
                             "    expect name: string\n"
                             "    final kind = \"item\"\n"
                             "    expect function used?(self)\n"
                             "end";
    checkTypewriting(*this, before, parameters, kTraitTypewriterAfter);

    const char* access = "trait Toggle\n"
                         "    expect private model: string\n"
                         "    private on = false\n"
                         "    public function toggle(self): boolean\n"
                         "        self.on = not self.on\n"
                         "        return self.on\n"
                         "    end\n"
                         "end";
    // `self.` with no name yet is also an incomplete statement, a plain-Luau cascade that fix-existing-cascades (#63)
    // removes
    checkTypewriting(*this, before, access, kTraitTypewriterAfter, {"Incomplete statement"});
}

// Luwu Traits (rfcs/classes/traits.md): typing `Self` values doesn't cascade
TEST_CASE_FIXTURE(ClassesFixture, "typing_self_class_values")
{
    ScopedFastFlag flags[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuGenericNominals, true}};
    const char* before = "local a = 1\n";

    const char* inTrait = "trait Cloner\n"
                          "    expect value: number\n"
                          "    expect function __init(self, value: number)\n"
                          "    function clone(self)\n"
                          "        return Self(self.value)\n"
                          "    end\n"
                          "end";
    checkTypewriting(*this, before, inTrait, kTraitTypewriterAfter);

    const char* inClass = "class Point(x: number)\n"
                          "    function copy(self): Point\n"
                          "        return Self(self.x)\n"
                          "    end\n"
                          "end";
    checkTypewriting(*this, before, inClass, kTraitTypewriterAfter);
}

// Luwu Traits (rfcs/classes/traits.md): an unannotated `__implemented` is given a `class<Trait>`, the hook isn't a member of
// the implementing class, and a needing trait's own hook doesn't override the needed trait's final one
TEST_CASE_FIXTURE(ClassesFixture, "implemented_hook_is_given_the_class")
{
    ScopedFastFlag _{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Component
            expect function render(self): string
            final function __implemented(cls)
                local _n: number = cls
            end
        end

        trait Card needs Component
            final function __implemented(cls) end
        end

        class Button implements Component
            function render(self): string
                return "button"
            end
        end

        local _ = Button.__implemented
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(result.errors[0].location.begin.line, 4);
    const TypeMismatch* tm = get<TypeMismatch>(result.errors[0]);
    REQUIRE(tm);
    CHECK_EQ(toString(tm->givenType), "class<Component>");
    CHECK_EQ(result.errors[1].location.begin.line, 18);
    CHECK(get<UnknownProperty>(result.errors[1]));
}

// Luwu class reflection (rfcs/classes): `class.member` with a literal name has the member's type, and reports a name the
// class doesn't have; `class.fields` lists ClassMembers
TEST_CASE_FIXTURE(BuiltinsFixture, "class_member_has_the_members_type")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true},
        {FFlag::LuwuTraits, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::DebugLuwuTryExpressions, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::LuwuGenericNominals, true},
    };

    CheckResult result = check(R"(
        class Point(public const x: number, public y: string)
        end

        local p = Point(1, "a")
        local x = class.member(Point, "x")
        local n = x:get(p)
        local name = class.fields(p)[1].name
        local nope = class.member(p, "nope")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 8);
    CHECK(get<UnknownProperty>(result.errors[0]));
    CHECK_EQ(toString(requireType("x")), "ClassMember<number>");
    CHECK_EQ(toString(requireType("n")), "number");
    CHECK_EQ(toString(requireType("name")), "string");
}

// Luwu Traits (rfcs/classes/traits.md): a scoped function is read through the trait, and a class implements it with
// `function Trait.name`; neither is a member of the class
TEST_CASE_FIXTURE(ClassesFixture, "trait_scoped_functions_and_qualified_implementations")
{
    ScopedFastFlag _{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Json
            function Self.encode(value: unknown): string
                return "json"
            end

            function Self.serialize(self): string
                return "default"
            end

            expect function Self.tag(self): string
        end

        class Cat(public name: string) implements Json
            public function Json.serialize(self): string
                return self.name
            end

            public function Json.tag(self)
                return self.name
            end
        end

        local c = Cat("tom")
        local a = Json.encode(1)
        local b = c:Json:serialize()
        local d = c:Json:tag()
        local e = Cat:Json.encode(2)
        local f: number = c:Json:tag()
        local g = c:serialize()
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(toString(requireType("a")), "string");
    CHECK_EQ(toString(requireType("b")), "string");
    CHECK_EQ(toString(requireType("d")), "string");
    CHECK_EQ(toString(requireType("e")), "string");
    CHECK_EQ(result.errors[0].location.begin.line, 28);
    CHECK(get<TypeMismatch>(result.errors[0]));
    CHECK_EQ(result.errors[1].location.begin.line, 29);
    CHECK(get<UnknownProperty>(result.errors[1]));
}

// Luwu Traits (rfcs/classes/traits.md): what a class's qualified implementations and a trait's `needs` have to agree with
TEST_CASE_FIXTURE(ClassesFixture, "trait_qualified_implementations_are_checked")
{
    ScopedFastFlag _{FFlag::LuwuTraits, true};

    CheckResult result = check(R"(
        trait Json
            expect function Self.tag(self): string
        end

        trait Other
        end

        class Missing() implements Json
        end

        class Typo() implements Json
            function Json.tag(self): string return "" end
            function Json.nope(self) end
        end

        class Stray() implements Json
            function Json.tag(self): string return "" end
            function Other.x(self) end
        end

        trait Format needs Json
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(4, result);
    // a listed trait's scoped expectation, on the `implements` entry
    CHECK_EQ(result.errors[0].location.begin.line, 8);
    CHECK(toString(result.errors[0]).find("Json.tag") != std::string::npos);
    // a function the trait doesn't have, on its name
    CHECK_EQ(result.errors[1].location.begin.line, 13);
    CHECK(toString(result.errors[1]).find("nope") != std::string::npos);
    // a trait the class doesn't implement, on the qualifier
    CHECK_EQ(result.errors[2].location.begin.line, 18);
    CHECK(toString(result.errors[2]).find("Other") != std::string::npos);
    // a needing trait declares the scoped functions the trait it needs expects
    CHECK_EQ(result.errors[3].location.begin.line, 21);
    CHECK(toString(result.errors[3]).find("Self.tag") != std::string::npos);
}

// Luwu Traits (rfcs/classes/traits.md): typing scoped functions, qualified implementations and qualified reads
TEST_CASE_FIXTURE(ClassesFixture, "typing_trait_scoped_functions")
{
    ScopedFastFlag _{FFlag::LuwuTraits, true};
    const char* before = "local a = 1\n";

    const char* scoped = "trait Json\n"
                         "    function Self.encode(value: unknown): string\n"
                         "        return \"json\"\n"
                         "    end\n"
                         "    expect function Self.tag(self): string\n"
                         "end\n"
                         "class Cat() implements Json\n"
                         "    function Json.tag(self): string\n"
                         "        return \"cat\"\n"
                         "    end\n"
                         "end\n"
                         "local tag = Cat():Json:tag()\n"
                         "local encode = Cat:Json.encode";
    checkTypewriting(*this, before, scoped, kTraitTypewriterAfter);
}

// Luwu Traits (rfcs/classes/traits.md): typing a trait's `__implemented` hook
TEST_CASE_FIXTURE(ClassesFixture, "typing_an_implemented_hook")
{
    ScopedFastFlag _{FFlag::LuwuTraits, true};
    const char* before = "local a = 1\n";
    const char* hook = "trait Component\n"
                       "    final function __implemented(cls)\n"
                       "        local registered = cls\n"
                       "        print(registered)\n"
                       "    end\n"
                       "end";
    checkTypewriting(*this, before, hook, kTraitTypewriterAfter);
}

TEST_CASE_FIXTURE(ClassesFixture, "typing_traits_that_need_and_classes_that_implement")
{
    ScopedFastFlag _[2]{{FFlag::LuwuTraits, true}, {FFlag::LuwuDefaultArguments, true}};
    const char* before = R"(trait Named
    expect name: string
end

trait Item(category: string, count = 0)
    function is_weapon(self): boolean
        return self.category == "Weapon"
    end
end
)";

    const char* needs = "trait Greeter needs Named\n"
                        "    function greet(self): string\n"
                        "        return \"hi \" .. self.name\n"
                        "    end\n"
                        "end";
    checkTypewriting(*this, before, needs, kTraitTypewriterAfter);

    const char* implements = "class Cat implements Named\n"
                             "    name = \"whiskers\"\n"
                             "end";
    checkTypewriting(*this, before, implements, kTraitTypewriterAfter);

    const char* withArguments = "class Rifle implements Named, Item(\"Weapon\", 1)\n"
                                "    name = \"rifle\"\n"
                                "end";
    checkTypewriting(*this, before, withArguments, kTraitTypewriterAfter);
}

TEST_CASE_FIXTURE(ClassesFixture, "typing_trait_uses")
{
    ScopedFastFlag traits{FFlag::LuwuTraits, true};
    const char* before = R"(trait Greeter
    expect name: string
    function greet(self): string
        return "hi " .. self.name
    end
end

class Cat implements Greeter
    name = "whiskers"
end

local value: unknown = nil
)";

    checkTypewriting(*this, before, "local s = Cat():greet()", kTraitTypewriterAfter);
    checkTypewriting(*this, before, "local function welcome(g: Greeter): string\n    return g:greet()\nend", kTraitTypewriterAfter);
    checkTypewriting(*this, before, "if class.implements(value, Greeter) then\n    print(value:greet())\nend", kTraitTypewriterAfter);
}

// Luwu Classes (rfcs/classes): a method calling itself on another instance is checked like a recursive local function,
// with the signature its body is checked with
TEST_CASE_FIXTURE(ClassesFixture, "methods_call_themselves_on_other_instances")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    CheckResult result = check(R"(
class Node
    children: { Node } = {}

    function all(self): { Node }
        local nodes: { Node } = { self }
        for _, child in self.children do
            for _, node in child:all() do
                nodes[#nodes + 1] = node
            end
        end
        return nodes
    end

    function count(self)
        local total = 1
        for _, child in self.children do
            total += Node.count(child)
        end
        return total
    end
end
local all = Node():all()
local count = Node():count()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("all")), "{Node}");
    CHECK_EQ(toString(requireType("count")), "number");
}

// Luwu Traits (rfcs/classes/traits.md): a class's function in place of a trait's has to work as the trait's: code holding
// the trait calls it the trait's way
TEST_CASE_FIXTURE(ClassesFixture, "trait_function_overrides_fit_the_trait")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag luwuTraits{FFlag::LuwuTraits, true};
    // `Self` in a trait's signatures
    ScopedFastFlag luwuGenericNominals{FFlag::LuwuGenericNominals, true};
    CheckResult result = check(R"(
trait Layout
    function column(self): Self
        return self
    end
    function merge(self, other: Self): Self
        return self
    end
    function label(self, text: string, size: number?): string
        return text
    end
end

class Fine implements Layout
    function column(self, title: string?): Fine
        return self
    end
    function merge(self, other: Fine): Fine
        return self
    end
    function label(self, text: string, size: number?, bold: boolean?): string
        return text
    end
end

class Footer implements Layout
    function column(self, title: string, links: { string }): Footer
        return self
    end
    function label(self, text: number): string
        return ""
    end
end
    )");

    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(result.errors[0].location.begin.line, 26);
    CHECK_EQ(toString(result.errors[0]), "'column' requires 'title', which trait 'Layout' doesn't pass when it calls 'column'; make 'title' optional");
    CHECK_EQ(result.errors[1].location.begin.line, 29);
    CHECK(get<TypeMismatch>(result.errors[1]));
}

// Luwu Traits (rfcs/classes/traits.md): traits whose versions of a function can't be called each other's way conflict,
// which no function in the class can resolve
TEST_CASE_FIXTURE(ClassesFixture, "conflicting_trait_function_versions")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag luwuTraits{FFlag::LuwuTraits, true};
    CheckResult result = check(R"(
trait Named
    function label(self, text: string): string
        return text
    end
end
trait Numbered
    function label(self, index: number): number
        return index
    end
end
trait Titled
    function label(self, text: string): string
        return text
    end
end
class Item implements Named, Numbered
    function label(self, value: string): string
        return value
    end
end
class Same implements Named, Titled
    function label(self, value: number): string
        return ""
    end
end
    )");

    // `Same`'s traits agree, so it gets one ordinary mismatch rather than the conflict
    REQUIRE_EQ(result.errors.size(), 2);
    CHECK_EQ(result.errors[0].location.begin.line, 17);
    CHECK_EQ(
        toString(result.errors[0]),
        "Traits 'Named' and 'Numbered' conflict on function 'label'.\n- 'Named' has 'label: (string) -> string'\n- 'Numbered' has 'label: "
        "(number) -> number'\n\nHelp (conflicting trait functions):\n  - 'label' in class 'Item' can't be called as each of these at once\n"
        "  - Rename 'label' in one of the traits"
    );
    CHECK_EQ(result.errors[1].location.begin.line, 22);
    CHECK(get<TypeMismatch>(result.errors[1]));
}

TEST_CASE_FIXTURE(ClassesFixture, "classes_from_definition_files_keep_private_and_const_members")
{
    loadDefinition(R"(
        export class Counter(public const step: number)
            private count = 0
            public function bump(self)
                self.count += self.step
            end
        end
        declare counters: { Counter: class<Counter> }
    )");

    CheckResult result = check(R"(
        --!strict
        local counter = counters.Counter(1)
        local count = counter.count
        counter.step = 2
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<PrivatePropertyAccess>(result.errors[0]));
    CHECK(get<ConstPropertyAssignment>(result.errors[1]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "collections_classes_are_typed_from_their_declarations")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local map = collections.OrderedMap<<string, number>>()
        map:set("a", 1)
        local value = map:get("a")
        local range = bundle.pack(1, "x")
        local first = range.first
        local second = range.second
        for key, entry in map do
            local k: string = key
            local v: number = entry
        end
        local queue = collections.VecDeque<<string>>()
        queue:pushback("a")
        local front = queue:popfront()
        local list = collections.Vec<<number>>()
        for position, item in list do
            local p: number = position
            local i: number = item
        end
        map:set(1, "a")
        list:push("a")
        local private = map.order
        range.first = 2
    )");

    LUAU_REQUIRE_ERROR_COUNT(5, result);
    // `map:set(1, "a")`: the key and the value both have the wrong type
    CHECK_EQ(result.errors[0].location.begin.line, 20);
    CHECK_EQ(result.errors[1].location.begin.line, 20);
    CHECK_EQ(result.errors[2].location.begin.line, 21);
    // the declarations leave out private fields, which only the VM's classes have
    CHECK_EQ(result.errors[3].location.begin.line, 22);
    CHECK(get<ConstPropertyAssignment>(result.errors[4]));
    CHECK_EQ(toString(requireType("map")), "OrderedMap<string, number>");
    CHECK_EQ(toString(requireType("value")), "number?");
    CHECK_EQ(toString(requireType("first")), "number");
    CHECK_EQ(toString(requireType("second")), "string");
    CHECK_EQ(toString(requireType("front")), "string?");
}

// Luwu collections: Vec and VecDeque take their values as arguments, OrderedMap takes `.(key, value)` bundles, and the
// element types come from them
TEST_CASE_FIXTURE(BuiltinsFixture, "collections_constructors_type_their_elements_from_their_arguments")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local list = collections.Vec(1, 2, 3)
        list:push(4, 5)
        list:extend({ 6, 7 })
        local queue = collections.VecDeque("a", "b")
        queue:pushfront("y", "z")
        queue:extendback({ "c" })
        local map = collections.OrderedMap(bundle.pack("a", 1), bundle.pack("b", 2))
        local listed = collections.OrderedMap({ bundle.pack(true, "x") })
        local value = map:get("a")
        list:push(6, "seven")
        queue:extendfront({ 1 })
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(result.errors[0].location.begin.line, 11);
    CHECK_EQ(result.errors[1].location.begin.line, 12);
    CHECK_EQ(toString(requireType("list")), "Vec<number>");
    CHECK_EQ(toString(requireType("queue")), "VecDeque<string>");
    CHECK_EQ(toString(requireType("map")), "OrderedMap<string, number>");
    CHECK_EQ(toString(requireType("listed")), "OrderedMap<boolean, string>");
    CHECK_EQ(toString(requireType("value")), "number?");
}

// Luwu collections: a bundle iterates over its slots, so the value is any of its element types
TEST_CASE_FIXTURE(BuiltinsFixture, "collections_bundles_iterate_over_their_elements")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        for position, value in bundle.pack(1, "x") do
            local p: number = position
            local v: number | string = value
        end
        local color = bundle.pack(255, true, "red")
        for _, channel in color do
            local c: number | boolean | string = channel
        end
        for _, side in bundle.pack(1, 2, 3, 4) do
            local s: number = side
        end
        for _, value in bundle.pack(1, "x") do
            local wrong: number = value
        end
    )");

    // only the last loop is wrong: its value can be a string
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 14);
}

// Luwu try expressions: options and results are global classes, and an empty option is `none`, which gets `get` and
// `expect` while the library exists
TEST_CASE_FIXTURE(BuiltinsFixture, "option_and_result_types")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuTryExpressions, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local empty = none:get()
        local function fail(): number
            return none:expect("no value")
        end
        local hit = Some(5)
        local value = hit:get()
        local good = Ok("x")
        local text = good:expect()
        local bad = Err(42)
        local code = bad:err()
        local missing = bad:ok()
        local typed: Option<number> = None
        local typedhit: Option<number> = hit
        local either: Result<string, number> = if value > 1 then good else bad
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("empty")), "none");
    CHECK_EQ(toString(requireType("value")), "number");
    CHECK_EQ(toString(requireType("text")), "string");
    CHECK_EQ(toString(requireType("code")), "number");
    CHECK_EQ(toString(requireType("missing")), "none");
}

// Luwu try expressions: `try expr` is a Result of expr's type, with an unknown error
TEST_CASE_FIXTURE(BuiltinsFixture, "try_expression_is_a_result")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::DebugLuwuTryExpressions, true},
    };

    CheckResult result = check(R"(
        --!strict
        local function read(path: string): string
            return path
        end
        local text = try read("x")
        local sum = try 1 + 2
        local typed: Result<string, unknown> = try read("y")
        local wrong: number = try 1
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 8);
    CHECK_EQ(toString(requireType("text")), toString(requireType("typed")));
    CHECK(toString(requireType("sum")).find("Ok<number>") != std::string::npos);
}

// Luwu try expressions: `try` gives a Result, which isn't declared without `none`
TEST_CASE_FIXTURE(BuiltinsFixture, "try_needs_the_result_types")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuNonePrimitive, false},
        {FFlag::DebugLuwuTryExpressions, true},
    };

    CheckResult result = check(R"(
        local r = try tostring(1)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location, Location{{1, 18}, {1, 21}});
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_try_expressions")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::DebugLuwuTryExpressions, true},
    };
    const char* before = "local function read(path: string): string\n    return path\nend\n";
    const char* after = "\nlocal unrelated = 1\nprint(unrelated + 1)\n";

    checkTypewriting(*this, before, "local text = try read(\"x\")", after);
    checkTypewriting(*this, before, "local sum = try #\"ab\" + 1", after);
}

// Luwu try expressions: the error of `try f()` is f's hints and weakunknown, printed `unknown`
TEST_CASE_FIXTURE(BuiltinsFixture, "try_types_its_error_from_the_callees_hint")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::DebugLuwuTryExpressions, true},
    };

    CheckResult result = check(R"(
        --!strict
        type NotFound = { kind: "NotFound" }
        local function read(path: string): string ! NotFound
            return path
        end
        local function meow(): ! string
            error("unreachable")
        end
        local function parse(s: string)
            if s == "" then
                error({ line = 1 })
            end
            assert(#s < 10)
            if s == "x" then
                error("meow" :: "meow")
            end
            return s
        end
        local r = try read("x")
        local m = try meow()
        local p = try parse("x")
        local s = try 1 + 1
        local function wrap(): string
            return "x"
        end
        local w = try wrap()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("read")), "(string) -> string ! NotFound");
    CHECK_EQ(toString(requireType("wrap")), "() -> string");

    // the last statement's `error` doesn't count
    std::string meow = toString(requireType("meow"));
    CHECK_MESSAGE(meow.find("! string") != std::string::npos, meow);
    CHECK_MESSAGE(meow.find("unreachable") == std::string::npos, meow);

    // `error(v)` raises v's type as a local would hold it, and `assert` a string
    std::string parse = toString(requireType("parse"));
    CHECK_MESSAGE(parse.find("{ line: number }") != std::string::npos, parse);
    CHECK_MESSAGE(parse.find("\"meow\"") != std::string::npos, parse);
    CHECK_MESSAGE(parse.find("| string") != std::string::npos, parse);

    CHECK_EQ(toString(requireType("r")), "Err<NotFound | unknown> | Ok<string>");
    CHECK_EQ(toString(requireType("s")), "Err<unknown> | Ok<number>");
    CHECK_EQ(toString(requireType("w")), "Err<unknown> | Ok<string>");
}

// Luwu try expressions: `raises<F>` and `types.raises(f)` give what `try f()` would hold as its error
TEST_CASE_FIXTURE(BuiltinsFixture, "raises_gives_a_functions_error_hints")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::DebugLuwuTryExpressions, true},
    };

    CheckResult result = check(R"(
        --!strict
        type NotFound = { kind: "NotFound" }
        type Reader = (string) -> string ! NotFound
        type Plain = (string) -> string
        type function errorsof(fn)
            return types.raises(fn)
        end
        local a: raises<Reader> = { kind = "NotFound" }
        local b: raises<Plain> = 1
        local c: errorsof<Reader> = { kind = "NotFound" }
        local d: errorsof<Plain> = "x"
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("a")), "NotFound | unknown");
    CHECK_EQ(toString(requireType("b")), "unknown");
    std::string c = toString(requireType("c"));
    CHECK_MESSAGE(c.find("NotFound") != std::string::npos, c);
    CHECK_MESSAGE(c.find("unknown") != std::string::npos, c);
    CHECK_EQ(toString(requireType("d")), "unknown");
}

// Luwu try expressions: `X | unknown` keeps X's members. The unknown part is weakunknown, which accepts anything but
// doesn't absorb X.
TEST_CASE_FIXTURE(BuiltinsFixture, "weakunknown_keeps_the_rest_of_its_union")
{
    ScopedFastFlag sff{FFlag::DebugLuwuTryExpressions, true};

    CheckResult result = check(R"(
        --!strict
        type NotFound = { kind: "NotFound", path: string }
        local function handle(e: NotFound | unknown)
            local kind = e.kind
            local path: string = e.path
            return kind
        end
        local anything: NotFound | unknown = 1
        local plain: unknown = 1
        local optional: unknown? = 1
        local k = handle(2)
        local function pick(c: boolean, e: NotFound | unknown)
            return if c then e else 1
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("handle")), "(NotFound | unknown) -> \"NotFound\"");
    CHECK_EQ(toString(requireType("k")), "\"NotFound\"");
    CHECK_EQ(toString(requireType("plain")), "unknown");

    // a join keeps NotFound
    std::string pick = toString(requireType("pick"));
    CHECK_MESSAGE(pick.find("-> NotFound | ") != std::string::npos, pick);
}

// Luwu try expressions: weakunknown is found inside a nested union. `protect` returns `(Failure | weakunknown) | Failure`,
// and joining it must finish instead of recursing until the simplifier's limit raises an internal compiler error.
TEST_CASE_FIXTURE(BuiltinsFixture, "weakunknown_in_a_nested_union_simplifies")
{
    ScopedFastFlag sff{FFlag::DebugLuwuTryExpressions, true};

    CheckResult result = check(R"(
        --!strict
        type Failure = { message: string }
        local function protect<T>(f: () -> T): T | Failure
            return f()
        end
        local function g(): Failure | unknown
            return nil
        end
        local r = protect(g)
        local s = tostring(r)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("s")), "string");
}

// Luwu try expressions: a function whose return type is exactly an alias with a hint raises the hint, expanded with the
// alias's arguments and defaults
TEST_CASE_FIXTURE(BuiltinsFixture, "type_aliases_with_error_hints_are_return_types")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::DebugLuwuTryExpressions, true},
    };

    CheckResult result = check(R"(
        --!strict
        type NotFound = { kind: "NotFound" }
        type Lookup = string ! NotFound
        type Result<T, E = string> = T ! E
        type Checked<T> = T ! { bad: T }
        local function read(path: string): Lookup
            return path
        end
        local function parse(s: string): Result<number>
            return #s
        end
        local function load(s: string): (Result<boolean, NotFound>)
            return s == ""
        end
        local function check(n: number): Checked<number>
            return n
        end
        local function both(s: string): Lookup ! number
            return s
        end
        type Reader = (string) -> Result<string, NotFound>
        local reader: Reader = read
        declare function open(path: string): Result<number, NotFound>
        local r = try read("x")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("read")), "(string) -> string ! NotFound");
    CHECK_EQ(toString(requireType("parse")), "(string) -> number ! string");
    CHECK_EQ(toString(requireType("load")), "(string) -> boolean ! NotFound");
    CHECK_EQ(toString(requireType("check")), "(number) -> number ! { bad: number }");
    CHECK_EQ(toString(requireType("both")), "(string) -> string ! NotFound | number");
    CHECK_EQ(toString(requireType("reader")), "(string) -> string ! NotFound");
    CHECK_EQ(toString(requireType("open")), "(string) -> number ! NotFound");
    CHECK_EQ(toString(requireType("r")), "Err<NotFound | unknown> | Ok<string>");
}

// Luwu try expressions: the hint would be lost anywhere but a function's whole return type, so any other use is an error
TEST_CASE_FIXTURE(BuiltinsFixture, "type_aliases_with_error_hints_are_only_return_types")
{
    ScopedFastFlag sff{FFlag::DebugLuwuTryExpressions, true};

    CheckResult result = check(R"(
        --!strict
        type Lookup = string ! number
        type Result<T, E = string> = T ! E
        local v: Lookup = "x"
        type Holder = { value: Lookup }
        local f: () -> (Lookup, number)
        local list: { Result<string> }
        type Alias = Lookup
        local function take(x: Lookup) end
        local function returns(): Result<string>?
            return nil
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(7, result);
    for (size_t i = 0; i < result.errors.size(); ++i)
    {
        INFO(i);
        CHECK(get<GenericError>(result.errors[i]));
        CHECK_EQ(result.errors[i].location.begin.line, i + 4);
    }
}

// Luwu try expressions: a generic in a function parameter's hint is inferred from the argument's hint, so a pcall-like
// function can say what its failure value holds. An argument without a hint can raise anything.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_generic_error_hint_is_inferred_from_the_arguments_hint")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuTryExpressions, true},
        {FFlag::LuauHigherOrderGenericInference, true},
    };

    CheckResult result = check(R"(
        --!strict
        type BadCat = { kind: "BadCat" }
        local function protect<R, E, A...>(f: (A...) -> R ! E, ...: A...): (boolean, R | E | unknown)
            return true, f(...)
        end
        local function rethrow<R, E, A...>(f: (A...) -> R ! E, ...: A...): R | E
            return f(...)
        end
        local function meow(name: string): string ! BadCat
            return name
        end
        local function purr<T>(name: T): T ! BadCat
            return name
        end
        local function plain(n: number): number
            return n
        end
        local cat_name = "Taz"
        local succ, response = protect(meow, cat_name)
        local ok, plain_response = protect(plain, 1)
        local purred = rethrow(purr, cat_name)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    std::string response = toString(requireType("response"));
    CHECK_MESSAGE(response.find("string") != std::string::npos, response);
    CHECK_MESSAGE(response.find("BadCat") != std::string::npos, response);
    CHECK_MESSAGE(response.find("unknown") != std::string::npos, response);
    std::string plainResponse = toString(requireType("plain_response"));
    CHECK_MESSAGE(plainResponse.find("number") != std::string::npos, plainResponse);
    CHECK_MESSAGE(plainResponse.find("BadCat") == std::string::npos, plainResponse);

    // a generic function's hint too
    CHECK_EQ(toString(requireType("purred")), "BadCat | string");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_error_hints")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::DebugLuwuTryExpressions, true},
    };
    const char* before = "type NotFound = { kind: \"NotFound\" }\n";
    const char* after = "\nlocal unrelated = 1\nprint(unrelated + 1)\n";

    checkTypewriting(*this, before, "local function read(path: string): string ! NotFound | string\n    return path\nend", after);
    checkTypewriting(*this, before, "local function meow(): ! NotFound\n    return 1\nend", after);
    checkTypewriting(*this, before, "local function apply(read: (string) -> string ! NotFound)\n    return read(\"x\")\nend", after);
    checkTypewriting(*this, "type NotFound = { kind: \"NotFound\" }\ntype Lookup = string", " ! NotFound", after);
    const char* withLookup = "type NotFound = { kind: \"NotFound\" }\ntype Lookup = string ! NotFound\n";
    checkTypewriting(*this, withLookup, "local function read(path: string): Lookup\n    return path\nend", after);
}

// Luwu try expressions: an `Err` can say where its error was raised, with optional fields after the error
TEST_CASE_FIXTURE(BuiltinsFixture, "err_has_optional_position_fields")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuTryExpressions, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local bare = Err("boom")
        local placed = Err("boom", "main.luau", 12, "trace")
        local module = placed.module
        local line = placed.line
        local traceback = placed.traceback
        local wrong = Err("boom", 12)
    )");

    // the module has to be a string
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 7);
    CHECK_EQ(toString(requireType("module")), "string?");
    CHECK_EQ(toString(requireType("line")), "number?");
    CHECK_EQ(toString(requireType("traceback")), "string?");
}

// Luwu try expressions: `none` has only the methods the result library gives it, so any other key is missing
TEST_CASE_FIXTURE(BuiltinsFixture, "indexing_none_by_an_unknown_key_is_an_error")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuTryExpressions, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local empty = none:get()
        local a = none.foo
        local b = none["bar"]
        local function f(x: number | none)
            return x.baz
        end
        none.get = none.get
        local function g(s: string | none)
            return s.len
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(5, result);
    CHECK_EQ(result.errors[0].location.begin.line, 3);
    CHECK_EQ(result.errors[1].location.begin.line, 4);
    CHECK_EQ(result.errors[2].location.begin.line, 6);
    CHECK_EQ(result.errors[3].location.begin.line, 8);
    CHECK(get<PropertyAccessViolation>(result.errors[3]));
    // only `none` lacks `len`
    CHECK_EQ(toString(result.errors[4]), "this 'string' value is optional and may be 'none', use an if condition to check");
    CHECK_EQ(toString(requireType("empty")), "none");
}

// Luwu: without the result library `none` has no methods at all
TEST_CASE_FIXTURE(BuiltinsFixture, "none_has_no_keys_without_the_result_library")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuTryExpressions, false},
        {FFlag::LuwuNonePrimitive, true},
    };

    CheckResult result = check(R"(
        --!strict
        local a = none.foo
        local b = none:get()
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(result.errors[0].location.begin.line, 2);
    CHECK_EQ(result.errors[1].location.begin.line, 3);
}

// Luwu bundles: `bundle.pack`, `unpack`, `concat`, `sub` and `with` keep each position's own type. A literal position
// picks out one; any other position of `with` could be any of them.
TEST_CASE_FIXTURE(BuiltinsFixture, "bundle_functions_keep_each_position_type")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    // an `unknown` would fail to assign to any of these annotations
    CheckResult result = check(R"(
        --!strict
        local function f(n: number, s: string, flag: boolean, i: number)
            local b: bundle.Bundle3<number, string, boolean> = bundle.pack(n, s, flag)
            local x: number, y: string, z: boolean = bundle.unpack(b)
            local middle: string = bundle.unpack(b, 2, 2)
            local tail: bundle.Bundle2<string, boolean> = b:sub(2, 3)
            local replaced: bundle.Bundle3<string, string, boolean> = b:with(1, "text")
            local anywhere: bundle.Bundle3<number | boolean, string | boolean, boolean> = b:with(i, true)
            local joined: bundle.Bundle5<number, string, boolean, number, string> = bundle.concat(b, bundle.pack(1, "x"))
            local last: number = bundle.pack(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12).twelfth
            local empty: bundle.Bundle0 = bundle.pack()
            local wrong: bundle.Bundle2<number, string> = b:sub(2, 3)
            local tailFunction: bundle.Bundle2<string, boolean> = bundle.sub(b, 2, 3)
            local replacedFunction: bundle.Bundle3<string, string, boolean> = bundle.with(b, 1, "text")
            return x, y, z, middle, tail, replaced, anywhere, joined, last, empty, wrong, tailFunction, replacedFunction
        end
    )");

    // only `wrong` is: its positions are a string and a boolean
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 12);
}

// Luwu bundles: two bundles of the same size compare with their metamethods, whatever their position types
TEST_CASE_FIXTURE(BuiltinsFixture, "bundles_compare_with_their_metamethods")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local a = bundle.pack(1, 2)
        local b = bundle.pack(1, 3)
        local less: boolean = a < b
        local atMost: boolean = a <= b
        local same: boolean = a == b
        local mixed: boolean = bundle.pack(1, "x") == bundle.pack(2, "y")
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu bundles: assigning a bundle's field names the `with` call that makes a changed copy
TEST_CASE_FIXTURE(BuiltinsFixture, "assigning_a_bundle_field_points_at_with")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local b = bundle.pack(1, 2)
        b.second = 3
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<ConstPropertyAssignment>(result.errors[0]));
    CHECK(toString(result.errors[0]).find("b:with(2, value)") != std::string::npos);
}

// Luwu bundles: a bundle type prints the way `.(A, B)` writes it, whatever its class is called
TEST_CASE_FIXTURE(BuiltinsFixture, "bundle_types_print_as_dot_positional")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::DebugLuwuCollections, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
    };

    CheckResult result = check(R"(
        --!strict
        local pair = bundle.pack(1, "x")
        local nested = bundle.pack(bundle.pack(true), pair)
        local empty = bundle.pack()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ(toString(requireType("pair")), ".(number, string)");
    CHECK_EQ(toString(requireType("nested")), ".(.(boolean), .(number, string))");
    CHECK_EQ(toString(requireType("empty")), ".()");
}

// Luwu: `none` has no keys, so indexing an optional `T | none` names its `none` as the problem
TEST_CASE_FIXTURE(BuiltinsFixture, "indexing_none_by_a_key_is_an_error")
{
    ScopedFastFlag sff{FFlag::LuwuNonePrimitive, true};

    CheckResult result = check(R"(
        --!strict
        local a = none.foo
        local b = none["bar"]
        local function f(x: number | none)
            return x.baz
        end
        local function g(s: string | none)
            return s.len
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(4, result);
    CHECK_EQ(result.errors[0].location.begin.line, 2);
    CHECK_EQ(result.errors[1].location.begin.line, 3);
    CHECK_EQ(result.errors[2].location.begin.line, 5);
    // only `none` lacks `len`
    CHECK_EQ(result.errors[3].location.begin.line, 8);
    CHECK(toString(result.errors[3]).find("may be 'none'") != std::string::npos);
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_function_naming_self_without_a_self_parameter")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag luwuTraits{FFlag::LuwuTraits, true};
    ScopedFastFlag luwuGenericNominals{FFlag::LuwuGenericNominals, true};

    // `__create` names `Self` but has no `self`, so a class's copy has no `self` type to replace. Copying the trait into
    // the module's interface for the error on it must handle that missing type.
    CheckResult result = check(R"(
trait Path
    function __create(p: Self): number
    end
end

class FilePath implements Path
end
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<FunctionExitsWithoutReturning>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "trait_expecting_a_private_init_is_not_uninstantiable")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag luwuTraits{FFlag::LuwuTraits, true};

    // the trait's `__init` is the constructor its classes must have, not one of its own
    CheckResult result = check(R"(
trait Path
    expect private function __init(self, components: { string })
end
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "method_call_on_a_union_of_classes")
{
    CheckResult result = check(R"(
        --!strict
        class Cat(public name: string)
            public function speak(self): string
                return "meow"
            end
        end
        class Dog(public name: string)
            public function speak(self): string
                return "woof"
            end
        end

        local function talk(pet: Cat | Dog): string
            return pet:speak()
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// A class method takes extra arguments (`...any`) where a table's signature takes none; merged, the method takes none
TEST_CASE_FIXTURE(ClassesFixture, "method_call_on_a_union_of_a_class_and_a_table")
{
    CheckResult result = check(R"(
        --!strict
        type Named = { name: (self: Named) -> number }
        class Cat(public label: string)
            public function name(self): string
                return self.label
            end
        end

        local function adopt(): Cat | Named
            return Cat("tom")
        end
        local pet = adopt()
        local n = pet:name()
        pet:name("extra")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 14);
    CHECK_EQ(toString(requireType("n")), "number | string");
}

TEST_SUITE_END();
