#include "lapix.h"
#include "lapi.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "Luau/Common.h"

#include <stdexcept>
#include <string.h>

using namespace std;

LUA_API const void* lua_getmetatablepointer(lua_State* L, int objindex)
{
    const TValue* obj = luaA_toobject(L, objindex);
    if (!obj)
        return NULL;

    switch (ttype(obj))
    {
    case LUA_TTABLE:
        return hvalue(obj)->metatable;
    case LUA_TUSERDATA:
        return uvalue(obj)->metatable;
    default:
        return NULL;
    }
}

LUA_API const char* lua_gcstatename(int state)
{
    return luaC_statename(state);
}

LUA_API int64_t lua_gcallocationrate(lua_State* L) {
    return luaC_allocationrate(L);
}

LUA_API void lua_gcdump(lua_State* L, void* file, const char* (*categoryName)(lua_State* L, uint8_t memcat))
{
    luaC_dump(L, file, categoryName);
}

LUA_API int luau_setfflag(const char* name, int value)
{
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
    {
        if (strcmp(flag->name, name) == 0)
        {
            flag->value = value;
            return 1;
        }
    }
    return 0;
}

LUA_API int luau_getfflag(const char* name)
{
    for (Luau::FValue<bool>* flag = Luau::FValue<bool>::list; flag; flag = flag->next)
    {
        if (strcmp(flag->name, name) == 0)
        {
            return flag->value ? 1 : 0;
        }
    }
    return -1;
}

int lua_findunuseduserdatatag(lua_State *L)
{
    global_State* global = L->global;

    for (int tag = 0; tag < LUA_UTAG_LIMIT; tag++)
    {
        if (!global->udatamt[tag] && !global->udatagc[tag] && !global->udatadirectfields[tag])
            return tag;
    }

    return -1;
}

int lua_findunusedlightuserdatatag(lua_State *L)
{
    global_State* global = L->global;

    for (int tag = 0; tag < LUA_LUTAG_LIMIT; tag++)
    {
        if (!global->lightuserdataname[tag])
            return tag;
    }

    return -1;
}

LUA_API const char* lua_namecallwithlen(lua_State* L, int* atom, size_t* len)
{
    TString* s = L->namecall;
    if (!s)
        return NULL;
    if (atom)
    {
        luaS_updateatom(L, s);
        *atom = s->atom;
    }
    if (len)
        *len = s->len;
    return getstr(s);
}

