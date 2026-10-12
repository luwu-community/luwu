// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// Luwu typewriter tests: typing syntax every Luwu build has. A feature adds its own cases next to its other tests.
#include "Typewriter.h"

#include "Fixture.h"

#include "doctest.h"

using namespace Luau;

namespace
{

// Code after the typed line. It uses nothing a typed line declares, so it stays clean whatever is typed above it.
const char* const kAfter = R"(
local unrelated = 1
print(unrelated + 1)
)";

} // namespace

TEST_SUITE_BEGIN("Typewriter");

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_tables_and_calls")
{
    const char* before = "local a, b = 1, 2\nlocal function wrap(t) return t end\n";
    checkTypewriting(*this, before, "local t = { a = a, b = { c = b } }", kAfter);
    checkTypewriting(*this, before, "local w = wrap({ a, b })", kAfter);
    checkTypewriting(*this, before, "print(string.format(\"%d\", a))", kAfter);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_blocks")
{
    const char* before = "local a = 1\n";
    checkTypewriting(*this, before, "local function f(x: number): number\n    return x + a\nend", kAfter);
    checkTypewriting(*this, before, "if a > 0 then\n    print(a)\nend", kAfter);
    checkTypewriting(*this, before, "for i = 1, a do\n    print(i)\nend", kAfter);
}

TEST_SUITE_END();
