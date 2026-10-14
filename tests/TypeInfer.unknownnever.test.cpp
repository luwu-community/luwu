// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Fixture.h"

#include "doctest.h"

using namespace Luau;

LUAU_FASTFLAG(DebugLuauForceOldSolver);

TEST_SUITE_BEGIN("TypeInferUnknownNever");

TEST_CASE_FIXTURE(Fixture, "string_subtype_and_unknown_supertype")
{
    CheckResult result = check(R"(
        local function f(x: string)
            local foo: unknown = x
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "unknown_subtype_and_string_supertype")
{
    CheckResult result = check(R"(
        local function f(x: unknown)
            local foo: string = x
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(Fixture, "unknown_is_reflexive")
{
    CheckResult result = check(R"(
        local function f(x: unknown)
            local foo: unknown = x
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "string_subtype_and_never_supertype")
{
    CheckResult result = check(R"(
        local function f(x: string)
            local foo: never = x
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(Fixture, "never_subtype_and_string_supertype")
{
    CheckResult result = check(R"(
        local function f(x: never)
            local foo: string = x
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "never_is_reflexive")
{
    CheckResult result = check(R"(
        local function f(x: never)
            local foo: never = x
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "unknown_is_optional_because_it_too_encompasses_nil")
{
    CheckResult result = check(R"(
        local t: {x: unknown} = {}
    )");
}

TEST_CASE_FIXTURE(Fixture, "table_with_prop_of_type_never_is_uninhabitable")
{
    CheckResult result = check(R"(
        local t: {x: never} = {}
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
}

TEST_CASE_FIXTURE(Fixture, "table_with_prop_of_type_never_is_also_reflexive")
{
    CheckResult result = check(R"(
        local t: {x: never} = {x = 5 :: never}
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "array_like_table_of_never_is_inhabitable")
{
    CheckResult result = check(R"(
        local t: {never} = {}
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "type_packs_containing_never_is_itself_uninhabitable")
{
    CheckResult result = check(R"(
        local function f() return "foo", 5 :: never end

        local x, y, z = f()
    )");

    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK("Function only returns 2 values, but 3 are required here" == toString(result.errors[0]));

        CHECK("string" == toString(requireType("x")));
        CHECK("never" == toString(requireType("y")));
        CHECK("nil" == toString(requireType("z")));
    }
    else
    {
        LUAU_REQUIRE_NO_ERRORS(result);

        CHECK_EQ("never", toString(requireType("x")));
        CHECK_EQ("never", toString(requireType("y")));
        CHECK_EQ("never", toString(requireType("z")));
    }
}

TEST_CASE_FIXTURE(Fixture, "type_packs_containing_never_is_itself_uninhabitable2")
{
    CheckResult result = check(R"(
        local function f(): (string, never) return "", 5 :: never end
        local function g(): (never, string) return 5 :: never, "" end

        local x1, x2 = f()
        local y1, y2 = g()
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    if (!FFlag::DebugLuauForceOldSolver)
    {
        CHECK_EQ("string", toString(requireType("x1")));
        CHECK_EQ("never", toString(requireType("x2")));
        CHECK_EQ("never", toString(requireType("y1")));
        CHECK_EQ("string", toString(requireType("y2")));
    }
    else
    {
        CHECK_EQ("never", toString(requireType("x1")));
        CHECK_EQ("never", toString(requireType("x2")));
        CHECK_EQ("never", toString(requireType("y1")));
        CHECK_EQ("never", toString(requireType("y2")));
    }
}

TEST_CASE_FIXTURE(Fixture, "index_on_never")
{
    CheckResult result = check(R"(
        local x: never = 5 :: never
        local z = x.y
    )");

    // Luwu: using `never` is unreachable code (UnreachableCodeUse); upstream reports nothing
    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        const UnreachableCodeUse* use = get<UnreachableCodeUse>(result.errors[0]);
        REQUIRE(use);
        CHECK_EQ(UnreachableCodeUse::Reason::DeclaredNever, use->reason);
    }
    else
    {
        LUAU_REQUIRE_NO_ERRORS(result);
    }

    CHECK_EQ("never", toString(requireType("z")));
}

TEST_CASE_FIXTURE(Fixture, "call_never")
{
    CheckResult result = check(R"(
        local f: never = 5 :: never
        local x, y, z = f()
    )");

    // Luwu: calling `never` is unreachable code (UnreachableCodeUse); upstream reports nothing
    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK(get<UnreachableCodeUse>(result.errors[0]));
    }
    else
    {
        LUAU_REQUIRE_NO_ERRORS(result);
    }

    CHECK_EQ("never", toString(requireType("x")));
    CHECK_EQ("never", toString(requireType("y")));
    CHECK_EQ("never", toString(requireType("z")));
}

TEST_CASE_FIXTURE(Fixture, "assign_to_local_which_is_never")
{
    // CLI-117119 - What do we do about assigning to never?
    CheckResult result = check(R"(
        local t: never
        t = 3
    )");

    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
    }
    else
    {
        LUAU_REQUIRE_NO_ERRORS(result);
    }
}

TEST_CASE_FIXTURE(Fixture, "assign_to_global_which_is_never")
{
    CheckResult result = check(R"(
        --!nonstrict
        t = 5 :: never
        t = ""
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "assign_to_prop_which_is_never")
{
    CheckResult result = check(R"(
        local function f(t: never)
            t.x = 5
        end
    )");

    // Luwu: writing into `never` is unreachable code (UnreachableCodeUse); upstream reports nothing
    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK(get<UnreachableCodeUse>(result.errors[0]));
    }
    else
    {
        LUAU_REQUIRE_NO_ERRORS(result);
    }
}

TEST_CASE_FIXTURE(Fixture, "assign_to_subscript_which_is_never")
{
    CheckResult result = check(R"(
        local function f(t: never)
            t[5] = 7
        end
    )");

    // Luwu: writing into `never` is unreachable code (UnreachableCodeUse); upstream reports nothing
    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK(get<UnreachableCodeUse>(result.errors[0]));
    }
    else
    {
        LUAU_REQUIRE_NO_ERRORS(result);
    }
}

TEST_CASE_FIXTURE(Fixture, "for_loop_over_never")
{
    CheckResult result = check(R"(
        for i, v in (5 :: never) do
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "pick_never_from_variadic_type_pack")
{
    CheckResult result = check(R"(
        local function f(...: never)
            local x, y = (...)
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "index_on_union_of_tables_for_properties_that_is_never")
{
    // CLI-117116 - We are erroneously warning when passing a valid table literal where we expect a union of tables.
    if (!FFlag::DebugLuauForceOldSolver)
        return;
    CheckResult result = check(R"(
        type Disjoint = {foo: never, bar: unknown, tag: "ok"} | {foo: never, baz: unknown, tag: "err"}

        function f(disjoint: Disjoint)
            return disjoint.foo
        end

        local foo = f({foo = 5 :: never, bar = true, tag = "ok"})
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ("never", toString(requireType("foo")));
}

TEST_CASE_FIXTURE(Fixture, "index_on_union_of_tables_for_properties_that_is_sorta_never")
{
    // CLI-117116 - We are erroneously warning when passing a valid table literal where we expect a union of tables.
    if (!FFlag::DebugLuauForceOldSolver)
        return;
    CheckResult result = check(R"(
        type Disjoint = {foo: string, bar: unknown, tag: "ok"} | {foo: never, baz: unknown, tag: "err"}

        function f(disjoint: Disjoint)
            return disjoint.foo
        end

        local foo = f({foo = 5 :: never, bar = true, tag = "ok"})
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ("string", toString(requireType("foo")));
}

TEST_CASE_FIXTURE(Fixture, "unary_minus_of_never")
{
    CheckResult result = check(R"(
        local x = -(5 :: never)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ("never", toString(requireType("x")));
}

TEST_CASE_FIXTURE(Fixture, "length_of_never")
{
    CheckResult result = check(R"(
        local x = #({} :: never)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);

    CHECK_EQ("number", toString(requireType("x")));
}

TEST_CASE_FIXTURE(Fixture, "dont_unify_operands_if_one_of_the_operand_is_never_in_any_ordering_operators")
{
    CheckResult result = check(R"(
        local function ord(x: nil, y)
            return x ~= nil and x > y
        end
    )");


    LUAU_REQUIRE_NO_ERRORS(result);
    if (!FFlag::DebugLuauForceOldSolver)
        CHECK_EQ("(nil, nil & ~nil) -> boolean", toString(requireType("ord")));
    else
        CHECK_EQ("<a>(nil, a) -> boolean", toString(requireType("ord")));
}

TEST_CASE_FIXTURE(Fixture, "math_operators_and_never")
{
    CheckResult result = check(R"(
        local function mul(x: nil, y)
            return x ~= nil and x * y -- infers boolean | never, which is normalized into boolean
        end
    )");

    if (!FFlag::DebugLuauForceOldSolver)
    {
        LUAU_REQUIRE_ERROR_COUNT(1, result);
        CHECK(get<ExplicitFunctionAnnotationRecommended>(result.errors[0]));

        // CLI-114134 Egraph-based simplification.
        // CLI-116549 x ~= nil : false when x : nil
        CHECK("<a>(nil, a) -> false | mul<nil & ~nil, a>" == toString(requireType("mul")));
    }
    else
    {
        LUAU_REQUIRE_NO_ERRORS(result);
        CHECK_EQ("<a>(nil, a) -> boolean", toString(requireType("mul")));
    }
}

TEST_CASE_FIXTURE(Fixture, "compare_never")
{
    CheckResult result = check(R"(
        local function cmp(x: nil, y: number)
            return x ~= nil and x > y and x < y -- infers boolean | never, which is normalized into boolean
        end
    )");

    LUAU_CHECK_NO_ERRORS(result);
    CHECK_EQ("(nil, number) -> boolean", toString(requireType("cmp")));
}

TEST_CASE_FIXTURE(Fixture, "lti_error_at_declaration_for_never_normalizations")
{
    ScopedFastFlag sff_LuauSolverV2{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        local function num(x: number) end
        local function str(x: string) end
        local function cond(): boolean return false end

        local function f(a)
            if cond() then
                num(a)
            else
                str(a)
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK(toString(result.errors[0]) == "Parameter 'a' has been reduced to never. This function is not callable with any possible value.");
    CHECK(toString(result.errors[1]) == "Parameter 'a' is required to be a subtype of 'number' here.");
    CHECK(toString(result.errors[2]) == "Parameter 'a' is required to be a subtype of 'string' here.");
}

TEST_CASE_FIXTURE(Fixture, "lti_permit_explicit_never_annotation")
{
    ScopedFastFlag sff_LuauSolverV2{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        local function num(x: number) end
        local function str(x: string) end
        local function cond(): boolean return false end

        local function f(a: never)
            if cond() then
                num(a)
            else
                str(a)
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(Fixture, "cast_from_never_does_not_error")
{
    CheckResult result = check(R"(
        local function f(x: never): number
            return x :: number
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

// Luwu: calling or indexing `never` is unreachable code, reported once per local with why it can't run
TEST_CASE_FIXTURE(BuiltinsFixture, "using_never_is_unreachable_code")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    CheckResult result = check(R"(
        local function f(x: string | number)
            if typeof(x) == "string" then
                return
            elseif typeof(x) == "number" then
                return
            end
            x.foo()
            local y = x.bar
            x()
        end

        local z = error("boom")
        z.thing()
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(Location{{7, 12}, {7, 13}}, result.errors[0].location);
    CHECK_EQ(
        "Attempt to use unreachable code: 'x' is 'never' here\n\n"
        "Help (unreachable code):\n"
        "  • The checks before this line rule out every type 'x' could have, so this line never runs",
        toString(result.errors[0])
    );
    CHECK_EQ(
        "Attempt to use unreachable code: 'z' is 'never' here\n\n"
        "Help (unreachable code):\n"
        "  • 'z' holds the result of a call that never returns, so this line never runs",
        toString(result.errors[1])
    );
}

// Luwu: a `never` the normalizer has to work out is unreachable code too; upstream reported it can't be called
TEST_CASE_FIXTURE(BuiltinsFixture, "calling_an_unsimplified_never_is_unreachable_code")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    CheckResult result = check(R"(
        local function f(x: string | number)
            if typeof(x) == "string" then
                return
            elseif typeof(x) == "number" then
                return
            end
            x()
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const UnreachableCodeUse* use = get<UnreachableCodeUse>(result.errors[0]);
    REQUIRE(use);
    CHECK_EQ(ImproperUse::Call, use->use);
    CHECK_EQ("x", use->name);
}

// Luwu: code that can't run once the declared types are taken at their word is unreachable code, tables included: a
// property typed `nil` checked for a value, tags of a union all ruled out, or two checks that contradict each other
TEST_CASE_FIXTURE(BuiltinsFixture, "never_proven_from_the_declared_types_is_unreachable_code")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    CheckResult result = check(R"(
        type Box = { field: nil, n: number }
        local function fromNil(b: Box): number
            if b.field then
                return b.n
            end
            return 0
        end

        type Shape = { kind: "circle", r: number } | { kind: "square", side: number }
        local function fromTags(s: Shape): number
            if s.kind == "circle" then
                return s.r
            elseif s.kind == "square" then
                return s.side
            end
            return s.r
        end

        type Holder = { enc: { go: (any) -> () }? }
        local function fromContradiction(h: Holder)
            if not h.enc then
                if h.enc then
                    h.enc:go()
                end
            end
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    for (const TypeError& error : result.errors)
    {
        const UnreachableCodeUse* use = get<UnreachableCodeUse>(error);
        REQUIRE(use);
        CHECK_EQ(UnreachableCodeUse::Reason::NarrowedAway, use->reason);
    }
}

// Luwu: a `never` the declared types can't explain may be a type checker bug, which leaves the value unchecked. Here
// the solver keeps `not h.enc` after `h.enc` is assigned, so `h` is `never` in a branch that runs.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_never_that_may_be_a_type_bug_says_so")
{
    DOES_NOT_PASS_OLD_SOLVER_GUARD();

    CheckResult result = check(R"(
        type Enc = { go: (Enc) -> () }
        type Holder = { enc: Enc? }
        local function f(h: Holder, make: () -> Enc)
            if not h.enc then
                h.enc = make()
                if h.enc then
                    h.enc:go()
                end
            end
        end
    )");

    std::vector<std::string> names;
    for (const TypeError& error : result.errors)
    {
        if (const UnreachableCodeUse* use = get<UnreachableCodeUse>(error))
        {
            CHECK_EQ(UnreachableCodeUse::Reason::PossibleTypeBug, use->reason);
            names.push_back(use->name);
        }
    }
    CHECK_EQ(std::vector<std::string>{"h"}, names);

    UnreachableCodeUse bug{ImproperUse::Index, "self.dave", UnreachableCodeUse::Reason::PossibleTypeBug, true};
    CHECK_EQ(
        "'self.dave' is 'never' here but we're not sure it's truly unreachable. If this looks like a bug, report it to "
        "https://github.com/luwu-community/luwu/issues\n\n"
        "Help (possible type bug):\n"
        "  • Until it's fixed, nothing 'self.dave' is used for here is checked",
        toString(TypeError{Location{}, bug})
    );
}

TEST_SUITE_END();
