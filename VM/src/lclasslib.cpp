#include "lapi.h"
#include "lbytecode.h"
#include "lobject.h"
#include "lstring.h"
#include "lua.h"
#include "lualib.h"
#include "lstate.h"

#include "Luau/Common.h"

LUAU_FASTFLAG(LuauNonePrimitive)

static int class_isinstance(lua_State* L)
{
    luaL_checkany(L, 1);
    luaL_checktype(L, 2, LUA_TCLASS);
    const TValue* inst = luaA_toobject(L, 1);
    const TValue* obj = luaA_toobject(L, 2);
    const LuauClass* lclass = classvalue(obj);
    bool isInstance = ttisobject(inst) && objectvalue(inst)->lclass == lclass;
    lua_pushboolean(L, isInstance);
    return 1;
}

static int class_classof(lua_State* L)
{
    luaL_checkany(L, 1);
    if (!lua_isobject(L, 1))
    {
        lua_pushnil(L);
        return 1;
    }
    const TValue* inst = luaA_toobject(L, 1);
    const LuauObject* ci = objectvalue(inst);
    luaA_pushclass(L, ci->lclass);
    return 1;
}

static int class_fields(lua_State* L)
{
    luaL_checkany(L, 1);

    const LuauClass* lclass;
    const LuauObject* object = NULL;
    if (lua_isclass(L, 1))
    {
        lclass = classvalue(luaA_toobject(L, 1));
    }
    else if (lua_isobject(L, 1))
    {
        object = objectvalue(luaA_toobject(L, 1));
        lclass = object->lclass;
    }
    else
    {
        luaL_argerror(L, 1, "class or object expected");
    }

    lua_createtable(L, 0, lclass->numberofinstancemembers);

    bool complete = true;
    for (uint32_t offset = 0; offset < lclass->numberofinstancemembers; offset++)
    {
        if (lclass->memberflags[offset] & LBC_CLASSMEMBER_PRIVATE)
        {
            complete = false;
            continue;
        }

        lua_pushstring(L, getstr(lclass->offsettomember[offset]));

        const TValue* value = object ? &object->members[offset] : NULL;
        if (value && !ttisnil(value))
            luaA_pushvalue(L, value);
        else if (FFlag::LuauNonePrimitive)
            lua_pushsymnone(L);
        else
            lua_pushnil(L);

        lua_rawset(L, -3);
    }

    lua_pushboolean(L, complete);
    return 2;
}

static const luaL_Reg classlib[] = {
    {"isinstance", class_isinstance},
    {"classof", class_classof},
    {"fields", class_fields},
    {nullptr, nullptr},
};

/*
** Open class library
*/
int luaopen_class(lua_State* L)
{
    luaL_register(L, LUA_CLASSLIBNAME, classlib);
    return 1;
}