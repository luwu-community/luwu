// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Fixture.h"

#include "Luau/BuiltinDefinitions.h"
#include "Luau/Error.h"
#include "ScopedFlags.h"
#include "doctest.h"

using namespace Luau;

LUAU_FASTFLAG(DebugLuauUserDefinedClasses)
LUAU_FASTFLAG(LuauBetterUserDefinedClasses)
LUAU_FASTFLAG(LuauGenericNominals)
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

declare class: {
    isinstance: @checked (o: unknown, c: class) -> boolean,
    classof: @checked (o: unknown) -> class?,
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
    auto result = check(R"(
class Point
    public x
    public y
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
    auto result = check(R"(
class Point
    public x
    public y

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
    auto result = check(R"(
class Point
    public x
    public y

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
    auto result = check(R"(
class Person
    public name
    public age

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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

    auto result = check(R"(
class Cat
    age: string = 4

    function __init(self) end
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<TypeMismatch>(result.errors[0]));
}

TEST_CASE_FIXTURE(ClassesFixture, "class_pod_constructor_argument_optional_when_all_properties_have_defaults")
{
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

    auto result = check(R"(
class Mixed
    a = 0
    public b: string

    function greet(self)
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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

    auto result = check(R"(
class Thingy
    public name: string
    public age: number

    function __init(self, name: string, age: number)
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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

    auto result = check(R"(
class Thingy
    public name: string
    public age: number

    function __init(self, name: string, age: number)
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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

    auto result = check(R"(
class Thingy
    public const name: string

    function __init(self, name: string)
        self.name = name
    end
end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_const_property_cannot_be_assigned_from_other_methods")
{
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

    auto result = check(R"(
class Thingy
    public const name: string

    function __init(self, name: string)
        self.name = name
    end

    function rename(self, name: string)
        self.name = name
    end
end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    auto err = get<ConstPropertyAssignment>(result.errors[0]);
    REQUIRE(err);
    CHECK_EQ("name", err->key);
}

TEST_CASE_FIXTURE(ClassesFixture, "class_const_property_cannot_be_assigned_from_outside_the_class")
{
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

    auto result = check(R"(
class Thingy
    public const name: string

    function __init(self, name: string)
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
}

TEST_CASE_FIXTURE(ClassesFixture, "class_non_const_property_can_be_assigned_anywhere")
{
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

    auto result = check(R"(
class Thingy
    public name: string

    function __init(self, name: string)
        self.name = name
    end

    function rename(self, name: string)
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
        {FFlag::LuauBetterUserDefinedClasses, true},
        {FFlag::LuauGenericNominals, true},
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
    CheckResult result = check(R"(
class Point
    public x: number
    public y: number

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
    ScopedFastFlag sff_LuauBetterUserDefinedClasses{FFlag::LuauBetterUserDefinedClasses, true};

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
    CheckResult result = check(R"(
class Point
    public x: number
    public y: number

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


TEST_SUITE_END();
