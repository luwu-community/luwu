// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lualib.h"

#include "lapi.h"
#include "lclass.h"
#include "ldebug.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "lvm.h"

#include <stdio.h>
#include <string.h>

// Luwu try expressions: options and results, the builtin classes `Some`, `Ok` and `Err` (luaR_newnativeclass) and the
// `None` global. A full option is a `Some` object and an empty one is the `none` primitive itself, so an empty option
// allocates nothing; `None` is the same value. `none` gets `get` and `expect` through its type's metatable, whose
// `__index` only runs when code indexes `none`, which is otherwise an error: nothing else `none` does gets slower. A
// result is an `Ok` or an `Err` object. Each holds its value in its first field. An `Err` also has where its error was
// raised, which a `try` expression fills in.

static LuauClass* builtinclass(lua_State* L, LuwuBuiltinClass kind)
{
    return L->global->builtinclasses[kind];
}

static bool isnovalue(const TValue* value)
{
    return ttisnil(value) || ttissymnone(value);
}

// The value of a `Some`, `Ok` or `Err`, readable as `value` (`error` for `Err`)
static const uint8_t kPayloadField = LBC_CLASSMEMBER_CONST;

// The first field of a `Some`, `Ok` or `Err`
static const int kPayload = 0;
// `Err`'s position fields, after its error
static const int kErrModule = 1;
static const int kErrLine = 2;
static const int kErrTraceback = 3;

template<LuwuBuiltinClass Kind>
static LuauObject* checkpayload(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, builtinclass(L, Kind), method);
}

// `__init` for `Ok`, which holds any value
static int payloadinit(lua_State* L)
{
    LuauObject* holder = objectvalue(L->base);
    setobj(L, &luaR_members(holder)[kPayload], L->base + 1);
    luaC_barrier(L, holder, L->base + 1);
    return 0;
}

static void formatsomemissing(lua_State* L, char (&message)[LUA_BUFFERSIZE], int type)
{
    snprintf(message, sizeof(message), "Some needs a value, but you passed %s", getstr(L->global->ttname[type]));
}

// `__init` runs under the class's C constructor, so its error is raised at the Lua code constructing
static int someinit(lua_State* L)
{
    if (lua_gettop(L) < 2 || isnovalue(L->base + 1))
    {
        char message[LUA_BUFFERSIZE];
        formatsomemissing(L, message, lua_gettop(L) < 2 ? LUA_TNIL : ttype(L->base + 1));
        luaG_runerroratlua(L, message);
    }
    return payloadinit(L);
}

LuauObject* luaR_newpayload(lua_State* L, LuwuBuiltinClass kind, const TValue* value)
{
    LuauClass* classdef = builtinclass(L, kind);
    if (!classdef)
        luaG_runerror(L, "open the result library (luaopen_result) before building an Ok, Err or Some");

    if (kind == LUWU_BUILTIN_SOME && isnovalue(value))
    {
        char message[LUA_BUFFERSIZE];
        formatsomemissing(L, message, ttype(value));
        luaG_runerror(L, "%s", message);
    }

    // A new object is white, so storing into it needs no barrier
    LuauObject* object = luaR_newobjectuninit(L, classdef);
    TValue* members = luaR_members(object);
    setobj(L, &members[kPayload], value);
    for (int i = kPayload + 1; i < int(object->numberofmembers); i++)
        setnilvalue(&members[i]);
    return object;
}

const TValue* luaR_payload(lua_State* L, const TValue* value)
{
    if (!ttisobject(value))
        return NULL;

    LuauObject* object = objectvalue(value);
    bool holdsPayload = object->lclass == builtinclass(L, LUWU_BUILTIN_SOME) || object->lclass == builtinclass(L, LUWU_BUILTIN_OK) ||
                        object->lclass == builtinclass(L, LUWU_BUILTIN_ERR);
    return holdsPayload ? &luaR_members(object)[kPayload] : NULL;
}

// `get` of a `Some` and `ok` of an `Ok`: the value
template<LuwuBuiltinClass Kind>
static int payloadget(lua_State* L)
{
    const char* method = Kind == LUWU_BUILTIN_SOME ? "get" : "ok";
    luaA_pushvalue(L, &luaR_members(checkpayload<Kind>(L, method))[kPayload]);
    return 1;
}

template<LuwuBuiltinClass Kind>
static int payloadexpect(lua_State* L)
{
    LuauObject* holder = checkpayload<Kind>(L, "expect");
    luaL_optstring(L, 2, NULL);
    luaA_pushvalue(L, &luaR_members(holder)[kPayload]);
    return 1;
}

template<LuwuBuiltinClass Kind>
static int payloadeq(lua_State* L)
{
    LuauObject* holder = checkpayload<Kind>(L, "__eq");
    const TValue* other = L->base + 1;
    bool equal = ttisobject(other) && objectvalue(other)->lclass == holder->lclass &&
                 equalobj(L, &luaR_members(holder)[kPayload], &luaR_members(objectvalue(other))[kPayload]);
    lua_pushboolean(L, equal);
    return 1;
}

// `__tostring`: the constructor call, `Some(1)`. An `Err` adds where its error was raised when it knows, and the
// traceback on the lines after.
template<LuwuBuiltinClass Kind>
static int payloadtostring(lua_State* L)
{
    LuauObject* holder = checkpayload<Kind>(L, "__tostring");
    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, getstr(holder->lclass->name));
    luaL_addchar(&b, '(');
    luaA_pushvalue(L, &luaR_members(holder)[kPayload]);
    luaR_addshowntop(&b);
    luaL_addchar(&b, ')');

    if (Kind == LUWU_BUILTIN_ERR)
    {
        const TValue* members = luaR_members(holder);
        if (ttisstring(&members[kErrModule]))
        {
            luaL_addstring(&b, " at ");
            luaL_addstring(&b, svalue(&members[kErrModule]));
            if (ttisnumber(&members[kErrLine]))
            {
                char line[32];
                snprintf(line, sizeof(line), ":%d", int(nvalue(&members[kErrLine])));
                luaL_addstring(&b, line);
            }
        }
        if (ttisstring(&members[kErrTraceback]))
        {
            luaL_addchar(&b, '\n');
            luaL_addstring(&b, svalue(&members[kErrTraceback]));
        }
    }

    luaL_pushresult(&b);
    return 1;
}

// `err` of an `Ok`: there's no error
static int okerr(lua_State* L)
{
    checkpayload<LUWU_BUILTIN_OK>(L, "err");
    lua_pushsymnone(L);
    return 1;
}

// `ok` of an `Err`: there's no value
static int errok(lua_State* L)
{
    checkpayload<LUWU_BUILTIN_ERR>(L, "ok");
    lua_pushsymnone(L);
    return 1;
}

static int errerr(lua_State* L)
{
    luaA_pushvalue(L, &luaR_members(checkpayload<LUWU_BUILTIN_ERR>(L, "err"))[kPayload]);
    return 1;
}

// `expect` of an `Err` raises: the message and the error, or without a message the error itself, which keeps an error
// that isn't a string intact
static int errexpect(lua_State* L)
{
    LuauObject* failed = checkpayload<LUWU_BUILTIN_ERR>(L, "expect");
    const char* message = luaL_optstring(L, 2, NULL);
    luaA_pushvalue(L, &luaR_members(failed)[kPayload]);
    if (!message)
        lua_error(L);

    luaL_error(L, "%s: %s", message, luaL_tolstring(L, -1, NULL));
}

// The fast methods of `Some`, `Ok` and `Err` (luaR_FastMethod): each gives the one field or `none`

static int payloadgetfast(lua_State* L, LuauObject* holder, StkId res, StkId args, int nargs)
{
    setobj2s(L, res, &luaR_members(holder)[kPayload]);
    return 1;
}

// `expect` of a `Some` or an `Ok`. A message that isn't a string is left to the C method, which converts or refuses it.
static int payloadexpectfast(lua_State* L, LuauObject* holder, StkId res, StkId args, int nargs)
{
    if (nargs >= 1 && !ttisnil(args) && !ttisstring(args))
        return -1;
    return payloadgetfast(L, holder, res, args, nargs);
}

static int nonefast(lua_State* L, LuauObject* holder, StkId res, StkId args, int nargs)
{
    setsymnonevalue(res);
    return 1;
}

static const luaR_NativeMember kSomeMembers[] = {
    {"value", NULL, kPayloadField},
    {"get", payloadget<LUWU_BUILTIN_SOME>, 0, payloadgetfast},
    {"expect", payloadexpect<LUWU_BUILTIN_SOME>, 0, payloadexpectfast},
    {"__eq", payloadeq<LUWU_BUILTIN_SOME>, 0},
    {"__tostring", payloadtostring<LUWU_BUILTIN_SOME>, 0},
};

static const luaR_NativeMember kOkMembers[] = {
    {"value", NULL, kPayloadField},
    {"ok", payloadget<LUWU_BUILTIN_OK>, 0, payloadgetfast},
    {"expect", payloadexpect<LUWU_BUILTIN_OK>, 0, payloadexpectfast},
    {"err", okerr, 0, nonefast},
    {"__eq", payloadeq<LUWU_BUILTIN_OK>, 0},
    {"__tostring", payloadtostring<LUWU_BUILTIN_OK>, 0},
};

static const luaR_NativeMember kErrMembers[] = {
    {"error", NULL, kPayloadField},
    {"module", NULL, kPayloadField},
    {"line", NULL, kPayloadField},
    {"traceback", NULL, kPayloadField},
    {"ok", errok, 0, nonefast},
    {"expect", errexpect, 0},
    {"err", errerr, 0, payloadgetfast},
    {"__eq", payloadeq<LUWU_BUILTIN_ERR>, 0},
    {"__tostring", payloadtostring<LUWU_BUILTIN_ERR>, 0},
};

// `none:get()`: an empty option has no value, so `none`
static int noneget(lua_State* L)
{
    lua_pushsymnone(L);
    return 1;
}

// `none:expect(message?)` raises: an empty option has no value
static int noneexpect(lua_State* L)
{
    const char* message = luaL_optstring(L, 2, NULL);
    if (message)
        luaL_error(L, "%s", message);
    luaL_error(L, "expected a value, but got none");
}

// `none`'s `__index`: `get` and `expect`, kept as its upvalues, and the usual index error for anything else
static int noneindex(lua_State* L)
{
    const char* key = lua_isstring(L, 2) ? lua_tostring(L, 2) : NULL;
    if (key && strcmp(key, "get") == 0)
    {
        lua_pushvalue(L, lua_upvalueindex(1));
        return 1;
    }
    if (key && strcmp(key, "expect") == 0)
    {
        lua_pushvalue(L, lua_upvalueindex(2));
        return 1;
    }
    luaG_indexerroratlua(L, L->base, L->base + 1);
}

static void setnonemetatable(lua_State* L)
{
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, noneget, "get");
    lua_pushcfunction(L, noneexpect, "expect");
    lua_pushcclosure(L, noneindex, "__index", 2);
    lua_setfield(L, -2, "__index");
    lua_setreadonly(L, -1, true);

    lua_pushsymnone(L);
    lua_insert(L, -2);
    lua_setmetatable(L, -2);
    lua_pop(L, 1);
}


// Builds a class, records it as builtin `kind` and makes it the global `name`
template<size_t N>
static void addclass(lua_State* L, LuwuBuiltinClass kind, const char* name, const luaR_NativeMember (&members)[N], lua_CFunction init)
{
    L->global->builtinclasses[kind] = luaR_newnativeclass(L, name, members, int(N), init);
    lua_setglobal(L, name);
}

int luaopen_result(lua_State* L)
{
    addclass(L, LUWU_BUILTIN_SOME, "Some", kSomeMembers, someinit);
    addclass(L, LUWU_BUILTIN_OK, "Ok", kOkMembers, payloadinit);
    addclass(L, LUWU_BUILTIN_ERR, "Err", kErrMembers, luaR_fieldsinit);

    lua_pushsymnone(L);
    lua_setglobal(L, "None");

    setnonemetatable(L);
    return 0;
}
