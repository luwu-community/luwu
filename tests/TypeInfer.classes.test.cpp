// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Fixture.h"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/Error.h"
#include "Luau/StringUtils.h"
#include "ScopedFlags.h"
#include "doctest.h"

#include <algorithm>

using namespace Luau;

LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuDefaultArguments)
LUAU_FASTFLAG(LuwuGenericNominals)
LUAU_FASTFLAG(LuauAllowGlobalDeclarationToBeCalledClass);
LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(LuauExportValueSyntax)
LUAU_FASTFLAG(LuauExportValueTypecheck)
LUAU_FASTFLAG(LuwuDeclareStatements)
LUAU_FASTFLAG(LuwuTraits)

namespace
{

struct ClassesFixture : Fixture
{
    const std::string definitions = R"LUAU_SRC(
@checked declare function require(target: any): any
declare function sqrt(n: number): number
declare function tostring<T>(value: T): string
declare function typeof<T>(value: T): string

declare extern type Duration with
    read seconds: number
end

declare class: {
    isinstance: @checked (o: unknown, c: class) -> boolean,
    implements: @checked (o: unknown, t: class) -> boolean,
    of: @checked (o: unknown) -> class?,
    name: @checked (o: class | object) -> string,
    fields: @checked (o: class | object) -> ({ [string]: unknown }, boolean)
}
)LUAU_SRC";
    // the `class` library as the embedded definitions declare it with traits on, where `trait` is a type
    const std::string traitDefinitions = R"LUAU_SRC(
declare class: {
    isinstance: @checked (o: unknown, c: class) -> boolean,
    implements: @checked (o: unknown, t: trait) -> boolean,
    of: @checked (o: unknown) -> class?,
    name: @checked (o: class | object | trait) -> string,
    fields: @checked (o: class | object | trait) -> ({ [string]: unknown }, boolean)
}
)LUAU_SRC";
    Frontend& getFrontend() override
    {
        if (frontend)
            return *frontend;

        Frontend& f = Fixture::getFrontend();
        Luau::unfreeze(f.globals.globalTypes);

        f.loadDefinitionFile(f.globals, f.globals.globalScope, definitions, "@test", false);
        if (FFlag::LuwuTraits)
            f.loadDefinitionFile(f.globals, f.globals.globalScope, traitDefinitions, "@test", false);
        AstName reqName = f.globals.globalNames.names->getOrAdd("require");
        auto it = f.globals.globalScope->bindings.find(reqName);
        LUAU_ASSERT(it != f.globals.globalScope->bindings.end());
        attachTag(it->second.typeId, kRequireTagName);
        attachMagicFunction(it->second.typeId, std::make_shared<MagicRequire>());

        AstName classLibName = f.globals.globalNames.names->getOrAdd("class");
        auto classLibIt = f.globals.globalScope->bindings.find(classLibName);
        LUAU_ASSERT(classLibIt != f.globals.globalScope->bindings.end());
        if (TableType* ctv = getMutable<TableType>(classLibIt->second.typeId))
        {
            auto fieldsIt = ctv->props.find("fields");
            LUAU_ASSERT(fieldsIt != ctv->props.end() && fieldsIt->second.readTy);
            attachMagicFunction(*fieldsIt->second.readTy, std::make_shared<MagicClassFields>());

            auto nameIt = ctv->props.find("name");
            LUAU_ASSERT(nameIt != ctv->props.end() && nameIt->second.readTy);
            attachMagicFunction(*nameIt->second.readTy, std::make_shared<MagicClassName>());

            auto ofIt = ctv->props.find("of");
            LUAU_ASSERT(ofIt != ctv->props.end() && ofIt->second.readTy);
            attachMagicFunction(*ofIt->second.readTy, std::make_shared<MagicClassOf>());
        }

        registerTestTypes();
        Luau::freeze(f.globals.globalTypes);


        return *frontend;
    }
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    ScopedFastFlag sff_LuauAllowGlobalDeclarationToBeCalledClass{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
};

} // namespace

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

    CHECK(et->props.find("zero") != et->props.end());

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

        export const notAPoint = nil
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
    CHECK_EQ("Key 'age' not found in object 'Dog'", toString(result.errors[0]));
    CHECK_EQ("Key 'age' not found in class 'Dog'", toString(result.errors[1]));
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
    CHECK_EQ("Expected this to be 'class<Item>', but got 'Item'", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "object_class_mismatch_says_which_side_is_the_class")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    CheckResult result = check(R"(
class Item
    name: string
end

local a: Item = Item
)");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Expected this to be 'Item', but got 'class<Item>'", toString(result.errors[0]));
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

TEST_CASE_FIXTURE(ClassesFixture, "generic_class_value_against_its_object_is_spelled_as_class")
{
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
    CHECK_EQ("Expected this to be 'class<Box>', but got 'Box<number>'", toString(result.errors[0]));
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
            declare class type Path(public raw: string, public sep = string)
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

TEST_CASE_FIXTURE(ClassesFixture, "declared_class_construction_and_access")
{
    ScopedFastFlag luwuDeclareStatements{FFlag::LuwuDeclareStatements, true};

    CheckResult result = check(R"(
        --!strict
        declare class type Point
            x: number
            y = number
        end

        declare class type Counter
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
        declare class type Path
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
        export declare class type Path(raw: string)
            function join(self, other: string): Path
        end
        declare class type Hidden
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
        declare class type List<T>(items: { T })
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
        declare class type Path
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
        export declare class type Path
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
    CHECK_EQ("Missing field 'name: string' required for 'Package' to implement 'Item'", toString(result.errors[2]));
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
    CHECK_EQ("Missing function 'f(self: T): number' required for 'A' to implement 'T'", toString(result.errors[4]));
    CHECK(get<TypeMismatch>(result.errors[5]));
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

    // D settles the clash of `f` by defining it
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK_EQ("'shared' is already provided by trait 'A'", toString(result.errors[0]));
    CHECK_EQ("'shared' is already provided by trait 'A'", toString(result.errors[1]));
    CHECK_EQ(result.errors[1].location.begin.line, 11);
    CHECK_EQ("Traits 'A' and 'B' both provide 'f'", toString(result.errors[2]));
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
    )");

    // the diamond through Base is fine
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ("Traits 'A' and 'B' need each other; combine them into one trait", toString(result.errors[0]));
    CHECK_EQ("Traits 'B' and 'A' need each other; combine them into one trait", toString(result.errors[1]));
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
    CHECK_EQ("Missing field 'name: string' required for 'Missing' to implement 'Base'", toString(result.errors[0]));
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

TEST_SUITE_END();
