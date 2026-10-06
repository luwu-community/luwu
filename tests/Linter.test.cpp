// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Luau/Linter.h"
#include "Luau/BuiltinDefinitions.h"

#include "Fixture.h"
#include "ScopedFlags.h"

#include "doctest.h"

LUAU_FASTFLAG(DebugLuauForceOldSolver)
LUAU_FASTFLAG(DebugLuwuCompilerTrustsTypeAnnotations)
LUAU_FASTFLAG(LuwuAttributesEverywhere)
LUAU_FASTFLAG(LuauCstAttr)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuTraits)
LUAU_FASTFLAG(LuwuDestructuring)
LUAU_FASTFLAG(LuwuIfLocal)
LUAU_FASTFLAG(DebugLuwuDoExpr)
LUAU_FASTFLAG(LuwuDeclareStatements)
LUAU_FASTFLAG(LuauSolverV2)
LUAU_FASTFLAG(LuwuDefaultArguments)
LUAU_FASTFLAG(LuwuNonePrimitive)
LUAU_FASTFLAG(LuauDeprecatedAttributeOnAnonymousFunctions)
LUAU_FASTFLAG(LuauFunctionUnusedRecursiveLinting)
LUAU_FASTFLAG(LuwuTableRemoveFootgunLint)
LUAU_FASTFLAG(LuwuTableDrop)

using namespace Luau;

static std::vector<LintWarning> warningsWithCode(const LintResult& result, LintWarning::Code code)
{
    std::vector<LintWarning> found;
    for (const LintWarning& warning : result.warnings)
    {
        if (warning.code == code)
            found.push_back(warning);
    }
    return found;
}

static std::vector<LintWarning> withoutCode(const LintResult& result, LintWarning::Code code)
{
    std::vector<LintWarning> kept;
    for (const LintWarning& warning : result.warnings)
    {
        if (warning.code != code)
            kept.push_back(warning);
    }
    return kept;
}

TEST_SUITE_BEGIN("Linter");

TEST_CASE_FIXTURE(Fixture, "CleanCode")
{
    // Luwu: LuaAndOr reports every `a and b or c`, which this otherwise clean code uses
    LintResult result = lint(R"(--!nolint LuaAndOr
function _fib(n)
    return n < 2 and 1 or _fib(n-1) + _fib(n-2)
end

)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "type_function_fully_reduces")
{
    LintResult result = lint(R"(
function _fib(n)
    return n < 2 or  _fib(n-2)
end

)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "UnknownGlobal")
{
    LintResult result = lint("--!nocheck\nreturn foo");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Unknown global 'foo'; consider assigning to it first");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedGlobal")
{
    // Normally this would be defined externally, so hack it in for testing
    addGlobalBinding(getFrontend().globals, "Wait", Binding{getBuiltins()->anyType, {}, true, "wait", "@test/global/Wait"});

    LintResult result = lint("Wait(5)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Global 'Wait' is deprecated, use 'wait' instead");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedGlobalNoReplacement")
{
    // Normally this would be defined externally, so hack it in for testing
    const char* deprecationReplacementString = "";
    addGlobalBinding(getFrontend().globals, "Version", Binding{getBuiltins()->anyType, {}, true, deprecationReplacementString});

    LintResult result = lint("Version()");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Global 'Version' is deprecated");
}

TEST_CASE_FIXTURE(Fixture, "PlaceholderRead")
{
    LintResult result = lint(R"(
local _ = 5
return _
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Placeholder value '_' is read here; consider using a named variable");
}

TEST_CASE_FIXTURE(Fixture, "PlaceholderReadGlobal")
{
    LintResult result = lint(R"(
_ = 5
print(_)
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Placeholder value '_' is read here; consider using a named variable");
}

TEST_CASE_FIXTURE(Fixture, "PlaceholderWrite")
{
    LintResult result = lint(R"(
local _ = 5
_ = 6
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(BuiltinsFixture, "BuiltinGlobalWrite")
{
    LintResult result = lint(R"(
math = {}

function assert(x)
end

assert(5)
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Built-in global 'math' is overwritten here; consider using a local or changing the name");
    CHECK_EQ(result.warnings[1].text, "Built-in global 'assert' is overwritten here; consider using a local or changing the name");
}

TEST_CASE_FIXTURE(Fixture, "MultilineBlock")
{
    LintResult result = lint(R"(
if true then print(1) print(2) print(3) end
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "A new statement is on the same line; add semi-colon on previous statement to silence");
}

TEST_CASE_FIXTURE(Fixture, "MultilineBlockSemicolonsWhitelisted")
{
    LintResult result = lint(R"(
print(1); print(2); print(3)
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "MultilineBlockMissedSemicolon")
{
    LintResult result = lint(R"(
print(1); print(2) print(3)
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "A new statement is on the same line; add semi-colon on previous statement to silence");
}

TEST_CASE_FIXTURE(Fixture, "MultilineBlockLocalDo")
{
    LintResult result = lint(R"(
local _x do
    _x = 5
end
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "ConfusingIndentation")
{
    LintResult result = lint(R"(
print(math.max(1,
2))
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Statement spans multiple lines; use indentation to silence");
}

TEST_CASE_FIXTURE(Fixture, "GlobalAsLocal")
{
    LintResult result = lint(R"(
function bar()
    foo = 6
    return foo
end

return bar()
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Global 'foo' is only used in the enclosing function 'bar'; consider changing it to local");
}

TEST_CASE_FIXTURE(Fixture, "GlobalAsLocalMultiFx")
{
    LintResult result = lint(R"(
function bar()
    foo = 6
    return foo
end

function baz()
    foo = 6
    return foo
end

return bar() + baz()
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Global 'foo' is never read before being written. Consider changing it to local");
}

TEST_CASE_FIXTURE(Fixture, "GlobalAsLocalMultiFxWithRead")
{
    LintResult result = lint(R"(
function bar()
    foo = 6
    return foo
end

function baz()
    foo = 6
    return foo
end

function read()
    print(foo)
end

return bar() + baz() + read()
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "GlobalAsLocalWithConditional")
{
    LintResult result = lint(R"(
function bar()
    if true then foo = 6 end
    return foo
end

function baz()
    foo = 6
    return foo
end

return bar() + baz()
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "GlobalAsLocal3WithConditionalRead")
{
    LintResult result = lint(R"(
function bar()
    foo = 6
    return foo
end

function baz()
    foo = 6
    return foo
end

function read()
    if false then print(foo) end
end

return bar() + baz() + read()
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "GlobalAsLocalInnerRead")
{
    LintResult result = lint(R"(
function foo()
   local f = function() return bar end
   f()
   bar = 42
end

function baz() bar = 0 end

return foo() + baz()
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "GlobalAsLocalMulti")
{
    LintResult result = lint(R"(
local createFunction = function(configValue)
    -- Create an internal convenience function
    local function internalLogic()
        print(configValue) -- prints passed-in value
    end
    -- Here, we thought we were creating another internal convenience function
    -- that closed over the passed-in configValue, but this is actually being
    -- declared at module scope!
    function moreInternalLogic()
        print(configValue) -- nil!!!
    end
    return function()
        internalLogic()
        moreInternalLogic()
        return nil
    end
end
fnA = createFunction(true)
fnB = createFunction(false)
fnA() -- prints "true", "nil"
fnB() -- prints "false", "nil"
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(
        result.warnings[0].text, "Global 'moreInternalLogic' is only used in the enclosing function defined at line 2; consider changing it to local"
    );
}

TEST_CASE_FIXTURE(Fixture, "LocalShadowLocal")
{
    LintResult result = lint(R"(
local arg = 6
print(arg)

local arg = 5
print(arg)
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Variable 'arg' shadows previous declaration at line 2");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "LocalShadowGlobal")
{
    LintResult result = lint(R"(
local math = math
global = math

function bar()
    local global = math.max(5, 1)
    return global
end

return bar()
)");

    // Luwu: plus BuiltinShadow for `local math = math`
    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "'math' hides the builtin 'math' here");
    CHECK_EQ(result.warnings[1].text, "Variable 'global' shadows a global variable used at line 3");
}

TEST_CASE_FIXTURE(Fixture, "LocalShadowArgument")
{
    LintResult result = lint(R"(
function bar(a, b)
    local a = b + 1
    return a
end

return bar()
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Variable 'a' shadows previous declaration at line 2");
}

TEST_CASE_FIXTURE(Fixture, "LocalShadowClass")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    // Two locals shadow the class and are reported. The one before the class hides it from the rest of
    // the module, and the one in a function hides it from the rest of that function. Neither `local cat`
    // (a different name) nor the unrelated class `Dog` is reported.
    LintResult result = lint(R"(
local Cat = 1
local before = Cat
class Cat
    public x: number = 2
end
local function f()
    local function Cat() return 3 end
    return Cat()
end
class Dog
    public y: number = 1
end
local cat = Dog()
return before, f, cat
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].code, LintWarning::Code_LocalShadow);
    CHECK_EQ(result.warnings[0].location.begin.line, 1);
    CHECK_EQ(result.warnings[0].text, "Variable 'Cat' shadows class 'Cat' declared at line 4");
    CHECK_EQ(result.warnings[1].code, LintWarning::Code_LocalShadow);
    CHECK_EQ(result.warnings[1].location.begin.line, 7);
    CHECK_EQ(result.warnings[1].text, "Variable 'Cat' shadows class 'Cat' declared at line 4");
}

TEST_CASE_FIXTURE(Fixture, "LocalShadowClassAfterTheClass")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    LintResult result = lint(R"(
class Cat
    public x: number = 2
end
local Cat = 1
return Cat
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Variable 'Cat' shadows class 'Cat' declared at line 2");
}

TEST_CASE_FIXTURE(Fixture, "LocalUnused")
{
    LintResult result = lint(R"(
local arg = 6

local function bar()
    local arg = 5
    local blarg = 6
    if arg then
        blarg = 42
    end
end

return bar()
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Variable 'arg' is never used; prefix with '_' to silence");
    CHECK_EQ(result.warnings[1].text, "Variable 'blarg' is never used; prefix with '_' to silence");
}

TEST_CASE_FIXTURE(Fixture, "ImportUnused")
{
    // Normally this would be defined externally, so hack it in for testing
    addGlobalBinding(getFrontend().globals, "game", getBuiltins()->anyType, "@test");

    LintResult result = lint(R"(
local Roact = require(game.Packages.Roact)
local _Roact = require(game.Packages.Roact)
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Import 'Roact' is never used; prefix with '_' to silence");
}

TEST_CASE_FIXTURE(Fixture, "FunctionUnused")
{
    ScopedFastFlag sff{FFlag::LuauFunctionUnusedRecursiveLinting, true};

    LintResult result = lint(R"(
function bar()
end

local function qux()
end

function foo()
end

local function _unusedl()
end

function _unusedg()
end

function meow()
    meow()
end

local function nyanya()
    nyanya()
end

function kya()
    kya()
end

kya()

return foo()
)");

    REQUIRE(4 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Function 'bar' is never used; prefix with '_' to silence");
    CHECK_EQ(result.warnings[1].text, "Function 'qux' is never used; prefix with '_' to silence");
    CHECK_EQ(result.warnings[2].text, "Function 'meow' is never used outside its own body; prefix with '_' to silence");
    CHECK_EQ(result.warnings[3].text, "Function 'nyanya' is never used outside its own body; prefix with '_' to silence");
}

TEST_CASE_FIXTURE(Fixture, "FunctionUnusedFutureImprovement")
{
    ScopedFastFlag sff{FFlag::LuauFunctionUnusedRecursiveLinting, true};

    LintResult result = lint(R"(
-- note: ideally, these would both give warnings, but currently neither of them do (https://github.com/luau-lang/luau/pull/2553/changes#r3738290571)

purr = function(n)
    return if n < 0 then 1 else n * purr(n - 1)
end

local hiss
hiss = function(n)
    return if n < 0 then 1 else n * hiss(n - 1)
end
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "FunctionUnusedRuntimeChange")
{
    ScopedFastFlag sff{FFlag::LuauFunctionUnusedRecursiveLinting, true};

    LintResult result = lint(R"(
-- note: ideally, these would both give warnings, but currently neither of them do (https://github.com/luau-lang/luau/pull/2553/changes#r3738290571)

purr = function(n)
    return if n < 0 then 1 else n * purr(n - 1)
end

local hiss
hiss = function(n)
    return if n < 0 then 1 else n * hiss(n - 1)
end
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeBasic")
{
    LintResult result = lint(R"(
do
return 'ok'
end

print("hi!")
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 5);
    CHECK_EQ(result.warnings[0].text, "Unreachable code (previous statement always returns)");
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeLoopBreak")
{
    LintResult result = lint(R"(
while true do
    do break end
    print("nope")
end

print("hi!")
)");

    // Luwu: the loop also runs at most once, which UselessLoop reports
    result.warnings = warningsWithCode(result, LintWarning::Code_UnreachableCode);

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 3);
    CHECK_EQ(result.warnings[0].text, "Unreachable code (previous statement always breaks)");
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeLoopContinue")
{
    LintResult result = lint(R"(
while true do
    do continue end
    print("nope")
end

print("hi!")
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 3);
    CHECK_EQ(result.warnings[0].text, "Unreachable code (previous statement always continues)");
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeIfMerge")
{
    LintResult result = lint(R"(
function foo1(a)
    if a then
        return 'x'
    else
        return 'y'
    end
    return 'z'
end

function foo2(a)
    if a then
        return 'x'
    end
    return 'z'
end

function foo3(a)
    if a then
        return 'x'
    else
        print('y')
    end
    return 'z'
end

return { foo1, foo2, foo3 }
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 7);
    CHECK_EQ(result.warnings[0].text, "Unreachable code (previous statement always returns)");
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeErrorReturnSilent")
{
    LintResult result = lint(R"(
function foo1(a)
    if a then
        error('x')
        return 'z'
    else
        error('y')
    end
end

return foo1
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeAssertFalseReturnSilent")
{
    LintResult result = lint(R"(
function foo1(a)
    if a then
        return 'z'
    end

    assert(false)
end

return foo1
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeErrorReturnNonSilentBranchy")
{
    LintResult result = lint(R"(
function foo1(a)
    if a then
        error('x')
    else
        error('y')
    end
    return 'z'
end

return foo1
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 7);
    CHECK_EQ(result.warnings[0].text, "Unreachable code (previous statement always errors)");
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeErrorReturnPropagate")
{
    LintResult result = lint(R"(
function foo1(a)
    if a then
        error('x')
        return 'z'
    else
        error('y')
    end
    return 'x'
end

return foo1
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 8);
    CHECK_EQ(result.warnings[0].text, "Unreachable code (previous statement always errors)");
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeLoopWhile")
{
    LintResult result = lint(R"(
function foo1(a)
    while a do
        return 'z'
    end
    return 'x'
end

return foo1
)");

    // Luwu: the loop also runs at most once, which UselessLoop reports
    result.warnings = warningsWithCode(result, LintWarning::Code_UnreachableCode);

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeLoopRepeat")
{
    LintResult result = lint(R"(
function foo1(a)
    repeat
        return 'z'
    until a
    return 'x'
end

return foo1
)");

    // this is technically a bug, since the repeat body always returns; fixing this bug is a bit more involved than I'd like
    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "UnknownType")
{
    unfreeze(getFrontend().globals.globalTypes);
    TableType::Props instanceProps{
        {"ClassName", {getBuiltins()->anyType}},
    };

    TableType instanceTable{instanceProps, std::nullopt, getFrontend().globals.globalScope->level, Luau::TableState::Sealed};
    TypeId instanceType = getFrontend().globals.globalTypes.addType(instanceTable);
    TypeFun instanceTypeFun{{}, instanceType};

    getFrontend().globals.globalScope->exportedTypeBindings["Part"] = instanceTypeFun;

    LintResult result = lint(R"(
local game = ...
local _e01 = type(game) == "Part"
local _e02 = typeof(game) == "Bar"
local _ok = typeof(game) == "vector"

local _o01 = type(game) == "number"
local _o02 = type(game) == "vector"
local _o03 = typeof(game) == "Part"
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 2);
    CHECK_EQ(result.warnings[0].text, "Unknown type 'Part' (expected primitive type)");
    CHECK_EQ(result.warnings[1].location.begin.line, 3);
    CHECK_EQ(result.warnings[1].text, "Unknown type 'Bar'");
}

TEST_CASE_FIXTURE(Fixture, "UnknownTypeKnowsClassesAndTraits")
{
    ScopedFastFlag _[2]{{FFlag::LuwuClasses, true}, {FFlag::LuwuTraits, true}};

    LintResult result = lint(R"(
local x = ...
local _a = type(x) == "class"
local _b = type(x) == "object"
local _c = type(x) == "trait"
local _d = typeof(x) == "trait"
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "ForRangeTable")
{
    LintResult result = lint(R"(
local t = {}

for i=#t,1 do
end

for i=#t,1,-1 do
end
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 3);
    CHECK_EQ(result.warnings[0].text, "For loop should iterate backwards; did you forget to specify -1 as step?");
}

TEST_CASE_FIXTURE(Fixture, "ForRangeBackwards")
{
    LintResult result = lint(R"(
for i=8,1 do
end

for i=8,1,-1 do
end
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 1);
    CHECK_EQ(result.warnings[0].text, "For loop should iterate backwards; did you forget to specify -1 as step?");
}

TEST_CASE_FIXTURE(Fixture, "ForRangeImprecise")
{
    LintResult result = lint(R"(
for i=1.3,7.5 do
end

for i=1.3,7.5,1 do
end
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 1);
    CHECK_EQ(result.warnings[0].text, "For loop ends at 7.3 instead of 7.5; did you forget to specify step?");
}

TEST_CASE_FIXTURE(Fixture, "ForRangeZero")
{
    LintResult result = lint(R"(
for i=0,#t do
end

for i=(0),#t do -- to silence
end

for i=#t,0 do
end
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 1);
    CHECK_EQ(result.warnings[0].text, "For loop starts at 0, but arrays start at 1");
    CHECK_EQ(result.warnings[1].location.begin.line, 7);
    CHECK_EQ(
        result.warnings[1].text,
        "For loop should iterate backwards; did you forget to specify -1 as step? Also consider changing 0 to 1 since arrays start at 1"
    );
}

TEST_CASE_FIXTURE(Fixture, "UnbalancedAssignment")
{
    LintResult result = lint(R"(
do
local _a,_b,_c = pcall()
end
do
local _a,_b,_c = pcall(), 5
end
do
local _a,_b,_c = pcall(), 5, 6
end
do
local _a,_b,_c = pcall(), 5, 6, 7
end
do
local _a,_b,_c = pcall(), nil
end
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 5);
    CHECK_EQ(result.warnings[0].text, "Assigning 2 values to 3 variables initializes extra variables with nil; add 'nil' to value list to silence");
    CHECK_EQ(result.warnings[1].location.begin.line, 11);
    CHECK_EQ(result.warnings[1].text, "Assigning 4 values to 3 variables leaves some values unused");
}

TEST_CASE_FIXTURE(Fixture, "ImplicitReturn")
{
    // Luwu: LuaIterators reports the `pairs` calls, which aren't what this tests
    LintResult result = lint(R"(--!nolint LuaIterators
--!nonstrict
function f1(a)
    if not a then
        return 5
    end
end

function f2(a)
    if not a then
        return
    end
end

function f3(a)
    if not a then
        return 5
    else
        return
    end
end

function f4(a)
    for i in pairs(a) do
        if i > 5 then
            return i
        end
    end

    print("element not found")
end

function f5(a)
    for i in pairs(a) do
        if i > 5 then
            return i
        end
    end

    error("element not found")
end

f6 = function(a)
    if a == 0 then
        return 42
    end
end

function f7(a)
    repeat
        return 10
    until a ~= nil
end

return f1,f2,f3,f4,f5,f6,f7
)");

    REQUIRE(3 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 5);
    CHECK_EQ(
        result.warnings[0].text,
        "Function 'f1' can implicitly return no values even though there's an explicit return at line 5; add explicit return to silence"
    );
    CHECK_EQ(result.warnings[1].location.begin.line, 29);
    CHECK_EQ(
        result.warnings[1].text,
        "Function 'f4' can implicitly return no values even though there's an explicit return at line 26; add explicit return to silence"
    );
    CHECK_EQ(result.warnings[2].location.begin.line, 45);
    CHECK_EQ(
        result.warnings[2].text,
        "Function can implicitly return no values even though there's an explicit return at line 45; add explicit return to silence"
    );
}

TEST_CASE_FIXTURE(Fixture, "ImplicitReturnInfiniteLoop")
{
    LintResult result = lint(R"(
--!nonstrict
function f1(a)
    while true do
        if math.random() > 0.5 then
            return 5
        end
    end
end

function f2(a)
    repeat
        if math.random() > 0.5 then
            return 5
        end
    until false
end

function f3(a)
    while true do
        if math.random() > 0.5 then
            return 5
        end
        if math.random() < 0.1 then
            break
        end
    end
end

function f4(a)
    repeat
        if math.random() > 0.5 then
            return 5
        end
        if math.random() < 0.1 then
            break
        end
    until false
end

return f1,f2,f3,f4
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line, 26);
    CHECK_EQ(
        result.warnings[0].text,
        "Function 'f3' can implicitly return no values even though there's an explicit return at line 22; add explicit return to silence"
    );
    CHECK_EQ(result.warnings[1].location.begin.line, 37);
    CHECK_EQ(
        result.warnings[1].text,
        "Function 'f4' can implicitly return no values even though there's an explicit return at line 33; add explicit return to silence"
    );
}

TEST_CASE_FIXTURE(Fixture, "TypeAnnotationsShouldNotProduceWarnings")
{
    LintResult result = lint(R"(--!strict
type InputData = {
    id: number,
    inputType: EnumItem,
    inputState: EnumItem,
    updated: number,
    position: Vector3,
    keyCode: EnumItem,
    name: string
}
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "BreakFromInfiniteLoopMakesStatementReachable")
{
    LintResult result = lint(R"(
local bar = ...

repeat
    if bar then
        break
    end

    return 2
until true

return 1
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "IgnoreLintAll")
{
    LintResult result = lint(R"(
--!nolint
return foo
)");

    // Luwu: a bare `--!nolint` leaves BareNolint on, which asks whether it was meant (see BareNolintAsksForLintNames)
    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].code, LintWarning::Code_BareNolint);
}

TEST_CASE_FIXTURE(Fixture, "IgnoreLintSpecific")
{
    LintResult result = lint(R"(
--!nolint UnknownGlobal
local x = 1
return foo
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Variable 'x' is never used; prefix with '_' to silence");
}

TEST_CASE_FIXTURE(Fixture, "FormatStringFormat")
{
    LintResult result = lint(R"(
-- incorrect format strings
string.format("%")
string.format("%??d")
string.format("%Y")

-- incorrect format strings, self call
local _ = ("%"):format()

-- correct format strings, just to uh make sure
string.format("hello %+10d %.02f %%", 4, 5)
)");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(4 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid format string: unfinished format specifier");
    CHECK_EQ(result.warnings[1].text, "Invalid format string: invalid format specifier: must be a string format specifier or %");
    CHECK_EQ(result.warnings[2].text, "Invalid format string: invalid format specifier: must be a string format specifier or %");
    CHECK_EQ(result.warnings[3].text, "Invalid format string: unfinished format specifier");
}

TEST_CASE_FIXTURE(Fixture, "FormatStringPack")
{
    LintResult result = lint(R"(
-- incorrect pack specifiers
string.pack("?")
string.packsize("?")
string.unpack("?")

-- missing size
string.packsize("bc")

-- incorrect X alignment
string.packsize("X")
string.packsize("X i")

-- correct X alignment
string.packsize("Xi")

-- packsize can't be used with variable sized formats
string.packsize("s")

-- out of range size specifiers
string.packsize("i0")
string.packsize("i17")

-- a very very very out of range size specifier
string.packsize("i99999999999999999999")
string.packsize("c99999999999999999999")

-- correct format specifiers
string.packsize("=!1bbbI3c42")
)");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(11 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid pack format: unexpected character; must be a pack specifier or space");
    CHECK_EQ(result.warnings[1].text, "Invalid pack format: unexpected character; must be a pack specifier or space");
    CHECK_EQ(result.warnings[2].text, "Invalid pack format: unexpected character; must be a pack specifier or space");
    CHECK_EQ(result.warnings[3].text, "Invalid pack format: fixed-sized string format must specify the size");
    CHECK_EQ(result.warnings[4].text, "Invalid pack format: X must be followed by a size specifier");
    CHECK_EQ(result.warnings[5].text, "Invalid pack format: X must be followed by a size specifier");
    CHECK_EQ(result.warnings[6].text, "Invalid pack format: pack specifier must be fixed-size");
    CHECK_EQ(result.warnings[7].text, "Invalid pack format: integer size must be in range [1,16]");
    CHECK_EQ(result.warnings[8].text, "Invalid pack format: integer size must be in range [1,16]");
    CHECK_EQ(result.warnings[9].text, "Invalid pack format: size specifier is too large");
    CHECK_EQ(result.warnings[10].text, "Invalid pack format: size specifier is too large");
}

TEST_CASE_FIXTURE(Fixture, "FormatStringMatch")
{
    LintResult result = lint(R"(
local s = ...

-- incorrect character class specifiers
string.match(s, "%q")
string.gmatch(s, "%q")
string.find(s, "%q")
string.gsub(s, "%q", "")

-- various errors
string.match(s, "%")
string.match(s, "[%1]")
string.match(s, "%0")
string.match(s, "(%d)%2")
string.match(s, "%bx")
string.match(s, "%foo")
string.match(s, '(%d))')
string.match(s, '(%d')
string.match(s, '[%d')
string.match(s, '%,')

-- self call - not detected because we don't know the type!
local _ = s:match("%q")

-- correct patterns
string.match(s, "[A-Z]+(%d)%1")
)");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(14 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid match pattern: invalid character class, must refer to a defined class or its inverse");
    CHECK_EQ(result.warnings[1].text, "Invalid match pattern: invalid character class, must refer to a defined class or its inverse");
    CHECK_EQ(result.warnings[2].text, "Invalid match pattern: invalid character class, must refer to a defined class or its inverse");
    CHECK_EQ(result.warnings[3].text, "Invalid match pattern: invalid character class, must refer to a defined class or its inverse");
    CHECK_EQ(result.warnings[4].text, "Invalid match pattern: unfinished character class");
    CHECK_EQ(result.warnings[5].text, "Invalid match pattern: sets can not contain capture references");
    CHECK_EQ(result.warnings[6].text, "Invalid match pattern: invalid capture reference, must be 1-9");
    CHECK_EQ(result.warnings[7].text, "Invalid match pattern: invalid capture reference, must refer to a valid capture");
    CHECK_EQ(result.warnings[8].text, "Invalid match pattern: missing brace characters for balanced match");
    CHECK_EQ(result.warnings[9].text, "Invalid match pattern: missing set after a frontier pattern");
    CHECK_EQ(result.warnings[10].text, "Invalid match pattern: unexpected ) without a matching (");
    CHECK_EQ(result.warnings[11].text, "Invalid match pattern: expected ) at the end of the string to close a capture");
    CHECK_EQ(result.warnings[12].text, "Invalid match pattern: expected ] at the end of the string to close a set");
    CHECK_EQ(result.warnings[13].text, "Invalid match pattern: expected a magic character after %");
}

TEST_CASE_FIXTURE(Fixture, "FormatStringMatchNested")
{
    LintResult result = lint(R"~(
local s = ...

-- correct reference to nested pattern
string.match(s, "((a)%2)")

-- incorrect reference to nested pattern (not closed yet)
string.match(s, "((a)%1)")

-- incorrect reference to nested pattern (index out of range)
string.match(s, "((a)%3)")
)~");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid match pattern: invalid capture reference, must refer to a closed capture");
    CHECK_EQ(result.warnings[0].location.begin.line, 7);
    CHECK_EQ(result.warnings[1].text, "Invalid match pattern: invalid capture reference, must refer to a valid capture");
    CHECK_EQ(result.warnings[1].location.begin.line, 10);
}

TEST_CASE_FIXTURE(Fixture, "FormatStringMatchSets")
{
    LintResult result = lint(R"~(
local s = ...

-- fake empty sets (but actually sets that aren't closed)
string.match(s, "[]")
string.match(s, "[^]")

-- character ranges in sets
string.match(s, "[%a-b]")
string.match(s, "[a-%b]")

-- invalid escapes
string.match(s, "[%q]")
string.match(s, "[%;]")

-- capture refs in sets
string.match(s, "[%1]")

-- valid escapes and - at the end
string.match(s, "[%]x-]")

-- % escapes itself
string.match(s, "[%%]")

-- this abomination is a valid pattern due to rules wrt handling empty sets
string.match(s, "[]|'[]")
string.match(s, "[^]|'[]")
)~");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(7 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid match pattern: expected ] at the end of the string to close a set");
    CHECK_EQ(result.warnings[1].text, "Invalid match pattern: expected ] at the end of the string to close a set");
    CHECK_EQ(result.warnings[2].text, "Invalid match pattern: character range can't include character sets");
    CHECK_EQ(result.warnings[3].text, "Invalid match pattern: character range can't include character sets");
    CHECK_EQ(result.warnings[4].text, "Invalid match pattern: invalid character class, must refer to a defined class or its inverse");
    CHECK_EQ(result.warnings[5].text, "Invalid match pattern: expected a magic character after %");
    CHECK_EQ(result.warnings[6].text, "Invalid match pattern: sets can not contain capture references");
}

TEST_CASE_FIXTURE(Fixture, "FormatStringFindArgs")
{
    LintResult result = lint(R"(
local s = ...

-- incorrect character class specifier
string.find(s, "%q")

-- raw string find
string.find(s, "%q", 1, true)
string.find(s, "%q", 1, math.random() < 0.5)

-- incorrect character class specifier
string.find(s, "%q", 1, false)

-- missing arguments
string.find()
string.find("foo");
("foo"):find()
)");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid match pattern: invalid character class, must refer to a defined class or its inverse");
    CHECK_EQ(result.warnings[0].location.begin.line, 4);
    CHECK_EQ(result.warnings[1].text, "Invalid match pattern: invalid character class, must refer to a defined class or its inverse");
    CHECK_EQ(result.warnings[1].location.begin.line, 11);
}

TEST_CASE_FIXTURE(Fixture, "FormatStringReplace")
{
    LintResult result = lint(R"(
local s = ...

-- incorrect replacements
string.gsub(s, '(%d+)', "%")
string.gsub(s, '(%d+)', "%x")
string.gsub(s, '(%d+)', "%2")
string.gsub(s, '', "%1")

-- correct replacements
string.gsub(s, '[A-Z]+(%d)', "%0%1")
string.gsub(s, 'foo', "%0")
)");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(4 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid match replacement: unfinished replacement");
    CHECK_EQ(result.warnings[1].text, "Invalid match replacement: unexpected replacement character; must be a digit or %");
    CHECK_EQ(result.warnings[2].text, "Invalid match replacement: invalid capture index, must refer to pattern capture");
    CHECK_EQ(result.warnings[3].text, "Invalid match replacement: invalid capture index, must refer to pattern capture");
}

TEST_CASE_FIXTURE(Fixture, "FormatStringDate")
{
    LintResult result = lint(R"(
-- incorrect formats
os.date("%")
os.date("%L")
os.date("%?")
os.date("\0")

-- correct formats
os.date("it's %c now")
os.date("!*t")
)");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(4 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid date format: unfinished replacement");
    CHECK_EQ(result.warnings[1].text, "Invalid date format: unexpected replacement character; must be a date format specifier or %");
    CHECK_EQ(result.warnings[2].text, "Invalid date format: unexpected replacement character; must be a date format specifier or %");
    CHECK_EQ(result.warnings[3].text, "Invalid date format: date format can not contain null characters");
}

TEST_CASE_FIXTURE(Fixture, "FormatStringTyped")
{
    LintResult result = lint(R"~(
local s: string, nons = ...

string.match(s, "[]")
s:match("[]")

-- no warning here since we don't know that it's a string
nons:match("[]")
)~");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Invalid match pattern: expected ] at the end of the string to close a set");
    CHECK_EQ(result.warnings[0].location.begin.line, 3);
    CHECK_EQ(result.warnings[1].text, "Invalid match pattern: expected ] at the end of the string to close a set");
    CHECK_EQ(result.warnings[1].location.begin.line, 4);
}

TEST_CASE_FIXTURE(Fixture, "TableLiteral")
{
    LintResult result = lint(R"(-- line 1
_ = {
    first = 1,
    second = 2,
    first = 3,
}

_ = {
    first = 1,
    ["first"] = 2,
}

_ = {
    1, 2, 3,
    [1] = 42
}

_ = {
    [3] = 42,
    1, 2, 3,
}

local _: {
    first: number,
    second: string,
    first: boolean
}

_ = {
    1, 2, 3,
    [0] = 42,
    [4] = 42,
}

_ = {
    [1] = 1,
    [2] = 2,
    [1] = 3,
}

function _foo(): { first: number, second: string, first: boolean }
end
)");

    REQUIRE(7 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Table field 'first' is a duplicate; previously defined at line 3");
    CHECK_EQ(result.warnings[1].text, "Table field 'first' is a duplicate; previously defined at line 9");
    CHECK_EQ(result.warnings[2].text, "Table index 1 is a duplicate; previously defined as a list entry");
    CHECK_EQ(result.warnings[3].text, "Table index 3 is a duplicate; previously defined as a list entry");
    CHECK_EQ(result.warnings[4].text, "Table type field 'first' is a duplicate; previously defined at line 24");
    CHECK_EQ(result.warnings[5].text, "Table index 1 is a duplicate; previously defined at line 36");
    CHECK_EQ(result.warnings[6].text, "Table type field 'first' is a duplicate; previously defined at line 41");
}

TEST_CASE_FIXTURE(Fixture, "read_write_table_props")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    LintResult result = lint(R"(-- line 1
        type A = {x: number}
        type B = {read x: number, write x: number}
        type C = {x: number, read x: number} -- line 4
        type D = {x: number, write x: number}
        type E = {read x: number, x: boolean}
        type F = {read x: number, read x: number}
        type G = {write x: number, x: boolean}
        type H = {write x: number, write x: boolean}
    )");

    REQUIRE(6 == result.warnings.size());
    CHECK(result.warnings[0].text == "Table type field 'x' is already read-write; previously defined at line 4");
    CHECK(result.warnings[1].text == "Table type field 'x' is already read-write; previously defined at line 5");
    CHECK(result.warnings[2].text == "Table type field 'x' already has a read type defined at line 6");
    CHECK(result.warnings[3].text == "Table type field 'x' is a duplicate; previously defined at line 7");
    CHECK(result.warnings[4].text == "Table type field 'x' already has a write type defined at line 8");
    CHECK(result.warnings[5].text == "Table type field 'x' is a duplicate; previously defined at line 9");
}

TEST_CASE_FIXTURE(Fixture, "ImportOnlyUsedInTypeAnnotation")
{
    LintResult result = lint(R"(
        local Foo = require(script.Parent.Foo)

        local x: Foo.Y = 1
    )");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Variable 'x' is never used; prefix with '_' to silence");
}

TEST_CASE_FIXTURE(Fixture, "ImportOnlyUsedInReturnType")
{
    LintResult result = lint(R"(
        local Foo = require(script.Parent.Foo)

        function foo(): Foo.Y
        end
    )");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Function 'foo' is never used; prefix with '_' to silence");
}

TEST_CASE_FIXTURE(Fixture, "DisableUnknownGlobalWithTypeChecking")
{
    LintResult result = lint(R"(
        --!strict
        unknownGlobal()
    )");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "no_spurious_warning_after_a_function_type_alias")
{
    LintResult result = lint(R"(
        local exports = {}
        export type PathFunction<P> = (P?) -> string
        exports.tokensToFunction = function() end
        return exports
    )");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "use_all_parent_scopes_for_globals")
{
    ScopePtr testScope = getFrontend().addEnvironment("Test");
    unfreeze(getFrontend().globals.globalTypes);
    getFrontend().loadDefinitionFile(
        getFrontend().globals,
        testScope,
        R"(
        declare Foo: number
    )",
        "@test",
        /* captureComments */ false
    );
    freeze(getFrontend().globals.globalTypes);

    fileResolver.environments["A"] = "Test";

    fileResolver.source["A"] = R"(
        local _foo: Foo = 123
        -- os.clock comes from the global scope, the parent of this module's environment
        local _bar: typeof(os.clock) = os.clock
    )";

    LintResult result = lintModule("A");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "DeadLocalsUsed")
{
    // Luwu: LuaIterators reports the `pairs` call, which isn't what this tests
    LintResult result = lint(R"(--!nolint LuaIterators
--!nolint LocalShadow
do
    local x
    for x in pairs({}) do
        print(x)
    end
    print(x) -- x is not initialized
end

do
    local a, b, c = 1, 2
    print(a, b, c) -- c is not initialized
end

do
    local a, b, c = table.unpack({})
    print(a, b, c) -- no warning as we don't know anything about c
end
    )");

    REQUIRE(3 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Variable 'x' defined at line 4 is never initialized or assigned; initialize with 'nil' to silence");
    CHECK_EQ(result.warnings[1].text, "Assigning 2 values to 3 variables initializes extra variables with nil; add 'nil' to value list to silence");
    CHECK_EQ(result.warnings[2].text, "Variable 'c' defined at line 12 is never initialized or assigned; initialize with 'nil' to silence");
}

TEST_CASE_FIXTURE(Fixture, "LocalFunctionNotDead")
{
    LintResult result = lint(R"(
local foo
function foo() end
    )");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "DuplicateGlobalFunction")
{
    LintResult result = lint(R"(
        function x() end

        function x() end

        return x
    )");

    REQUIRE_EQ(1, result.warnings.size());

    const auto& w = result.warnings[0];

    CHECK_EQ(LintWarning::Code_DuplicateFunction, w.code);
    CHECK_EQ("Duplicate function definition: 'x' also defined on line 2", w.text);
}

TEST_CASE_FIXTURE(Fixture, "DuplicateLocalFunction")
{
    LintOptions options;
    options.setDefaults();
    options.enableWarning(LintWarning::Code_DuplicateFunction);
    options.enableWarning(LintWarning::Code_LocalShadow);

    LintResult result = lint(
        R"(
        local function x() end

        print(x)

        local function x() end

        return x
    )",
        options
    );

    REQUIRE_EQ(1, result.warnings.size());

    CHECK_EQ(LintWarning::Code_DuplicateFunction, result.warnings[0].code);
}

TEST_CASE_FIXTURE(Fixture, "DuplicateMethod")
{
    LintResult result = lint(R"(
        local T = {}
        function T:x() end

        function T:x() end

        return x
    )");

    REQUIRE_EQ(1, result.warnings.size());

    const auto& w = result.warnings[0];

    CHECK_EQ(LintWarning::Code_DuplicateFunction, w.code);
    CHECK_EQ("Duplicate function definition: 'T.x' also defined on line 3", w.text);
}

TEST_CASE_FIXTURE(Fixture, "DontTriggerTheWarningIfTheFunctionsAreInDifferentScopes")
{
    LintResult result = lint(R"(
        if true then
            function c() end
        else
            function c() end
        end

        return c
    )");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "LintHygieneUAF")
{
    LintResult result = lint(R"(
        local Hooty = require(workspace.A)

        local  HoHooty = require(workspace.A)

        local h: Hooty.Pointy = ruire(workspace.A)

        local h: H
        local h: Hooty.Pointy = ruire(workspace.A)

        local hh: Hooty.Pointy = ruire(workspace.A)

        local h: Hooty.Pointy = ruire(workspace.A)

        linooty.Pointy = ruire(workspace.A)

        local hh: Hooty.Pointy = ruire(workspace.A)

        local h: Hooty.Pointy = ruire(workspace.A)

        linty = ruire(workspace.A)

        local h: Hooty.Pointy = ruire(workspace.A)

        local hh: Hooty.Pointy = ruire(workspace.A)

        local h: Hooty.Pointy = ruire(workspace.A)

        local h: Hooty.Pt
    )");

    REQUIRE(12 == result.warnings.size());
}

TEST_CASE_FIXTURE(BuiltinsFixture, "DeprecatedApiTyped")
{
    unfreeze(getFrontend().globals.globalTypes);
    TypeId instanceType = getFrontend().globals.globalTypes.addType(ExternType{"Instance", {}, std::nullopt, std::nullopt, {}, {}, "Test", {}});
    persist(instanceType);
    getFrontend().globals.globalScope->exportedTypeBindings["Instance"] = TypeFun{{}, instanceType};

    getMutable<ExternType>(instanceType)->props() = {
        {"Name", {getBuiltins()->stringType}},
        {"DataCost", {getBuiltins()->numberType, /* deprecated= */ true}},
        {"Wait", {getBuiltins()->anyType, /* deprecated= */ true}},
    };

    TypeId colorType =
        getFrontend().globals.globalTypes.addType(TableType{{}, std::nullopt, getFrontend().globals.globalScope->level, Luau::TableState::Sealed});

    getMutable<TableType>(colorType)->props = {{"toHSV", {getBuiltins()->anyType, /* deprecated= */ true, "Color3:ToHSV"}}};

    addGlobalBinding(getFrontend().globals, "Color3", Binding{colorType, {}});

    if (TableType* ttv = getMutable<TableType>(getGlobalBinding(getFrontend().globals, "table")))
    {
        ttv->props["foreach"].deprecated = true;
        ttv->props["getn"].deprecated = true;
        ttv->props["getn"].deprecatedSuggestion = "#";
    }

    freeze(getFrontend().globals.globalTypes);

    LintResult result = lint(R"(
return function (i: Instance)
    i:Wait(1.0)
    print(i.Name)
    print(Color3.toHSV())
    print(Color3.doesntexist, i.doesntexist) -- type error, but this verifies we correctly handle non-existent members
    print(table.getn({}))
    table.foreach({}, function() end)
    print(table.nogetn()) -- verify that we correctly handle non-existent members
    return i.DataCost
end
)");

    REQUIRE(5 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Member 'Instance.Wait' is deprecated");
    CHECK_EQ(result.warnings[1].text, "Member 'toHSV' is deprecated, use 'Color3:ToHSV' instead");
    CHECK_EQ(result.warnings[2].text, "Member 'table.getn' is deprecated, use '#' instead");
    CHECK_EQ(result.warnings[3].text, "Member 'table.foreach' is deprecated");
    CHECK_EQ(result.warnings[4].text, "Member 'Instance.DataCost' is deprecated");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "DeprecatedApiUntyped")
{
    if (TableType* ttv = getMutable<TableType>(getGlobalBinding(getFrontend().globals, "table")))
    {
        ttv->props["foreach"].deprecated = true;
        ttv->props["getn"].deprecated = true;
        ttv->props["getn"].deprecatedSuggestion = "#";
    }

    LintResult result = lint(R"(
-- TODO
return function ()
    print(table.getn({}))
    table.foreach({}, function() end)
    print(table.nogetn()) -- verify that we correctly handle non-existent members
end
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Member 'table.getn' is deprecated, use '#' instead");
    CHECK_EQ(result.warnings[1].text, "Member 'table.foreach' is deprecated");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "DeprecatedApiFenv")
{
    LintResult result = lint(R"(
local f, g, h = ...

getfenv(1)
getfenv(f :: () -> ())
getfenv(g :: number)
getfenv(h :: any)

setfenv(1, {})
setfenv(f :: () -> (), {})
setfenv(g :: number, {})
setfenv(h :: any, {})
)");

    // Luwu: every call also gets an OptimizationHint (see OptimizationHintFenvDeoptimizesTheModule)
    std::vector<LintWarning> deprecations;
    for (const LintWarning& warning : result.warnings)
    {
        if (warning.code == LintWarning::Code_DeprecatedApi)
            deprecations.push_back(warning);
    }
    result.warnings = std::move(deprecations);

    REQUIRE(4 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Function 'getfenv' is deprecated; consider using 'debug.info' instead");
    CHECK_EQ(result.warnings[0].location.begin.line + 1, 4);
    CHECK_EQ(result.warnings[1].text, "Function 'getfenv' is deprecated; consider using 'debug.info' instead");
    CHECK_EQ(result.warnings[1].location.begin.line + 1, 6);
    CHECK_EQ(result.warnings[2].text, "Function 'setfenv' is deprecated");
    CHECK_EQ(result.warnings[2].location.begin.line + 1, 9);
    CHECK_EQ(result.warnings[3].text, "Function 'setfenv' is deprecated");
    CHECK_EQ(result.warnings[3].location.begin.line + 1, 11);
}

static void checkDeprecatedWarning(const Luau::LintWarning& warning, const Luau::Position& begin, const Luau::Position& end, const char* msg)
{
    CHECK_EQ(warning.code, LintWarning::Code_DeprecatedApi);
    CHECK_EQ(warning.location, Location(begin, end));
    CHECK_EQ(warning.text, msg);
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeBeyondFunctions")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuAttributesEverywhere, true},
    };

    // A deprecated local reports at each use, not at its declaration.
    {
        LintResult result = lint(R"(
@[deprecated { use = "dog" }]
local puppy = "whimper"

print(puppy)
)");

        REQUIRE_EQ(1, result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(4, 6), Position(4, 11), "Variable 'puppy' is deprecated, use 'dog' instead");
    }

    // A deprecated type alias reports where the type is referenced.
    {
        LintResult result = lint(R"(
@deprecated
type Puppy = string

local x: Puppy = "whimper"
print(x)
)");

        REQUIRE_EQ(1, result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(4, 9), Position(4, 14), "Type 'Puppy' is deprecated");
    }

    // Both halves of the attribute's payload are reported.
    {
        LintResult result = lint(R"(
@[deprecated { use = "dog", reason = "puppies grow up" }]
local puppy = "whimper"

print(puppy)
)");

        REQUIRE_EQ(1, result.warnings.size());
        checkDeprecatedWarning(
            result.warnings[0],
            Position(4, 6),
            Position(4, 11),
            "Variable 'puppy' is deprecated, use 'dog' instead. puppies grow up"
        );
    }

    // A deprecated field of a table type reports where it is read through a value of that type.
    {
        LintResult result = lint(R"(
type PetSounds = {
    @[deprecated { use = "dog" }] puppy: string,
    dog: string,
}

local function f(sounds: PetSounds)
    return sounds.puppy
end
return f
)");

        REQUIRE_EQ(1, result.warnings.size());
        checkDeprecatedWarning(
            result.warnings[0], Position(7, 11), Position(7, 23), "Member 'PetSounds.puppy' is deprecated, use 'dog' instead"
        );
    }

    // A field with no attribute is untouched.
    {
        LintResult result = lint(R"(
type PetSounds = {
    @deprecated puppy: string,
    dog: string,
}

local function f(sounds: PetSounds)
    return sounds.dog
end
return f
)");

        CHECK_EQ(0, result.warnings.size());
    }
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeFieldOverridesValue")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuAttributesEverywhere, true},
    };

    // The RFC's "Table Fields" example: an attribute on the entry takes priority over one on the
    // value bound to it, both where the value is bound and where the member is later read.
    LintResult result = lint(R"(
@[deprecated { reason = "cat is a more modern API" }]
local function get_cat_sound()
    return "meow"
end

local bad_module = {
    get_cat_sound = get_cat_sound,
}
print(bad_module.get_cat_sound())

local module = {
    @[deprecated { use = "cat" }] get_cat_sound = get_cat_sound,
    cat = "meow",
}
print(module.get_cat_sound())
)");

    REQUIRE_EQ(3, result.warnings.size());

    // Bound into a plain field: the value's own deprecation is what is reported.
    checkDeprecatedWarning(
        result.warnings[0], Position(7, 20), Position(7, 33), "Function 'get_cat_sound' is deprecated. cat is a more modern API"
    );
    checkDeprecatedWarning(
        result.warnings[1], Position(9, 6), Position(9, 30), "Member 'get_cat_sound' is deprecated. cat is a more modern API"
    );

    // Bound into an attributed field: nothing is reported at the binding, because the field's
    // attribute replaced the value's -- and the read reports the field's message, not the value's.
    checkDeprecatedWarning(
        result.warnings[2], Position(15, 6), Position(15, 26), "Member 'get_cat_sound' is deprecated, use 'cat' instead"
    );
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeOnClasses")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuAttributesEverywhere, true},
        {FFlag::LuwuClasses, true},
    };

    // The class reports where it is used, and a deprecated field where it is read -- but a class
    // referring to itself inside its own body is not what the warning is for.
    LintResult result = lint(R"(
@[deprecated { use = "Dog" }]
class Puppy
    @[deprecated { use = "bark" }]
    public sound: string

    public function speak(self): string
        return self.sound
    end
end

local p = Puppy { sound = "whimper" }
print(p.sound)
)");

    REQUIRE_EQ(2, result.warnings.size());
    checkDeprecatedWarning(result.warnings[0], Position(11, 10), Position(11, 15), "Class 'Puppy' is deprecated, use 'Dog' instead");
    checkDeprecatedWarning(result.warnings[1], Position(12, 6), Position(12, 13), "Member 'Puppy.sound' is deprecated, use 'bark' instead");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeInsideTypeDefinitions")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuAttributesEverywhere, true},
    };

    // A deprecated type is reported wherever its name is written, inside other types included --
    // except as the type of a field that is itself deprecated, and inside its own definition.
    LintResult result = lint(R"(
@deprecated
type Puppy = "whimper"

type CanineSounds = {
    puppy: Puppy,
    dog: "woof",
}

type PetSounds = {
    @[deprecated { use = "dog" }] puppy: Puppy,
    dog: "bark",
}

local litter: { Puppy } = {}

@deprecated
type Node = { next: Node? }

return litter
)");

    REQUIRE_EQ(2, result.warnings.size());
    checkDeprecatedWarning(result.warnings[0], Position(5, 11), Position(5, 16), "Type 'Puppy' is deprecated");
    checkDeprecatedWarning(result.warnings[1], Position(14, 16), Position(14, 21), "Type 'Puppy' is deprecated");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeOnALibraryTypeField")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuAttributesEverywhere, true},
    };

    // How a library's type marks one function deprecated: the field reports where it is used through
    // a value, never at the definition.
    LintResult result = lint(R"(
type FsLib = {
    @[deprecated { use = "readfile" }] read: (path: string) -> string,
    readfile: (path: string) -> string,
}

local fs = {} :: FsLib
fs.read("config.toml")
fs.readfile("config.toml")
return fs
)");

    REQUIRE_EQ(1, result.warnings.size());
    checkDeprecatedWarning(result.warnings[0], Position(7, 0), Position(7, 7), "Member 'FsLib.read' is deprecated, use 'readfile' instead");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedTypeAliasIsScoped")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuAttributesEverywhere, true},
    };

    // A deprecated alias only reaches the block it is declared in, and a nearer alias or a generic
    // parameter with the same name shadows it.
    LintResult result = lint(R"(
local function a()
    local x: Foo = 1
    @deprecated
    type Foo = number
    return x
end

local function b()
    type Foo = string
    local y: Foo = "y"
    return y
end

local function c<Foo>(z: Foo): Foo
    return z
end

@deprecated
type Bar = number
type Box<Bar> = { value: Bar }

local function d()
    local w: Bar = 1
    return w
end

return a, b, c, d
)");

    REQUIRE_EQ(2, result.warnings.size());
    // Aliases are visible throughout their block, above the declaration included.
    checkDeprecatedWarning(result.warnings[0], Position(2, 13), Position(2, 16), "Type 'Foo' is deprecated");
    checkDeprecatedWarning(result.warnings[1], Position(23, 13), Position(23, 16), "Type 'Bar' is deprecated");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeInPrimaryConstructorParameters")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuAttributesEverywhere, true},
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuDefaultArguments, true},
    };

    LintResult result = lint(R"(
@deprecated
type Puppy = string

@deprecated
local whimper = "whimper"

class Dog(public sound: Puppy = whimper, @deprecated public old: Puppy)
end

return Dog
)");

    // The deprecated `old` parameter declares a deprecated field, so its own type is not reported.
    REQUIRE_EQ(2, result.warnings.size());
    checkDeprecatedWarning(result.warnings[0], Position(7, 24), Position(7, 29), "Type 'Puppy' is deprecated");
    checkDeprecatedWarning(result.warnings[1], Position(7, 32), Position(7, 39), "Variable 'whimper' is deprecated");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttribute")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    // @deprecated works on local functions
    {
        LintResult result = lint(R"(
@deprecated
local function testfun(x)
    return x + 1
end

testfun(1)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 0), Position(6, 7), "Function 'testfun' is deprecated");
    }

    // @deprecated works on globals functions
    {
        LintResult result = lint(R"(
@deprecated
function testfun(x)
    return x + 1
end

testfun(1)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 0), Position(6, 7), "Function 'testfun' is deprecated");
    }

    // @deprecated works on fully typed functions
    {
        LintResult result = lint(R"(
@deprecated
local function testfun(x:number):number
    return x + 1
end

if math.random(2) == 2 then
    testfun(1)
end
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(7, 4), Position(7, 11), "Function 'testfun' is deprecated");
    }

    // @deprecated works on functions without an explicit return type
    {
        LintResult result = lint(R"(
@deprecated
local function testfun(x:number)
    return x + 1
end

g(testfun)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 2), Position(6, 9), "Function 'testfun' is deprecated");
    }

    // @deprecated works on functions without an explicit argument type
    {
        LintResult result = lint(R"(
@deprecated
local function testfun(x):number
    if x == 1 then
        return x
    else
        return 1 + testfun(x - 1)
    end
end

testfun(1)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(10, 0), Position(10, 7), "Function 'testfun' is deprecated");
    }

    // @deprecated works on inner functions
    {
        LintResult result = lint(R"(
function flipFlop()
    local state = false

    @deprecated
    local function invert()
        state = !state
        return state
    end

    return invert
end

f = flipFlop()
assert(f() == true)
)");

        REQUIRE(2 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(10, 11), Position(10, 17), "Function 'invert' is deprecated");
        checkDeprecatedWarning(result.warnings[1], Position(14, 7), Position(14, 8), "Function 'f' is deprecated");
    }

    // @deprecated does not automatically apply to inner functions
    {
        LintResult result = lint(R"(
@deprecated
function flipFlop()
    local state = false

    local function invert()
        state = !state
        return state
    end

    return invert
end

f = flipFlop()
assert(f() == true)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(13, 4), Position(13, 12), "Function 'flipFlop' is deprecated");
    }

    // @deprecated works correctly if deprecated function is shadowed
    {
        LintResult result = lint(R"(
@deprecated
local function doTheThing()
    print("doing")
end

doTheThing()

local function shadow()
    local function doTheThing()
        print("doing!")
    end

    doTheThing()
end

shadow()
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 0), Position(6, 10), "Function 'doTheThing' is deprecated");
    }

    // @deprecated does not issue warnings if a deprecated function uses itself
    {
        LintResult result = lint(R"(
@deprecated
function fibonacci(n)
    if n == 0 then
        return 0
    elseif n == 1 then
        return 1
    else
        return fibonacci(n - 1) + fibonacci(n - 2)
    end
end

fibonacci(5)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(12, 0), Position(12, 9), "Function 'fibonacci' is deprecated");
    }

    // @deprecated works for mutually recursive functions
    {
        LintResult result = lint(R"(
@deprecated
function odd(x)
    if x == 0 then
        return false
    else
        return even(x - 1)
    end
end

@deprecated
function even(x)
    if x == 0 then
        return true
    else
        return odd(x - 1)
    end
end

assert(odd(1) == true)
assert(even(0) == true)
)");

        REQUIRE(4 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 15), Position(6, 19), "Function 'even' is deprecated");
        checkDeprecatedWarning(result.warnings[1], Position(15, 15), Position(15, 18), "Function 'odd' is deprecated");
        checkDeprecatedWarning(result.warnings[2], Position(19, 7), Position(19, 10), "Function 'odd' is deprecated");
        checkDeprecatedWarning(result.warnings[3], Position(20, 7), Position(20, 11), "Function 'even' is deprecated");
    }

    // @deprecated works for methods with a literal class name
    {
        LintResult result = lint(R"(
Account = { balance=0 }

@deprecated
function Account:deposit(v)
    self.balance = self.balance + v
end

Account:deposit(200.00)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(8, 0), Position(8, 15), "Member 'Account.deposit' is deprecated");
    }

    // @deprecated works for methods with a compound expression class name
    {
        LintResult result = lint(R"(
Account = { balance=0 }

function getAccount()
    return Account
end

@deprecated
function Account:deposit (v)
    self.balance = self.balance + v
end

(getAccount()):deposit(200.00)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(12, 0), Position(12, 22), "Member 'deposit' is deprecated");
    }

    // @deprecated works on anonymous functions assigned to locals
    {
        ScopedFastFlag sflag{FFlag::LuauDeprecatedAttributeOnAnonymousFunctions, true};

        LintResult result = lint(R"(
local foo = @deprecated function()
end

foo()
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(4, 0), Position(4, 3), "Function 'foo' is deprecated");
    }
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeWithParams")
{
    // @deprecated works on local functions
    {
        LintResult result = lint(R"(
@[deprecated{ use = "prodfun", reason = "Too old." }]
local function testfun(x)
    return x + 1
end

testfun(1)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(
            result.warnings[0], Position(6, 0), Position(6, 7), "Function 'testfun' is deprecated, use 'prodfun' instead. Too old."
        );
    }

    // @deprecated works on globals functions
    {
        LintResult result = lint(R"(
@[deprecated{ use = "prodfun", reason = "Too old." }]
function testfun(x)
    return x + 1
end

testfun(1)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(
            result.warnings[0], Position(6, 0), Position(6, 7), "Function 'testfun' is deprecated, use 'prodfun' instead. Too old."
        );
    }

    // @deprecated with only 'use' works on local functions
    {
        LintResult result = lint(R"(
@[deprecated{ use = "prodfun" }]
local function testfun(x)
    return x + 1
end

testfun(1)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 0), Position(6, 7), "Function 'testfun' is deprecated, use 'prodfun' instead");
    }

    // @deprecated with only 'use' works on globals functions
    {
        LintResult result = lint(R"(
@[deprecated{ use = "prodfun" }]
function testfun(x)
    return x + 1
end

testfun(1)
)");
        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 0), Position(6, 7), "Function 'testfun' is deprecated, use 'prodfun' instead");
    }


    // @deprecated with only 'reason' works on local functions
    {
        LintResult result = lint(R"(
@[deprecated{ reason = "Too old." }]
local function testfun(x)
    return x + 1
end

testfun(1)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 0), Position(6, 7), "Function 'testfun' is deprecated. Too old.");
    }

    // @deprecated with only 'reason' works on globals functions
    {
        LintResult result = lint(R"(
@[deprecated{ reason = "Too old." }]
function testfun(x)
    return x + 1
end

testfun(1)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(6, 0), Position(6, 7), "Function 'testfun' is deprecated. Too old.");
    }

    // @deprecated works for methods with a literal class name
    {
        LintResult result = lint(R"(
Account = { balance=0 }

@[deprecated{use = 'credit', reason = 'It sounds cool'}]
function Account:deposit(v)
    self.balance = self.balance + v
end

Account:deposit(200.00)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(
            result.warnings[0], Position(8, 0), Position(8, 15), "Member 'Account.deposit' is deprecated, use 'credit' instead. It sounds cool"
        );
    }

    // @deprecated works for methods with a compound expression class name
    {
        LintResult result = lint(R"(
Account = { balance=0 }

function getAccount()
    return Account
end

@[deprecated{use = 'credit', reason = 'It sounds cool'}]
function Account:deposit (v)
    self.balance = self.balance + v
end

(getAccount()):deposit(200.00)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(
            result.warnings[0], Position(12, 0), Position(12, 22), "Member 'deposit' is deprecated, use 'credit' instead. It sounds cool"
        );
    }

    {
        loadDefinition(R"(
@[deprecated{use = 'foo', reason = 'Do better.'}] declare function bar(x: number): string
)");

        LintResult result = lint(R"(
bar(2)
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(1, 0), Position(1, 3), "Function 'bar' is deprecated, use 'foo' instead. Do better.");
    }

    {
        loadDefinition(R"(
declare Hooty : {
    tooty : @[deprecated{use = 'foo', reason = 'bar'}] @checked (number) -> number
}
)");
        LintResult result = lint(R"(
print(Hooty:tooty(2.0))
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(1, 6), Position(1, 17), "Member 'Hooty.tooty' is deprecated, use 'foo' instead. bar");
    }

    {
        loadDefinition(R"(
declare extern type Foo with
   @[deprecated{use = 'foo', reason = 'baz'}]
   function bar(self, value: number) : number
end

declare Foo: {
   new: () -> Foo
}
)");

        LintResult result = lint(R"(
local foo = Foo.new()
print(foo:bar(2.0))
)");

        REQUIRE(1 == result.warnings.size());
        checkDeprecatedWarning(result.warnings[0], Position(2, 6), Position(2, 13), "Member 'bar' is deprecated, use 'foo' instead. baz");
    }
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeFunctionDeclaration")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    // @deprecated works on function type declarations

    loadDefinition(R"(
@deprecated declare function bar(x: number): string
)");

    LintResult result = lint(R"(
bar(2)
)");

    REQUIRE(1 == result.warnings.size());
    checkDeprecatedWarning(result.warnings[0], Position(1, 0), Position(1, 3), "Function 'bar' is deprecated");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeTableDeclaration")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    // @deprecated works on table type declarations

    loadDefinition(R"(
declare Hooty : {
    tooty : @deprecated @checked (number) -> number
}
)");

    LintResult result = lint(R"(
print(Hooty:tooty(2.0))
)");

    REQUIRE(1 == result.warnings.size());
    checkDeprecatedWarning(result.warnings[0], Position(1, 6), Position(1, 17), "Member 'Hooty.tooty' is deprecated");
}

TEST_CASE_FIXTURE(Fixture, "DeprecatedAttributeMethodDeclaration")
{
    ScopedFastFlag _{FFlag::DebugLuauForceOldSolver, false};

    // @deprecated works on table type declarations

    loadDefinition(R"(
declare extern type Foo with
   @deprecated
   function bar(self, value: number) : number
end

declare Foo: {
   new: () -> Foo
}
)");

    LintResult result = lint(R"(
local foo = Foo.new()
print(foo:bar(2.0))
)");

    REQUIRE(1 == result.warnings.size());
    checkDeprecatedWarning(result.warnings[0], Position(2, 6), Position(2, 13), "Member 'bar' is deprecated");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "TableOperations")
{
    LintResult result = lint(R"(
local t = {}
local tt = {}

table.insert(t, #t, 42)
table.insert(t, (#t), 42) -- silenced

table.insert(t, #t + 1, 42)
table.insert(t, #tt + 1, 42) -- different table, ok

table.insert(t, 0, 42)

table.remove(t, 0)

table.remove(t, #t-1)

table.insert(t, string.find("hello", "h"))

table.move(t, 0, #t, 1, tt)
table.move(t, 1, #t, 0, tt)

table.create(42, {})
table.create(42, {} :: {})
)");

    // Luwu: these calls are made for their warnings, so their discarded results don't matter here
    result.warnings = withoutCode(result, LintWarning::Code_DiscardedResult);

    REQUIRE(10 == result.warnings.size());
    CHECK_EQ(
        result.warnings[0].text,
        "table.insert will insert the value before the last element, which is likely a bug; consider removing the "
        "second argument or wrap it in parentheses to silence"
    );
    CHECK_EQ(result.warnings[1].text, "table.insert will append the value to the table; consider removing the second argument for efficiency");
    CHECK_EQ(result.warnings[2].text, "table.insert uses index 0 but arrays are 1-based; did you mean 1 instead?");
    CHECK_EQ(result.warnings[3].text, "table.remove uses index 0 but arrays are 1-based; did you mean 1 instead?");
    CHECK_EQ(
        result.warnings[4].text,
        "table.remove will remove the value before the last element, which is likely a bug; consider removing the "
        "second argument or wrap it in parentheses to silence"
    );
    CHECK_EQ(
        result.warnings[5].text,
        "table.insert may change behavior if the call returns more than one result; consider adding parentheses around second argument"
    );
    CHECK_EQ(result.warnings[6].text, "table.move uses index 0 but arrays are 1-based; did you mean 1 instead?");
    CHECK_EQ(result.warnings[7].text, "table.move uses index 0 but arrays are 1-based; did you mean 1 instead?");
    CHECK_EQ(
        result.warnings[8].text, "table.create with a table literal will reuse the same object for all elements; consider using a for loop instead"
    );
    CHECK_EQ(
        result.warnings[9].text, "table.create with a table literal will reuse the same object for all elements; consider using a for loop instead"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "TableOperationsIndexer")
{
    // CLI-116824 Linter incorrectly issues false positive when taking the length of a unannotated string function argument
    if (!FFlag::DebugLuauForceOldSolver)
        return;

    LintResult result = lint(R"(
local t1 = {} -- ok: empty
local t2 = {1, 2} -- ok: array
local t3 = { a = 1, b = 2 } -- not ok: dictionary
local t4: {[number]: number} = {} -- ok: array
local t5: {[string]: number} = {} -- not ok: dictionary
local t6: typeof(setmetatable({1, 2}, {})) = {} -- ok: table with metatable
local t7: string = "hello" -- ok: string
local t8: {number} | {n: number} = {} -- ok: union

-- not ok
print(#t3)
print(#t5)
ipairs(t5)

-- disabled
-- ipairs(t3) adds indexer to t3, silencing error on #t3

-- ok
print(#t1)
print(#t2)
print(#t4)
print(#t6)
print(#t7)
print(#t8)

ipairs(t1)
ipairs(t2)
ipairs(t4)
ipairs(t6)
ipairs(t7)
ipairs(t8)

-- ok, subtle: text is a string here implicitly, but the type annotation isn't available
-- type checker assigns a type of generic table with the 'sub' member; we don't emit warnings on generic tables
-- to avoid generating a false positive here
function _impliedstring(element, text)
        for i = 1, #text do
                element:sendText(text:sub(i, i))
        end
end
)");

    REQUIRE(3 == result.warnings.size());
    CHECK_EQ(result.warnings[0].location.begin.line + 1, 12);
    CHECK_EQ(result.warnings[0].text, "Using '#' on a table without an array part is likely a bug");
    CHECK_EQ(result.warnings[1].location.begin.line + 1, 13);
    CHECK_EQ(result.warnings[1].text, "Using '#' on a table with string keys is likely a bug");
    CHECK_EQ(result.warnings[2].location.begin.line + 1, 14);
    CHECK_EQ(result.warnings[2].text, "Using 'ipairs' on a table with string keys is likely a bug");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "TableRemoveFootgunLint")
{
    ScopedFastFlag featureFlag{FFlag::LuwuTableRemoveFootgunLint, true};

    LintResult result = lint(R"(
        local t = {"apple", "banana"}

        table.remove(t, table.find(t, "banana"))
        table.remove(t, nil)

        local i: number? = table.find(t, "apple")
        table.remove(t, i)

        local j: number = 1
        table.remove(t, j)

        local k = table.find(t, "pear")
        table.remove(t, k)
    )");

    REQUIRE(3 == result.warnings.size());
    CHECK_EQ(
        result.warnings[0].text,
        "If this is `nil`, `table.remove` will remove the last element of the array.\nConsider using `table.drop` instead. If order is not important, use a key/value table for better performance."
    );
    CHECK_EQ(
        result.warnings[1].text,
        "If this is `nil`, `table.remove` will remove the last element of the array.\nConsider using `table.drop` instead. If order is not important, use a key/value table for better performance."
    );
    CHECK_EQ(
        result.warnings[2].text,
        "If this is `nil`, `table.remove` will remove the last element of the array.\nConsider using `table.drop` instead. If order is not important, use a key/value table for better performance."
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "TableRemoveFootgunLintFlagDisabled")
{
    ScopedFastFlag featureFlag{FFlag::LuwuTableRemoveFootgunLint, false};

    LintResult result = lint(R"(
local t = {"apple", "banana"}

local i: number? = table.find(t, "apple")
table.remove(t, i)
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "DuplicateConditions")
{
    // Luwu: LuaAndOr reports the and-or ternary below, which DuplicateConditions leaves alone on purpose
    LintResult result = lint(R"(--!nolint LuaAndOr
if true then
elseif false then
elseif true then -- duplicate
end

if true then
elseif false then
else
    if true then -- duplicate
    end
end

_ = true and true
_ = true or true
_ = (true and false) and true
_ = (true and true) and true
_ = (true and true) or true
_ = (true and false) and (42 and false)

_ = true and true or false -- no warning since this is is a common pattern used as a ternary replacement

_ = if true then 1 elseif true then 2 else 3
)");

    REQUIRE(8 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Condition has already been checked on line 2");
    CHECK_EQ(result.warnings[0].location.begin.line + 1, 4);
    CHECK_EQ(result.warnings[1].text, "Condition has already been checked on column 5");
    CHECK_EQ(result.warnings[2].text, "Condition has already been checked on column 5");
    CHECK_EQ(result.warnings[3].text, "Condition has already been checked on column 6");
    CHECK_EQ(result.warnings[4].text, "Condition has already been checked on column 6");
    CHECK_EQ(result.warnings[5].text, "Condition has already been checked on column 6");
    CHECK_EQ(result.warnings[6].text, "Condition has already been checked on column 15");
    CHECK_EQ(result.warnings[6].location.begin.line + 1, 19);
    CHECK_EQ(result.warnings[7].text, "Condition has already been checked on column 8");
}

// Luwu If Local (rfcs/if-local.md): a `when` chain binds new locals, so the same value in two chains isn't a
// repeated check
TEST_CASE_FIXTURE(Fixture, "DuplicateConditionsSkipIfLocalChains")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    LintResult result = lint(R"(
local function f() return nil end

if local a = f() then
    print(a)
elseif local b = f() then
    print(b)
end

_ = if local a = f() then a elseif local b = f() then b else nil

if f() when local a = f() then
    print(a)
elseif f() then -- not a duplicate: the first branch also needed 'a'
end
)");

    CHECK(warningsWithCode(result, LintWarning::Code_DuplicateCondition).empty());
}

// Luwu If Local (rfcs/if-local.md): the local lints see a `when` chain's bindings, with their usual messages
TEST_CASE_FIXTURE(Fixture, "LocalLintsSeeIfLocalBindings")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    LintResult result = lint(R"(
local function f() return 1 end
local a = 1

if local unused = f() then
end

if local a = f() then
    print(a)
end
print(a)
)");

    std::vector<LintWarning> unused = warningsWithCode(result, LintWarning::Code_LocalUnused);
    REQUIRE_EQ(unused.size(), 1);
    CHECK_EQ(unused[0].text, "Variable 'unused' is never used; prefix with '_' to silence");

    std::vector<LintWarning> shadow = warningsWithCode(result, LintWarning::Code_LocalShadow);
    REQUIRE_EQ(shadow.size(), 1);
    CHECK_EQ(shadow[0].text, "Variable 'a' shadows previous declaration at line 3");
}

// Luwu Do Expressions (rfcs/do-expressions.md)
TEST_CASE_FIXTURE(Fixture, "ReturnOnNextLine")
{
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

    LintResult result = lint(R"(
local function maybe() return nil end
local function f()
    local a = maybe() or do return
        maybe()
    local b = maybe() or return
        maybe()
    local c = maybe() or return (
        maybe()
    )
    local d = maybe() or do return maybe()
    return
        maybe()
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_ReturnOnNextLine);
    REQUIRE_EQ(found.size(), 2);
    CHECK_EQ(found[0].location.begin.line, 4);
    CHECK_EQ(
        found[0].text,
        "This is the value 'return' returns, since it starts on the line after it; if 'return' should have no value, write '(do return)'"
    );
    CHECK_EQ(found[1].location.begin.line, 6);
    CHECK_EQ(
        found[1].text,
        "This is the value 'return' returns, since it starts on the line after it; if 'return' should have no value, write '(return)'"
    );

    // a statement that starts mid-line in a do expression is measured against the statement around it
    CHECK(warningsWithCode(result, LintWarning::Code_MultiLineStatement).empty());
}

// Luwu: bindings named after contextual keywords or standard globals
TEST_CASE_FIXTURE(BuiltinsFixture, "KeywordShadowAndBuiltinShadow")
{
    ScopedFastFlag sffs[] = {
        {FFlag::LuwuIfLocal, true},
        {FFlag::LuwuDestructuring, true},
    };

    LintResult result = lint(R"(
local continue = 1
local function class() end
function implements() end
declare = 5
for _, type in {} do end
local function f(typeof, ok) return typeof, ok end
local print = print
if local table = {} then end
local .{string} = { string = "s" }
local export = 1
local function g(const) return const end

-- not bindings: fields, keys, members, type names
local t = { continue = 1, type = 2 }
t.class = 3
local p = 1
type declare_ = number
return t, f, print, p, export, g
)");

    std::vector<LintWarning> keyword = warningsWithCode(result, LintWarning::Code_KeywordShadow);
    REQUIRE_EQ(keyword.size(), 6);
    CHECK_EQ(
        keyword[0].text,
        "'continue' is a keyword used for skipping an iteration of a loop and should not be used as an identifier; naming bindings 'continue' will "
        "become a hard error in the future"
    );
    CHECK_EQ(
        keyword[1].text,
        "'class' is a keyword used for declaring classes and should not be used as an identifier; naming bindings 'class' will become a hard "
        "error in the future"
    );
    CHECK_EQ(keyword[1].location.begin.line, 2);
    CHECK_EQ(keyword[2].location.begin.line, 3);
    CHECK_EQ(keyword[3].location.begin.line, 4);
    CHECK_EQ(
        keyword[4].text,
        "'export' is a keyword used for exporting values and types from a module and should not be used as an identifier; naming bindings "
        "'export' will become a hard error in the future"
    );
    CHECK_EQ(
        keyword[5].text,
        "'const' is a keyword used for declaring bindings that can't be reassigned and should not be used as an identifier; naming bindings "
        "'const' will become a hard error in the future"
    );

    std::vector<LintWarning> builtin = warningsWithCode(result, LintWarning::Code_BuiltinShadow);
    REQUIRE_EQ(builtin.size(), 5);
    CHECK_EQ(builtin[0].text, "'type' hides the builtin 'type' here");
    CHECK_EQ(builtin[1].text, "'typeof' hides the builtin 'typeof' here");
    CHECK_EQ(builtin[2].text, "'print' hides the builtin 'print' here");
    CHECK_EQ(builtin[3].text, "'table' hides the builtin 'table' here");
    CHECK_EQ(builtin[4].text, "'string' hides the builtin 'string' here");
}

TEST_CASE_FIXTURE(Fixture, "OrContinue")
{
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

    // A module that never uses `continue` as a name gets the shorthand, so there is nothing to warn about
    LintResult shorthand = lint(R"(
local function maybe() return nil end
for i = 1, 3 do
    local x = maybe() or continue
    print(x)
end
)");
    CHECK(warningsWithCode(shorthand, LintWarning::Code_OrContinue).empty());

    // This one reads `continue` elsewhere, so `x or continue` keeps reading the variable: in a loop, that is probably a
    // mistake, and outside one `or do continue` isn't a fix
    LintResult result = lint(R"(
local function maybe() return nil end
print(continue)
for i = 1, 3 do
    local x = maybe() or continue
    print(x)
end
local y = maybe() or continue
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_OrContinue);
    REQUIRE_EQ(found.size(), 1);
    CHECK_EQ(found[0].location.begin.line, 4);
    CHECK_EQ(
        found[0].text,
        "'continue' here reads a global variable named 'continue', since this module uses that name; to continue the loop, write 'or do continue'"
    );
}

TEST_CASE_FIXTURE(Fixture, "UnreachableCodeAfterGive")
{
    ScopedFastFlag doExpr{FFlag::DebugLuwuDoExpr, true};

    LintResult result = lint(R"(
local function maybe() return nil end
local v = do
    if maybe() then give 1 else give 2 end
    give 3
local w = do
    error("nope")
    give 1
print(v, w)
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_UnreachableCode);
    REQUIRE_EQ(found.size(), 1);
    CHECK_EQ(found[0].location.begin.line, 4);
    CHECK_EQ(found[0].text, "Unreachable code (previous statement always gives)");
}

TEST_CASE_FIXTURE(Fixture, "DuplicateConditionsExpr")
{
    LintResult result = lint(R"(
local correct, opaque = ...

if correct({a = 1, b = 2 * (-2), c = opaque.path['with']("calls", `string {opaque}`)}) then
elseif correct({a = 1, b = 2 * (-2), c = opaque.path['with']("calls", `string {opaque}`)}) then
elseif correct({a = 1, b = 2 * (-2), c = opaque.path['with']("calls", false)}) then
end
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Condition has already been checked on line 4");
    CHECK_EQ(result.warnings[0].location.begin.line + 1, 5);
}

TEST_CASE_FIXTURE(Fixture, "DuplicateLocal")
{
    LintResult result = lint(R"(
function foo(a1, a2, a3, a1)
end

local _, _, _ = ... -- ok!
local a1, a2, a1 = ... -- not ok

local moo = {}
function moo:bar(self)
end

return foo, moo, a1, a2
)");

    REQUIRE(4 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Function parameter 'a1' already defined on column 14");
    CHECK_EQ(result.warnings[1].text, "Variable 'a1' is never used; prefix with '_' to silence");
    CHECK_EQ(result.warnings[2].text, "Variable 'a1' already defined on column 7");
    CHECK_EQ(result.warnings[3].text, "Function parameter 'self' already defined implicitly");
}

TEST_CASE_FIXTURE(Fixture, "MisleadingAndOr")
{
    // Luwu: LuaAndOr reports every line here too; LuaAndOrReportsEveryAndOr covers it
    LintResult result = lint(R"(--!nolint LuaAndOr
_ = math.random() < 0.5 and true or 42
_ = math.random() < 0.5 and false or 42 -- misleading
_ = math.random() < 0.5 and nil or 42 -- misleading
_ = math.random() < 0.5 and 0 or 42
_ = (math.random() < 0.5 and false) or 42 -- currently ignored
)");

    REQUIRE(2 == result.warnings.size());
    // Luwu: shorter messages
    CHECK_EQ(result.warnings[0].text, "this 'a and b or c' always evaluates to 'c' because 'b' is false, use 'if a then b else c' instead");
    CHECK_EQ(result.warnings[1].text, "this 'a and b or c' always evaluates to 'c' because 'b' is nil, use 'if a then b else c' instead");
}

TEST_CASE_FIXTURE(Fixture, "WrongComment")
{
    ScopedFastFlag allowTrust{FFlag::DebugLuwuCompilerTrustsTypeAnnotations, true};

    LintResult result = lint(R"(
--!strict
--!struct
--!nolintGlobal
--!nolint Global
--!nolint KnownGlobal
--!nolint UnknownGlobal
--! no more lint
--!strict here
--!native on
--!trust
--!trust me
do end
--!nolint
)");

    REQUIRE(8 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Unknown comment directive 'struct'; did you mean 'strict'?");
    CHECK_EQ(result.warnings[1].text, "Unknown comment directive 'nolintGlobal'");
    CHECK_EQ(result.warnings[2].text, "nolint directive refers to unknown lint rule 'Global'");
    CHECK_EQ(result.warnings[3].text, "nolint directive refers to unknown lint rule 'KnownGlobal'; did you mean 'UnknownGlobal'?");
    CHECK_EQ(result.warnings[4].text, "Comment directive with the type checking mode has extra symbols at the end of the line");
    CHECK_EQ(result.warnings[5].text, "native directive has extra symbols at the end of the line");
    // `--!trust` on its own is a known directive and warns about nothing
    CHECK_EQ(result.warnings[6].text, "trust directive has extra symbols at the end of the line");
    CHECK_EQ(result.warnings[7].text, "Comment directive is ignored because it is placed after the first non-comment token");
}

TEST_CASE_FIXTURE(Fixture, "TrustDirectiveWithoutTheFlag")
{
    // The flag only permits `--!trust`, so without it the directive does nothing, and says so.
    ScopedFastFlag disallowTrust{FFlag::DebugLuwuCompilerTrustsTypeAnnotations, false};

    LintResult result = lint(R"(
--!trust
do end
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "trust directive has no effect because DebugLuwuCompilerTrustsTypeAnnotations is disabled");
}

TEST_CASE_FIXTURE(Fixture, "WrongCommentMuteSelf")
{
    LintResult result = lint(R"(
--!nolint
--!nolint All
--!struct
)");

    // Luwu: `--!nolint All` too, since a bare `--!nolint` leaves BareNolint on
    REQUIRE(0 == result.warnings.size()); // --!nolint disables WrongComment lint :)
}

TEST_CASE_FIXTURE(Fixture, "DuplicateConditionsIfStatAndExpr")
{
    LintResult result = lint(R"(
if if 1 then 2 else 3 then
elseif if 1 then 2 else 3 then
elseif if 0 then 5 else 4 then
end
)");

    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Condition has already been checked on line 2");
}

TEST_CASE_FIXTURE(Fixture, "WrongCommentOptimize")
{
    LintResult result = lint(R"(
--!optimize
--!optimize me
--!optimize 100500
--!optimize 2
)");

    REQUIRE(3 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "optimize directive requires an optimization level");
    CHECK_EQ(result.warnings[1].text, "optimize directive uses unknown optimization level 'me', 0..2 expected");
    CHECK_EQ(result.warnings[2].text, "optimize directive uses unknown optimization level '100500', 0..2 expected");

    result = lint("--!optimize   ");
    REQUIRE(1 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "optimize directive requires an optimization level");
}

TEST_CASE_FIXTURE(Fixture, "TestStringInterpolation")
{
    LintResult result = lint(R"(
        --!nocheck
        local _ = `unknown {foo}`
    )");

    REQUIRE(1 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "IntegerParsing")
{
    LintResult result = lint(R"(
local _ = 0b10000000000000000000000000000000000000000000000000000000000000000
local _ = 0x10000000000000000
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Binary number literal exceeded available precision and was truncated to 2^64");
    CHECK_EQ(result.warnings[1].text, "Hexadecimal number literal exceeded available precision and was truncated to 2^64");
}

TEST_CASE_FIXTURE(Fixture, "IntegerParsingDecimalImprecise")
{
    LintResult result = lint(R"(
local _ = 10000000000000000000000000000000000000000000000000000000000000000
local _ = 10000000000000001
local _ = -10000000000000001

-- 10^16 = 2^16 * 5^16, 5^16 only requires 38 bits
local _ = 10000000000000000
local _ = -10000000000000000

-- smallest possible number that is parsed imprecisely
local _ = 9007199254740993
local _ = -9007199254740993

-- note that numbers before and after parse precisely (number after is even => 1 more mantissa bit)
local _ = 9007199254740992
local _ = 9007199254740994

-- large powers of two should work as well (this is 2^63)
local _ = -9223372036854775808
)");

    REQUIRE(5 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Number literal exceeded available precision and was truncated to closest representable number");
    CHECK_EQ(result.warnings[0].location.begin.line, 1);
    CHECK_EQ(result.warnings[1].text, "Number literal exceeded available precision and was truncated to closest representable number");
    CHECK_EQ(result.warnings[1].location.begin.line, 2);
    CHECK_EQ(result.warnings[2].text, "Number literal exceeded available precision and was truncated to closest representable number");
    CHECK_EQ(result.warnings[2].location.begin.line, 3);
    CHECK_EQ(result.warnings[3].text, "Number literal exceeded available precision and was truncated to closest representable number");
    CHECK_EQ(result.warnings[3].location.begin.line, 10);
    CHECK_EQ(result.warnings[4].text, "Number literal exceeded available precision and was truncated to closest representable number");
    CHECK_EQ(result.warnings[4].location.begin.line, 11);
}

TEST_CASE_FIXTURE(Fixture, "IntegerParsingHexImprecise")
{
    LintResult result = lint(R"(
local _ = 0x1234567812345678

-- smallest possible number that is parsed imprecisely
local _ = 0x20000000000001

-- note that numbers before and after parse precisely (number after is even => 1 more mantissa bit)
local _ = 0x20000000000000
local _ = 0x20000000000002

-- large powers of two should work as well (this is 2^63)
local _ = 0x80000000000000
)");

    REQUIRE(2 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "Number literal exceeded available precision and was truncated to closest representable number");
    CHECK_EQ(result.warnings[0].location.begin.line, 1);
    CHECK_EQ(result.warnings[1].text, "Number literal exceeded available precision and was truncated to closest representable number");
    CHECK_EQ(result.warnings[1].location.begin.line, 4);
}

TEST_CASE_FIXTURE(Fixture, "ComparisonPrecedence")
{
    LintResult result = lint(R"(
local a, b = ...

local _ = not a == b
local _ = not a ~= b
local _ = not a <= b
local _ = a <= b == 0
local _ = a <= b <= 0

local _ = not a == not b -- weird but ok

-- silence tests for all of the above
local _ = not (a == b)
local _ = (not a) == b
local _ = not (a ~= b)
local _ = (not a) ~= b
local _ = not (a <= b)
local _ = (not a) <= b
local _ = (a <= b) == 0
local _ = a <= (b == 0)
)");

    REQUIRE(5 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "not X == Y is equivalent to (not X) == Y; consider using X ~= Y, or add parentheses to silence");
    CHECK_EQ(result.warnings[1].text, "not X ~= Y is equivalent to (not X) ~= Y; consider using X == Y, or add parentheses to silence");
    CHECK_EQ(result.warnings[2].text, "not X <= Y is equivalent to (not X) <= Y; add parentheses to silence");
    CHECK_EQ(result.warnings[3].text, "X <= Y == Z is equivalent to (X <= Y) == Z; add parentheses to silence");
    CHECK_EQ(result.warnings[4].text, "X <= Y <= Z is equivalent to (X <= Y) <= Z; did you mean X <= Y and Y <= Z?");
}

TEST_CASE_FIXTURE(Fixture, "RedundantNativeAttribute")
{
    LintResult result = lint(R"(
--!native

@native
local function f(a)
    @native
    local function g(b)
        return (a + b)
    end
    return g
end

f(3)(4)
)");

    REQUIRE(2 == result.warnings.size());

    CHECK_EQ(result.warnings[0].text, "native attribute on a function is redundant in a native module; consider removing it");
    CHECK_EQ(result.warnings[0].location, Location(Position(3, 0), Position(3, 7)));

    CHECK_EQ(result.warnings[1].text, "native attribute on a function is redundant in a native module; consider removing it");
    CHECK_EQ(result.warnings[1].location, Location(Position(5, 4), Position(5, 11)));
}

TEST_CASE_FIXTURE(Fixture, "type_instantiation_lints")
{
    LintResult result = lint(R"(
local function a<b>(cool: b)
    print(cool)
end

a<<"hi">>("hi")
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(BuiltinsFixture, "NilNoneComparison")
{
    ScopedFastFlag nonePrimitive{FFlag::LuwuNonePrimitive, true};

    LintResult result = lint(R"(
local function getNumber(x: string | number): number | none
    if type(x) == "number" then
        return x
    end
    return none
end

local function find(t: { string }, s: string): number?
    return table.find(t, s)
end

local x = getNumber("4")
if x ~= nil then end
if nil == x then end
if x ~= none then end -- ok
if x then end -- ok

local i = find({}, "a")
while i ~= none do end

local t = { value = getNumber(1) }
local _hasValue = t.value ~= nil

local flag: boolean | none = none
if flag ~= nil then end

local both: number | nil | none = nil
if both ~= nil then end -- ok: can be either
if both ~= none then end -- ok

local neither = 5
if neither ~= nil then end -- ok: a different mistake, not this one

local function _untyped(u) return u ~= nil end -- ok: unknown
local a: any = nil
if a ~= none then end -- ok
)");

    REQUIRE_EQ(5, result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "'x' can be 'none' but never 'nil', so this is always true; did you mean 'x ~= none' or 'if x then'?");
    CHECK_EQ(result.warnings[0].location, Location(Position(13, 3), Position(13, 11)));
    CHECK_EQ(result.warnings[1].text, "'x' can be 'none' but never 'nil', so this is always false; did you mean 'x == none' or 'if not x then'?");
    CHECK_EQ(result.warnings[2].text, "'i' can be 'nil' but never 'none', so this is always true; did you mean 'i ~= nil' or 'while i do'?");
    // Not a condition: `t.value` on its own isn't a boolean.
    CHECK_EQ(result.warnings[3].text, "'t.value' can be 'none' but never 'nil', so this is always true; did you mean 't.value ~= none'?");
    // `if flag then` would also skip `false`.
    CHECK_EQ(result.warnings[4].text, "'flag' can be 'none' but never 'nil', so this is always true; did you mean 'flag ~= none'?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "NilNoneComparisonNeedsTheNonePrimitive")
{
    ScopedFastFlag nonePrimitive{FFlag::LuwuNonePrimitive, false};

    // Without the `none` runtime there is nothing to mix up with `nil`, so the lint doesn't run.
    LintResult result = lint(R"(
local x: number | none = none
if x ~= nil then end
)");

    for (const LintWarning& w : result.warnings)
        CHECK(w.code != LintWarning::Code_NilNoneComparison);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "VarargCastIgnoresCallsThatReturnOneValue")
{
    // A cast on a call only truncates when the call can return more than one value; these are
    // ordinary idioms that lose nothing.
    LintResult result = lint(R"(
local function one(): number return 1 end
local function pair(): (number, number) return 1, 2 end
local function many(...: number): ...number return ... end

local function ok(x: any)
    print(tostring(x) :: string)
    local t = { tostring(x) :: string, one() :: any }
    print(t)
    return require(x) :: any
end

local function bad()
    print(pair() :: number)
    print(many(1, 2) :: number)
end

return ok, bad
)");

    std::vector<LintWarning> warnings;
    for (const LintWarning& w : result.warnings)
    {
        if (w.code == LintWarning::Code_VarargCast)
            warnings.push_back(w);
    }

    REQUIRE_EQ(2, warnings.size());
    CHECK(warnings[0].text.find("'pair()'") != std::string::npos);
    CHECK_EQ(warnings[0].location.begin.line, 13);
    CHECK(warnings[1].text.find("'many()'") != std::string::npos);
    CHECK_EQ(warnings[1].location.begin.line, 14);
}

TEST_CASE_FIXTURE(Fixture, "VarargCast")
{
    LintResult result = lint(R"(
local function count(...: number) return select("#", ...) end
local function pair(): (number, number) return 1, 2 end
local lib = { pair = pair }

local function f(...: string | number)
    count(... :: number)
    count(1, pair() :: number)
    local t = { ... :: number }
    local a, b = lib.pair() :: number
    count(... :: any :: number)
    return ... :: number
end

local function ok(...: string | number)
    count((... :: number)) -- ok: parenthesized
    count(... :: number, 1) -- ok: not last, one value either way
    local x = ... :: number -- ok: one name
    local t = { (... :: number), n = ... :: number } -- ok
    count(((...)) :: number) -- ok: the parentheses truncated it already
    return count(...)
end

return f, ok
)");

    // `local a, b = ...` also reports UnbalancedAssignment; only VarargCast is under test here.
    std::vector<LintWarning> warnings;
    for (const LintWarning& w : result.warnings)
    {
        if (w.code == LintWarning::Code_VarargCast)
            warnings.push_back(w);
    }

    REQUIRE_EQ(6, warnings.size());
    CHECK_EQ(
        warnings[0].text,
        "This type cast silently truncates '...' to its first value at runtime; use a helper function to convert these values to '...number', "
        "or wrap this in parentheses to silence"
    );
    CHECK_EQ(warnings[0].location, Location(Position(6, 10), Position(6, 23)));
    CHECK_EQ(
        warnings[1].text,
        "This type cast silently truncates 'pair()' to its first result at runtime; use a helper function to convert these results to "
        "'...number', or wrap this in parentheses to silence"
    );
    CHECK_EQ(warnings[2].location.begin.line, 8);
    CHECK(warnings[3].text.find("'lib.pair()'") != std::string::npos);
    CHECK(warnings[4].text.find("'...'") != std::string::npos);
    CHECK_EQ(warnings[5].location.begin.line, 11);
}

TEST_CASE_FIXTURE(Fixture, "destructuring_lints_like_the_names_it_binds")
{
    ScopedFastFlag luwuDestructuring{FFlag::LuwuDestructuring, true};

    // The declaration desugars to several statements on one line, and each unnamed pattern has a hidden local
    // of the same name; neither is visible in the source, so neither warns. An unused destructured name does.
    LintResult result = lint(R"(
local function f(t)
    local .{a, unused} = t
    local .{b} = t; print(a, b)
end
f({})
)");

    REQUIRE_EQ(result.warnings.size(), 1);
    CHECK_EQ(result.warnings[0].code, LintWarning::Code_LocalUnused);
    CHECK_EQ(result.warnings[0].text, "Variable 'unused' is never used; prefix with '_' to silence");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "declared_globals_are_known")
{
    ScopedFastFlag sffs[] = {{FFlag::LuauSolverV2, true}, {FFlag::LuwuDeclareStatements, true}};

    // UnknownGlobal only runs in nocheck files; the writes would otherwise suggest a local
    LintResult result = lint(R"(
--!nocheck
print(early)
local function f()
    counter = counter + 1
end
f()
declare early: number
declare counter: number
)");

    REQUIRE(0 == result.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "DeclareMismatch")
{
    ScopedFastFlag sffs[] = {{FFlag::LuauSolverV2, true}, {FFlag::LuwuDeclareStatements, true}};

    loadDefinition(R"(
        export type Path = { path: string }
        declare script: Path
        declare version: string
        declare function exit(code: number): never
    )");

    // The same declaration as the loaded definitions, spelled with or without the alias, is silent
    LintResult same = lint(R"(
declare script: Path
declare version: string
declare function exit(code: number): never
)");
    CHECK(0 == same.warnings.size());

    LintResult different = lint(R"(
declare version: number
declare function exit(code: string): never
declare unrelated: number
)");
    REQUIRE(2 == different.warnings.size());
    CHECK_EQ(different.warnings[0].code, LintWarning::Code_DeclareMismatch);
    CHECK_EQ(
        different.warnings[0].text,
        "'version' is declared here as 'number', but the loaded definitions declare it as 'string'; add '--!nolint "
        "DeclareMismatch' if this is intended"
    );
    CHECK_EQ(different.warnings[0].location.begin.line, 1);
    CHECK_EQ(different.warnings[1].code, LintWarning::Code_DeclareMismatch);
    CHECK_EQ(different.warnings[1].location.begin.line, 2);

    LintResult silenced = lint(R"(
--!nolint DeclareMismatch
declare version: number
)");
    CHECK(0 == silenced.warnings.size());
}

TEST_CASE_FIXTURE(Fixture, "DeclareMismatchOnTypes")
{
    ScopedFastFlag sffs[] = {{FFlag::LuauSolverV2, true}, {FFlag::LuwuDeclareStatements, true}};

    loadDefinition(R"(
        declare extern type Path with
            raw: string
            function join(self, other: string): Path
        end
    )");

    // The same declaration, with or without `with`, is silent
    LintResult same = lint(R"(
export declare extern type Path
    raw: string
    function join(self, other: string): Path
end
)");
    CHECK(0 == same.warnings.size());

    LintResult different = lint(R"(
declare extern type Path
    raw: number
    function join(self, other: string): Path
end
)");
    REQUIRE(1 == different.warnings.size());
    CHECK_EQ(different.warnings[0].code, LintWarning::Code_DeclareMismatch);
    CHECK_EQ(
        different.warnings[0].text,
        "Type 'Path' is declared here differently from the loaded definitions; add '--!nolint DeclareMismatch' if this is intended"
    );
}

TEST_CASE_FIXTURE(Fixture, "DeclareMismatchIgnoresUntypedDeclarations")
{
    ScopedFastFlag sffs[] = {{FFlag::LuauSolverV2, true}, {FFlag::LuwuDeclareStatements, true}};

    loadDefinition(R"(
        declare version: string
    )");

    // `declare version` takes the environment's type, so there is nothing to disagree about
    LintResult result = lint(R"(
declare version
return version
)");
    CHECK(0 == result.warnings.size());
}

static std::vector<LintWarning> optimizationHints(const LintResult& result)
{
    std::vector<LintWarning> hints;
    for (const LintWarning& warning : result.warnings)
    {
        // Luwu: the hints OptimizationHint's own pass reports, each under its own part
        bool ownHint = warning.code == LintWarning::Code_MethodsNotInlined || warning.code == LintWarning::Code_FloorDivision ||
                       warning.code == LintWarning::Code_FenvDeoptimization;
        if (ownHint)
            hints.push_back(warning);
    }
    return hints;
}

TEST_CASE_FIXTURE(Fixture, "OptimizationHintSuggestsIsinstanceForNilCheckedClassObjects")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    LintResult result = lint(R"(
class Cat(name: string)
    function meow(self): string
        return self.name
    end
end

local function a(cat: Cat?, other: Cat | none)
    if cat then
        print(cat:meow())
    elseif other ~= none then
        print(other.name)
    end
end

-- already proven, no use in the body, only a closure uses it, a non-optional annotation, not a class
local function quiet(cat: Cat?, plain: Cat, s: string?)
    if class.isinstance(cat, Cat) then print(cat:meow()) end
    if cat then print("has a cat") end
    if cat then print(function() return cat:meow() end) end
    if plain then print(plain:meow()) end
    if s then print(s:upper()) end
end

return a, quiet
)");

    std::vector<LintWarning> hints = optimizationHints(result);
    REQUIRE(2 == hints.size());
    CHECK_EQ(hints[0].code, LintWarning::Code_MethodsNotInlined);
    CHECK_EQ(8, hints[0].location.begin.line);
    CHECK_EQ(
        hints[0].text,
        "Checking 'cat' for nil doesn't prove to the compiler that it's a 'Cat', since type annotations are only trusted under '--!trust'; use "
        "'if class.isinstance(cat, Cat) then' so 'Cat' methods can be inlined here"
    );
    CHECK_EQ(10, hints[1].location.begin.line);
}

TEST_CASE_FIXTURE(Fixture, "OptimizationHintIsSilentInTrustedFiles")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    const char* source = R"(--!trust
class Cat(name: string) end

local function a(cat: Cat?)
    if cat then
        print(cat.name)
    end
end

return a
)";

    {
        // the directive does nothing while the embedder doesn't allow it, so the hint still applies
        ScopedFastFlag disallowTrust{FFlag::DebugLuwuCompilerTrustsTypeAnnotations, false};
        CHECK(1 == optimizationHints(lint(source)).size());
    }

    ScopedFastFlag allowTrust{FFlag::DebugLuwuCompilerTrustsTypeAnnotations, true};
    CHECK(optimizationHints(lint(source)).empty());
}

TEST_CASE_FIXTURE(Fixture, "OptimizationHintAssertAndFieldMethodCalls")
{
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};

    LintResult result = lint(R"(
class Vec2(x: number, y: number)
    function add(self, o: Vec2): Vec2
        return Vec2(self.x + o.x, self.y + o.y)
    end
end

class Body(pos: Vec2, vel: Vec2)
    function sum(self): Vec2
        local moved = self.pos:add(self.vel)
        return moved:add(self.pos:add(moved))
    end

    function step(self)
        self.pos = self.pos:add(self.vel)
        self.pos = self.pos:add(self.vel)
    end
end

local function guard(v: Vec2?)
    assert(v)
    return v:add(v)
end

local function quietGuard(v: Vec2?)
    assert(v)
    return "no use after it"
end

return guard, quietGuard
)");

    // `step` assigns `self.pos` between its calls, so binding it to a local once would change what it does
    std::vector<LintWarning> hints = optimizationHints(result);
    REQUIRE(2 == hints.size());
    CHECK_EQ(9, hints[0].location.begin.line);
    CHECK_EQ(
        hints[0].text,
        "'self.pos' has 2 method calls here, but a field can't be proven to be a 'Vec2' in place, so none of them can be inlined; check it "
        "once in a local: 'local pos = self.pos' and 'assert(class.isinstance(pos, Vec2))'"
    );
    CHECK_EQ(20, hints[1].location.begin.line);
    CHECK_EQ(
        hints[1].text,
        "Asserting 'v' isn't nil doesn't prove to the compiler that it's a 'Vec2', since type annotations are only trusted under '--!trust'; "
        "use 'assert(class.isinstance(v, Vec2))' so 'Vec2' methods can be inlined after it"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "OptimizationHintFenvAndFloorDivision")
{
    const std::string source = R"(
local function f(a: number, b: number, v: vector)
    local env = getfenv(1)
    setfenv(2, env)
    return math.floor(a / b), math.floor(a), math.floor(v / v)
end

return f
)";

    std::vector<LintWarning> hints = optimizationHints(lint(source));
    REQUIRE(3 == hints.size());
    CHECK_EQ(hints[0].code, LintWarning::Code_FenvDeoptimization);
    CHECK_EQ(hints[0].text, "Using 'getfenv' deoptimizes this entire module and makes your code run slower");
    CHECK_EQ(hints[1].code, LintWarning::Code_FenvDeoptimization);
    CHECK_EQ(hints[1].text, "Using 'setfenv' deoptimizes this entire module and makes your code run slower");
    CHECK_EQ(hints[2].code, LintWarning::Code_FloorDivision);
    CHECK_EQ(hints[2].text, "'math.floor(a / b)' divides and then calls a function; 'a // b' gives the same result in one instruction");

    // each part can be turned off alone, and turning off OptimizationHint turns them all off
    std::vector<LintWarning> noFenv = optimizationHints(lint("--!nolint FenvDeoptimization" + source));
    REQUIRE(1 == noFenv.size());
    CHECK_EQ(noFenv[0].code, LintWarning::Code_FloorDivision);

    CHECK(optimizationHints(lint("--!nolint OptimizationHint" + source)).empty());
}

static size_t countWarnings(const LintResult& result, LintWarning::Code code)
{
    size_t count = 0;
    for (const LintWarning& warning : result.warnings)
    {
        if (warning.code == code)
            ++count;
    }
    return count;
}

TEST_CASE_FIXTURE(BuiltinsFixture, "OptimizationHintLoopParts")
{
    const std::string source = R"(
local function f(names: { string }, queue: { number })
    local s = ""
    for _, name in names do
        s ..= name
        s = s .. ","
        local line = ""
        line ..= name
    end
    while #queue > 0 do
        local first = table.remove(queue, 1)
        table.insert(queue, 1, first :: number)
        table.remove(queue)
        local inner = {}
        table.insert(inner, 1, 0)
    end
    local function later() s ..= "!" end
    return s, later
end

return f
)";

    LintResult all = lint(source);
    CHECK_EQ(2, countWarnings(all, LintWarning::Code_LoopConcat));
    CHECK_EQ(1, countWarnings(all, LintWarning::Code_InefficientTableInsert));
    CHECK_EQ(1, countWarnings(all, LintWarning::Code_InefficientTableRemove));

    // each part can be turned off alone, and turning off OptimizationHint turns them all off
    LintResult noConcat = lint("--!nolint LoopConcat\n" + source);
    CHECK_EQ(0, countWarnings(noConcat, LintWarning::Code_LoopConcat));
    CHECK_EQ(1, countWarnings(noConcat, LintWarning::Code_InefficientTableRemove));

    LintResult noHints = lint("--!nolint OptimizationHint\n" + source);
    CHECK_EQ(0, countWarnings(noHints, LintWarning::Code_LoopConcat));
    CHECK_EQ(0, countWarnings(noHints, LintWarning::Code_InefficientTableInsert));
    CHECK_EQ(0, countWarnings(noHints, LintWarning::Code_InefficientTableRemove));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "FloatIndexReportsDivisionInIndexArithmetic")
{
    LintResult result = lint(R"(
local xs = { 1, 2, 3 }
local a = xs[#xs / 2]
local b = xs[#xs / 2 + 1]
local c = xs[#xs // 2]
local d = xs[math.floor(#xs / 2)]
xs[-(#xs / 2)] = 0
return a, b, c, d
)");

    std::vector<LintWarning> found;
    for (const LintWarning& warning : result.warnings)
        if (warning.code == LintWarning::Code_FloatIndex)
            found.push_back(warning);

    // `//` and a call around the division are fine
    REQUIRE_EQ(found.size(), 3);
    CHECK_EQ(
        found[0].text,
        "'/' always gives a float, so this index reads nil whenever the result isn't whole; use '//' to divide to an integer"
    );
    // the warning points at the division, not the whole index
    CHECK_EQ(found[1].location, Location({3, 13}, {3, 20}));
    CHECK_EQ(found[2].location.begin.line, 6);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "LuaIteratorsReportsPairsAndIpairsSeparately")
{
    const std::string source = R"(
local t = { 1, 2 }
for k in pairs(t) do print(k) end
for i in ipairs(t) do print(i) end
local next_pair = pairs
return next_pair
)";

    LintResult all = lint(source);
    CHECK_EQ(countWarnings(all, LintWarning::Code_Pairs), 2);
    CHECK_EQ(countWarnings(all, LintWarning::Code_Ipairs), 1);

    // each part can be turned off alone, and turning off the group turns off both
    LintResult noPairs = lint("--!nolint Pairs\n" + source);
    CHECK_EQ(countWarnings(noPairs, LintWarning::Code_Pairs), 0);
    CHECK_EQ(countWarnings(noPairs, LintWarning::Code_Ipairs), 1);

    LintResult noGroup = lint("--!nolint LuaIterators\n" + source);
    CHECK_EQ(countWarnings(noGroup, LintWarning::Code_Pairs), 0);
    CHECK_EQ(countWarnings(noGroup, LintWarning::Code_Ipairs), 0);

    // a local named `pairs` is someone else's function
    LintResult shadowed = lint("local function pairs(t) return next, t end\nfor k in pairs({}) do print(k) end\n");
    CHECK_EQ(countWarnings(shadowed, LintWarning::Code_Pairs), 0);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "MisleadingAndOrUsesTheMiddleOperandsType")
{
    LintResult result = lint(R"(
local function f(c: boolean, maybe: boolean?, count: number)
    local a = c and maybe or 1
    local b = c and count or 0
    return a, b
end
return f
)");

    std::vector<LintWarning> misleading;
    for (const LintWarning& warning : result.warnings)
        if (warning.code == LintWarning::Code_MisleadingAndOr)
            misleading.push_back(warning);

    // only `maybe` can be falsy; LuaAndOr reports both, at the same places
    REQUIRE_EQ(misleading.size(), 1);
    CHECK_EQ(misleading[0].location.begin.line, 2);
    CHECK_EQ(misleading[0].text, "an 'a and b or c' expression is misleading when 'b' is falsy, use 'if a then b else c' instead");
    CHECK_EQ(countWarnings(result, LintWarning::Code_LuaAndOr), 2);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "AndOrUsedAsBooleanLogicIsNotATernary")
{
    LintResult result = lint(R"(
local function f(err: string?, a: number, b: number, flag: boolean?)
    if err and err:match("ERR") or err and err:match("PANIC") then
        print(err)
    end
    local ordered = a == b and flag or a < b
    local negated = not (flag and err or nil)
    local ternary = a > 0 and flag or false
    local value = a > 0 and flag or 0
    return ordered, negated, ternary, value
end
return f
)");

    // only `value` is a ternary: the rest are conditions, or end in a comparison or a boolean, where falling through to
    // the last operand when the middle one is false is the point
    std::vector<LintWarning> found;
    for (const LintWarning& warning : result.warnings)
        if (warning.code == LintWarning::Code_LuaAndOr || warning.code == LintWarning::Code_MisleadingAndOr)
            found.push_back(warning);

    REQUIRE_EQ(found.size(), 2);
    CHECK_EQ(found[0].location.begin.line, 8);
    CHECK_EQ(found[1].location.begin.line, 8);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "MisleadingAndOrKnowsNoneIsFalsy")
{
    LintResult result = lint(R"(
local function f(c: boolean, gone: none | number)
    local a = c and none or 1
    local b = c and gone or 2
    return a, b
end
return f
)");

    std::vector<LintWarning> misleading;
    for (const LintWarning& warning : result.warnings)
        if (warning.code == LintWarning::Code_MisleadingAndOr)
            misleading.push_back(warning);

    REQUIRE_EQ(misleading.size(), 2);
    CHECK_EQ(misleading[0].text, "this 'a and b or c' always evaluates to 'c' because 'b' is none, use 'if a then b else c' instead");
    CHECK_EQ(misleading[1].text, "an 'a and b or c' expression is misleading when 'b' is falsy, use 'if a then b else c' instead");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "LuaAndOrReportsEveryAndOr")
{
    LintResult result = lint(R"(
local c = math.random() > 0.5
local a = c and 1 or 2
local b = c and false or true
local d = (c and 1) or 2
return a, b, d
)");

    // - the parenthesized form is left alone, as MisleadingAndOr does
    // - `c and false or true` ends in a boolean, so it's logic, not a ternary; MisleadingAndOr still reports the `false`
    REQUIRE_EQ(countWarnings(result, LintWarning::Code_LuaAndOr), 1);
    CHECK_EQ(countWarnings(result, LintWarning::Code_MisleadingAndOr), 1);

    for (const LintWarning& warning : result.warnings)
        if (warning.code == LintWarning::Code_LuaAndOr)
            CHECK_EQ(warning.text, "'a and b or c' gives 'c' whenever 'b' is falsy, not only when 'a' is; use 'if a then b else c'");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "SelfAssignmentReportsFieldsOnlyInStrict")
{
    const std::string source = R"(
local x = 1
local t = { x = 1 }
local a, b = 1, 2
x = x
t.x = t.x
a, b = b, a
a, b = a, 3
return x, t, a, b
)";

    LintResult nonstrict = lint("--!nonstrict" + source);
    REQUIRE_EQ(countWarnings(nonstrict, LintWarning::Code_SelfAssignment), 2);

    std::vector<LintWarning> found;
    for (const LintWarning& warning : nonstrict.warnings)
        if (warning.code == LintWarning::Code_SelfAssignment)
            found.push_back(warning);
    CHECK_EQ(found[0].text, "Assigning 'x' to itself does nothing; did you mean to assign something else?");
    CHECK_EQ(found[1].location.begin.line, 7);

    // a field can be assigned to itself to run its `__newindex`, so only strict reports it
    LintResult strict = lint("--!strict" + source);
    CHECK_EQ(countWarnings(strict, LintWarning::Code_SelfAssignment), 3);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "DeadStoreReportsComputedValuesOverwrittenUnread")
{
    LintResult result = lint(R"(
local function f(a: number, b: boolean)
    local x = a * 2
    x = a + 1

    local y = math.random()
    print(y)
    y = math.random()

    local z = 0
    z = math.random()

    local w = math.random()
    if b then print(w) end
    w = math.random()

    local u = math.random()
    local read_later = function() return u end
    u = math.random()

    local q = math.random()
    for _ = 1, 3 do
        if b then break end
    end
    q = math.random()

    local r = math.random()
    if b then return end
    r = math.random()

    return x, y, z, w, u, read_later, q, r
end
return f
)");

    std::vector<LintWarning> found;
    for (const LintWarning& warning : result.warnings)
        if (warning.code == LintWarning::Code_DeadStore)
            found.push_back(warning);

    // - `y` and `w` are read first, `z` starts as a plain default, `u` is read by a closure
    // - `q`: the `break` leaves the inner loop, not the block, so the store is still dead
    // - `r`: the `return` might be taken, so what follows it might not run
    REQUIRE_EQ(found.size(), 2);
    CHECK_EQ(found[0].text, "The value stored in 'x' here is never read: line 4 overwrites it first; did you forget to use it?");
    CHECK_EQ(found[0].location.begin.line, 2);
    CHECK_EQ(found[1].location.begin.line, 20);
}

TEST_CASE_FIXTURE(Fixture, "ForRangeReportsStepsAwayFromTheEnd")
{
    LintResult result = lint(R"(
local t = { 1, 2, 3 }
for i = 1, 10, -1 do print(i) end
for i = 1, #t, -1 do print(i) end
for i = 10, 1, 1 do print(i) end
for i = #t, 1, 1 do print(i) end
for i = 10, 1, -1 do print(i) end
for i = 1, 10, 2 do print(i) end
for i = #t, 1, -1 do print(i) end
)");

    REQUIRE_EQ(countWarnings(result, LintWarning::Code_ForRange), 4);
    CHECK_EQ(
        result.warnings[0].text,
        "For loop counts down but ends above where it starts, so it never runs; did you mean to swap the bounds?"
    );
    CHECK_EQ(
        result.warnings[2].text,
        "For loop counts up but ends below where it starts, so it never runs; did you mean to swap the bounds, or step by -1?"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "OptimizationHintLoopPartsAreSilencedOverTheBinding")
{
    ScopedFastFlag attributes{FFlag::LuwuAttributesEverywhere, true};

    LintResult result = lint(R"(
local function f(lines: { string }, queue: { number })
    @[nolint(LoopConcat)]
    local quiet = ""
    @[nolint(OptimizationHint)]
    const state = { text = "", queue = {} }
    local loud = ""
    for _, line in lines do
        quiet ..= line
        state.text ..= line
        table.remove(state.queue, 1)
        loud ..= line
    end
    return quiet, state, loud
end

return f
)");

    // only `loud` is reported, and the help names the binding to put the attribute over
    REQUIRE_EQ(result.warnings.size(), 1);
    CHECK_EQ(result.warnings[0].code, LintWarning::Code_LoopConcat);
    CHECK(result.warnings[0].text.find("'loud' in a loop is O(n²)") != std::string::npos);
    CHECK(result.warnings[0].text.find("  - Add '@[nolint(LoopConcat)]' over 'loud' to silence") != std::string::npos);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "OptimizationHintLoopPartsGiveTheCostOfTheLoopNest")
{
    LintResult result = lint(R"(
local function f(rows: { { string } }, queue: { number })
    local all = ""
    for _, row in rows do
        local line = ""
        for _, cell in row do
            all ..= cell
            line ..= cell
            table.remove(queue, 1)
        end
        all ..= "\n"
    end
    for _ = 1, 10 do
        for _ = 1, 10 do
            for _ = 1, 10 do
                for _ = 1, 10 do
                    for _ = 1, 10 do
                        all ..= "."
                    end
                end
            end
        end
    end
    return all
end

return f
)");

    REQUIRE_EQ(result.warnings.size(), 5);

    // outlives both loops: n² cells, each copying a string n² long
    CHECK_EQ(
        result.warnings[0].text,
        "Appending to 'all' in a loop is O(n⁴) because it copies the string every iteration\n\n"
        "Help (expensive loop concat):\n"
        "  - Consider building an array of strings and 'table.concat' when finished\n"
        "  - If size is known up front, use a `buffer` instead\n"
        "  - Add '@[nolint(LoopConcat)]' over 'all' to silence"
    );
    // declared in the outer loop, so it starts over every row
    CHECK(result.warnings[1].text.find("'line' in a loop is O(n³)") != std::string::npos);
    CHECK_EQ(
        result.warnings[2].text,
        "Removing the first element of 'queue' in a loop is O(n⁴) because it moves every other element every iteration\n\n"
        "Help (expensive loop remove):\n"
        "  - Consider reading from a head index instead (`local item = queue[head]; head += 1`)\n"
        "  - Add '@[nolint(InefficientTableRemove)]' over the function to silence"
    );
    CHECK(result.warnings[3].text.find("'all' in a loop is O(n²)") != std::string::npos);
    CHECK(result.warnings[4].text.find("'all' in a loop is O(n¹⁰)") != std::string::npos);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "LintAttributesScopeWarnings")
{
    ScopedFastFlag cstAttr{FFlag::LuauCstAttr, true};
    ScopedFastFlag luwuClasses{FFlag::LuwuClasses, true};
    // attributes on classes and class members
    ScopedFastFlag attributesEverywhere{FFlag::LuwuAttributesEverywhere, true};

    LintResult result = lint(R"(--!nolint LocalUnused
local function build(names: { string }): string
    local s = ""
    for _, n in names do s ..= n end
    return s
end

@[nolint(LoopConcat)]
local function quiet(names: { string }): string
    local s = ""
    for _, n in names do s ..= n end
    return s
end

@[lint(LocalUnused)]
local function loud()
    local unused = 1
    @[nolint]
    local function inner()
        local alsoUnused = 2
    end
    return inner
end

class Cat(name: string)
    @[nolint(OptimizationHint)]
    function names(self, names: { string }): string
        local s = ""
        for _, n in names do s ..= n end
        return s
    end
end

@[nolint(LocalUnused)]
class Quiet(n: number)
    @[lint(LocalUnused)]
    function loudAgain(self)
        local unused = 1
    end
end

@[nolint(LoopConcats)]
local function typo() end

return build, quiet, loud, Cat, Quiet, typo
)");

    // - `build`: the module turns off LocalUnused, LoopConcat stays on
    // - `quiet`: LoopConcat turned off
    // - `loud`: LocalUnused turned back on, then everything off again in `inner`
    // - `Cat.names`: OptimizationHint turned off, which takes its parts with it
    // - `Quiet.loudAgain`: on again inside a class that turns it off
    // - `typo`: not a lint, with a suggestion
    // - `inner`'s bare `@[nolint]` is reported by BareNolint
    REQUIRE(5 == result.warnings.size());
    CHECK_EQ(result.warnings[0].code, LintWarning::Code_LoopConcat);
    CHECK_EQ(3, result.warnings[0].location.begin.line);
    CHECK_EQ(result.warnings[1].code, LintWarning::Code_LocalUnused);
    CHECK_EQ(16, result.warnings[1].location.begin.line);
    CHECK_EQ(result.warnings[2].code, LintWarning::Code_BareNolint);
    CHECK_EQ(17, result.warnings[2].location.begin.line);
    CHECK_EQ(result.warnings[3].code, LintWarning::Code_LocalUnused);
    CHECK_EQ(37, result.warnings[3].location.begin.line);
    CHECK_EQ(result.warnings[4].code, LintWarning::Code_CommentDirective);
    CHECK_EQ(result.warnings[4].text, "nolint attribute refers to unknown lint rule 'LoopConcats'; did you mean 'LoopConcat'?");
}

TEST_CASE_FIXTURE(Fixture, "BareNolintAsksForLintNames")
{
    ScopedFastFlag cstAttr{FFlag::LuauCstAttr, true};

    LintResult bare = lint(R"(--!nolint
local unused = 1
@nolint
local function f()
    local alsoUnused = 2
end
@[nolint]
local function g() end
return f, g
)");

    // everything else is off; only the three bare `nolint`s are reported
    REQUIRE(3 == bare.warnings.size());
    CHECK_EQ(bare.warnings[0].code, LintWarning::Code_BareNolint);
    CHECK_EQ(
        bare.warnings[0].text,
        "'--!nolint' without lint names turns off every lint; did you forget to specify lints? Name them ('--!nolint LocalUnused'), or write "
        "'--!nolint All' to turn them all off on purpose"
    );
    CHECK_EQ(bare.warnings[1].code, LintWarning::Code_BareNolint);
    CHECK_EQ(
        bare.warnings[1].text,
        "'@nolint' without lint names turns off every lint in here; did you forget to specify lints? Name them ('@[nolint(LocalUnused)]'), or "
        "write '@[nolint(All)]' to turn them all off on purpose"
    );
    CHECK_EQ(bare.warnings[2].code, LintWarning::Code_BareNolint);

    // on purpose: `--!nolint All`, before or after a bare one, turns off everything, BareNolint included
    LintResult allowed = lint(R"(--!nolint
--!nolint All
@nolint
local function f()
    local unused = 1
end
return f
)");
    CHECK(0 == allowed.warnings.size());

    // `@[nolint(All)]` does the same inside a function, and `@[lint(All)]` isn't allowed
    LintResult scoped = lint(R"(
local unused = 1
@[nolint(All)]
local function f()
    local alsoUnused = 2
end
@[lint(All)]
local function g() end
return f, g
)");
    REQUIRE(2 == scoped.warnings.size());
    CHECK_EQ(scoped.warnings[0].code, LintWarning::Code_LocalUnused);
    CHECK_EQ(1, scoped.warnings[0].location.begin.line);
    CHECK_EQ(scoped.warnings[1].code, LintWarning::Code_CommentDirective);
    CHECK_EQ(scoped.warnings[1].text, "'All' can only turn lints off; name the lints to turn on, like '@[lint(LocalUnused)]'");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "RemoveWhileIterating")
{
    LintResult result = lint(R"(
local function f(t: { number })
    for i, v in ipairs(t) do
        if v > 1 then table.remove(t, i) end
    end
    for i = 1, #t do
        if t[i] == 0 then table.remove(t, i) end
    end
    for i, v in t do
        table.remove(t, i)
    end

    -- leaving right after the remove, or iterating backwards, is fine
    for i = 1, #t do
        if t[i] == 0 then
            table.remove(t, i)
            break
        end
    end
    for i = #t, 1, -1 do
        if t[i] == 0 then table.remove(t, i) end
    end
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_RemoveWhileIterating);
    REQUIRE(3 == found.size());
    CHECK_EQ(3, found[0].location.begin.line);
    CHECK_EQ(
        found[0].text,
        "Removing element 'i' from 't' while iterating it forwards skips the element after it, which moves into slot 'i'; iterate backwards "
        "('for i = #t, 1, -1 do') or build a new table"
    );
    CHECK_EQ(6, found[1].location.begin.line);
    CHECK_EQ(9, found[2].location.begin.line);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "LoopVariableWrite")
{
    LintResult result = lint(R"(
local function f(xs: { number }, m: { [string]: number })
    for i = 1, 10 do
        if i == 2 then i = 5 end
    end
    for _, v in xs do
        v += 1
    end
    for k, v in m do
        v = 0
    end

    -- reusing the variable as a scratch value is fine
    for _, line in m do
        line = line * 2
        print(line)
    end
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_LoopVariableWrite);
    REQUIRE(3 == found.size());
    CHECK_EQ(
        found[0].text,
        "Assigning to the loop variable 'i' doesn't change which iteration runs next; use a 'while' loop to control the counter"
    );
    CHECK_EQ(
        found[1].text,
        "Assigning to 'v' only changes the loop's copy, and nothing reads it afterwards; to change the table, write through the table instead"
    );
    CHECK_EQ(
        found[2].text,
        "Assigning to 'v' only changes the loop's copy, and nothing reads it afterwards; to change the table, write through the table instead "
        "('m[k] = ...')"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "IteratedTableWrite")
{
    LintResult result = lint(R"(
local function f(xs: { number }, m: { [string]: number })
    for k, v in pairs(m) do
        m[k] = nil
        m[k .. "x"] = v
    end
    for k, v in m do
        m.extra = v
    end

    -- the loop's own key, and the existing elements of an array, are fine
    for k, v in m do
        m[k] = v + 1
    end
    for i, v in xs do
        xs[i + 1] = v
    end
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_IteratedTableWrite);
    REQUIRE(2 == found.size());
    CHECK_EQ(4, found[0].location.begin.line);
    CHECK_EQ(
        found[0].text,
        "Writing a key of 'm' other than 'k' while iterating it is undefined: the loop may skip or repeat entries; collect the changes and "
        "apply them after the loop"
    );
    CHECK_EQ(7, found[1].location.begin.line);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "ForeverLoop")
{
    LintResult result = lint(R"(
local function f(xs: { number }, n: number)
    local i = 1
    while i <= #xs do
        print(xs[i])
    end
    local done = false
    repeat print("x") until done

    -- quiet: the counter changes, a constant condition, a closure can change it, a yield, a call changes the length
    local j = 1
    while j <= n do j += 1 end
    while true do print("service") end
    local running = true
    local function stop() running = false end
    while running do print("y") end
    local k = 1
    while k <= n do coroutine.yield() end
    local queue = {1}
    while #queue > 0 do table.remove(queue) end
    repeat local line = tostring(n) until line == ""
    return stop
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_ForeverLoop);
    REQUIRE(2 == found.size());
    CHECK_EQ(3, found[0].location.begin.line);
    CHECK_EQ(found[0].text, "Nothing in this loop changes 'i', so once it starts it never stops; did you forget to update it?");
    CHECK_EQ(7, found[1].location.begin.line);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "UselessLoop")
{
    LintResult result = lint(R"(
local function f(xs: { number }): number?
    for _, x in xs do
        if x > 1 then return x else return nil end
    end
    while true do
        print("a")
        break
    end
    for i = 1, #xs do
        return xs[i]
    end

    -- quiet: taking the first item, and a path that continues
    for _, x in xs do
        return x
    end
    for _, x in xs do
        if x > 1 then continue end
        return x
    end
    return nil
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_UselessLoop);
    REQUIRE(3 == found.size());
    CHECK_EQ(2, found[0].location.begin.line);
    CHECK_EQ(5, found[1].location.begin.line);
    CHECK_EQ(9, found[2].location.begin.line);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "StringIndexZero")
{
    LintResult result = lint(R"(
local function f(s: string)
    return s:byte(0), string.sub(s, 0, 3), s:sub(1, 3), string.byte(s, 1)
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_StringIndexZero);
    REQUIRE(2 == found.size());
    CHECK_EQ(found[0].text, "Strings are indexed from 1: 'byte' at 0 is before the first character and returns nothing");
    CHECK_EQ(
        found[1].text,
        "Strings are indexed from 1: 'sub' treats a start of 0 as 1, so an end index written for 0-based indexing is one character short"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "NewValueComparison")
{
    LintResult result = lint(R"(
local function f(t: { number }, g: () -> ())
    return t == {}, t ~= {}, g == function() end, t == t
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_NewValueComparison);
    REQUIRE(3 == found.size());
    CHECK_EQ(
        found[0].text,
        "This comparison is always false: a table literal makes a new table, which is never equal to another value; to check whether a "
        "table is empty, use 'next(t) == nil'"
    );
    CHECK(found[1].text.find("always true") != std::string::npos);
    CHECK_EQ(
        found[2].text, "This comparison is always false: a function literal makes a new function, which is never equal to another value"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "TableTruthiness")
{
    LintResult result = lint(R"(
type Options = { verbose: boolean }

local function f(xs: { number }, m: { [string]: number }, opts: Options, maybe: { number }?, lookup: { [string]: { number } })
    if xs then print("a") end
    if not m then print("b") end
    local results = {}
    if results then print("c") end

    -- quiet: a record (a defensive check), an optional table, a map read and a local reassigned from one
    if opts then print("d") end
    if maybe then print("e") end
    if lookup.x then print("f") end
    local rows = lookup["k"]
    if rows then print("g") end
    local later = {}
    later = lookup["k"]
    if later then print("h") end
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_TableTruthiness);
    REQUIRE(3 == found.size());
    CHECK_EQ(
        found[0].text,
        "'xs' is a table, which is truthy even when it's empty; to check whether it has entries, use 'next(xs) ~= nil' (or '#xs > 0' for an "
        "array)"
    );
    CHECK_EQ(found[1].text, "'not m' is always false: a table is truthy even when it's empty; to check whether 'm' is empty, use 'next(m) == nil'");
    CHECK_EQ(7, found[2].location.begin.line);
}

// Luwu If Local (rfcs/if-local.md): a binding passes when its value is truthy, so binding a table always passes
TEST_CASE_FIXTURE(BuiltinsFixture, "TableTruthinessIfLocal")
{
    ScopedFastFlag luwuIfLocal{FFlag::LuwuIfLocal, true};

    LintResult result = lint(R"(
local function f(xs: { number }, maybe: { number }?)
    if local t = xs then print(t) end
    if local n = 1 when xs then print(n) end
    if local m = maybe then print(m) end
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_TableTruthiness);
    REQUIRE_EQ(found.size(), 2);
    CHECK_EQ(found[0].location.begin.line, 2);
    CHECK_EQ(found[1].location.begin.line, 3);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "DiscardedResult")
{
    LintResult result = lint(R"(
@nodiscard
local function copy(t: { number }): { number }
    return table.clone(t)
end

@[nodiscard("it returns the new list")]
local function appended(t: { number }, v: number): { number }
    return table.clone(t)
end

local function f(xs: { number }, s: string)
    s:upper()
    string.gsub(s, "a", "b")
    tostring(1)
    copy(xs)
    appended(xs, 1)

    -- quiet: calls with an effect, results that are used, and results discarded on purpose
    math.random(1, 2)
    table.insert(xs, 1)
    local kept = copy(xs)
    const _ = copy(xs)
    const _ = s:upper()
    return kept
end
return f
)");

    std::vector<LintWarning> found = warningsWithCode(result, LintWarning::Code_DiscardedResult);
    REQUIRE(5 == found.size());
    CHECK_EQ(
        found[0].text,
        "'s:upper' returns a new string and doesn't change 's', so calling it without using the result does nothing; did you mean 's = "
        "s:upper(...)'? To discard the result on purpose, write 'const _ = s:upper(...)'"
    );
    CHECK_EQ(
        found[1].text,
        "'string.gsub' only returns a result, so calling it without using the result does nothing; use the result, or write 'const _ = "
        "string.gsub(...)' to discard it on purpose"
    );
    CHECK_EQ(found[3].text, "The result of 'copy' shouldn't be discarded; to discard it on purpose, write 'const _ = copy(...)'");
    CHECK_EQ(
        found[4].text,
        "The result of 'appended' shouldn't be discarded: it returns the new list; to discard it on purpose, write 'const _ = appended(...)'"
    );
}

TEST_CASE_FIXTURE(Fixture, "ConstLocalIsOffUntilAskedFor")
{
    ScopedFastFlag cstAttr{FFlag::LuauCstAttr, true};

    const std::string source = R"(
local a, b = 1, 2
local c = 3
c += 1
local function f() return a + b end
local g = function() end
local function h() end
h = g
const d = 4
return f, c, d
)";

    // off by default
    CHECK(warningsWithCode(lint(source), LintWarning::Code_ConstLocal).empty());

    // `--!lint ConstLocal`: everything never reassigned, but not `c` or `h`
    std::vector<LintWarning> found = warningsWithCode(lint("--!lint ConstLocal" + source), LintWarning::Code_ConstLocal);
    REQUIRE(3 == found.size());
    CHECK_EQ(found[0].text, "'a' and 'b' are immutable and can be marked 'const'");
    CHECK_EQ(found[1].text, "'f' should be a 'const function'");
    CHECK_EQ(found[2].text, "'g' is immutable and can be marked 'const'");

    // `--!nolint` wins over `--!lint`
    CHECK(warningsWithCode(lint("--!lint ConstLocal\n--!nolint ConstLocal" + source), LintWarning::Code_ConstLocal).empty());

    // a function can ask for it too, and only gets it inside
    LintResult scoped = lint(R"(
local x = 1
@[lint(ConstLocal)]
local function inner()
    local y = 2
    return x + y
end
return inner
)");
    std::vector<LintWarning> inScope = warningsWithCode(scoped, LintWarning::Code_ConstLocal);
    REQUIRE(2 == inScope.size());
    CHECK_EQ(3, inScope[0].location.begin.line);
    CHECK_EQ(4, inScope[1].location.begin.line);
}

TEST_CASE_FIXTURE(Fixture, "LintDirectiveNeedsAKnownLint")
{
    LintResult result = lint(R"(--!lint
--!lint All
--!lint ConstLocl
return 1
)");

    REQUIRE(3 == result.warnings.size());
    CHECK_EQ(result.warnings[0].text, "lint directive needs the lint to turn on, like '--!lint ConstLocal'");
    CHECK_EQ(result.warnings[1].text, "'All' can only turn lints off; name the lint to turn on, like '--!lint ConstLocal'");
    CHECK_EQ(result.warnings[2].text, "lint directive refers to unknown lint rule 'ConstLocl'; did you mean 'ConstLocal'?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "ReturnSelfFlagsSelfReturnedAsTheTrait")
{
    ScopedFastFlag _[2]{{FFlag::LuwuClasses, true}, {FFlag::LuwuTraits, true}};

    LintResult result = lint(R"(
trait Aliased
    public function annotated(self): Aliased
        return self
    end
    public function unannotated(self)
        return (self)
    end
    public function copy(self): Aliased
        return class.of(self)()
    end
    public function good(self): Self
        return self
    end
    public function nested(self): Aliased
        local f = function()
            return self
        end
        return f()
    end
end
)");

    std::vector<LintWarning> found;
    for (const LintWarning& warning : result.warnings)
        if (warning.code == LintWarning::Code_ReturnSelf)
            found.push_back(warning);

    // on the return type, which is what's wrong, not on `return self`; a method without one already returns `Self`
    REQUIRE_EQ(found.size(), 2);
    CHECK_EQ(found[0].location, Location{{2, 37}, {2, 44}});
    CHECK_EQ(found[1].location, Location{{8, 32}, {8, 39}});
    CHECK_EQ(
        found[0].text,
        "Did you mean to return 'Self' here?\n\n"
        "Help (method returns trait instead of Self):\n"
        "  - Returning 'Aliased' here loses self's class and any other traits on it\n"
        "  - Callers rely on knowing self's class to pass into other functions\n"
        "  - Return 'Self' here so the type checker knows to use the class type instead of the trait type"
    );
}
TEST_SUITE_END();
