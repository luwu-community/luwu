// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "../CodeGen/src/EmitCommonA64.h"
#include "doctest.h"

#include <array>

using namespace Luau::CodeGen;
using namespace Luau::CodeGen::A64;

TEST_SUITE_BEGIN("EmitCommonA64");

TEST_CASE("ThreeDoubleArgumentsPreserveOverlappingSources")
{
    // Cover ABI-register permutations, repeated arguments, external registers,
    // and an input occupying d31 before it is used to break a cycle.
    const RegisterA64 choices[] = {d0, d1, d2, d3, d4, d31};
    for (RegisterA64 x : choices)
        for (RegisterA64 y : choices)
            for (RegisterA64 z : choices)
            {
                CAPTURE(unsigned(x.index));
                CAPTURE(unsigned(y.index));
                CAPTURE(unsigned(z.index));
                AssemblyBuilderA64 build(nullptr, false, 0);
                std::array<uint64_t, 32> values, original;
                for (unsigned i = 0; i < 32; ++i)
                    values[i] = original[i] = 0x1234567800000000ull + i;

                emitMoveDoubleArguments3(build, x, y, z);
                build.finalize();
                CHECK(build.code.size() <= 4); // three copies and at most one cycle break
                for (uint32_t word : build.code)
                {
                    CHECK((word & 0xfffffc00u) == 0x1e604000u); // fmov dN, dM
                    unsigned to = word & 31;
                    unsigned from = (word >> 5) & 31;
                    CHECK(to != from);
                    values[to] = values[from];
                }
                CHECK(values[0] == original[x.index]);
                CHECK(values[1] == original[y.index]);
                CHECK(values[2] == original[z.index]);
                for (unsigned i = 3; i < 31; ++i)
                    CHECK(values[i] == original[i]);
            }
}

TEST_SUITE_END();
