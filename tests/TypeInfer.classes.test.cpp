// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Fixture.h"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/Error.h"
#include "ScopedFlags.h"
#include "doctest.h"

using namespace Luau;

LUAU_FASTFLAG(DebugLuauUserDefinedClasses)
LUAU_FASTFLAG(DebugLuauUserDefinedClassesRuntime)
LUAU_FASTFLAG(LuwuBetterUserDefinedClasses)
LUAU_FASTFLAG(LuwuDefaultArguments)
LUAU_FASTFLAG(LuwuGenericNominals)
LUAU_FASTFLAG(LuauAllowGlobalDeclarationToBeCalledClass);
LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(LuauExportValueSyntax)
LUAU_FASTFLAG(LuauExportValueTypecheck)

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
    of: @checked (o: unknown) -> class?,
    name: @checked (o: class | object) -> string,
    fields: @checked (o: class | object) -> ({ [string]: unknown }, boolean)
}
)LUAU_SRC";
    Frontend& getFrontend() override
    {
        if (frontend)
            return *frontend;

        Frontend& f = Fixture::getFrontend();
        Luau::unfreeze(f.globals.globalTypes);

        f.loadDefinitionFile(f.globals, f.globals.globalScope, definitions, "@test", false);
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
        }

        registerTestTypes();
        Luau::freeze(f.globals.globalTypes);


        return *frontend;
    }
    ScopedFastFlag sff_DebugLuauUserDefinedClasses{FFlag::DebugLuauUserDefinedClasses, true};
    ScopedFastFlag sff_LuauAllowGlobalDeclarationToBeCalledClass{FFlag::LuauAllowGlobalDeclarationToBeCalledClass, true};
    DOES_NOT_PASS_OLD_SOLVER_GUARD();
};

} // namespace

TEST_SUITE_BEGIN("ClassesConformance");

TEST_CASE_FIXTURE(ClassesFixture, "Point_tostring")
{
    ScopedFastFlag sff_DebugLuauUserDefinedClasses{FFlag::DebugLuauUserDefinedClasses, true};
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};
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
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauUserDefinedClasses, true},
        {FFlag::LuwuBetterUserDefinedClasses, true},
    };

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
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauUserDefinedClasses, true},
        {FFlag::LuwuBetterUserDefinedClasses, true},
    };

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
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauUserDefinedClasses, true},
        {FFlag::LuwuBetterUserDefinedClasses, true},
    };

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
    CHECK_EQ("make_name", get<UnknownSymbol>(result.errors[0])->name);
    CHECK_EQ("next_id", get<UnknownSymbol>(result.errors[1])->name);
    CHECK_EQ("compute_price", get<UnknownSymbol>(result.errors[2])->name);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_pod_constructor_argument_optional_when_all_properties_have_defaults")
{
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
    ScopedFastFlag sff_better{FFlag::LuwuBetterUserDefinedClasses, true};
    ScopedFastFlag sff_runtime{FFlag::DebugLuauUserDefinedClassesRuntime, true};

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
    // `userdata` is now a writable type annotation (like `object`/`class`), and `typeof(x) == "..."`
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
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauUserDefinedClasses, true},
        {FFlag::LuwuBetterUserDefinedClasses, true},
    };

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauUserDefinedClasses, true},
        {FFlag::LuwuBetterUserDefinedClasses, true},
    };

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

    CheckResult result = check(R"(
class Cat end
class Dog end

local function nameOf(x: Cat | typeof(Cat) | Dog)
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
    ScopedFastFlag sff_LuwuBetterUserDefinedClasses{FFlag::LuwuBetterUserDefinedClasses, true};

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


// Primary constructors (rfcs/classes.md): each parameter declares a public field, and the class is
// constructed positionally through the `__init` the parameter list implies.

TEST_CASE_FIXTURE(ClassesFixture, "primary_constructor_declares_a_field_per_parameter")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    // a parameter may carry its field's access specifier and `const` modifier directly, instead of
    // restating the field in the class body (rfcs/classes.md)
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuBetterUserDefinedClasses, true},
    };

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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
        {FFlag::LuwuBetterUserDefinedClasses, true},
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
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauUserDefinedClasses, true},
        {FFlag::LuwuBetterUserDefinedClasses, true},
    };

    // A class is referenced as a global, and the join at the end of the `if` produces a phi def
    // that nothing binds a type to: `prepopulateGlobalScope` maps every global reference's def
    // onto its binding up front, but it runs before the class prepass creates that binding. Every
    // reference to the class downstream of the branch used to silently come back as *error-type*,
    // which swallowed real errors rather than producing new ones -- hence the deliberately wrong
    // annotations, which are what the bug made disappear.
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
    CHECK_EQ("Prefix", toString(get<TypeMismatch>(result.errors[0])->givenType));
    CHECK_EQ("Prefix", toString(get<TypeMismatch>(result.errors[1])->givenType));
}

TEST_CASE_FIXTURE(ClassesFixture, "constructor_argument_that_is_itself_a_constructor_call")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauUserDefinedClasses, true},
        {FFlag::LuwuBetterUserDefinedClasses, true},
    };

    // Construction dispatches through the class's `__call` metamethod, and a final argument that
    // is itself a call contributes a type *pack*, which skips the per-argument check in
    // TypeChecker2 and leaves overload resolution as the only thing looking at it. Its report was
    // indexed one slot short of the pack the resolver actually tested (which carries the forwarded
    // callee at the front), so for a one-argument constructor it resolved against nothing at all
    // and the mismatch went unreported. Every other spelling of the same wrong argument -- a
    // literal, a local, a parenthesized call -- was caught, which is what made this so quiet.
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
        CHECK_EQ("Comp", toString(get<TypeMismatch>(result.errors[i])->wantedType));
        CHECK_EQ("Other", toString(get<TypeMismatch>(result.errors[i])->givenType));
    }
}

TEST_SUITE_END();
