#include "lua.h"
#include "lualib.h"
#include "luacodegen.h"
#include "luacode.h"

#include "doctest.h"

#include <memory>
#include <string.h>

static int s_externalStringFreeCount = 0;
static void test_string_free_cb(lua_State* L, const char* data, size_t sz, void* userdata)
{
    s_externalStringFreeCount++;
}

static int dostring(lua_State* L, const char* code)
{
    size_t bytecodeSize = 0;
    char* bytecode = luau_compile(code, strlen(code), NULL, &bytecodeSize);
    if (!bytecode) return -1;
    int result = luau_load(L, "chunk", bytecode, bytecodeSize, 0);
    free(bytecode);
    if (result == 0)
    {
        return lua_pcall(L, 0, 0, 0);
    }
    return result;
}

TEST_SUITE_BEGIN("ExternalStrings");

TEST_CASE("ExternalStringBasic")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    lua_State* L = state.get();
    luaL_openlibs(L);

    s_externalStringFreeCount = 0;
    
    const char* my_string = "hello external world";
    size_t len = strlen(my_string);
    int test_userdata = 42;

    lua_newexternalstring(L, my_string, len, &test_userdata, test_string_free_cb);
    
    CHECK(lua_isstringexternal(L, -1) == 1);
    CHECK(lua_getstringexternaluserdata(L, -1) == &test_userdata);
    
    lua_setglobal(L, "ext_str");

    // Operations in Lua
    const char* lua_code = R"(
        assert(ext_str == "hello external world")
        assert(string.len(ext_str) == 20)
        assert(string.sub(ext_str, 1, 5) == "hello")
        
        local t = {}
        t[ext_str] = "value"
        assert(t["hello external world"] == "value")
    )";
    
    CHECK(dostring(L, lua_code) == 0);

    // Free the state to trigger GC which should call the free_cb
    state.reset();
    CHECK(s_externalStringFreeCount == 1);
}

TEST_CASE("ExternalStringDedup")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    lua_State* L = state.get();

    s_externalStringFreeCount = 0;
    
    // Push normal string first
    lua_pushstring(L, "hello");
    
    // Now push external string with same content
    const char* my_string = "hello";
    lua_newexternalstring(L, my_string, 5, nullptr, test_string_free_cb);
    
    // Should be exactly the same pointer due to interning
    CHECK(lua_topointer(L, -1) == lua_topointer(L, -2));
    
    // Because it was deduped against an inline string, it is NOT external!
    CHECK(lua_isstringexternal(L, -1) == 0);
    
    // The free callback should have been called immediately
    CHECK(s_externalStringFreeCount == 1);
    
    state.reset();
    
    // Shouldn't be called again
    CHECK(s_externalStringFreeCount == 1);
}

TEST_CASE("ExternalStringDedupReverse")
{
    std::unique_ptr<lua_State, void (*)(lua_State*)> state(luaL_newstate(), lua_close);
    lua_State* L = state.get();

    s_externalStringFreeCount = 0;
    
    // Push external string first
    const char* my_string = "hello";
    lua_newexternalstring(L, my_string, 5, nullptr, test_string_free_cb);
    
    CHECK(lua_isstringexternal(L, -1) == 1);
    
    // Now push normal string with same content
    lua_pushstring(L, "hello");
    
    // Should be exactly the same pointer
    CHECK(lua_topointer(L, -1) == lua_topointer(L, -2));
    
    // And it should still be external
    CHECK(lua_isstringexternal(L, -1) == 1);
    
    state.reset();
    
    // Callback called during GC
    CHECK(s_externalStringFreeCount == 1);
}

TEST_SUITE_END();
