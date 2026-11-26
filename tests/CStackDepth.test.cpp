// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lua.h"
#include "lualib.h"
#include "luacode.h"
#include "doctest.h"

#include <cstring>
#include <memory>
#include <string>

TEST_SUITE_BEGIN("CStackDepth");

static int reserveUnits(lua_State* L)
{
    int units = luaL_checkinteger(L, 1);
    int before = lua_cstackdepth(L);
    lua_cstackreserve(L, units);
    int during = lua_cstackdepth(L);

    int status = 0;
    if (lua_isfunction(L, 2))
    {
        lua_pushvalue(L, 2);
        status = lua_pcall(L, 0, 0, 0);
        if (status != 0)
            lua_pop(L, 1);
    }

    lua_cstackrelease(L, units);
    lua_pushinteger(L, during - before);
    lua_pushboolean(L, status == 0);
    lua_pushinteger(L, lua_cstackdepth(L) - before);
    return 3;
}

static int currentDepth(lua_State* L)
{
    lua_pushinteger(L, lua_cstackdepth(L));
    return 1;
}

static int depthLimit(lua_State* L)
{
    lua_pushinteger(L, lua_cstacklimit(L));
    return 1;
}

static int runScript(lua_State* L, const char* code)
{
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code, strlen(code), nullptr, &bytecodeSize);
    int result = luau_load(L, "=chunk", bytecode, bytecodeSize, 0);
    free(bytecode);
    if (result == 0)
        result = lua_pcall(L, 0, 0, 0);
    if (result != 0)
    {
        MESSAGE(lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    return result;
}

TEST_CASE("CStackReserveAndRelease")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    luaL_openlibs(L);

    lua_pushcfunction(L, reserveUnits, "reserve");
    lua_setglobal(L, "reserve");
    lua_pushcfunction(L, currentDepth, "depth");
    lua_setglobal(L, "depth");

    int outside = lua_cstackdepth(L);
    CHECK(outside == 0);

    const char* script = R"LUA(
        local base = depth()

        local held, ok, after = reserve(0)
        assert(held == 0 and ok and after == 0)

        held, ok, after = reserve(10)
        assert(held == 10 and ok and after == 0)
        assert(depth() == base)

        local inside
        held, ok, after = reserve(25, function()
            inside = depth()
        end)
        assert(ok and after == 0)
        assert(inside > base + 25)

        local nested
        reserve(40, function()
            reserve(40, function()
                nested = depth()
            end)
        end)
        assert(nested > base + 80)
        assert(depth() == base)
    )LUA";

    CHECK_EQ(runScript(L, script), 0);
    CHECK(lua_cstackdepth(L) == outside);
}

TEST_CASE("CStackReserveOverflow")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    luaL_openlibs(L);

    lua_pushcfunction(L, reserveUnits, "reserve");
    lua_setglobal(L, "reserve");
    lua_pushcfunction(L, currentDepth, "depth");
    lua_setglobal(L, "depth");
    lua_pushcfunction(L, depthLimit, "limit");
    lua_setglobal(L, "limit");

    CHECK(lua_cstacklimit(L) == LUAI_MAXCCALLS);

    const char* script = R"LUA(
        local base = depth()

        assert(limit() > base)
        local room = limit() - depth()
        assert(pcall(reserve, room - 2) == true)
        assert(pcall(reserve, room) == false)
        assert(depth() == base)

        local ok, err = pcall(reserve, 10000)
        assert(not ok and string.find(tostring(err), "C stack overflow", 1, true))
        assert(depth() == base)

        ok, err = pcall(reserve, limit())
        assert(not ok and string.find(tostring(err), "C stack overflow", 1, true))
        assert(depth() == base)

        local innerOk, innerErr
        local held, completed, after = reserve(120, function()
            innerOk, innerErr = pcall(reserve, 120)
        end)
        assert(held == 120 and completed and after == 0)
        assert(not innerOk and string.find(tostring(innerErr), "C stack overflow", 1, true))
        assert(depth() == base)

        held, completed, after = reserve(150, function()
            error("boom")
        end)
        assert(held == 150 and not completed and after == 0)
        assert(depth() == base)

        local level = 0
        local function recurse()
            level += 1
            local a, b = reserve(30, recurse)
            return a
        end
        recurse()
        assert(level >= 4 and level <= 8)
        assert(depth() == base)

        local viaCoroutine
        reserve(100, function()
            local co = coroutine.wrap(function()
                viaCoroutine = pcall(reserve, 110)
            end)
            co()
        end)
        assert(viaCoroutine == false)
        assert(depth() == base)
    )LUA";

    CHECK_EQ(runScript(L, script), 0);
    CHECK(lua_cstackdepth(L) == 0);
}

TEST_SUITE_END();
