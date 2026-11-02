// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Fixture.h"

#include "doctest.h"

#include "ScopedFlags.h"

LUAU_FASTFLAG(DebugLuwuPoperators)
LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(DebugLuauForceOldSolver)

using namespace Luau;

// Luwu Push and Pop Operators (rfcs/push-pop-operators.md)
TEST_SUITE_BEGIN("TypeInferPushPop");

// Luwu: a metamethod in a metatable's `__index` table never runs; operators read the metatable itself
TEST_CASE_FIXTURE(BuiltinsFixture, "metamethods_on_index_are_reported")
{
    ScopedFastFlag poperators{FFlag::DebugLuwuPoperators, true};

    CheckResult result = check(R"(
        --!strict
        local methods = {
            __push = function(self, ...) end,
            __tostring = function(self) return "x" end,
            increment = function(self) end,
        }
        local lines = setmetatable({}, { __index = methods })

        -- a class table that is its own metatable: its metamethods are for its instances
        local Base = {}
        Base.__index = Base
        function Base.__tostring(self) return "base" end
        local Derived = setmetatable({}, { __index = Base })

        -- metamethods on the metatable itself are fine
        local ok = setmetatable({}, { __index = methods, __push = methods.__push, __tostring = methods.__tostring })
        return lines, Derived, ok
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(toString(result.errors[0]), "Metamethod '__tostring' should go directly on the metatable, not on __index");
    CHECK_EQ(toString(result.errors[1]), "Metamethod '__push' should go directly on the metatable, not on __index");
    CHECK_EQ(result.errors[0].location.begin.line, 7);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "push_grows_an_empty_table_and_pop_gives_its_element_or_nil")
{
    ScopedFastFlag poperators{FFlag::DebugLuwuPoperators, true};

    CheckResult result = check(R"(
        --!strict
        const cats = {}
        cats <+ "meow"
        cats <+ "mrrp", nil
        const cat = <-cats

        local function grows(t)
            t <+ 1
        end

        const nums: { number } = { 1, 2 }
        const num = <-nums

        const empty = {}
        const nothing = <-empty
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CHECK_EQ(toString(requireType("cats"), {true}), "{string}");
    CHECK_EQ(toString(requireType("cat")), "string?");
    CHECK_EQ(toString(requireType("grows")), "({number}) -> ()");
    CHECK_EQ(toString(requireType("num")), "number?");
    CHECK_EQ(toString(requireType("nothing")), "nil");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "pushed_values_must_fit_the_element_type_but_nil_always_does")
{
    ScopedFastFlag poperators{FFlag::DebugLuwuPoperators, true};

    CheckResult result = check(R"(
        --!strict
        const nums: { number } = {}
        local maybe: number? = nil
        local function pair(): (number, number)
            return 1, 2
        end
        nums <+ 1, nil, maybe
        nums <+ pair()
        nums <+ "nope"
    )");

    if (FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_NO_ERRORS(result);
        return;
    }

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const TypeMismatch* mismatch = get<TypeMismatch>(result.errors[0]);
    REQUIRE(mismatch);
    CHECK_EQ(toString(mismatch->wantedType), "number");
    CHECK_EQ(toString(mismatch->givenType), "string");
    CHECK_EQ(result.errors[0].location, Location{{9, 16}, {9, 22}});
}

TEST_CASE_FIXTURE(BuiltinsFixture, "only_arrays_and_values_with_the_metamethod_can_be_pushed_to_or_popped_from")
{
    ScopedFastFlag poperators{FFlag::DebugLuwuPoperators, true};

    CheckResult result = check(R"(
        --!strict
        const map: { [string]: number } = {}
        map <+ 1
        local x = 5
        x <+ 1
        local proxy = setmetatable({}, { __newindex = function(t, k, v) end })
        proxy <+ 1
        print(<-map, <-x)
    )");

    if (FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_NO_ERRORS(result);
        return;
    }

    LUAU_REQUIRE_ERROR_COUNT(5, result);
    CHECK_EQ(
        toString(result.errors[0]), "'{ [string]: number }' isn't an array; '<+' only works on arrays and on values with a '__push' metamethod"
    );
    CHECK_EQ(toString(result.errors[1]), "Expected '<+' to push to a container, got 'number' instead");
    CHECK_EQ(toString(result.errors[2]), "'<+' skips the '__newindex' metamethod of 'proxy'; give its metatable a '__push' to handle it");
    CHECK_EQ(toString(result.errors[3]), "'{ [string]: number }' isn't an array; '<-' only works on arrays and on values with a '__pop' metamethod");
    CHECK_EQ(toString(result.errors[4]), "Expected '<-' to pop from a container, got 'number' instead");
}

// A table literal pushed into a container with a declared element type is checked like an annotated local's value, so
// it can leave out optional fields, as `table.insert` allows
TEST_CASE_FIXTURE(BuiltinsFixture, "pushed_table_literals_are_checked_against_the_element_type")
{
    ScopedFastFlag poperators{FFlag::DebugLuwuPoperators, true};

    CheckResult result = check(R"(
        --!strict
        type Comment = { prefix: string, message: string, location: ("above" | "side")? }
        const comments: { Comment } = {}
        comments <+ { message = "meow", prefix = "---" }
        comments <+ { message = "meow", prefix = "---", location = "side" }
        table.insert(comments, { message = "meow", prefix = "---" })
        comments <+ { message = 1, prefix = "---" }
    )");

    if (FFlag::DebugLuauForceOldSolver)
        return;

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 7);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "push_and_pop_metamethods_are_checked_and_typed")
{
    ScopedFastFlag poperators{FFlag::DebugLuwuPoperators, true};

    CheckResult result = check(R"(
        --!strict
        type Stack = typeof(setmetatable(
            {} :: { items: { number } },
            {} :: { __push: (any, ...number) -> (), __pop: (any) -> number? }
        ))
        local s: Stack = nil :: any
        s <+ 1, 2
        s <+ "bad"
        const top = <-s
    )");

    if (FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_NO_ERRORS(result);
        return;
    }

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(result.errors[0].location.begin.line, 8);
    CHECK_EQ(toString(requireType("top")), "number?");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "class_push_and_pop_metamethods")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuwuPoperators, true},
        {FFlag::LuwuClasses, true},
    };

    // the old solver doesn't check classes
    if (FFlag::DebugLuauForceOldSolver)
        return;

    CheckResult result = check(R"(
        --!strict
        class Stack()
            items: { number } = {}

            function __push(self, ...: number)
                self.items <+ ...
            end

            function __pop(self): number?
                return <-self.items
            end
        end

        class Plain()
            x: number = 1
        end

        const s = Stack()
        s <+ 1, 2
        const top = <-s
        const p = Plain()
        p <+ 1
        print(<-p)
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(toString(result.errors[0]), "'Plain' has no '__push' metamethod for '<+' to call");
    CHECK_EQ(toString(result.errors[1]), "'Plain' has no '__pop' metamethod for '<-' to call");
    CHECK_EQ(toString(requireType("top")), "number?");
}

TEST_SUITE_END();
