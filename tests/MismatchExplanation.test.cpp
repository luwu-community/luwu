// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details

#include "Luau/MismatchExplanation.h"
#include "Luau/MismatchWording.h"

#include "Luau/Normalize.h"
#include "Luau/Subtyping.h"
#include "Luau/Type.h"
#include "Luau/TypeFunction.h"

#include "doctest.h"
#include "Fixture.h"

#include <algorithm>

LUAU_FASTFLAG(DebugLuauForceOldSolver)
LUAU_FASTFLAG(LuwuHelpfulSubtypingErrors)

using namespace Luau;

namespace
{

// Luwu (helpful subtyping errors): calls `explainMismatch` on types built by hand, for shapes the
// solver can't be relied on to produce: huge unions, cycles, degenerate unions and metatables.
struct ExplainFixture : Fixture
{
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    TypeArena arena;
    InternalErrorReporter iceReporter;
    UnifierSharedState sharedState{&ice};
    Normalizer normalizer{&arena, getBuiltins(), NotNull{&sharedState}, SolverMode::New};
    TypeCheckLimits limits;
    TypeFunctionRuntime typeFunctionRuntime{NotNull{&iceReporter}, NotNull{&limits}};
    ScopePtr rootScope{new Scope(getBuiltins()->emptyTypePack)};
    Subtyping subtyping{getBuiltins(), NotNull{&arena}, NotNull{&normalizer}, NotNull{&typeFunctionRuntime}, NotNull{&iceReporter}};

    TypeId tbl(TableType::Props props)
    {
        return arena.addType(TableType{std::move(props), std::nullopt, {}, TableState::Sealed});
    }

    TypeId unionOf(std::vector<TypeId> options)
    {
        return arena.addType(UnionType{std::move(options)});
    }

    std::optional<MismatchExplanation> explain(TypeId given, TypeId expected, std::string_view arrival = "given")
    {
        return explainMismatch(given, expected, NotNull{&subtyping}, NotNull{rootScope.get()}, getBuiltins(), std::nullopt, arrival);
    }

    // `{ a: <leaf>, b: { c: <leaf>, d: { e: <leaf> } } }`, nested `depth` more times under `x`.
    TypeId nested(TypeId leaf, int depth)
    {
        TypeId ty = tbl({{"a", leaf}, {"b", tbl({{"c", leaf}, {"d", tbl({{"e", leaf}})}})}});
        for (int i = 0; i < depth; ++i)
            ty = tbl({{"x", ty}, {"y", getBuiltins()->numberType}});
        return ty;
    }
};

} // namespace

TEST_SUITE_BEGIN("MismatchExplanation");

// An instantiated recursive generic has no name `display` can stop at, so printing its element type
// has to notice the cycle itself.
TEST_CASE_FIXTURE(BuiltinsFixture, "recursive_generic_array_does_not_overflow")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Rec<T> = { Rec<T> }
        local a: Rec<number> = { { 5 } }
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<TypeMismatch>(result.errors[0]));
    CHECK(toString(result.errors[0]).find("'number' is unrelated to 'Rec<number>'.") != std::string::npos);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "recursive_generic_array_in_a_property_does_not_overflow")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Rec<T> = { Rec<T> }
        local b: { value: Rec<number> } = { value = "x" }
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<TypeMismatch>(result.errors[0]));
    CHECK(toString(result.errors[0]).find("'string' is unrelated to 'Rec<number>'.") != std::string::npos);
}

// A table indexed by itself: its key type is the table, so printing the key walks back into it.
TEST_CASE_FIXTURE(Fixture, "self_keyed_table_does_not_overflow")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};
    ScopedFastFlag newSolver{FFlag::DebugLuauForceOldSolver, false};

    CheckResult result = check(R"(
        local _ = {}
        _[function<t0...>(...)
            _[function(...)
                _[_] %= _
                _ = {}
                _ = (- _)()
            end] %= _
            _[_] %= _
        end] %= true
    )");

    LUAU_REQUIRE_ERRORS(result);
    LUAU_REQUIRE_NO_ERROR(result, ConstraintSolvingIncompleteError);
}

TEST_CASE_FIXTURE(ExplainFixture, "unnamed_cyclic_map_is_printed_by_tostring")
{
    TypeId cyclic = arena.addType(BlockedType{});
    asMutable(cyclic)->ty.emplace<TableType>(TableType{{}, TableIndexer{cyclic, cyclic}, {}, TableState::Sealed});
    TypeId expected = tbl({{"value", getBuiltins()->numberType}});
    TypeId given = tbl({{"value", cyclic}});

    std::optional<MismatchExplanation> explanation = explain(given, expected);
    REQUIRE(explanation);
    CHECK(explanation->reason.find("t1 where t1 = { [t1]: t1 }") != std::string::npos);
}

// Collecting the strings a misspelling could be meant as opens each union once, so a union holding
// itself ends.
TEST_CASE_FIXTURE(ExplainFixture, "union_holding_itself_does_not_hang")
{
    TypeId blue = arena.addType(SingletonType{StringSingleton{"Blue"}});
    TypeId red = arena.addType(SingletonType{StringSingleton{"Red"}});
    TypeId colors = arena.addType(BlockedType{});
    asMutable(colors)->ty.emplace<UnionType>(UnionType{{colors, blue, red}});
    TypeId bleu = arena.addType(SingletonType{StringSingleton{"Bleu"}});

    std::optional<MismatchExplanation> explanation = explain(bleu, colors);
    REQUIRE(explanation);
    CHECK_EQ(explanation->reason, "\n\nHelp (typo):\n  • Did you mean '\"Blue\"'?");
}

// A union of only `nil`s has no members to list fields from.
TEST_CASE_FIXTURE(ExplainFixture, "empty_table_against_union_of_nils")
{
    TypeId allNil = unionOf({getBuiltins()->nilType, getBuiltins()->nilType});
    TypeId empty = tbl({});

    std::optional<MismatchExplanation> explanation = explain(empty, allNil);
    REQUIRE(explanation);
    CHECK_EQ(explanation->reason, "\n\n'{  }' is unrelated to 'nil'.");
}

// A metatable type whose table part isn't a table is still shaped like a table, but has no
// properties to diff.
TEST_CASE_FIXTURE(ExplainFixture, "metatable_without_a_table_part")
{
    TypeId meta = tbl({{"__index", getBuiltins()->numberType}});
    TypeId expected = arena.addType(MetatableType{getBuiltins()->numberType, meta});
    TypeId given = tbl({{"x", getBuiltins()->numberType}});

    std::optional<MismatchExplanation> explanation = explain(given, expected);
    CHECK(!explanation);
}

// Two tables only disagree about properties.
TEST_CASE_FIXTURE(ExplainFixture, "tables_say_which_properties_are_incompatible")
{
    TypeId expected = tbl({{"a", getBuiltins()->numberType}, {"b", getBuiltins()->numberType}});
    TypeId given = tbl({{"a", getBuiltins()->stringType}, {"b", getBuiltins()->numberType}});

    std::optional<MismatchExplanation> explanation = explain(given, expected);
    REQUIRE(explanation);
    CHECK_EQ(explanation->reason, "\n\nSome properties are incompatible:\n  • given.a: expected 'number', got 'string'");
    CHECK_EQ(explanation->notation, MismatchNotationGotRoot);
}

// Any other header says how the value got here, from a string the caller builds.
TEST_CASE_FIXTURE(ExplainFixture, "arrival_names_how_the_value_arrived")
{
    TypeId expected = tbl({{"a", getBuiltins()->numberType}, {"b", getBuiltins()->numberType}});
    TypeId given = arena.addType(IntersectionType{{tbl({{"a", getBuiltins()->stringType}}), tbl({{"b", getBuiltins()->numberType}})}});

    std::string arrival = std::string("ret") + "urned";
    std::optional<MismatchExplanation> explanation = explain(given, expected, arrival);
    REQUIRE(explanation);
    CHECK_EQ(explanation->reason, "\n\nThe returned type is similar, but has an incompatible path:\n  • given.a: expected 'number', got 'string'");
}

// A string isn't compared with a union's members one by one: it has no places to point at, and
// naming the closest member only restates the two printed types.
TEST_CASE_FIXTURE(ExplainFixture, "scalar_against_union_with_a_table_member_says_nothing_more")
{
    TypeId expected = unionOf({tbl({{"a", getBuiltins()->numberType}}), getBuiltins()->numberType});

    std::optional<MismatchExplanation> explanation = explain(getBuiltins()->stringType, expected);
    REQUIRE(explanation);
    CHECK_EQ(explanation->reason, "");
    CHECK_EQ(explanation->notation, 0);
}

// One budget covers the whole message: a given union of many failing tables explains a few members
// and counts the rest, instead of diffing every one of them.
TEST_CASE_FIXTURE(ExplainFixture, "huge_given_union_shares_one_budget" * doctest::timeout(5.0))
{
    TableType::Props expectedProps;
    for (int f = 0; f < 20; ++f)
        expectedProps["f" + std::to_string(f)] = nested(getBuiltins()->numberType, 0);
    TypeId expected = tbl(std::move(expectedProps));

    // Half of each member's properties agree, so no member is unrelated to the expected table.
    std::vector<TypeId> members;
    for (int m = 0; m < 300; ++m)
    {
        TableType::Props props;
        for (int f = 0; f < 20; ++f)
            props["f" + std::to_string(f)] = nested(f < 10 ? getBuiltins()->numberType : getBuiltins()->stringType, 0);
        props["tag" + std::to_string(m)] = getBuiltins()->numberType;
        members.push_back(tbl(std::move(props)));
    }
    TypeId given = unionOf(std::move(members));

    std::optional<MismatchExplanation> explanation = explain(given, expected);

    REQUIRE(explanation);
    INFO(explanation->reason);
    // Members the budget didn't reach may fit, so it doesn't claim that none do.
    CHECK(explanation->reason.find("Not every member of the union is compatible with the expected type:") != std::string::npos);
    CHECK(explanation->reason.find(" more") != std::string::npos);
    CHECK(explanation->reason.size() < 20000);
}

// Picking the closest of many expected members gives each candidate a bounded share of one budget.
TEST_CASE_FIXTURE(ExplainFixture, "huge_expected_union_shares_one_budget" * doctest::timeout(5.0))
{
    std::vector<TypeId> members;
    for (int m = 0; m < 300; ++m)
    {
        TableType::Props props;
        for (int f = 0; f < 20; ++f)
            props["f" + std::to_string(f)] = nested(getBuiltins()->numberType, 0);
        props["k" + std::to_string(m)] = getBuiltins()->numberType;
        members.push_back(tbl(std::move(props)));
    }
    TypeId expected = unionOf(std::move(members));

    TableType::Props givenProps;
    for (int f = 0; f < 20; ++f)
        givenProps["f" + std::to_string(f)] = nested(getBuiltins()->stringType, 0);
    TypeId given = tbl(std::move(givenProps));

    std::optional<MismatchExplanation> explanation = explain(given, expected);

    REQUIRE(explanation);
    INFO(explanation->reason);
    CHECK(explanation->reason.find("The closest match is expected#1") != std::string::npos);
    CHECK(explanation->reason.find("...and ") != std::string::npos);
}

// Deep nesting stops at the diff's depth limit, and many failing properties are listed up to the
// bullet limit without comparing each against every other.
TEST_CASE_FIXTURE(ExplainFixture, "deep_and_wide_tables_stay_bounded" * doctest::timeout(5.0))
{
    TypeId deepExpected = nested(getBuiltins()->numberType, 50);
    TypeId deepGiven = nested(getBuiltins()->stringType, 50);

    TableType::Props wideExpected;
    TableType::Props wideGiven;
    for (int f = 0; f < 1000; ++f)
    {
        wideExpected["p" + std::to_string(f)] = getBuiltins()->numberType;
        wideGiven["p" + std::to_string(f)] = getBuiltins()->stringType;
    }

    std::optional<MismatchExplanation> deep = explain(deepGiven, deepExpected);
    std::optional<MismatchExplanation> wide = explain(tbl(std::move(wideGiven)), tbl(std::move(wideExpected)));

    REQUIRE(deep);
    INFO(deep->reason);
    CHECK(deep->reason.find("given.x.x.x.x.x.x.x.x.x: expected") != std::string::npos);
    REQUIRE(wide);
    INFO(wide->reason);
    CHECK(wide->reason.find("...and ") != std::string::npos);
}

// One candidate's allowance holds across every property of a wide table, so choosing among a few
// wide members leaves the budget to explain the one chosen.
TEST_CASE_FIXTURE(ExplainFixture, "wide_candidates_leave_budget_for_the_closest" * doctest::timeout(5.0))
{
    std::vector<TypeId> members;
    for (int m = 0; m < 4; ++m)
    {
        TableType::Props props;
        for (int f = 0; f < 500; ++f)
            props["p" + std::to_string(f)] = getBuiltins()->numberType;
        props["k" + std::to_string(m)] = getBuiltins()->numberType;
        members.push_back(tbl(std::move(props)));
    }
    TypeId expected = unionOf(std::move(members));

    TableType::Props givenProps;
    for (int f = 0; f < 500; ++f)
        givenProps["p" + std::to_string(f)] = f == 1 ? getBuiltins()->stringType : getBuiltins()->numberType;
    givenProps["k0"] = getBuiltins()->numberType;
    TypeId given = tbl(std::move(givenProps));

    std::optional<MismatchExplanation> explanation = explain(given, expected);
    REQUIRE(explanation);
    INFO(explanation->reason);
    CHECK(explanation->reason.find("The closest match is expected#1") != std::string::npos);
    CHECK(explanation->reason.find("  • given.p1: expected 'number', got 'string'") != std::string::npos);
}

// A long union inside a message is cut off the way `ToString` cuts off any long type.
TEST_CASE_FIXTURE(ExplainFixture, "long_union_inside_a_message_is_truncated")
{
    std::vector<TypeId> options;
    for (int m = 0; m < 500; ++m)
        options.push_back(arena.addType(SingletonType{StringSingleton{"option" + std::to_string(m)}}));
    TypeId expected = tbl({{"kind", unionOf(std::move(options))}, {"n", getBuiltins()->numberType}});
    TypeId given = tbl({{"kind", getBuiltins()->numberType}, {"n", getBuiltins()->numberType}});

    std::optional<MismatchExplanation> explanation = explain(given, expected);
    REQUIRE(explanation);
    INFO(explanation->reason);
    CHECK(explanation->reason.find("*TRUNCATED*") != std::string::npos);
    CHECK(explanation->reason.size() < 1000);
}

// An empty table against a union of many tables lists what a few members need and counts the rest.
TEST_CASE_FIXTURE(ExplainFixture, "empty_table_against_many_members_is_capped")
{
    std::vector<TypeId> members;
    for (int m = 0; m < 30; ++m)
        members.push_back(tbl({{"f" + std::to_string(m), getBuiltins()->numberType}}));
    TypeId expected = unionOf(std::move(members));

    std::optional<MismatchExplanation> explanation = explain(tbl({}), expected);
    REQUIRE(explanation);
    INFO(explanation->reason);
    CHECK(explanation->reason.find("expected#8 needs 'f7: number'") != std::string::npos);
    CHECK(explanation->reason.find("expected#9 needs") == std::string::npos);
    CHECK(explanation->reason.find("  • ...and 22 more") != std::string::npos);
}

// Every `\n`-separated line of a reason, and how many start with `prefix`.
static size_t countLines(const std::string& text, const std::string& prefix = "")
{
    size_t count = 0;
    size_t at = 0;
    while (at <= text.size())
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        if (text.compare(at, prefix.size(), prefix) == 0)
            ++count;
        at = end + 1;
    }
    return count;
}

// A long aliased union lists only the members the message labels, by their own numbers.
TEST_CASE_FIXTURE(ExplainFixture, "long_aliased_union_lists_only_members_it_refers_to")
{
    TypeId expected = tbl({{"a", getBuiltins()->numberType}, {"b", getBuiltins()->stringType}});
    std::vector<TypeId> members;
    for (int m = 0; m < 40; ++m)
        members.push_back(tbl({{"a", getBuiltins()->numberType}, {"b", getBuiltins()->numberType}, {"t" + std::to_string(m), getBuiltins()->numberType}}));
    TypeId given = unionOf(std::move(members));
    getMutable<UnionType>(given)->name = "Drop";

    std::optional<MismatchExplanation> explanation = explain(given, expected);
    REQUIRE(explanation);
    INFO(explanation->reason);
    REQUIRE(explanation->givenExpansion);
    INFO(*explanation->givenExpansion);

    // Three detailed, five named, and the rest counted.
    CHECK(explanation->reason.find("...and 32 more members") != std::string::npos);
    CHECK(explanation->givenExpansion->rfind("a union of 40 members, including:\n#1 = { a: number, b: number, t0: number }\n", 0) == 0);
    CHECK(countLines(*explanation->givenExpansion, "#") == 8);
    CHECK(explanation->givenExpansion->find("\n#8 = ") != std::string::npos);
    CHECK(explanation->givenExpansion->find("\n#9 = ") == std::string::npos);
    CHECK(explanation->givenExpansion->find("\n...and 32 others") != std::string::npos);
}

// Many bad arguments to one function are listed up to a limit and counted after it.
TEST_CASE_FIXTURE(ExplainFixture, "many_bad_arguments_are_counted")
{
    std::vector<TypeId> numbers(20, getBuiltins()->numberType);
    std::vector<TypeId> strings(20, getBuiltins()->stringType);
    TypeId expected = arena.addType(FunctionType{arena.addTypePack(numbers), arena.addTypePack({})});
    TypeId given = arena.addType(FunctionType{arena.addTypePack(strings), arena.addTypePack({})});

    std::optional<MismatchExplanation> explanation = explain(given, expected);
    REQUIRE(explanation);
    INFO(explanation->reason);
    CHECK(countLines(explanation->reason, "  • its ") == 6);
    CHECK(explanation->reason.find("  • ...and 14 more arguments") != std::string::npos);
}

// However many sections a message has, it stops at the reason's line cap and counts the lines left.
TEST_CASE_FIXTURE(ExplainFixture, "reason_is_held_to_its_line_cap")
{
    std::vector<TypeId> numbers(20, getBuiltins()->numberType);
    std::vector<TypeId> strings(20, getBuiltins()->stringType);
    TableType::Props expectedProps;
    TableType::Props givenProps;
    for (int f = 0; f < 8; ++f)
    {
        expectedProps["f" + std::to_string(f)] = arena.addType(FunctionType{arena.addTypePack(numbers), arena.addTypePack({})});
        givenProps["f" + std::to_string(f)] = arena.addType(FunctionType{arena.addTypePack(strings), arena.addTypePack({})});
    }

    std::optional<MismatchExplanation> explanation = explain(tbl(std::move(givenProps)), tbl(std::move(expectedProps)));
    REQUIRE(explanation);
    INFO(explanation->reason);
    // The cap counts line breaks, the blank line under the two printed types included.
    CHECK(size_t(std::count(explanation->reason.begin(), explanation->reason.end(), '\n')) <= MismatchWording::kMaxReasonLines);
    CHECK(explanation->reason.size() <= MismatchWording::kMaxReasonChars);

    // Each function is a bullet of 9 lines: its signature, the intro, 6 arguments and a count.
    size_t shown = countLines(explanation->reason, "  • given.f");
    CHECK(shown > 0);
    CHECK(explanation->reason.find("\n...and " + std::to_string((8 - shown) * 9) + " more lines") != std::string::npos);
}

// An array where one of its elements is expected is said in one line about the braces, not diffed field
// by field against the element.
TEST_CASE_FIXTURE(BuiltinsFixture, "array_of_the_expected_type_in_a_map")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Animals = { [string]: { { name: string } } }
        local function takes_animals(animals: { [string]: { name: string } }) end
        local animals: Animals = {}
        takes_animals(animals)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'animals' to be\n"
        "    '{ [string]: { name: string } }'\n"
        "but was given\n"
        "    'Animals: { [string]: {{ name: string }} }'\n"
        "\n"
        "Some properties are incompatible:\n"
        "  • animals[string] is an array where a single '{ name: string }' is expected"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "element_of_the_expected_array_in_a_field")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function takes_pack(pack: { cats: { { name: string } } }) end
        local pack: { cats: { name: string } } = { cats = { name = "Taz" } }
        takes_pack(pack)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'pack' to be\n"
        "    '{ cats: {{ name: string }} }'\n"
        "but was given\n"
        "    '{ cats: { name: string } }'\n"
        "\n"
        "Some properties are incompatible:\n"
        "  • pack.cats is a single '{ name: string }' where an array of them is expected"
    );
}

// With nothing around the array, there is no path to point at: the whole value is the problem.
TEST_CASE_FIXTURE(BuiltinsFixture, "whole_value_is_an_array_of_the_expected_type")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function takes_cat(cat: { name: string }) end
        local function takes_cats(cats: { { name: string } }) end
        local cats: { { name: string } } = {}
        local cat: { name: string } = { name = "Taz" }
        takes_cat(cats)
        takes_cats(cat)
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'cats' to be\n"
        "    '{ name: string }'\n"
        "but was given\n"
        "    '{{ name: string }}'\n"
        "\n"
        "'cats' is an array where a single value is expected."
    );
    CHECK_EQ(
        toString(result.errors[1]),
        "Expected 'cat' to be\n"
        "    '{{ name: string }}'\n"
        "but was given\n"
        "    '{ name: string }'\n"
        "\n"
        "'cat' is a single value where an array is expected: pass '{ cat }' instead."
    );
}

// A function slot gets the same sentence, after the argument it's about.
TEST_CASE_FIXTURE(BuiltinsFixture, "array_of_the_expected_type_as_a_callback_argument")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function each(f: (cat: { name: string }) -> ()) end
        each(function(cats: { { name: string } }) end)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    '(cat: { name: string }) -> ()'\n"
        "but was given\n"
        "    '(cats: {{ name: string }}) -> ()'\n"
        "\n"
        "This function can be passed a value it doesn't accept:\n"
        "  • cats: expected a single '{ name: string }', got an array of them"
    );
}

TEST_CASE_FIXTURE(BuiltinsFixture, "array_of_the_expected_type_as_a_callback_return")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function with_cat(f: () -> { name: string }) end
        with_cat(function(): { { name: string } } return {} end)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    '() -> { name: string }'\n"
        "but was given\n"
        "    '(...any) -> {{ name: string }}'\n"
        "\n"
        "This function should return:\n"
        "  • a single '{ name: string }', not an array of them"
    );
}

// An aliased table isn't spelled out when every bullet says both what was expected and what was given: the
// declaration would only say it again. The variable the paths are rooted at is printed with its type, and a
// table literal's type is never printed as the name of the local holding it.
TEST_CASE_FIXTURE(BuiltinsFixture, "an_alias_is_not_spelled_out_when_the_bullets_say_both_types")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Config = { host: string, port: number }
        local settings = { host = "localhost", port = "8080" }
        local config: Config = settings
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'settings' to be\n"
        "    'Config'\n"
        "but was given\n"
        "    '{ port: string, … }'\n"
        "\n"
        "Some properties are incompatible:\n"
        "  • settings.port: expected 'number', got 'string'"
    );
}

// An argument's bullet says what the parameter takes as well as what it's passed: the signature above it
// can hide the parameter's type behind a name.
TEST_CASE_FIXTURE(BuiltinsFixture, "an_argument_bullet_says_what_the_parameter_takes")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Response = { status_code: number, headers: { [string]: string }, body: string }
        local function request(on_response: (response: Response) -> ()) end
        request(function(response: { status_code: string, headers: { [string]: string }, body: string }) end)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    '(response: Response) -> ()'\n"
        "but was given\n"
        "    '(response: { body: string, headers: { [string]: string }, status_code: string }) -> ()'\n"
        "\n"
        "This function can be passed a value it doesn't accept:\n"
        "  • response.status_code: expected 'number', got 'string'"
    );
}

// An optional argument is reported as what was passed, `Player?`, not as the `nil` member that failed.
TEST_CASE_FIXTURE(BuiltinsFixture, "an_optional_argument_is_reported_whole")
{
    CheckResult result = check(R"(
        type Player = { name: string }
        local function find_player(id: number): Player? return nil end
        local function announce(player: Player) end
        announce(find_player(42))
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(toString(result.errors[0]), "Expected this to be 'Player', but got 'Player?'");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "an_array_where_a_map_is_expected_says_so")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Permissions = { [string]: boolean }
        local function grant(permissions: Permissions) end
        local permissions = { "read", "write" }
        grant(permissions)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'permissions' to be\n"
        "    'Permissions'\n"
        "but was given\n"
        "    '{string}'\n"
        "\n"
        "permissions: expected a map of '{ [string]: boolean }', got an array of 'string'"
    );
}

// An alias the message mentions without saying what it is gets declared, one level in from the two types; a
// large one doesn't, being one the reader knows by name.
TEST_CASE_FIXTURE(BuiltinsFixture, "mentioned_aliases_are_declared")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Method = "GET" | "POST" | "PUT" | "DELETE"
        type Request = { url: string, method: Method }
        local function send(request: Request) end
        local request = { url = "/users", method = "PATCH" :: "PATCH" }
        send(request)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'request' to be\n"
        "    'Request'\n"
        "but was given\n"
        "    '{ method: \"PATCH\", … }'\n"
        "where\n"
        "    type Method = \"GET\" | \"POST\" | \"PUT\" | \"DELETE\"\n"
        "\n"
        "Some properties are incompatible:\n"
        "  • request.method: expected 'Method', got '\"PATCH\"'"
    );
}

// A large record printed as a block shows the properties the reason is about first, then the ones that say
// what it is (`kind`, `name`, literal-typed ones), and counts the rest.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_large_record_shows_the_properties_that_matter")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Player = {
            kind: "player", name: string, user_id: number, team: "red" | "blue", health: number, max_health: number,
            level: number, xp: number, gold: number, position: { x: number, y: number }, inventory: { string }, muted: boolean,
        }
        local function spawn(player: Player) end
        local player = {
            kind = "player" :: "player", name = "deviaze", user_id = 7, team = "red" :: "red", health = "100", max_health = 100,
            level = 3, xp = 0, gold = "12", position = { x = 0, y = 0 }, inventory = {}, muted = false,
        }
        spawn(player)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'player' to be\n"
        "    'Player'\n"
        "but was given\n"
        "    '{ health: string, gold: string, kind: \"player\", team: \"red\", … }'\n"
        "\n"
        "Some properties are incompatible:\n"
        "  • player.health: expected 'number', got 'string'\n"
        "  • player.gold: expected 'number', got 'string'"
    );
}

// A value with a name is called by it, except when the name is one of the types printed under it.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_name_that_is_also_a_printed_type_is_not_the_subject")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Config = { port: number }
        local Config = 5
        local config: Config = Config
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    'Config: { port: number }'\n"
        "but was given\n"
        "    'number'\n"
        "\n"
        "'number' is unrelated to 'Config'."
    );
}

// A function in a table literal is named by its place in the literal, and its parameters and return values are
// listed without its signature again: the header prints it, parameter names included.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_function_in_a_table_literal_is_named_by_its_place")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Handler = (inv: { slots: { number? } }) -> ()
        local bus: { handlers: { craft: { Handler } } } = {
            handlers = { craft = { function(inv: { slots: { number } }) end } },
        }
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'bus.handlers.craft[1]' to be\n"
        "    '(inv: { slots: {number?} }) -> ()'\n"
        "but was given\n"
        "    '(inv: { slots: {number} }) -> ()'\n"
        "\n"
        "This function can be passed a value it doesn't accept:\n"
        "  • inv.slots[i]: expected 'number?', got 'number'\n"
        "\n"
        "Help (narrowed parameter):\n"
        "  • 'inv.slots[i]' can't be narrowed from 'number?' to 'number'\n"
        "  • Code calling this function as the expected type is allowed to pass 'nil' for 'inv.slots[i]'\n"
        "  • This parameter must accept 'number?' to match the caller's expected signature"
    );
}

// A function taken as a parameter gets a bullet of its own among the arguments.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_callback_parameter_gets_its_own_bullet")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function use(handler: (req: { user: string? }, next: () -> ()) -> ()) end
        local function auth(req: { user: string? }, next: (err: string?) -> ()) end
        use(auth)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'auth' to be\n"
        "    '(req: { user: string? }, next: () -> ()) -> ()'\n"
        "but was given\n"
        "    '(req: { user: string? }, next: (err: string?) -> ()) -> ()'\n"
        "\n"
        "This function can be passed a value it doesn't accept:\n"
        "  • next = () -> ()\n"
        "    'next' has no arguments but is incorrectly called with 'string?'"
    );
}

// `...T` returned where `...U` is declared is about every value.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_variadic_return_is_about_every_value")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function parts(...: string): ...number
            return ...
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected every returned value to be 'number', but was given 'string'"
    );
}

// Every overload is listed in the one error, by number, with its parameter names.
TEST_CASE_FIXTURE(BuiltinsFixture, "overloads_are_listed_in_one_error")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type On = ((event: "join", fn: () -> ()) -> ()) & ((event: "chat", fn: (message: string) -> ()) -> ())
        local on: On = nil :: any
        on("chat", function(message: number) end)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "None of the overloads of 'on' accept '(\"chat\", (message: number) -> ())':\n"
        "  • #1 '(event: \"join\", fn: () -> ()) -> ()'\n"
        "  • #2 '(event: \"chat\", fn: (message: string) -> ()) -> ()'"
    );
}

// The receiver of a method call is its first argument, and literals are printed as written.
TEST_CASE_FIXTURE(BuiltinsFixture, "overloads_error_lists_a_method_calls_arguments")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Emitter = {
            on: ((self: Emitter, event: "join", fn: () -> ()) -> ()) & ((self: Emitter, event: "chat", fn: (message: string) -> ()) -> ()),
        }
        local e: Emitter = nil :: any
        e:on('say "hi"', true)
        local function many(...: number)
            e.on(e, "join", ...)
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    const std::string overloads =
        "  • #1 '(self: Emitter, event: \"join\", fn: () -> ()) -> ()'\n"
        "  • #2 '(self: Emitter, event: \"chat\", fn: (message: string) -> ()) -> ()'";
    CHECK_EQ("None of the overloads of 'e:on' accept '(Emitter, \"say \\\"hi\\\"\", true)':\n" + overloads, toString(result.errors[0]));
    CHECK_EQ("None of the overloads of 'e.on' accept '(Emitter, \"join\", ...number)':\n" + overloads, toString(result.errors[1]));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "overloads_taking_other_argument_counts_are_listed_in_one_error")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type On = ((event: "join", fn: () -> ()) -> ()) & ((event: "chat", fn: (message: string) -> ()) -> ())
        local on: On = nil :: any
        on("chat")
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "No overload of 'on' takes 1 argument:\n"
        "  • #1 '(event: \"join\", fn: () -> ()) -> ()'\n"
        "  • #2 '(event: \"chat\", fn: (message: string) -> ()) -> ()'"
    );
}

// A function whose only problem is a parameter the expected type never passes is one sentence, without the two types,
// and a function in a table literal passed to a call is named after the call.
TEST_CASE_FIXTURE(BuiltinsFixture, "an_extra_parameter_is_one_sentence")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Props = { onClick: (button: string) -> () }
        local function Button(props: Props) end
        Button({ onClick = function(button: string, modifiers: { string }) end })
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(toString(result.errors[0]), "'Button.onClick' should only have 1 parameter, but was passed a function with an extra parameter 'modifiers: {string}'.");
}

TEST_CASE_FIXTURE(BuiltinsFixture, "an_unnamed_function_with_extra_parameters_is_one_sentence")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function each(f: (item: string) -> ()) end
        each(function(item: string, index: number, total: number) end)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(toString(result.errors[0]), "This function should only have 1 parameter, but has extra parameters 'index: number' and 'total: number'.");
}

// With something else wrong too, an extra parameter is one of the bullets.
TEST_CASE_FIXTURE(BuiltinsFixture, "an_extra_parameter_among_other_problems_is_a_bullet")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function each(f: (item: string) -> ()) end
        each(function(item: number, index: number) end)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    '(item: string) -> ()'\n"
        "but was given\n"
        "    '(item: number, index: number) -> ()'\n"
        "\n"
        "This function can be passed values it doesn't accept:\n"
        "  • item: expected 'string', got 'number'\n"
        "  • 'index: number' is never passed"
    );
}

// An aliased union in the header shows only the members the message refers to.
TEST_CASE_FIXTURE(BuiltinsFixture, "union_members_the_message_doesnt_refer_to_are_left_out")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Drop = { gold: number } | { id: string, weight: number } | { xp: number } | { mana: number }
        local function take(_: { id: string, count: number }) end
        local function f(lastDrop: Drop) take(lastDrop) end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'lastDrop' to be\n"
        "    '{ count: number, id: string }'\n"
        "but was given\n"
        "    type Drop = … | { id: string, weight: number } | … (2 more)\n"
        "\n"
        "No member of 'Drop' is compatible with the expected type:\n"
        "lastDrop#2 = { id: string, weight: number } is almost what we expected, but:\n"
        "  • missing property 'lastDrop.count: number'"
    );
}

// When the one problem is a misspelled property, the error is a typo, not a type mismatch.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_typo_is_only_its_correction")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type GameEvent = { kind: "join", player: string } | { kind: "leave", player: string, reason: "kick" | "ban" }
        local function emit(_: GameEvent) end
        local leaving = { kind = "leave" :: "leave", player = "p", reson = "kick" :: "kick" }
        emit(leaving)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    REQUIRE(get<Typo>(result.errors[0]));
    CHECK_EQ(toString(result.errors[0]), "Did you mean 'leaving.reason' instead of 'leaving.reson'?");
}

// A misspelled property in a table literal is reported at its name, even for an optional property, which nothing else
// reports: the literal fits, and the value is never read.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_misspelled_optional_property_in_a_literal_is_a_typo")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Config = { name: string, program: { path: string }?, verbose: boolean? }
        local cfg: Config = {
            name = "build",
            pogram = { path = "/usr/bin/cc" },
        }
        local again: Config = { name = "x", ["verbos"] = true }
        local both: Config = { name = "y", program = nil, progam = 1 }
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    REQUIRE(get<Typo>(result.errors[0]));
    CHECK_EQ(toString(result.errors[0]), "Did you mean 'program' instead of 'pogram'?");
    // Only the name, not its value.
    CHECK_EQ(result.errors[0].location, Location{{4, 12}, {4, 18}});
    REQUIRE(get<Typo>(result.errors[1]));
    CHECK_EQ(toString(result.errors[1]), "Did you mean 'verbose' instead of 'verbos'?");
}

// A string one typo from an expected one is a typo (a swapped pair of letters is one), and a string too short to have
// typos isn't, nor is a short word two edits from another.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_misspelled_string_is_a_typo")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local key: "LeftControl" | "LeftShift" = "LeftCtrl"
        local team: "Red" | "Blue" = "Bleu"
        local letter: "s" = "t"
        local item: "Tool" | "Weapon" = "Food"
    )");

    LUAU_REQUIRE_ERROR_COUNT(4, result);
    REQUIRE(get<Typo>(result.errors[0]));
    CHECK_EQ(toString(result.errors[0]), "Did you mean '\"LeftControl\"' instead of '\"LeftCtrl\"'?");
    REQUIRE(get<Typo>(result.errors[1]));
    CHECK_EQ(toString(result.errors[1]), "Did you mean '\"Blue\"' instead of '\"Bleu\"'?");
    CHECK(get<TypeMismatch>(result.errors[2]));
    CHECK(get<TypeMismatch>(result.errors[3]));
}

// A misspelled tag in a table literal is a typo at the string, though the checker widens the literal's strings to
// `string`. With another problem, a misspelled tag still picks the member it meant.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_misspelled_tag_in_a_literal_is_a_typo")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Event = { kind: "join", player: string } | { kind: "leave", player: string, reason: string }
        local function emit(_: Event) end
        emit({ kind = "laeve", player = "p", reason = "r" })
        local _e: Event = { kind = "leavee", player = "p", reason = "r" }
        emit({ kind = "leeve" :: "leeve", player = "p", reason = 1 })
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    REQUIRE(get<Typo>(result.errors[0]));
    CHECK_EQ(toString(result.errors[0]), "Did you mean '\"leave\"' instead of '\"laeve\"'?");
    CHECK_EQ(result.errors[0].location, Location{{3, 22}, {3, 29}});
    REQUIRE(get<Typo>(result.errors[1]));
    CHECK_EQ(toString(result.errors[1]), "Did you mean '\"leave\"' instead of '\"leavee\"'?");
    CHECK_EQ(
        toString(result.errors[2]),
        "Expected this to be\n"
        "    type Event = … | { kind: \"leave\", player: string, reason: string }\n"
        "but was given\n"
        "    '{ kind: \"leeve\", reason: number, … }'\n"
        "\n"
        "Help (typo):\n"
        "  • Did you mean '\"leave\"' instead of '\"leeve\"' for 'given.kind'?\n"
        "\n"
        "The closest match is Event#2, but:\n"
        "  • given.kind: expected '\"leave\"', got '\"leeve\"'\n"
        "  • given.reason: expected 'string', got 'number'"
    );
}

// The member a tag picked is spelled out in the union printed above, so the header only names it.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_tagged_match_is_not_printed_twice")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Shape = { kind: "circle", radius: number } | { kind: "rect", width: number, height: number }
        local function draw(_: Shape) end
        draw({ kind = "rect", width = 1, height = true })
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected this to be\n"
        "    type Shape = … | { height: number, kind: \"rect\", width: number }\n"
        "but was given\n"
        "    '{ height: boolean, kind: \"rect\", … }'\n"
        "\n"
        "'given.kind' is '\"rect\"', so 'given' was compared with Shape#2:\n"
        "  • given.height: expected 'number', got 'boolean'"
    );
}

// A swap the help names needs no bullets of its own; anything else wrong keeps its bullet.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_swap_is_said_only_by_its_help")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        local function parse(input: string): (string?, boolean)
            return nil, #input > 0
        end
        local _validate: (input: string) -> (boolean, string?) = parse
        local function both(name: string, age: number) end
        local _call: (age: number, name: string) -> () = both
        local function partly(input: string): (string?, boolean, number)
            return nil, true, 1
        end
        local _three: (input: string) -> (boolean, string?, string) = partly
    )");

    LUAU_REQUIRE_ERROR_COUNT(3, result);
    CHECK_EQ(
        toString(result.errors[0]),
        "Expected 'parse' to be\n"
        "    '(input: string) -> (boolean, string?)'\n"
        "but was given\n"
        "    '(input: string) -> (string?, boolean)'\n"
        "\n"
        "Help (return order):\n"
        "  • The 1st and 2nd return values look like they're in the wrong order"
    );
    CHECK_EQ(
        toString(result.errors[1]),
        "Expected 'both' to be\n"
        "    '(age: number, name: string) -> ()'\n"
        "but was given\n"
        "    '(name: string, age: number) -> ()'\n"
        "\n"
        "Help (parameter order):\n"
        "  • The parameters 'name' and 'age' look like they're in the wrong order"
    );
    CHECK_EQ(
        toString(result.errors[2]),
        "Expected 'partly' to be\n"
        "    '(input: string) -> (boolean, string?, string)'\n"
        "but was given\n"
        "    '(input: string) -> (string?, boolean, number)'\n"
        "\n"
        "Help (return order):\n"
        "  • The 1st and 2nd return values look like they're in the wrong order\n"
        "\n"
        "This function should return:\n"
        "  • 'string' as its 3rd value, not 'number'"
    );
}

// A mismatched property gets a note where each side declares it, pointing back at the error: a deep path is hard to
// trace back to the two declarations by hand.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_mismatched_property_notes_where_both_sides_declare_it")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Stack = { id: string, count: number }
        type Inventory = { byCategory: { [string]: { [string]: { Stack } } } }
        local function load(raw: { [string]: { [string]: { { id: string, count: string } } } }): Inventory
            return { byCategory = raw }
        end
        local _ = load
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const std::vector<DiagnosticNote>& notes = getMainModule()->notes;
    REQUIRE_EQ(notes.size(), 2);
    CHECK_EQ(notes[0].message, "Expected 'count: number' because of this");
    CHECK_EQ(notes[0].location, Location{{1, 35}, {1, 40}});
    CHECK_EQ(notes[1].message, "'count' is 'string' here, but should be 'number'");
    CHECK_EQ(notes[1].location, Location{{3, 73}, {3, 78}});
    CHECK_EQ(notes[0].origin, result.errors[0].location);
    CHECK_EQ(notes[1].origin, result.errors[0].location);
}

// A missing property gets a note at the table it's missing from, which can be a few variables away from the error,
// and one where the expected type requires it.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_missing_property_notes_the_table_it_is_missing_from")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Color = { r: number, g: number, b: number }
        type Theme = { name: string, colors: { background: Color, text: Color } }
        local function apply(_: Theme) end
        local white = { r = 1, g = 1 }
        local colors = { background = white, text = { r = 0, g = 0, b = 0 } }
        apply({ name = "light", colors = colors })
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    const std::vector<DiagnosticNote>& notes = getMainModule()->notes;
    REQUIRE_EQ(notes.size(), 2);
    CHECK_EQ(notes[0].message, "Expected 'b: number' because of this");
    CHECK_EQ(notes[0].location, Location{{1, 45}, {1, 46}});
    CHECK_EQ(notes[1].message, "'colors.background' is missing 'b: number'");
    CHECK_EQ(notes[1].location, Location{{4, 22}, {4, 38}});
    CHECK_EQ(notes[1].origin, result.errors[0].location);
}

// Inside a function, the table a property is missing from is named from its parameter, as the bullet names it.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_missing_property_note_names_a_parameter")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type EnemyImpl = {
            __index: EnemyImpl,
            take_damage: (self: Enemy, amount: number) -> boolean,
        }
        type Enemy = typeof(setmetatable({} :: { name: string, health: number }, {} :: EnemyImpl))
        type BossImpl = EnemyImpl & { phase: (self: Boss) -> number }
        type Boss = typeof(setmetatable({} :: { name: string, health: number, enraged: boolean }, {} :: BossImpl))
        local Boss = {} :: BossImpl
        Boss.take_damage = function(self: Boss, amount: number): number
            return self.health - amount
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    INFO(toString(result.errors[0]));
    const std::vector<DiagnosticNote>& notes = getMainModule()->notes;
    REQUIRE_EQ(notes.size(), 2);
    CHECK_EQ(notes[0].message, "Expected 'enraged: boolean' because of this");
    CHECK_EQ(notes[1].message, "'self' is missing 'enraged: boolean'");
    // Enemy's own table, which is what reaches 'self'
    CHECK_EQ(notes[1].location.begin.line, 5);
}

// A generic's property is declared once for both sides, where neither note would say anything.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_mismatch_inside_one_generic_declaration_has_no_notes")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Box<T> = { value: T }
        local function wrap(b: Box<string>): Box<number>
            return b
        end
        local _ = wrap
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK(getMainModule()->notes.empty());
}

// A property misspelled from one the value has is a typo, read or written, on a table, a string or a library. One that
// differs only in case keeps upstream's suggestion, and one with nothing close stays unknown.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_misspelled_property_is_a_typo")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Player = { name: string, level: number }
        type Settings = { volume: number, muted: boolean? }
        local function use(player: Player, settings: Settings, text: string)
            local _ = player.nmae
            settings.mutd = true
            local _ = text:uper()
            table.srot({ "b", "a" })
            local _ = player.Name
            local _ = player.zzzz
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(6, result);
    const char* typos[] = {
        "Did you mean 'player.name' instead of 'player.nmae'?",
        "Did you mean 'settings.muted' instead of 'settings.mutd'?",
        "Did you mean 'text:upper' instead of 'text:uper'?",
        "Did you mean 'table.sort' instead of 'table.srot'?",
    };
    for (size_t i = 0; i < 4; ++i)
    {
        REQUIRE(get<Typo>(result.errors[i]));
        CHECK_EQ(toString(result.errors[i]), typos[i]);
    }
    CHECK(get<UnknownPropButFoundLikeProp>(result.errors[4]));
    CHECK(get<UnknownProperty>(result.errors[5]));
}

// A type name misspelled from one in scope is a typo too.
TEST_CASE_FIXTURE(BuiltinsFixture, "a_misspelled_type_name_is_a_typo")
{
    ScopedFastFlag sff{FFlag::LuwuHelpfulSubtypingErrors, true};

    CheckResult result = check(R"(
        type Player = { name: string }
        local function greet(_: Plyer) end
        local function count(_: Zzzzzz) end
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(toString(result.errors[0]), "Typo 'Plyer': Did you mean 'Player'?");
    CHECK_EQ(toString(result.errors[1]), "Unknown type 'Zzzzzz'");
}

// Luwu: every help is written by a Helpifier, which gives nothing when help is off
TEST_CASE("helpifier_writes_every_help_the_same_way")
{
    Helpifier helpifier;
    CHECK_EQ("Help (typo):\n  • Did you mean 'reason'?", helpifier("typo", {"Did you mean 'reason'?"}));
    CHECK_EQ("Help (two):\n  • a\n  • b", helpifier("two", {"a", "b"}));
    CHECK_EQ("", helpifier("empty", {}));

    Helpifier off{false};
    CHECK_EQ("", off("typo", {"Did you mean 'reason'?"}));
}

TEST_CASE_FIXTURE(BuiltinsFixture, "show_help_off_leaves_no_help_in_type_errors")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuHelpfulSubtypingErrors, true},
    };

    // A frontend-wide option, like the others the checker reads: a per-check override doesn't reach it
    getFrontend().options.showHelp = false;
    CheckResult result = check(R"(
        type Scoped = { name: string }
        type Vcn = { name: string, vcn: number }
        type Event = { reason: string }

        local function process(rows: { Scoped }) end
        local vcns: { Vcn } = {}
        process(vcns)

        local e: Event = { reson = "x" }
    )");

    // With help on, the read/write mismatch also reports a help at `process`'s parameter, alone. The typo is reported
    // as a `Typo` of its own whether or not help is on.
    LUAU_REQUIRE_ERROR_COUNT(3, result);
    for (const TypeError& error : result.errors)
        CHECK_EQ(toString(error).find("Help"), std::string::npos);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "a_read_only_array_is_printed_with_read")
{
    ScopedFastFlag sffs[] = {
        {FFlag::DebugLuauForceOldSolver, false},
        {FFlag::LuwuHelpfulSubtypingErrors, true},
    };

    CheckResult result = check(R"(
        type Item = string | number
        local function give_back(items: { read Item }?): { Item }
            local result: { Item } = items or {}
            return result
        end
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    std::string message = toString(result.errors[0]);
    INFO(message);
    CHECK(message.find("got '{read Item}'") != std::string::npos);
}

TEST_SUITE_END();
