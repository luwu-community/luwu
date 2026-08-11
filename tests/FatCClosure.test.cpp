#include "lua.h"
#include "lualib.h"

#include "doctest.h"
#include "ScopedFlags.h"

#include <memory>
#include <string.h>

LUAU_FASTFLAG(LuauFatCClosure)

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
    ScopedFastFlag sff{FFlag::LuauFatCClosure, true};
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
    ScopedFastFlag sff{FFlag::LuauFatCClosure, true};
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
    ScopedFastFlag sff{FFlag::LuauFatCClosure, true};
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
    ScopedFastFlag sff{FFlag::LuauFatCClosure, true};
    StateRef state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    
    lua_pushcclosure(L, my_plain_closure, "my_plain_closure", 0);
    lua_call(L, 0, 0);
}

TEST_SUITE_END();
