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

} // namespace

TEST_SUITE_BEGIN("TypeInferIntegers");

TEST_CASE_FIXTURE(IntegersFixture, "integer_operators_give_integers")
{
    CheckResult result = check(R"(
        --!strict
        local a = 5u8
        local b: integer = a + 3u8
        local c: int = 7int * 2int // 3int % 2int
        local d: integer = -a
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
        local flags: integer = (0b0001_u8):bor(0b0100_u8, 0b1000_u8)
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
    const std::string before = "local a = 5u8\nlocal b = 2u8\n";
    const std::string after = "\nlocal after = 2\nprint(after)\n";

    checkTypewriting(*this, before, "local c = a + b * 3u8", after);
    checkTypewriting(*this, before, "local d = a:sadd(b):pow(2int)", after);
    checkTypewriting(*this, before, "local e = integer.cast(\"7\", integer.u8)", after);
}

TEST_SUITE_END();
