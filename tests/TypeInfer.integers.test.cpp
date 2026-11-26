// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// Luwu Integers (rfcs/integers.md): integer literals, operators, the `integer` library and the error variants in
// Analysis
#include "Fixture.h"
#include "Typewriter.h"

#include "ScopedFlags.h"
#include "doctest.h"

using namespace Luau;

LUAU_FASTFLAG(LuwuClasses)
LUAU_FASTFLAG(LuwuGenericNominals)
LUAU_FASTFLAG(LuwuNonePrimitive)
LUAU_FASTFLAG(LuwuDeclareStatements)
LUAU_FASTFLAG(DebugLuwuTryExpressions)
LUAU_FASTFLAG(LuauIntegerType2)
LUAU_FASTFLAG(DebugLuwuIntegers)

namespace
{

// The flags the `integer` library needs, in Analysis as in the VM
struct IntegersFixture : BuiltinsFixture
{
    ScopedFastFlag sffs[7] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::DebugLuwuTryExpressions, true},
        {FFlag::LuauIntegerType2, true},
        {FFlag::DebugLuwuIntegers, true},
    };
};

// The integer flags without try expressions, whose result library declares `Result`
struct IntegersWithoutTryFixture : BuiltinsFixture
{
    ScopedFastFlag sffs[7] = {
        {FFlag::LuwuClasses, true},
        {FFlag::LuwuGenericNominals, true},
        {FFlag::LuwuNonePrimitive, true},
        {FFlag::LuwuDeclareStatements, true},
        {FFlag::DebugLuwuTryExpressions, false},
        {FFlag::LuauIntegerType2, true},
        {FFlag::DebugLuwuIntegers, true},
    };
};

bool mentions(const TypeError& error, const std::string& text)
{
    return toString(error).find(text) != std::string::npos;
}

} // namespace

TEST_SUITE_BEGIN("TypeInferIntegers");

TEST_CASE_FIXTURE(IntegersFixture, "integer_literals_have_their_mode_type")
{
    CheckResult result = check(R"(
        --!strict
        local dynamic = 7int
        local i8 = 1i8
        local u8 = 2u8
        local i16 = 3i16
        local u16 = 4u16
        local i32 = 5i32
        local u32 = 6u32
        local i64 = 7_i64
        local u64 = 8u64
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("int", toString(requireType("dynamic")));
    for (const char* mode : {"i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64"})
        CHECK_EQ(mode, toString(requireType(mode)));
}

TEST_CASE_FIXTURE(IntegersFixture, "every_spelling_of_a_mode_is_one_type")
{
    CheckResult result = check(R"(
        --!strict
        local literal = 5u8
        local named: integer<"u8"> = literal
        local imported: integer.u8 = named
        local nested: integer<integer.u8> = imported
        local modeObject: integer<integer.Mode<"u8">> = nested
        local either: integer<integer.u8 | "u8"> = modeObject
        local back: typeof(literal) = either
        local int: int = 7int
        local dynamic: integer.dynamic = int
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    for (const char* name : {"named", "imported", "nested", "modeObject", "either", "back"})
        CHECK_EQ("u8", toString(requireType(name)));
    CHECK_EQ("int", toString(requireType("dynamic")));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_of_something_that_isnt_a_mode")
{
    CheckResult result = check(R"(
        --!strict
        local x: integer<"u9"> = 5u8
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(Location({2, 17}, {2, 30}), result.errors[0].location);
    CHECK(mentions(result.errors[0], "u9"));
}

TEST_CASE_FIXTURE(IntegersFixture, "modes_are_disjoint")
{
    CheckResult result = check(R"(
        --!strict
        local y: integer.u8 = 5u16
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(Location({2, 30}, {2, 34}), result.errors[0].location);
    const TypeMismatch* mismatch = get<TypeMismatch>(result.errors[0]);
    REQUIRE(mismatch);
    CHECK_EQ("u8", toString(mismatch->wantedType));
    CHECK_EQ("u16", toString(mismatch->givenType));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_accepts_every_mode")
{
    CheckResult result = check(R"(
        --!strict
        local function f(x: integer): integer
            return x
        end
        f(1u8)
        f(2i64)
        f(3int)
        local either: integer.u8 | integer.i8 = 4i8
        f(either)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_generic_binds_the_mode")
{
    CheckResult result = check(R"(
        --!strict
        local function foo<T>(a: integer<T>, b: integer<T>): integer<T>
            return a + b
        end
        local same = foo(1u8, 2u8)
        local mixed = foo(1u8, 2i64)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ("u8", toString(requireType("same")));
    CHECK(Location({6, 22}, {6, 36}).encloses(result.errors[0].location));
    CHECK(mentions(result.errors[0], "u8"));
    CHECK(mentions(result.errors[0], "i64"));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_arithmetic_keeps_the_mode")
{
    CheckResult result = check(R"(
        --!strict
        local a = 5u8
        local sum = a + 3u8
        local quotient = 7int // 2int
        local negated = -(5i16)
        local some: integer = a
        local narrowed = some * a
        local ordered = a < 6u8
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("u8", toString(requireType("sum")));
    CHECK_EQ("int", toString(requireType("quotient")));
    CHECK_EQ("i16", toString(requireType("negated")));
    CHECK_EQ("u8", toString(requireType("narrowed")));
    CHECK_EQ("boolean", toString(requireType("ordered")));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_arithmetic_on_two_modes")
{
    CheckResult result = check(R"(
        --!strict
        local a = 5u8 + 1i8
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(Location({2, 18}, {2, 27}), result.errors[0].location);
    CHECK(mentions(result.errors[0], "u8"));
    CHECK(mentions(result.errors[0], "i8"));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_arithmetic_with_a_number")
{
    CheckResult result = check(R"(
        --!strict
        local a = 5u8 * 2
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(Location({2, 18}, {2, 25}), result.errors[0].location);
    CHECK(mentions(result.errors[0], "u8"));
    CHECK(mentions(result.errors[0], "number"));
    CHECK(mentions(result.errors[0], "integer.u8"));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_comparisons_need_one_mode")
{
    CheckResult result = check(R"(
        --!strict
        local ordered = 5u8 < 6i8
        local equal = 5u8 == 5i8
        local sameMode = 5u8 == 5u8
    )");

    // Ordering raises at runtime and equality is always false, like comparing a number and a string
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK_EQ(Location({2, 24}, {2, 33}), result.errors[0].location);
    CHECK_EQ(Location({3, 22}, {3, 32}), result.errors[1].location);
    CHECK(get<CannotCompareUnrelatedTypes>(result.errors[0]));
    CHECK(get<CannotCompareUnrelatedTypes>(result.errors[1]));
}

TEST_CASE_FIXTURE(IntegersFixture, "unsigned_integers_cant_be_negated")
{
    CheckResult result = check(R"(
        --!strict
        local size = 5u32
        local negative = -size
        local some: integer = size
        local fine = -some
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(Location({3, 25}, {3, 30}), result.errors[0].location);
    CHECK(mentions(result.errors[0], "u32"));
    CHECK(mentions(result.errors[0], "0u32 - size"));
}

TEST_CASE_FIXTURE(IntegersFixture, "integers_have_no_power_operator")
{
    CheckResult result = check(R"(
        --!strict
        local a = 2int ^ 3int
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    CHECK_EQ(Location({2, 18}, {2, 29}), result.errors[0].location);
    CHECK(mentions(result.errors[0], ":pow()"));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_library_keeps_the_mode")
{
    CheckResult result = check(R"(
        --!strict
        local added = integer.sadd(100u8, 250u8)
        local size = 4096u64
        local small = size:cast(integer.u32)
        local parsed = integer.u8(300)
        local cast = integer.cast("7", integer.i16)
        local implicit = integer.cast("7u8")
        local mode = integer.mode(5i8)
        local biggest = integer.max(1int, 2int, 3int)
        local flags = (0b0001_u8):bor(0b0100_u8)
        local shifted = flags:lshift(1int)
        local typed: integer.Mode = mode
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("u8", toString(requireType("added")));
    CHECK_EQ("u32", toString(requireType("small")));
    CHECK_EQ("u8", toString(requireType("parsed")));
    CHECK_EQ("i16", toString(requireType("cast")));
    CHECK_EQ("integer", toString(requireType("implicit")));
    CHECK_EQ("int", toString(requireType("biggest")));
    CHECK_EQ("u8", toString(requireType("flags")));
    CHECK_EQ("u8", toString(requireType("shifted")));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_library_rejects_two_modes")
{
    CheckResult result = check(R"(
        --!strict
        local added = integer.sadd(100u8, 100i8)
        local method = (1u8):sadd(2u16)
    )");

    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(Location({2, 22}, {2, 48}).encloses(result.errors[0].location));
    CHECK(Location({3, 23}, {3, 39}).encloses(result.errors[1].location));
}

TEST_CASE_FIXTURE(IntegersFixture, "typeof_narrows_an_integer_to_its_mode")
{
    CheckResult result = check(R"(
        --!strict
        local function f(x: integer)
            if typeof(x) == "u8" then
                local y: integer.u8 = x
            elseif typeof(x) == "int" then
                local z: int = x
            end
        end
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(IntegersWithoutTryFixture, "integer_library_without_try_expressions")
{
    CheckResult result = check(R"(
        --!strict
        local added = integer.sadd(100u8, 250u8)
        local number = integer.tonumber(5int)
        local mode: integer.Mode = integer.u8
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
    CHECK_EQ("u8", toString(requireType("added")));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_operators_give_integers")
{
    CheckResult result = check(R"(
        --!strict
        local a = 5u8
        local b: integer = a + 3u8
        local c: int = 7int * 2int // 3int % 2int
        local d: integer = -(5i8)
        local e: boolean = a < b
        local f: boolean = 1int <= 2int
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(IntegersFixture, "integers_and_numbers_dont_mix")
{
    CheckResult result = check(R"(
        --!strict
        local a = 7int + 1
        local b = 2int ^ 3int
    )");

    // `+` with a number, and `^`, which integers don't have
    LUAU_REQUIRE_ERROR_COUNT(2, result);
    CHECK(Location({2, 18}, {2, 29}).encloses(result.errors[0].location));
    CHECK(Location({3, 18}, {3, 30}).encloses(result.errors[1].location));
}

TEST_CASE_FIXTURE(IntegersFixture, "integer_library_and_methods")
{
    CheckResult result = check(R"(
        --!strict
        local size: integer.u64 = integer.cast("4096", integer.u64)
        local mode: integer.Mode = integer.mode(size)
        local small = size:cast(integer.u32)
        local flags: integer.u8 = (0b0001_u8):bor(0b0100_u8, 0b1000_u8)
        local set: boolean = flags:btest(1u8)
        local field: integer = flags:extract(1int)
        local number = integer.tonumber(size)
        local biggest = integer.max(1int, 2int, 3int)
        local overflow = Error.Overflow
        local kind: Error.Overflow? = nil
        local data = buffer.create(8)
        buffer.writeinteger(data, 0, size)
        local header: integer = buffer.readinteger(data, 0, integer.u64)
        print(mode, small, set, field, number, biggest, overflow, kind, header)
    )");

    LUAU_REQUIRE_NO_ERRORS(result);
}

TEST_CASE_FIXTURE(IntegersFixture, "upstream_integer_library_is_gone")
{
    CheckResult result = check(R"(
        --!strict
        local i = integer.create(5)
    )");

    LUAU_REQUIRE_ERROR_COUNT(1, result);
    bool namesCreate = toString(result.errors[0]).find("'create'") != std::string::npos;
    CHECK(namesCreate);
}

TEST_CASE_FIXTURE(IntegersFixture, "typing_integer_code")
{
    const std::string before = "local a = 5u8\nlocal b = 2u8\ntype Small = integer.u8\n";
    const std::string after = "\nlocal after = 2\nprint(after)\n";

    checkTypewriting(*this, before, "local c = a + b * 3u8", after);
    checkTypewriting(*this, before, "local d = a:sadd(b):pow(2int)", after);
    checkTypewriting(*this, before, "local e = integer.cast(\"7\", integer.u8)", after);
    checkTypewriting(*this, before, "local f = 300u16 + 18446u16", after);
    checkTypewriting(*this, before, "local g = (a + b):cast(integer.i64)", after);
    checkTypewriting(*this, before, "local h: Small = integer.u8(255) - a", after);
}

TEST_SUITE_END();
