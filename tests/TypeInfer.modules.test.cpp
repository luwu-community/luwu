// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/AstQuery.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Scope.h"
#include "Luau/TypeInfer.h"
#include "Luau/Type.h"

#include "Fixture.h"

#include "Luau/VisitType.h"
#include "doctest.h"

LUAU_FASTFLAG(LuauInstantiateInSubtyping)
LUAU_FASTFLAG(DebugLuauForceOldSolver)
LUAU_FASTFLAG(DebugLuauMagicTypes)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuauExportValueSyntax)
LUAU_FASTFLAG(LuauExportValueTypecheck)
LUAU_FASTINT(LuauSolverConstraintLimit)
LUAU_FASTFLAG(LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier)
LUAU_FASTFLAG(LuwuDestructuring)

using namespace Luau;

TEST_SUITE_BEGIN("TypeInferModules");

TEST_CASE_FIXTURE(BuiltinsFixture, "dcr_require_basic")
{
    fileResolver.source["game/A"] = R"(
        --!strict
        return {
            a = 1,
        }
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local b = A.a
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    REQUIRE(b != nullptr);
    std::optional<TypeId> bType = requireType(b, "b");
    REQUIRE(bType);
    CHECK(toString(*bType) == "number");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "require")
{
    fileResolver.source["game/A"] = R"(
        local function hooty(x: number): string
            return "Hi there!"
        end

        return {hooty=hooty}
    )";

    if (!FFlag::DebugLuauForceOldSolver)
    {
        fileResolver.source["game/B"] = R"(
            local Hooty = require(game.A)

            local h = 4
            local i = Hooty.hooty(h)
        )";
    }
    else
    {
        fileResolver.source["game/B"] = R"(
            local Hooty = require(game.A)

            local h -- free!
            local i = Hooty.hooty(h)
        )";
    }

    CheckResult aResult = getFrontend().check("game/A");
    dumpErrors(aResult);
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    dumpErrors(bResult);
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");

    REQUIRE(b != nullptr);

    dumpErrors(bResult);

    std::optional<TypeId> iType = requireType(b, "i");
    REQUIRE_EQ("string", toString(*iType));

    std::optional<TypeId> hType = requireType(b, "h");
    REQUIRE_EQ("number", toString(*hType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "require_types")
{
    fileResolver.source["workspace/A"] = R"(
        export type Point = {x: number, y: number}

        return {}
    )";

    fileResolver.source["workspace/B"] = R"(
        local Hooty = require(workspace.A)

        local h: Hooty.Point
    )";

    CheckResult bResult = getFrontend().check("workspace/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("workspace/B");
    REQUIRE(b != nullptr);

    TypeId hType = requireType(b, "h");
    REQUIRE_MESSAGE(bool(get<TableType>(hType)), "Expected table but got " << toString(hType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "require_a_variadic_function")
{
    fileResolver.source["game/A"] = R"(
        local T = {}
        function T.f(...) end
        return T
    )";

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)
        local f = A.f
    )";

    CheckResult result = getFrontend().check("game/B");

    ModulePtr bModule = getFrontend().moduleResolver.getModule("game/B");
    REQUIRE(bModule != nullptr);

    TypeId f = follow(requireType(bModule, "f"));

    const FunctionType* ftv = get<FunctionType>(f);
    REQUIRE(ftv);

    auto iter = begin(ftv->argTypes);
    auto endIter = end(ftv->argTypes);

    REQUIRE(iter == endIter);
    REQUIRE(iter.tail());

    CHECK(get<VariadicTypePack>(*iter.tail()));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "cross_module_table_freeze")
{
    fileResolver.source["game/A"] = R"(
        --!strict
        return {
            a = 1,
        }
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        return table.freeze(require(game.A))
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr a = getFrontend().moduleResolver.getModule("game/A");
    REQUIRE(a != nullptr);
    // confirm that no cross-module mutation happened here!
    CHECK(toString(a->returnType) == "{ a: number }");

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    REQUIRE(b != nullptr);
    // confirm that no cross-module mutation happened here!
    if (!FFlag::DebugLuauForceOldSolver)
        CHECK(toString(b->returnType) == "{ read a: number }");
    else
        CHECK(toString(b->returnType) == "{ a: number }");
}

TEST_CASE_FIXTURE(Fixture, "type_error_of_unknown_qualified_type")
{
    CheckResult result = check(R"(
        local p: SomeModule.DoesNotExist
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    // Luwu: the error says `SomeModule` isn't a module; upstream's carries only the name
    UnknownSymbol expected{"SomeModule.DoesNotExist", UnknownSymbol::Context::Type};
    expected.qualifier = UnknownSymbol::Qualifier::NotAModule;
    REQUIRE_EQ(result.errors[0], (TypeError{Location{{1, 17}, {1, 40}}, expected}));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "require_module_that_does_not_export")
{
    const std::string sourceA = R"(
    )";

    const std::string sourceB = R"(
        local Hooty = require(script.Parent.A)
    )";

    fileResolver.source["game/Workspace/A"] = sourceA;
    fileResolver.source["game/Workspace/B"] = sourceB;

    getFrontend().check("game/Workspace/A");
    getFrontend().check("game/Workspace/B");

    ModulePtr aModule = getFrontend().moduleResolver.getModule("game/Workspace/A");
    ModulePtr bModule = getFrontend().moduleResolver.getModule("game/Workspace/B");

    CHECK(aModule->errors.empty());
    REQUIRE_EQ(1, bModule->errors.size());
    CHECK_MESSAGE(get<IllegalRequire>(bModule->errors[0]), "Should be IllegalRequire: " << toString(bModule->errors[0]));

    auto hootyType = requireType(bModule, "Hooty");

    CHECK_EQ("*error-type*", toString(hootyType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "warn_if_you_try_to_require_a_non_modulescript")
{
    fileResolver.source["Modules/A"] = "";
    fileResolver.sourceTypes["Modules/A"] = SourceCode::Script;

    fileResolver.source["Modules/B"] = R"(
        local M = require(script.Parent.A)
    )";

    CheckResult result = getFrontend().check("Modules/B");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    CHECK(get<IllegalRequire>(result.errors[0]));
}

// Luwu: the reason comes before the module's path, which is often a long absolute one
TEST_CASE_FIXTURE(BuiltinsFixture, "requiring_a_module_that_returns_nothing_says_why_before_the_path")
{
    fileResolver.source["Modules/A"] = "local x = 1";

    fileResolver.source["Modules/B"] = R"(
        local M = require(script.Parent.A)
    )";

    CheckResult result = getFrontend().check("Modules/B");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        "Cannot require this module because it doesn't evaluate to a value\n"
        "Module at path: Modules.A\n"
        "\n"
        "Help (requiring modules):\n"
        "  - You probably forgot to return a value\n"
        "  - A required module may return exactly one value or export one or more values\n"
        "  - If you don't need to return any values, return 'nil'",
        toString(result.errors[0])
    );
}

// Luwu: upstream takes the first of several returned values without a word
TEST_CASE_FIXTURE(BuiltinsFixture, "requiring_a_module_that_returns_several_values_is_an_error")
{
    fileResolver.source["Modules/A"] = "return 1, 'two'";
    fileResolver.source["Modules/C"] = "return 1, 2, ...";

    fileResolver.source["Modules/B"] = R"(
        local a = require(script.Parent.A)
        local c = require(script.Parent.C)
    )";

    CheckResult result = getFrontend().check("Modules/B");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(
        "This module returns too many values (2)\n"
        "Module at path: Modules.A\n"
        "\n"
        "Help (requiring modules):\n"
        "  - Luwu expects modules to return exactly one value\n"
        "  - Not all runtimes support returning multiple values",
        toString(result.errors[0])
    );
    CHECK(toString(result.errors[1]).find("This module returns too many values (at least 2)\n") == 0);

    // the require is still typed as the first value
    CHECK_EQ("number", toString(requireType("Modules/B", "a")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "general_require_call_expression")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS()
    fileResolver.source["game/A"] = R"(
--!strict
return { def = 4 }
    )";

    fileResolver.source["game/B"] = R"(
--!strict
local tbl = { abc = require(game.A) }
local a : string = ""
a = tbl.abc.def
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Expected this to be 'string', but got 'number'", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "general_require_type_mismatch")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS()
    fileResolver.source["game/A"] = R"(
return { def = 4 }
    )";

    fileResolver.source["game/B"] = R"(
local tbl: string = require(game.A)
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("Expected this to be 'string', but got '{ def: number }'", toString(result.errors[0]));
}

TEST_CASE_FIXTURE(Fixture, "bound_free_table_export_is_ok")
{
    CheckResult result = check(R"(
local n = {}
function n:Clone() end

local m = {}

function m.a(x)
    x:Clone()
end

function m.b()
    m.a(n)
end

return m
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "custom_require_global")
{
    CheckResult result = check(R"(
--!nonstrict
require = function(a) end

local crash = require(game.A)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "require_failed_module")
{
    fileResolver.source["game/A"] = R"(
return unfortunately()
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_ERRORS(aResult);

    CheckResult result = check(R"(
local ModuleA = require(game.A)
    )");
    LUAU_REQUIRE_NO_ERRORS(result);

    std::optional<TypeId> oty = requireType("ModuleA");

    CHECK_EQ("*error-type*", toString(*oty));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "do_not_modify_imported_types")
{
    fileResolver.source["game/A"] = R"(
export type Type = { unrelated: boolean }
return {}
    )";

    fileResolver.source["game/B"] = R"(
local types = require(game.A)
type Type = types.Type
local x: Type = {}
function x:Destroy(): () end
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(2, result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "do_not_modify_imported_types_2")
{
    fileResolver.source["game/A"] = R"(
export type Type = { x: { a: number } }
return {}
    )";

    fileResolver.source["game/B"] = R"(
local types = require(game.A)
type Type = types.Type
local x: Type = { x = { a = 2 } }
type Rename = typeof(x.x)
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "do_not_modify_imported_types_3")
{
    fileResolver.source["game/A"] = R"(
local y = setmetatable({}, {})
export type Type = { x: typeof(y) }
return { x = y }
    )";

    fileResolver.source["game/B"] = R"(
local types = require(game.A)
type Type = types.Type
local x: Type = types
type Rename = typeof(x.x)
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "do_not_modify_imported_types_4")
{
    fileResolver.source["game/A"] = R"(
export type Array<T> = {T}
local arrayops = {}
function arrayops.foo(x: Array<any>) end
return arrayops
    )";

    CheckResult result = check(R"(
local arrayops = require(game.A)

local tbl = {}
tbl.a = 2
function tbl:foo(b: number, c: number)
    -- introduce BoundType to imported type
    arrayops.foo(self._regions)
end
-- this alias decreases function type level and causes a demotion of its type
type Table = typeof(tbl)
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "do_not_modify_imported_types_5")
{
    fileResolver.source["game/A"] = R"(
export type Type = {x: number, y: number}
local arrayops = {}
function arrayops.foo(x: Type) end
return arrayops
    )";

    CheckResult result = check(R"(
local arrayops = require(game.A)

local tbl = {}
tbl.a = 2
function tbl:foo(b: number, c: number)
    -- introduce boundTo TableType to imported type
    self.x.a = 2
    arrayops.foo(self.x)
end
-- this alias decreases function type level and causes a demotion of its type
type Table = typeof(tbl)
)");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "module_type_conflict")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS()
    fileResolver.source["game/A"] = R"(
export type T = { x: number }
return {}
    )";

    fileResolver.source["game/B"] = R"(
export type T = { x: string }
return {}
    )";

    fileResolver.source["game/C"] = R"(
local A = require(game.A)
local B = require(game.B)
local a: A.T = { x = 2 }
local b: B.T = a
    )";

    CheckResult result = getFrontend().check("game/C");
    LUAU_REQUIRE_ERROR_COUNT(1, result);

    if (!FFlag::DebugLuauForceOldSolver)
    {
        const std::string expected = "Expected this to be 'T' from 'game/B', but got 'T' from 'game/A'\n"
                                     "accessing `x` results in `number` in the latter type and `string` in the former type, and "
                                     "`number` is not exactly `string`";
        CHECK(expected == toString(result.errors[0]));
    }
    else
    {
        const std::string expected = R"(Expected this to be exactly 'T' from 'game/B', but got 'T' from 'game/A'
caused by:
  Property 'x' is not compatible.
Expected this to be exactly 'string', but got 'number')";
        CHECK_EQ(expected, toString(result.errors[0]));
    }
}

TEST_CASE_FIXTURE(BuiltinsFixture, "module_type_conflict_instantiated")
{
    CHECKS_WORDING_WITHOUT_HELPFUL_SUBTYPING_ERRORS()
    fileResolver.source["game/A"] = R"(
export type Wrap<T> = { x: T }
return {}
    )";

    fileResolver.source["game/B"] = R"(
local A = require(game.A)
export type T = A.Wrap<number>
return {}
    )";

    fileResolver.source["game/C"] = R"(
local A = require(game.A)
export type T = A.Wrap<string>
return {}
    )";

    fileResolver.source["game/D"] = R"(
local A = require(game.B)
local B = require(game.C)
local a: A.T = { x = 2 }
local b: B.T = a
    )";

    CheckResult result = getFrontend().check("game/D");
    LUAU_REQUIRE_ERROR_COUNT(1, result);

    if (!FFlag::DebugLuauForceOldSolver)
    {
        const std::string expected = "Expected this to be 'T' from 'game/C', but got 'T' from 'game/B'\n"
                                     "accessing `x` results in `number` in the latter type and `string` in the former type, and "
                                     "`number` is not exactly `string`";
        CHECK(expected == toString(result.errors[0]));
    }
    else
    {
        const std::string expected = R"(Expected this to be exactly 'T' from 'game/C', but got 'T' from 'game/B'
caused by:
  Property 'x' is not compatible.
Expected this to be exactly 'string', but got 'number')";
        CHECK_EQ(expected, toString(result.errors[0]));
    }
}

TEST_CASE_FIXTURE(BuiltinsFixture, "constrained_anyification_clone_immutable_types")
{
    fileResolver.source["game/A"] = R"(
return function(...) end
    )";

    fileResolver.source["game/B"] = R"(
local l0 = require(game.A)
return l0
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "fuzz_anyify_variadic_return_must_follow")
{
    CheckResult result = check(R"(
return unpack(l0[_])
    )");

    LUAU_REQUIRE_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "check_imported_module_names")
{
    fileResolver.source["game/A"] = R"(
return function(...) end
    )";

    fileResolver.source["game/B"] = R"(
local l0 = require(game.A)
return l0
    )";

    CheckResult result = check(R"(
local l0 = require(game.B)
if true then
    local l1 = require(game.A)
end
return l0
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    ModulePtr mod = getMainModule();
    REQUIRE(mod);

    REQUIRE(mod->scopes.size() == 4);
    CHECK(mod->scopes[0].second->importedModules["l0"] == "game/B");
    CHECK(mod->scopes[3].second->importedModules["l1"] == "game/A");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "ensure_scope_is_nullptr_after_shallow_copy")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};
    getFrontend().options.retainFullTypeGraphs = false;

    fileResolver.source["game/A"] = R"(
-- Roughly taken from ReactTypes.lua
type CoreBinding<T> = {}
type BindingMap = {}
export type Binding<T> = CoreBinding<T> & BindingMap

return {}
    )";

    LUAU_REQUIRE_NO_ERRORS(check(R"(
local Types = require(game.A)
type Binding<T> = Types.Binding<T>
    )"));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "ensure_free_variables_are_generialized_across_function_boundaries")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    fileResolver.source["game/A"] = R"(
-- Roughly taken from react-shallow-renderer
function createUpdater(renderer)
    local updater = {
        _renderer = renderer,
    }

    function updater.enqueueForceUpdate(publicInstance, callback, _callerName)
        updater._renderer.render(
            updater._renderer,
            updater._renderer._element,
            updater._renderer._context
        )
    end

    function updater.enqueueReplaceState(
        publicInstance,
        completeState,
        callback,
        _callerName
    )
        updater._renderer.render(
            updater._renderer,
            updater._renderer._element,
            updater._renderer._context
        )
    end

    function updater.enqueueSetState(publicInstance, partialState, callback, _callerName)
        local currentState = updater._renderer._newState or publicInstance.state
        updater._renderer.render(
            updater._renderer,
            updater._renderer._element,
            updater._renderer._context
        )
    end

    return updater
end

local ReactShallowRenderer = {}

function ReactShallowRenderer:_reset()
    self._updater = createUpdater(self)
end

return ReactShallowRenderer
    )";

    LUAU_REQUIRE_NO_ERRORS(check(R"(
local ReactShallowRenderer = require(game.A);
    )"));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "untitled_segfault_number_13")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    fileResolver.source["game/A"] = R"(
        -- minimized from roblox-requests/http/src/response.lua
        local Response = {}
        Response.__index = Response
        function Response.new(content_type)
            -- creates response object from original request and roblox http response
            local self = setmetatable({}, Response)
            self.content_type = content_type
            return self
        end

        function Response:xml(ignore_content_type)
            if ignore_content_type or self.content_type:find("+xml") or self.content_type:find("/xml") then
            else
            end
        end

        ---------------

        return Response
    )";

    LUAU_REQUIRE_NO_ERRORS(check(R"(
        local _ = require(game.A);
    )"));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "spooky_blocked_type_laundered_by_bound_type")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    fileResolver.source["game/A"] = R"(
        local Cache = {}

        Cache.settings = {}

        Cache.data = {}

        function Cache.should_cache(url)
            url = url:split("?")[1]

            for key, _ in pairs(Cache.settings) do
                if url:match('') then
                    return key
                end
            end

            return ""
        end

        function Cache.is_cached(url, req_id)
            -- check local server cache first

            local setting_key = Cache.should_cache(url)
            local settings = Cache.settings[setting_key]

            if not setting_key then
                return false
            end

            if Cache.data[req_id] ~= nil then
                return true
            end

            if Cache.settings[setting_key].cache_globally then
                return false
            else
                return true
            end
        end

        function Cache.get_expire(url)
            local setting_key = Cache.should_cache(url)
            return Cache.settings[setting_key].expires or math.huge
        end

        return Cache
    )";

    auto result = check(R"(
        local _ = require(game.A);
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "leaky_generics")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    auto result = check(R"(
        local Cache = {}

        Cache.settings = {}

        function Cache.should_cache(url)
            for key, _ in pairs(Cache.settings) do
                return key
            end

            return ""
        end

        function Cache.is_cached(url)
            local setting_key = Cache.should_cache(url)
            local settings = Cache.settings[setting_key]

            return settings
        end

        return Cache
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK("(unknown) -> unknown" == toString(requireTypeAtPosition({13, 23})));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "cycles_dont_make_everything_any")
{
    fileResolver.source["game/A"] = R"(
        --!strict
        local module = {}

        function module.foo()
            return 2
        end

        function module.bar()
            local m = require(game.B)
            return m.foo() + 1
        end

        return module
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local module = {}

        function module.foo()
            return 2
        end

        function module.bar()
            local m = require(game.A)
            return m.foo() + 1
        end

        return module
    )";

    getFrontend().check("game/A");

    CHECK("module" == toString(getFrontend().moduleResolver.getModule("game/B")->returnType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "cross_module_function_mutation")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    fileResolver.source["game/A"] = R"(
function test2(a: number, b: string)
    return 1
end

return test2
    )";

    fileResolver.source["game/B"] = R"(
function wrapper<A...>(f: (A...) -> number, ...: A...)
end

local test2 = require(game.A)

return wrapper(test2, 1, "")
    )";

    CheckResult result = getFrontend().check("game/B");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "internal_types_are_scrubbed_from_module")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::DebugLuauMagicTypes, true},
    };

    fileResolver.source["game/A"] = R"(
return function(): _luau_blocked_type return nil :: any end
    )";

    CheckResult result = getFrontend().check("game/A");
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(get<ConstraintSolvingIncompleteError>(result.errors[0]));
    CHECK(get<InternalError>(result.errors[1]));
    CHECK("(...any) -> *error-type*" == toString(getFrontend().moduleResolver.getModule("game/A")->returnType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "internal_type_errors_are_only_reported_once")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::DebugLuauMagicTypes, true},
        {FFlag::LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier, true},
    };

    fileResolver.source["game/A"] = R"(
return function(): { X: _luau_blocked_type, Y: _luau_blocked_type } return nil :: any end
    )";

    CheckResult result = getFrontend().check("game/A");
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    // We always fail to solve all constraints here because we have an un-owned blocked type.
    CHECK(get<ConstraintSolvingIncompleteError>(result.errors[0]));
    CHECK(get<InternalError>(result.errors[1]));
    CHECK("(...any) -> { X: *error-type*, Y: *error-type* }" == toString(getFrontend().moduleResolver.getModule("game/A")->returnType));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "scrub_unsealed_tables")
{
    ScopedFastFlag sff{FFlag::DebugLuauForceOldSolver, false};

    ScopedFastInt sfi{FInt::LuauSolverConstraintLimit, 5};
    ScopedFastFlag _{FFlag::LuauRemovePrimitiveTypeConstraintAndSubtypingUnifier, true};

    fileResolver.source["game/A"] = R"(
        type Array<T> = {T}
        type Hello = Array<Array<Array<Array<Array<Array<Array<Array<Array<Array<number>>>>>>>>>>
        local X = {}
        X.foo = 42
        X.bar = ""
        return X
    )";

    fileResolver.source["game/B"] = R"(
        local x = require(game.A)
        x.lmao = 42
        return {}
    )";

    CheckResult result = getFrontend().check("game/B");
    // This is going to have a _ton_ of errors
    LUAU_CHECK_ERROR(result, CodeTooComplex);
    LUAU_CHECK_ERROR(result, ConstraintSolvingIncompleteError);
    LUAU_CHECK_ERROR(result, CannotExtendTable);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "invalid_local_alias_shouldnt_shadow_imported_type")
{
    fileResolver.source["game/A"] = R"(
        export type bad<T> = {T}
        return {}
    )";

    fileResolver.source["game/B"] = R"(
        local a_mod = require(game.A)
        type bad<T> = {bad<{T}>}
        type fine<T> = a_mod.bad<T>
        local f: fine<number>
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<RecursiveRestraintViolation>(result.errors[0]));

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    REQUIRE(b != nullptr);
    std::optional<TypeId> fType = requireType(b, "f");
    REQUIRE(fType);
    // The important thing here is that it isn't *error-type*, since that would mean that the local definition of `bad` is shadowing the imported one
    CHECK(toString(*fType) == "fine<number>");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "invalid_alias_should_export_as_error_type")
{
    fileResolver.source["game/A"] = R"(
        export type bad<T> = {bad<{T}>}
        return {}
    )";

    fileResolver.source["game/B"] = R"(
        local a_mod = require(game.A)
        local f: a_mod.bad<number>
    )";

    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(get<RecursiveRestraintViolation>(result.errors[0]));

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    REQUIRE(b != nullptr);
    std::optional<TypeId> fType = requireType(b, "f");
    REQUIRE(fType);
    CHECK(toString(*fType) == "bad<number>");
}

// exported modules
TEST_CASE_FIXTURE(BuiltinsFixture, "exported_module_basic")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        --!strict
        export version = "1.0.0"
        export name = "test module"
        export count = 41 + 1
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local version = A.version
        local name = A.name
        local count = A.count
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    CHECK_EQ("number", toString(requireType(b, "count")));
    CHECK_EQ("string", toString(requireType(b, "version")));
    CHECK_EQ("string", toString(requireType(b, "name")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "exported_module_mutual_recursive_functions")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        --!strict
        local a, b

        function a()
            return b() + 1
        end

        function b()
            return 42
        end

        export a = a
        export b = b
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local a = A.a
        local b = A.b
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    CHECK_EQ("(...any) -> number", toString(requireType(b, "a")));
    CHECK_EQ("(...any) -> number", toString(requireType(b, "b")));
}

// Luwu Export Values (rfcs/export-values.md): `export function f` fills a forward-declared `local f`, and the module
// exports the function
TEST_CASE_FIXTURE(BuiltinsFixture, "exported_function_fills_a_forward_declaration")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        --!strict
        local is_odd
        export function is_even(n: number): boolean
            return if n == 0 then true else is_odd(n - 1)
        end
        export function is_odd(n: number): boolean
            return if n == 0 then false else is_even(n - 1)
        end
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local even = A.is_even
        local odd = A.is_odd
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    CHECK_EQ("(number) -> boolean", toString(requireType(b, "even")));
    CHECK_EQ("(number) -> boolean", toString(requireType(b, "odd")));

    // Hover shows the docs above both: the export's location is the forward declaration (line 2), and its function type's
    // definition is the `export function` (line 6)
    ModulePtr a = getFrontend().moduleResolver.getModule("game/A");
    std::optional<TypeId> exports = first(a->returnType);
    REQUIRE(exports);
    const TableType* exportTable = get<TableType>(follow(*exports));
    REQUIRE(exportTable);
    auto isOdd = exportTable->props.find("is_odd");
    REQUIRE(isOdd != exportTable->props.end());
    REQUIRE(isOdd->second.location);
    CHECK_EQ(2, isOdd->second.location->begin.line);
    REQUIRE(isOdd->second.readTy);
    const FunctionType* isOddFunction = get<FunctionType>(follow(*isOdd->second.readTy));
    REQUIRE(isOddFunction);
    REQUIRE(isOddFunction->definition);
    CHECK_EQ(6, isOddFunction->definition->definitionLocation.begin.line);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "exported_module_nil_values")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        --!strict
        export a = nil
        local b = function() return 1 end
        b = nil
        export b = b
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local a = A.a
        local b = A.b
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    CHECK_EQ("nil", toString(requireType(b, "a")));
    CHECK_EQ("nil", toString(requireType(b, "b")));
}

// maintain consistency with exported_module_nil_values
TEST_CASE_FIXTURE(BuiltinsFixture, "returned_module_unassigned_local_stays_nil")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        --!strict
        local a = nil
        local b = function() return 1 end
        b = nil
        return {a = a, b = b}
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local a = A.a
        local b = A.b
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    CHECK_EQ("nil", toString(requireType(b, "a")));
    CHECK_EQ("nil", toString(requireType(b, "b")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "exported_module_function")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        --!strict
        export function add(a: number, b: number): number
            return a + b
        end

        export function greet(name: string): string
            return "Hello, " .. name
        end

        export function noop()
            -- do nothing
        end
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local add = A.add
        local greet = A.greet
        local noop = A.noop
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    CHECK_EQ("(number, number) -> number", toString(requireType(b, "add")));
    CHECK_EQ("(string) -> string", toString(requireType(b, "greet")));
    CHECK_EQ("(...any) -> ()", toString(requireType(b, "noop")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "exported_multret")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        --!strict
        local function huh()
            return 42, "huh", false
        end

        export a, b, c = huh()
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local a = A.a
        local b = A.b
        local c = A.c
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    CHECK_EQ("number", toString(requireType(b, "a")));
    CHECK_EQ("string", toString(requireType(b, "b")));
    CHECK_EQ("boolean", toString(requireType(b, "c")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "exported_partial_multret")
{
    ScopedFastFlag _[3]{{FFlag::LuauExportValueSyntax, true}, {FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuauExportValueTypecheck, true}};

    fileResolver.source["game/A"] = R"(
        --!strict
        local function huh()
            return "huh", false
        end

        export a, b, c = 42, huh()
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)

        local a = A.a
        local b = A.b
        local c = A.c
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_NO_ERRORS(bResult);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    CHECK_EQ("number", toString(requireType(b, "a")));
    CHECK_EQ("string", toString(requireType(b, "b")));
    CHECK_EQ("boolean", toString(requireType(b, "c")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "export_class")
{
    ScopedFastFlag sff[] = {
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuClasses, true}
    };

    fileResolver.source["game/A"] = R"(
        export class Point
            x: number
            y: number

            function __tostring(self)
                return `Point x={self.x} y={self.y}`
            end
        end
    )";

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)

        local a: A.Point = A.Point { x=2, y=3 }

        local x, y = a.x, a.y
    )";

    CheckResult result = getFrontend().check("game/B");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK("number" == toString(requireType("game/B", "x")));
    CHECK("number" == toString(requireType("game/B", "y")));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "non_exported_class")
{
    ScopedFastFlag sff[] = {{FFlag::DebugLuauForceOldSolver, false}, {FFlag::LuwuClasses, true}};

    fileResolver.source["game/A"] = R"(
        class Point
            x: number
            y: number

            function __tostring(self)
                return `Point x={self.x} y={self.y}`
            end
        end

        -- A class returned in the table would export its type (every_common_way_to_export_a_class_brings_its_type)
        return {make = function() return Point { x = 0, y = 0 } end}
    )";

    fileResolver.source["game/B"] = R"(
        local A = require(game.A)

        local a: A.Point = A.make()
    )";

    CheckResult result = getFrontend().check("game/B");

    LUAU_REQUIRE_ERROR_COUNT(1, result);

    auto* err = get<UnknownSymbol>(result.errors[0]);
    REQUIRE(err);
    CHECK("A.Point" == err->name);
    CHECK(UnknownSymbol::Context::Type == err->context);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "unknown_module_types_say_whether_the_module_or_the_type_is_missing")
{
    fileResolver.source["game/A"] = R"(
        export type Point = { x: number }
        return {}
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)
        local notAModule = 5
        local missing = require(game.DoesNotExist)
        local a: A.Size = 1
        local b: notAModule.Size = 1
        local c: missing.Size = 1
        local d: Nowhere = 1
    )";

    getFrontend().check("game/A");
    CheckResult result = getFrontend().check("game/B");

    // the unresolved require reports itself; its type keeps the plain message so the two don't contradict
    LUAU_REQUIRE_ERROR_COUNT(5, result);
    CHECK(get<UnknownRequire>(result.errors[0]));
    CHECK_EQ(toString(result.errors[1]), "Unknown type 'A.Size': module 'A' exports no type named 'Size'");
    CHECK_EQ(toString(result.errors[2]), "Unknown type 'notAModule.Size': 'notAModule' isn't a required module");
    CHECK_EQ(toString(result.errors[3]), "Unknown type 'missing.Size'");
    CHECK_EQ(toString(result.errors[4]), "Unknown type 'Nowhere'");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "destructured_types_alias_a_modules_exported_types")
{
    ScopedFastFlag luwuDestructuring{FFlag::LuwuDestructuring, true};

    fileResolver.source["game/A"] = R"(
        export type Point = { x: number }
        export type Box<T> = { value: T }
        export type Pair<K, V = string> = { key: K, value: V }
        return {}
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)
        type .{Point, Box as Crate, Pair} = A
        local p: Point = { x = 1 }
        local b: Crate<number> = { value = 1 }
        local s: Crate<string> = { value = 1 }
        local pair: Pair<number> = { key = 1, value = "v" }
        export type .{Point as Exported, Box} = A
        return {}
    )";

    CheckResult aResult = getFrontend().check("game/A");
    LUAU_REQUIRE_NO_ERRORS(aResult);

    // A generic type keeps its generics, its defaults included; the one error is `Crate<string>` holding a number
    CheckResult bResult = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, bResult);
    CHECK_EQ(bResult.errors[0].location.begin.line, 6);

    ModulePtr b = getFrontend().moduleResolver.getModule("game/B");
    REQUIRE(b);
    CHECK(b->exportedTypeBindings.count("Exported"));
    REQUIRE(b->exportedTypeBindings.count("Box"));
    CHECK_EQ(b->exportedTypeBindings["Box"].typeParams.size(), 1);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "types_through_a_destructured_require_are_not_named_after_its_hidden_local")
{
    ScopedFastFlag luwuDestructuring{FFlag::LuwuDestructuring, true};

    fileResolver.source["game/A"] = R"(
        export type Point = { x: number }
        return { make = function(): Point return { x = 1 } end }
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local .{ make } = require(game.A)
        local p = make()
        return {}
    )";

    fileResolver.source["game/C"] = R"(
        --!strict
        local A = require(game.A)
        local p = A.make()
        return {}
    )";

    LUAU_REQUIRE_NO_ERRORS(getFrontend().check("game/B"));
    LUAU_REQUIRE_NO_ERRORS(getFrontend().check("game/C"));

    // the hidden local binding the module is no prefix anyone can write: `Point` isn't in scope by any other name
    ToStringOptions opts;
    opts.scope = getFrontend().moduleResolver.getModule("game/B")->getModuleScope();
    CHECK_EQ("Point", toString(requireType("game/B", "p"), opts));

    opts.scope = getFrontend().moduleResolver.getModule("game/C")->getModuleScope();
    CHECK_EQ("A.Point", toString(requireType("game/C", "p"), opts));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "locals_bound_to_an_imported_class_bring_its_type")
{
    ScopedFastFlag sff[] = {
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDestructuring, true},
    };

    fileResolver.source["game/A"] = R"(
        export class Point
            x: number
            y: number
        end

        export class Size
            w: number
        end
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)
        local Point = A.Point
        const .{Size as Dims} = require(game.A)
        const mod.{Size} = require(game.A)
        local p: Point = Point { x = 1, y = 2 }
        local d: Dims = Dims { w = 1 }
        local s: Size = mod.Size { w = 2 }
        local wrong: Point = Dims { w = 3 }
    )";

    // `wrong` is the one error: the bound types are the classes', not `any`
    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 9);

    // A type the scope already declares under the name wins
    fileResolver.source["game/C"] = R"(
        --!strict
        local A = require(game.A)
        type Point = number
        local Point = A.Point
        local n: Point = 1
    )";

    result = getFrontend().check("game/C");
    LUAU_REQUIRE_NO_ERRORS(result);
}

// Not only classes: a field brings any type the module exports under the field's name
TEST_CASE_FIXTURE(BuiltinsFixture, "locals_bound_to_an_imported_field_bring_the_type_of_that_name")
{
    ScopedFastFlag sff[] = {
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDestructuring, true},
    };

    fileResolver.source["game/A"] = R"(
        export type Config = { name: string }
        return { Config = { name = "default" } }
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        const .{ Config } = require(game.A)
        local A = require(game.A)
        local Settings = A.Config
        local c: Config = { name = "x" }
        local s: Settings = { name = "y" }
        local wrong: Config = { name = 1 }
    )";

    // `wrong` is the one error: both locals brought the module's `Config` type
    CheckResult result = getFrontend().check("game/B");
    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 7);
}

// A builtin library files its types in the global scope like a required module's (as `collections` does), so its
// fields bring their types the same way
TEST_CASE_FIXTURE(BuiltinsFixture, "fields_of_a_builtin_library_bring_its_types")
{
    ScopedFastFlag sff[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDestructuring, true},
    };

    loadDefinition("declare shapes: { Point: { x: number } }");
    ScopePtr globals = getFrontend().globals.globalScope;
    globals->importedTypeBindings["shapes"]["Point"] = TypeFun{{}, getBuiltins()->numberType};
    globals->importedModules["shapes"] = "@test/shapes";

    CheckResult result = check(R"(
        --!strict
        const .{ Point } = shapes
        local p: Point = 1
        local q: shapes.Point = 2
        local wrong: Point = "x"
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 5);
}

// A class's type comes with its value however the module exports it and however it's imported
TEST_CASE_FIXTURE(BuiltinsFixture, "every_common_way_to_export_a_class_brings_its_type")
{
    ScopedFastFlag sff[] = {
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDestructuring, true},
    };

    fileResolver.source["game/Keyword"] = R"(
        export class Cat
            name: string
        end
    )";
    fileResolver.source["game/Table"] = R"(
        class Cat
            name: string
        end
        return { Cat = Cat, Kitty = Cat }
    )";
    fileResolver.source["game/DotTable"] = R"(
        class Cat
            name: string
        end
        return .{Cat}
    )";
    fileResolver.source["game/Hub"] = R"(
        const animals = require(game.Keyword)
        export Cat = animals.Cat
    )";
    fileResolver.source["game/DestructuringHub"] = R"(
        export .{Cat} = require(game.Table)
    )";
    fileResolver.source["game/ImpliedConstHub"] = R"(
        export .{Cat} = require(game.DotTable)
    )";

    for (const char* exporter : {"Keyword", "Table", "DotTable", "Hub", "DestructuringHub", "ImpliedConstHub"})
    {
        INFO(std::string(exporter));
        fileResolver.source["game/Importer"] = format(
            R"(
            --!strict
            const .{Cat} = require(game.%s)
            const animals = require(game.%s)
            const Inline = require(game.%s).Cat
            local a: Cat = Cat { name = "a" }
            local b: animals.Cat = animals.Cat { name = "b" }
            local c: Inline = Inline { name = "c" }
            local wrong: Cat = 5
            )",
            exporter,
            exporter,
            exporter
        );
        getFrontend().markDirty("game/Importer");

        // `wrong` is the one error: the bound types are the class's, not `any`
        CheckResult result = getFrontend().check("game/Importer");
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK_EQ(result.errors[0].location.begin.line, 8);
    }

    // A returned table's key names the type
    fileResolver.source["game/Importer"] = R"(
        --!strict
        const animals = require(game.Table)
        local k: animals.Kitty = animals.Kitty { name = "k" }
    )";
    getFrontend().markDirty("game/Importer");
    CheckResult kitty = getFrontend().check("game/Importer");
    LUAU_REQUIRE_NO_ERRORS(kitty);
}

// A module exported as a field (`hub.t`) keeps its types, however the hub exports it
TEST_CASE_FIXTURE(BuiltinsFixture, "modules_exported_as_fields_keep_their_types")
{
    ScopedFastFlag sff[] = {
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDestructuring, true},
    };

    fileResolver.source["game/T"] = R"(
        export type Age = number
        export class Cat(age: Age) end
    )";
    fileResolver.source["game/Hub"] = R"(
        export t = require(game.T)
    )";
    fileResolver.source["game/AliasHub"] = R"(
        const t_module = require(game.T)
        export t = t_module
    )";
    fileResolver.source["game/TableHub"] = R"(
        const t = require(game.T)
        return { t = t }
    )";
    // Two levels: `outer.inner.t`
    fileResolver.source["game/Outer"] = R"(
        export inner = require(game.Hub)
    )";

    const std::vector<std::pair<std::string, std::string>> hubs = {
        {"Hub", "hub.t"}, {"AliasHub", "hub.t"}, {"TableHub", "hub.t"}, {"Outer", "hub.inner.t"},
    };
    for (const auto& hubAndPath : hubs)
    {
        const std::string& path = hubAndPath.second;
        INFO(hubAndPath.first);
        fileResolver.source["game/Importer"] = format(
            R"(
            --!strict
            const hub = require(game.%s)
            const .{ Cat } = %s
            const m = %s
            type .{ Age } = %s
            local c: Cat = Cat(3)
            local d: Cat = %s.Cat(4)
            local a: Age = 1
            local e: m.Age = 2
            local wrong: Age = "x"
            )",
            hubAndPath.first.c_str(),
            path.c_str(),
            path.c_str(),
            path.c_str(),
            path.c_str()
        );
        getFrontend().markDirty("game/Importer");

        // `wrong` is the one error: the types are the module's, not `any`
        CheckResult result = getFrontend().check("game/Importer");
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK_EQ(result.errors[0].location.begin.line, 10);
    }
}

// Modules that each export the next one under four names give 4^7 paths to the last one (`hub.a.b.c...`). A required
// module imports a bounded number of them, the short ones first.
TEST_CASE_FIXTURE(BuiltinsFixture, "modules_exported_under_many_paths_import_a_bounded_number")
{
    ScopedFastFlag sff[] = {
        {FFlag::LuauExportValueSyntax, true},
        {FFlag::LuauExportValueTypecheck, true},
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuDestructuring, true},
    };

    const int levels = 8;
    fileResolver.source[format("game/L%d", levels)] = "export type Age = number\nexport value = 1\n";
    for (int i = levels - 1; i >= 1; --i)
    {
        fileResolver.source[format("game/L%d", i)] = format(
            "export type Age = number\nconst next = require(game.L%d)\n"
            "export a = next\nexport b = next\nexport c = next\nexport d = next\n",
            i + 1
        );
    }

    fileResolver.source["game/Importer"] = R"(
        --!strict
        const hub = require(game.L1)
        type .{ Age } = hub.d.c.b
        local age: Age = 1
    )";
    CheckResult result = getFrontend().check("game/Importer");
    LUAU_REQUIRE_NO_ERRORS(result);

    // `hub` itself, and at most kMaxExportedModulePaths (256) paths through it
    ModulePtr importer = getFrontend().moduleResolver.getModule("game/Importer");
    REQUIRE(importer);
    REQUIRE(importer->hasModuleScope());
    CHECK_LE(importer->getModuleScope()->importedModules.size(), 257);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "destructured_types_report_what_the_module_does_not_export")
{
    ScopedFastFlag luwuDestructuring{FFlag::LuwuDestructuring, true};

    fileResolver.source["game/A"] = R"(
        export type Box<T> = { value: T }
        return {}
    )";

    fileResolver.source["game/B"] = R"(
        --!strict
        local A = require(game.A)
        type .{Missing, Box} = A
        local b: Box = { value = 1 }
    )";

    getFrontend().check("game/A");
    CheckResult result = getFrontend().check("game/B");

    // `Missing` isn't exported, and `Box` still takes its type argument where it's used
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    UnknownSymbol* unknown = get<UnknownSymbol>(result.errors[0]);
    REQUIRE(unknown);
    CHECK_EQ(unknown->name, "A.Missing");
    CHECK(get<IncorrectGenericParameterCount>(result.errors[1]));
}

TEST_SUITE_END();
