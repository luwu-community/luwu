// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// Luwu object notation: loading `.luwon` data files into a Lua state with `luwon_load`.
#include "Luau/Luwon.h"

#include "ScopedFlags.h"

#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "luwon.h"

#include "doctest.h"

#include <math.h>
#include <memory>
#include <string>

LUAU_FASTFLAG(LuwuNonePrimitive)
LUAU_FASTFLAG(LuwuClasses)

using namespace Luau;

namespace
{

std::unique_ptr<lua_State, void (*)(lua_State*)> newState()
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    luaL_openlibs(state.get());
    luaopen_luwon(state.get());
    lua_pop(state.get(), 1);
    return state;
}

} // namespace

TEST_SUITE_BEGIN("LuwonLoad");

TEST_CASE("luwon_formats_numbers_like_the_vm")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();
    lua_State* L = state.get();

    const double numbers[] = {
        0.0, -0.0, 1.0, -1.0, 0.1, 1.0 / 3.0, 100.0, 1e15, 1e21, 1e22, 123456789012345680000.0, 1e-5, 1e-6, 1e-7,
        5e-324, 1.7976931348623157e308, 9007199254740993.0, 2.5e-10, 3.14159, -123.456, INFINITY, -INFINITY, NAN,
    };

    for (double number : numbers)
    {
        lua_pushnumber(L, number);
        std::string vm = luaL_tolstring(L, -1, nullptr);
        lua_pop(L, 2);

        CHECK_MESSAGE(formatLuwonNumber(number) == vm, "formatting " << vm);
    }
}

TEST_CASE("luwon_load_pushes_the_value")
{
    ScopedFastFlag none{FFlag::LuwuNonePrimitive, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();
    lua_State* L = state.get();

    const std::string source = R"(
const shared = { 1, 2 }
return { a = shared, b = shared, v = vector.create(1, 2, 3), empty = none }
)";
    REQUIRE(luwon_load(L, "@settings.luwon", source.data(), source.size()) == 0);
    REQUIRE(lua_istable(L, -1));
    CHECK(lua_gettop(L) == 1);

    // a const referenced twice is one table, as running the file would give
    lua_getfield(L, -1, "a");
    lua_getfield(L, -2, "b");
    CHECK(lua_rawequal(L, -1, -2));
    lua_rawgeti(L, -1, 2);
    CHECK(lua_tonumber(L, -1) == 2);
    lua_pop(L, 3);

    lua_getfield(L, -1, "v");
    CHECK(lua_isvector(L, -1));
    lua_pop(L, 1);

    lua_getfield(L, -1, "empty");
    CHECK(lua_issymnone(L, -1));
    lua_pop(L, 2);
}

TEST_CASE("luwon_load_reports_errors_with_the_chunk_name")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();
    lua_State* L = state.get();

    const std::string source = "const x = 1\nreturn string.rep(x)";
    REQUIRE(luwon_load(L, "@settings.luwon", source.data(), source.size()) == 1);
    CHECK(std::string(lua_tostring(L, -1)) == "settings.luwon:2: Only 'vector.create', 'buffer.fromstring' and classes can be called in a data file");
}


// Runs Luwu code and returns the string it returns, or the error it raised.
static std::string run(lua_State* L, const std::string& code)
{
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code.data(), code.size(), nullptr, &bytecodeSize);
    int loaded = luau_load(L, "=test", bytecode, bytecodeSize, 0);
    free(bytecode);
    if (loaded != 0 || lua_pcall(L, 0, 1, 0) != 0)
    {
        std::string error = lua_tostring(L, -1);
        lua_pop(L, 1);
        return "error: " + error;
    }

    std::string result = lua_isstring(L, -1) ? lua_tostring(L, -1) : "not a string";
    lua_pop(L, 1);
    return result;
}

TEST_CASE("luwon_tables_carry_their_comments_and_locations")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();

    CHECK(run(state.get(), R"LUA(
local config = luwon.decode("-- settings\nreturn {\n    -- workers\n    threads = 4, -- per core\n}\n")
assert(config.threads == 4)

local info = luwon.comments(config, "threads")
assert(info.above[1].text == "-- workers" and info.after.text == "-- per core")
assert(info.location.begin.x == 4 and info.location.begin.y == 15)
assert(info.keylocation.begin.y == 5)

-- Without a key: every comment in the file the table came from
assert(#luwon.comments(config) == 3)
assert(string.find(luwon.source(config), "-- settings", 1, true))

-- What comes back is a copy
info.above[1].text = "-- changed"
assert(luwon.comments(config, "threads").above[1].text == "-- workers")

assert(luwon.comments(config, "missing") == nil)
assert(luwon.comments({}, "threads") == nil)
return "ok"
)LUA") == "ok");
}

TEST_CASE("luwon_values_written_as_a_const_carry_its_binding")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();

    CHECK(run(state.get(), R"LUA(
local manifest = luwon.decode(
    "--- the registry's name\nconst name = \"chimera\"\n\n-- what chimera needs at runtime\nconst dependencies = {\n    json = \"1.0.0\",\n} -- pinned\n\n"
        .. "return {\n    name = name,\n    dependencies = dependencies,\n    url = \"x\",\n}\n"
)

local deps = luwon.comments(manifest, "dependencies").binding
assert(deps.name == "dependencies")
assert(deps.above[1].text == "-- what chimera needs at runtime" and deps.after.text == "-- pinned")
assert(deps.location.begin.x == 5)

assert(luwon.comments(manifest, "name").binding.above[1].text == "--- the registry's name")
assert(luwon.comments(manifest, "url").binding == nil)

-- setcomments keeps it
luwon.setcomments(manifest, "name", { above = { "the name" } })
assert(luwon.comments(manifest, "name").binding.name == "name")
return "ok"
)LUA") == "ok");
}

TEST_CASE("luwon_encode_edits_the_file_a_table_came_from")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();

    const char* expected = R"LUA(-- settings
const root = "./src"
return {
    -- how many workers
    threads = 8, -- per core, at most 64
    lib = `{root}/lib`,
    -- off in releases
    debug = true,
}
)LUA";

    const char* edit = R"LUA(
local config = luwon.decode("-- settings\nconst root = \"./src\"\nreturn {\n    -- workers\n    threads = 4, -- per core\n    lib = `{root}/lib`,\n}\n")
config.threads = 8
config.debug = true
luwon.setcomments(config, "threads", { above = { "how many workers" }, after = "per core, at most 64" })
luwon.setcomments(config, "debug", { above = { "-- off in releases" } })
return luwon.encode(config)
)LUA";
    CHECK(run(state.get(), edit) == expected);
}

TEST_CASE("luwon_comments_travel_with_a_table")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();

    // A part of a file's value, saved somewhere else, keeps its comments
    const char* part = R"LUA(
local config = luwon.decode("return {\n    deps = {\n        -- pinned\n        json = \"1.0.0\",\n    },\n}\n")
return luwon.encode({ dependencies = config.deps })
)LUA";
    CHECK(run(state.get(), part) == "return {\n    dependencies = {\n        -- pinned\n        json = \"1.0.0\",\n    },\n}\n");

    // Data that never came from a file can carry comments too
    const char* built = R"LUA(
local package = { name = "chimera" }
luwon.setcomments(package, "name", { above = { "the registry's name for it" }, after = "lowercase" })
return luwon.encode(package)
)LUA";
    CHECK(run(state.get(), built) == "return {\n    -- the registry's name for it\n    name = \"chimera\", -- lowercase\n}\n");

    // A table with its own metatable can't carry them
    CHECK(run(state.get(), "return luwon.setcomments(setmetatable({}, {}), 'a', {})").find("metatable of its own") != std::string::npos);
    CHECK(run(state.get(), "return luwon.encode({ a = setmetatable({}, {}) })") == "error: test:1: luwon.encode: a data file can't hold a table with a metatable, at value.a");
}

TEST_CASE("luwon_encode_writes_new_values")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();

    CHECK(run(state.get(), "return luwon.encode({ b = 2, a = { 1, 2 }, [true] = 0, [3] = 'x' })") == "return { [3] = \"x\", a = { 1, 2 }, b = 2, [true] = 0 }\n");
    CHECK(run(state.get(), "return luwon.encode({ f = print })") == "error: test:1: luwon.encode: a data file can't hold a function, at value.f");
}

TEST_CASE("luwon_classes_are_built_and_written_only_when_allowed")
{
    ScopedFastFlag classes{FFlag::LuwuClasses, true};

    std::unique_ptr<lua_State, void (*)(lua_State*)> state = newState();

    CHECK(run(state.get(), R"LUA(
class Point(public x: number, public y: number)
end

local allowed = { Point = { class = Point, args = function(p) return p.x, p.y end } }
local spawn = luwon.decode("return { origin = Point(1, 2) }", allowed)
assert(class.isinstance(spawn.origin, Point) and spawn.origin.y == 2)

assert(not pcall(luwon.decode, "return Point(1, 2)"))
assert(not pcall(luwon.encode, Point(1, 2)))
spawn.origin = Point(3, 4)
return luwon.encode(spawn, allowed)
)LUA") == "return { origin = Point(3, 4) }");
}

TEST_SUITE_END();
