// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// Luwu typewriter tests: typing syntax every Luwu build has. A feature adds its own cases next to its other tests.
#include "Typewriter.h"

#include "ClassesFixture.h"
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
    checkTypewriting(*this, before, "local s = (\"abc\"):upper()", kAfter);
}

TEST_CASE_FIXTURE(BuiltinsFixture, "typing_blocks")
{
    const char* before = "local a = 1\n";
    checkTypewriting(*this, before, "local function f(x: number): number\n    return x + a\nend", kAfter);
    checkTypewriting(*this, before, "if a > 0 then\n    print(a)\nend", kAfter);
    checkTypewriting(*this, before, "for i = 1, a do\n    print(i)\nend", kAfter);
    checkTypewriting(*this, before, "local function f(t: { string })\n    if #t < a then\n        return nil\n    end\n    return t[a]\nend", kAfter);
    checkTypewriting(
        *this,
        before,
        "local function g(n: number)\n    if n == 1 then\n        return 1\n    elseif n == 2 then\n        return 2\n    elseif n == 3 then\n"
        "        return 3\n    end\n    return 4\nend",
        kAfter
    );
}

// Luwu Classes (rfcs/classes): declaring classes, and using them from the code below
TEST_CASE_FIXTURE(ClassesFixture, "typing_class_declarations")
{
    const char* before = "local a = 1\n";

    const char* pod = "class Point\n"
                      "    x: number\n"
                      "    y: number = 0\n"
                      "\n"
                      "    function length(self): number\n"
                      "        return self.x + self.y\n"
                      "    end\n"
                      "end";
    checkTypewriting(*this, before, pod, kAfter);

    const char* primaryConstructor = "class Cat(public name: string, private age: number)\n"
                                     "    public function greet(self): string\n"
                                     "        return self.name\n"
                                     "    end\n"
                                     "end";
    checkTypewriting(*this, before, primaryConstructor, kAfter);

    const char* constAndDefaults = "class Config\n"
                                   "    public const name: string = \"default\"\n"
                                   "    private retries: number = 3\n"
                                   "end";
    checkTypewriting(*this, before, constAndDefaults, kAfter);
}

TEST_CASE_FIXTURE(ClassesFixture, "typing_class_uses")
{
    const char* before = R"(class Point
    x: number
    y: number

    function length(self): number
        return self.x + self.y
    end
end

class Cat(public name: string, public age: number) end

local value: unknown = nil
)";
    checkTypewriting(*this, before, "local p = Point { x = 1, y = 2 }", kAfter);
    checkTypewriting(*this, before, "local c = Cat(\"Taz\", 3)", kAfter);
    checkTypewriting(*this, before, "local n = Point { x = 1, y = 2 }:length()", kAfter);
    checkTypewriting(*this, before, "print(Cat(\"Taz\", 3).name)", kAfter);
    checkTypewriting(*this, before, "if class.isinstance(value, Cat) then\n    print(value.name)\nend", kAfter);
}

TEST_SUITE_END();
