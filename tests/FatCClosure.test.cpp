#include "lua.h"
#include "lualib.h"
#include "luacode.h"

#include "doctest.h"
#include "ScopedFlags.h"

#include <memory>
#include <stdlib.h>
#include <string.h>

LUAU_FASTFLAG(LuwuFatCClosure)

TEST_SUITE_BEGIN("FatCClosure");

static int test_dtor_calls = 0;

static void my_dtor(lua_State* L, void* data, size_t sz)
{
    test_dtor_calls++;
    CHECK(sz == sizeof(int));
    int* val = (int*)data;
    CHECK(*val == 1234);
    *val = 0;
}

static int my_closure(lua_State* L)
{
    void* data = lua_getcclosuredata(L);
    CHECK(data != nullptr);
    int* val = (int*)data;
    
    lua_pushinteger(L, *val);
    return 1;
}

using StateRef = std::unique_ptr<lua_State, void (*)(lua_State*)>;

TEST_CASE("FatCClosureCreationAndCall")
{
    ScopedFastFlag sff{FFlag::LuwuFatCClosure, true};
    StateRef state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    
    test_dtor_calls = 0;
    
    void* data = lua_pushcclosurewithdatak(L, my_closure, "my_closure", nullptr, sizeof(int), my_dtor);
    CHECK(data != nullptr);
    
    int* val = (int*)data;
    *val = 1234;
    
    lua_setglobal(L, "f");
    
    lua_getglobal(L, "f");
    lua_call(L, 0, 1);
    
    CHECK(lua_tointeger(L, -1) == 1234);
    lua_pop(L, 1);
    
    // Trigger GC to run the dtor
    lua_pushnil(L);
    lua_setglobal(L, "f");
    lua_gc(L, LUA_GCCOLLECT, 0);
    
    CHECK(test_dtor_calls == 1);
}

TEST_CASE("FatCClosureDtorOnClose")
{
    ScopedFastFlag sff{FFlag::LuwuFatCClosure, true};
    test_dtor_calls = 0;
    
    {
        StateRef state(luaL_newstate(), lua_close);
        lua_State* L = state.get();
        
        void* data = lua_pushcclosurewithdatak(L, my_closure, "my_closure", nullptr, sizeof(int), my_dtor);
        CHECK(data != nullptr);
        
        int* val = (int*)data;
        *val = 1234;
        
        lua_setglobal(L, "f");
        
        // Let the state be closed without manually triggering GC
    }
    
    CHECK(test_dtor_calls == 1);
}

TEST_CASE("FatCClosureDebugInfo")
{
    ScopedFastFlag sff{FFlag::LuwuFatCClosure, true};
    StateRef state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    
    lua_pushcclosurewithdatak(L, my_closure, "my_super_fat_closure", nullptr, sizeof(int), nullptr);
    
    lua_Debug ar;
    int res = lua_getinfo(L, -1, "sn", &ar);
    CHECK(res != 0);
    CHECK(strcmp(ar.what, "C") == 0);
    CHECK(strcmp(ar.name, "my_super_fat_closure") == 0);
}

static int my_plain_closure(lua_State* L)
{
    void* data = lua_getcclosuredata(L);
    CHECK(data == nullptr);
    return 0;
}

TEST_CASE("FatCClosureDataNullForPlain")
{
    ScopedFastFlag sff{FFlag::LuwuFatCClosure, true};
    StateRef state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    
    lua_pushcclosure(L, my_plain_closure, "my_plain_closure", 0);
    lua_call(L, 0, 0);
}


// Luwu: a data function gets its closure's data and its argument count, however it's called
static int data_function(lua_State* L, void* data, int nargs)
{
    lua_pushinteger(L, *(int*)data);
    lua_pushinteger(L, nargs);
    return 2;
}

TEST_CASE("DataCFunctionGetsItsDataAndArgumentCount")
{
    ScopedFastFlag sff{FFlag::LuwuFatCClosure, true};
    StateRef state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    luaL_openlibs(L);

    int* data = (int*)lua_pushdatacfunctionk(L, data_function, "data_function", nullptr, sizeof(int), nullptr);
    *data = 1234;
    lua_pushvalue(L, -1);
    lua_setglobal(L, "f");

    // through the C API
    lua_pushnil(L);
    lua_pushnil(L);
    lua_pushnil(L);
    lua_call(L, 3, 2);
    CHECK(lua_tointeger(L, -2) == 1234);
    CHECK(lua_tointeger(L, -1) == 3);
    lua_pop(L, 2);

    // from Luau code, and through a C function that calls it
    const char* source = "local a, n = f(1, 2) local b, m = pcall(f) return a + n, b, m";
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(source, strlen(source), nullptr, &bytecodeSize);
    REQUIRE(luau_load(L, "=test", bytecode, bytecodeSize, 0) == 0);
    free(bytecode);
    lua_call(L, 0, 3);
    CHECK(lua_tointeger(L, -3) == 1236);
    CHECK(lua_toboolean(L, -2) == 1);
    CHECK(lua_tointeger(L, -1) == 1234);

    // it's an ordinary C function to everything else
    lua_getglobal(L, "f");
    CHECK(lua_iscfunction(L, -1));
    lua_Debug ar;
    REQUIRE(lua_getinfo(L, -1, "sn", &ar) != 0);
    CHECK(strcmp(ar.name, "data_function") == 0);
}

struct RawPcallData
{
    bool fail;
};

static void raw_pcall_body(lua_State* L, void* ud)
{
    // the arguments are where the caller left them
    CHECK(lua_tointeger(L, -1) == 2);
    lua_pushinteger(L, 3);
    if (static_cast<RawPcallData*>(ud)->fail)
        luaL_error(L, "boom");
}

TEST_CASE("RawPcallRunsOnTheCallersStack")
{
    StateRef state(luaL_newstate(), lua_close);
    lua_State* L = state.get();

    RawPcallData data = {false};
    lua_pushinteger(L, 1);
    lua_pushinteger(L, 2);
    CHECK(lua_rawpcall(L, 2, raw_pcall_body, &data) == LUA_OK);
    REQUIRE(lua_gettop(L) == 3);
    CHECK(lua_tointeger(L, 1) == 1);
    CHECK(lua_tointeger(L, 3) == 3);
    lua_settop(L, 0);

    // an error drops the arguments and leaves the error object, and the state stays usable
    data.fail = true;
    lua_pushinteger(L, 10);
    lua_pushinteger(L, 1);
    lua_pushinteger(L, 2);
    CHECK(lua_rawpcall(L, 2, raw_pcall_body, &data) == LUA_ERRRUN);
    REQUIRE(lua_gettop(L) == 2);
    CHECK(lua_tointeger(L, 1) == 10);
    CHECK(strstr(lua_tostring(L, 2), "boom") != nullptr);

    data.fail = false;
    lua_settop(L, 0);
    lua_pushinteger(L, 1);
    lua_pushinteger(L, 2);
    CHECK(lua_rawpcall(L, 2, raw_pcall_body, &data) == LUA_OK);
    CHECK(lua_gettop(L) == 3);
}

TEST_SUITE_END();
