// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details

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
    CHECK_EQ(explanation->reason, "\n\nHelp: did you mean '\"Blue\"'?");
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

// The header says how the value got here, from a string the caller builds.
TEST_CASE_FIXTURE(ExplainFixture, "arrival_names_how_the_value_arrived")
{
    TypeId expected = tbl({{"a", getBuiltins()->numberType}, {"b", getBuiltins()->numberType}});
    TypeId given = tbl({{"a", getBuiltins()->stringType}, {"b", getBuiltins()->numberType}});

    std::string arrival = std::string("ret") + "urned";
    std::optional<MismatchExplanation> explanation = explain(given, expected, arrival);
    REQUIRE(explanation);
    CHECK_EQ(explanation->reason, "\n\nThe returned type is similar, but has an incompatible path:\n  • given.a: expected 'number', got 'string'");
    CHECK_EQ(explanation->notation, MismatchNotationGotRoot);
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
    for (int f = 0; f < 3000; ++f)
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
    CHECK(countLines(explanation->reason, "      • 'number' as its") == 6);
    CHECK(explanation->reason.find("      • ...and 14 more arguments") != std::string::npos);
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

TEST_SUITE_END();
