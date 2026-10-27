// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lualib.h"

#include "lapi.h"
#include "lbuffer.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lvm.h"

#include <algorithm>

#include <stdio.h>
#include <string.h>

// Luwu collections: the `collections` library, builtin classes written in C++ (luaR_newnativeclass). Each method
// checks its receiver against the class in global_State::builtinclasses, then reads the object's fields by offset.

static LuauClass* builtinclass(lua_State* L, LuwuBuiltinClass kind)
{
    return L->global->builtinclasses[kind];
}

static void setfield(lua_State* L, LuauObject* object, int field, const TValue* value)
{
    setobj(L, &object->members[field], value);
    luaC_barrier(L, object, value);
}

// How a value reads inside a constructor call or an error: strings quoted, everything else as `tostring` gives it.
// Only for a buffer nothing sits above on the stack: growing one moves its storage to the top.
static void addshown(luaL_Strbuf* b, int idx)
{
    lua_State* L = b->L;
    if (lua_type(L, idx) != LUA_TSTRING)
    {
        luaL_tolstring(L, idx, NULL);
        luaL_addvalue(b);
        return;
    }

    size_t len = 0;
    const char* s = lua_tolstring(L, idx, &len);
    luaL_addchar(b, '"');
    for (size_t i = 0; i < len; i++)
    {
        unsigned char c = s[i];
        if (c == '"' || c == '\\')
        {
            luaL_addchar(b, '\\');
            luaL_addchar(b, c);
        }
        else if (c == '\n')
        {
            luaL_addstring(b, "\\n");
        }
        else if (c < 32 || c == 127)
        {
            char escape[8];
            snprintf(escape, sizeof(escape), "\\%03d", c);
            luaL_addstring(b, escape);
        }
        else
        {
            luaL_addchar(b, c);
        }
    }
    luaL_addchar(b, '"');
}

// Pushes the value at `idx` as addshown writes it
static const char* pushshown(lua_State* L, int idx)
{
    idx = lua_absindex(L, idx);
    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    addshown(&b, idx);
    luaL_pushresult(&b);
    return lua_tostring(L, -1);
}

// Pops the value on top of the stack and adds it to `b` as addshown writes it. The value is turned into its text
// first, so `b`'s storage is right below what's added.
static void addshowntop(luaL_Strbuf* b)
{
    lua_State* L = b->L;
    pushshown(L, -1);
    lua_remove(L, -2);
    luaL_addvalue(b);
}

// Whether `position` is a whole number from 1 to `last`
static bool isposition(double position, uint32_t last)
{
    return position == double(int64_t(position)) && position >= 1 && position <= double(last);
}

// The position at stack `idx`; raises unless it's a whole number from 1 to `last`
static uint32_t checkposition(lua_State* L, int idx, uint32_t last)
{
    double position = luaL_checknumber(L, idx);
    if (!isposition(position, last))
        luaL_error(L, "position %s is out of range 1 to %d", pushshown(L, idx), int(last));
    return uint32_t(position);
}

// vec and vecdeque hold one type of value: the first value one gets fixes it, and a value of any other type raises.
// nil never goes in. A type is a primitive type, an object's class, or a builtin enum.
//
// Builtin enums are types whose values come from several variants, classes or primitives: an `option` is a `some`
// object or `none`, a `result` an `ok` or an `err` object. A container of an enum takes all of its variants. Enums
// aren't user facing yet.

enum BuiltinEnum
{
    NOT_AN_ENUM,
    ENUM_OPTION,
    ENUM_RESULT,
};

// How an error names the values of each enum, by BuiltinEnum
static const char* const kEnumPlurals[] = {NULL, "options", "results"};

static bool isnovalue(const TValue* value)
{
    return ttisnil(value) || ttissymnone(value);
}

static BuiltinEnum enumof(lua_State* L, const TValue* value)
{
    if (ttissymnone(value))
        return ENUM_OPTION;
    if (!ttisobject(value))
        return NOT_AN_ENUM;

    const LuauClass* classdef = objectvalue(value)->lclass;
    if (classdef == builtinclass(L, LUWU_BUILTIN_SOME))
        return ENUM_OPTION;
    if (classdef == builtinclass(L, LUWU_BUILTIN_OK) || classdef == builtinclass(L, LUWU_BUILTIN_ERR))
        return ENUM_RESULT;
    return NOT_AN_ENUM;
}

// Sets `type` to the element type of `value`: its builtin enum (a number past the type tags), its class, or its type
// tag
static void elementtypeof(lua_State* L, TValue* type, const TValue* value)
{
    BuiltinEnum builtinenum = enumof(L, value);
    if (builtinenum != NOT_AN_ENUM)
    {
        setnvalue(type, double(LUA_T_COUNT + builtinenum));
    }
    else if (ttisobject(value))
    {
        setclassvalue(L, type, objectvalue(value)->lclass);
    }
    else
    {
        setnvalue(type, double(ttype(value)));
    }
}

static bool hastype(lua_State* L, const TValue* elementtype, const TValue* value)
{
    TValue type;
    elementtypeof(L, &type, value);
    if (ttisclass(elementtype))
        return ttisclass(&type) && classvalue(&type) == classvalue(elementtype);
    return ttisnumber(&type) && nvalue(&type) == nvalue(elementtype);
}

// Writes how an error names the values of type `elementtype`: "strings", "'Player' objects", "options"
static void addtypeplural(luaL_Strbuf* b, const TValue* elementtype)
{
    if (ttisclass(elementtype))
    {
        luaL_addchar(b, '\'');
        luaL_addstring(b, getstr(classvalue(elementtype)->name));
        luaL_addstring(b, "' objects");
        return;
    }

    int type = int(nvalue(elementtype));
    if (type >= LUA_T_COUNT)
    {
        luaL_addstring(b, kEnumPlurals[type - LUA_T_COUNT]);
        return;
    }

    const char* name = getstr(b->L->global->ttname[type]);
    luaL_addstring(b, name);
    if (strcmp(name, "userdata") == 0)
        luaL_addstring(b, " values");
    else if (name[strlen(name) - 1] == 's')
        luaL_addstring(b, "es");
    else
        luaL_addchar(b, 's');
}

// Writes how an error names `value`: "a table", "an 'Account' object", "nil", "none"
static void addvaluetype(luaL_Strbuf* b, const TValue* value)
{
    if (isnovalue(value))
    {
        luaL_addstring(b, getstr(b->L->global->ttname[ttype(value)]));
        return;
    }

    bool isobject = ttisobject(value);
    const char* name = isobject ? getstr(objectvalue(value)->lclass->name) : getstr(b->L->global->ttname[ttype(value)]);
    luaL_addstring(b, name[0] && strchr("aeiouAEIOU", name[0]) ? "an " : "a ");
    if (isobject)
        luaL_addchar(b, '\'');
    luaL_addstring(b, name);
    if (isobject)
        luaL_addstring(b, "' object");
}

// Checks the value at stack `idx` can go in `container`, whose element type is in field `typefield`; the first value
// fixes the type
static void checkelement(lua_State* L, LuauObject* container, int typefield, int idx)
{
    const TValue* value = L->base + idx - 1;
    const TValue* elementtype = &container->members[typefield];
    const char* containername = getstr(container->lclass->name);

    if (ttisnil(elementtype))
    {
        if (ttisnil(value))
            luaL_error(L, "this %s can't accept nil", containername);

        TValue type;
        elementtypeof(L, &type, value);
        setfield(L, container, typefield, &type);
        return;
    }

    if (hastype(L, elementtype, value))
        return;

    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, "this ");
    luaL_addstring(&b, containername);
    luaL_addstring(&b, " can only accept ");
    addtypeplural(&b, elementtype);
    luaL_addstring(&b, ", but you passed ");
    addvaluetype(&b, value);
    luaL_pushresult(&b);
    luaL_error(L, "%s", lua_tostring(L, -1));
}

// The constructor's optional list: an `__init` adds entries 1 to #items of a table in its argument 2, each with `push`
// so it's checked like any other value
static void containerfill(lua_State* L, LuauObject* container, lua_CFunction push, const char* pushname)
{
    if (lua_isnoneornil(L, 2))
        return;

    if (!lua_istable(L, 2))
    {
        luaL_Strbuf b;
        luaL_buffinit(L, &b);
        luaL_addstring(&b, getstr(container->lclass->name));
        luaL_addstring(&b, "() takes a list of values, but you passed ");
        addvaluetype(&b, L->base + 1);
        luaL_pushresult(&b);
        luaL_error(L, "%s", lua_tostring(L, -1));
    }

    int count = lua_objlen(L, 2);
    for (int i = 1; i <= count; i++)
    {
        lua_pushcfunction(L, push, pushname);
        lua_pushvalue(L, 1);
        lua_rawgeti(L, 2, i);
        lua_call(L, 2, 0);
    }
}

// `__tostring`: the call that builds an equal container, `vec({1, 2})`, or `vec()` when it's empty. `at` reads an
// entry by position.
static int containertostring(lua_State* L, LuauObject* container, uint32_t count, lua_CFunction at)
{
    const char* name = getstr(container->lclass->name);
    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, name);
    if (count == 0)
    {
        luaL_addstring(&b, "()");
        luaL_pushresult(&b);
        return 1;
    }

    luaL_addstring(&b, "({");
    for (uint32_t i = 1; i <= count; i++)
    {
        if (i > 1)
            luaL_addstring(&b, ", ");
        lua_pushcfunction(L, at, "at");
        lua_pushvalue(L, 1);
        lua_pushnumber(L, i);
        lua_call(L, 2, 1);
        addshowntop(&b);
    }
    luaL_addstring(&b, "})");
    luaL_pushresult(&b);
    return 1;
}

// pair, triple and quad: `first` to `fourth`, public and const

template<LuwuBuiltinClass Kind>
static LuauObject* checktuple(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, builtinclass(L, Kind), method);
}

// `__init` is only ever called by construction, with a new object of the class: it can't be read as a member
static int tupleinit(lua_State* L)
{
    LuauObject* tuple = objectvalue(L->base);
    int nargs = lua_gettop(L) - 1;
    for (uint32_t i = 0; i < tuple->numberofmembers && int(i) < nargs; i++)
        setfield(L, tuple, i, L->base + 1 + i);
    return 0;
}

template<LuwuBuiltinClass Kind>
static int tupleeq(lua_State* L)
{
    LuauObject* tuple = checktuple<Kind>(L, "__eq");
    const TValue* other = L->base + 1;
    bool equal = ttisobject(other) && objectvalue(other)->lclass == tuple->lclass;
    for (uint32_t i = 0; equal && i < tuple->numberofmembers; i++)
        equal = equalobj(L, &tuple->members[i], &objectvalue(other)->members[i]);
    lua_pushboolean(L, equal);
    return 1;
}

template<LuwuBuiltinClass Kind>
static int tupletostring(lua_State* L)
{
    LuauObject* tuple = checktuple<Kind>(L, "__tostring");
    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, getstr(tuple->lclass->name));
    luaL_addchar(&b, '(');
    for (uint32_t i = 0; i < tuple->numberofmembers; i++)
    {
        if (i > 0)
            luaL_addstring(&b, ", ");
        luaA_pushvalue(L, &tuple->members[i]);
        addshowntop(&b);
    }
    luaL_addchar(&b, ')');
    luaL_pushresult(&b);
    return 1;
}

static const uint8_t kTupleField = LBC_CLASSMEMBER_CONST;
static const uint8_t kPrivateField = LBC_CLASSMEMBER_PRIVATE;

static const luaR_NativeMember kPairMembers[] = {
    {"first", NULL, kTupleField},
    {"second", NULL, kTupleField},
    {"__eq", tupleeq<LUWU_BUILTIN_PAIR>, 0},
    {"__tostring", tupletostring<LUWU_BUILTIN_PAIR>, 0},
};

static const luaR_NativeMember kTripleMembers[] = {
    {"first", NULL, kTupleField},
    {"second", NULL, kTupleField},
    {"third", NULL, kTupleField},
    {"__eq", tupleeq<LUWU_BUILTIN_TRIPLE>, 0},
    {"__tostring", tupletostring<LUWU_BUILTIN_TRIPLE>, 0},
};

static const luaR_NativeMember kQuadMembers[] = {
    {"first", NULL, kTupleField},
    {"second", NULL, kTupleField},
    {"third", NULL, kTupleField},
    {"fourth", NULL, kTupleField},
    {"__eq", tupleeq<LUWU_BUILTIN_QUAD>, 0},
    {"__tostring", tupletostring<LUWU_BUILTIN_QUAD>, 0},
};

// orderedmap: a map in an order the user decides. Every entry has an id that stays the same while it exists, the
// index of its key and value. Lookups go key, id, value. The order is an array of ids and each id's position is kept
// current, so `index` and `at` take constant time and rearranging moves only 4-byte ids.

enum OrderedMapField
{
    OM_KEYS,      // table: entry id -> key
    OM_VALUES,    // table: entry id -> value
    OM_IDS,       // table: key -> entry id
    OM_ORDER,     // buffer of uint32: position - 1 -> entry id
    OM_POSITIONS, // buffer of uint32: entry id - 1 -> position
    OM_FREE,      // buffer of uint32: entry ids `remove` freed, for new keys to reuse
    OM_COUNT,     // number of entries
    OM_FREECOUNT, // number of ids in OM_FREE
    OM_NEXTID,    // the id a new key gets when none is free
};

// The order and position buffers start with room for this many ids
static const uint32_t kInitialIds = 8;

static LuaTable* omtable(LuauObject* map, OrderedMapField field)
{
    return hvalue(&map->members[field]);
}

static uint32_t* omids(LuauObject* map, OrderedMapField field)
{
    return reinterpret_cast<uint32_t*>(bufvalue(&map->members[field])->data);
}

static uint32_t omnumber(LuauObject* map, OrderedMapField field)
{
    return uint32_t(nvalue(&map->members[field]));
}

static void omsetnumber(LuauObject* map, OrderedMapField field, uint32_t value)
{
    setnvalue(&map->members[field], double(value));
}

static void omsettable(lua_State* L, LuauObject* map, OrderedMapField field)
{
    TValue table;
    sethvalue(L, &table, luaH_new(L, 0, 0));
    setfield(L, map, field, &table);
}

static void omsetbuffer(lua_State* L, LuauObject* map, OrderedMapField field, uint32_t ids)
{
    TValue buffer;
    setbufvalue(L, &buffer, luaB_newbuffer(L, ids * sizeof(uint32_t)));
    setfield(L, map, field, &buffer);
}

// The id buffer `field`, grown to hold at least `ids` ids
static uint32_t* omreserve(lua_State* L, LuauObject* map, OrderedMapField field, uint32_t ids)
{
    Buffer* buffer = bufvalue(&map->members[field]);
    uint32_t capacity = buffer->len / sizeof(uint32_t);
    if (ids <= capacity)
        return reinterpret_cast<uint32_t*>(buffer->data);

    uint32_t grown = capacity * 2 > ids ? capacity * 2 : ids;
    if (size_t(grown) * sizeof(uint32_t) > MAX_BUFFER_SIZE)
        luaL_error(L, "orderedmap can't hold more than %d entries", int(MAX_BUFFER_SIZE / sizeof(uint32_t)));

    Buffer* replacement = luaB_newbuffer(L, grown * sizeof(uint32_t));
    memcpy(replacement->data, buffer->data, buffer->len);

    TValue value;
    setbufvalue(L, &value, replacement);
    setfield(L, map, field, &value);
    return reinterpret_cast<uint32_t*>(replacement->data);
}

static LuauObject* checkmap(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, builtinclass(L, LUWU_BUILTIN_ORDEREDMAP), method);
}

// The id of `key`, or 0 when the map doesn't have it
static uint32_t omidof(LuauObject* map, const TValue* key)
{
    const TValue* id = luaH_get(omtable(map, OM_IDS), key);
    return ttisnumber(id) ? uint32_t(nvalue(id)) : 0;
}

// The id of the key at stack `idx`; raises when the map doesn't have it
static uint32_t omcheckid(lua_State* L, LuauObject* map, int idx)
{
    uint32_t id = omidof(map, L->base + idx - 1);
    if (id == 0)
        luaL_error(L, "orderedmap has no key %s", pushshown(L, idx));
    return id;
}

// Rewrites the positions of the entries at positions `first` to `last`
static void omrenumber(LuauObject* map, uint32_t first, uint32_t last)
{
    uint32_t* order = omids(map, OM_ORDER);
    uint32_t* positions = omids(map, OM_POSITIONS);
    for (uint32_t position = first; position <= last; position++)
        positions[order[position - 1] - 1] = position;
}

// Gives the key at stack `idx` an id, reusing a freed one when there is one. The caller places it in the order.
static uint32_t omnewentry(lua_State* L, LuauObject* map, int idx)
{
    const TValue* key = L->base + idx - 1;
    if (ttisnil(key))
        luaL_argerror(L, idx, "key can't be nil");
    if (ttisnumber(key) && nvalue(key) != nvalue(key))
        luaL_argerror(L, idx, "key can't be NaN");

    uint32_t id;
    uint32_t freecount = omnumber(map, OM_FREECOUNT);
    if (freecount > 0)
    {
        id = omids(map, OM_FREE)[freecount - 1];
        omsetnumber(map, OM_FREECOUNT, freecount - 1);
    }
    else
    {
        id = omnumber(map, OM_NEXTID);
        omreserve(L, map, OM_POSITIONS, id);
        omsetnumber(map, OM_NEXTID, id + 1);
    }

    LuaTable* keys = omtable(map, OM_KEYS);
    setobj2t(L, luaH_setnum(L, keys, int(id)), key);
    luaC_barriert(L, keys, key);

    setnvalue(luaH_set(L, omtable(map, OM_IDS), key), double(id));
    return id;
}

// Puts entry `id` at `position`, moving the entries from there on back by one
static void omplace(lua_State* L, LuauObject* map, uint32_t id, uint32_t position)
{
    uint32_t count = omnumber(map, OM_COUNT);
    uint32_t* order = omreserve(L, map, OM_ORDER, count + 1);
    memmove(order + position, order + position - 1, (count - position + 1) * sizeof(uint32_t));
    order[position - 1] = id;
    omsetnumber(map, OM_COUNT, count + 1);
    omrenumber(map, position, count + 1);
}

// Moves entry `id` to `position`; the entries in between shift by one
static void ommove(LuauObject* map, uint32_t id, uint32_t position)
{
    uint32_t* order = omids(map, OM_ORDER);
    uint32_t from = omids(map, OM_POSITIONS)[id - 1];
    if (from < position)
    {
        memmove(order + from - 1, order + from, (position - from) * sizeof(uint32_t));
        order[position - 1] = id;
        omrenumber(map, from, position);
    }
    else if (from > position)
    {
        memmove(order + position, order + position - 1, (from - position) * sizeof(uint32_t));
        order[position - 1] = id;
        omrenumber(map, position, from);
    }
}

static void omsetvalue(lua_State* L, LuauObject* map, uint32_t id, const TValue* value)
{
    LuaTable* values = omtable(map, OM_VALUES);
    setobj2t(L, luaH_setnum(L, values, int(id)), value);
    luaC_barriert(L, values, value);
}

static int orderedmapinit(lua_State* L)
{
    LuauObject* map = objectvalue(L->base);
    omsettable(L, map, OM_KEYS);
    omsettable(L, map, OM_VALUES);
    omsettable(L, map, OM_IDS);
    omsetbuffer(L, map, OM_ORDER, kInitialIds);
    omsetbuffer(L, map, OM_POSITIONS, kInitialIds);
    omsetbuffer(L, map, OM_FREE, 0);
    omsetnumber(map, OM_COUNT, 0);
    omsetnumber(map, OM_FREECOUNT, 0);
    omsetnumber(map, OM_NEXTID, 1);
    return 0;
}

static int orderedmapget(lua_State* L)
{
    LuauObject* map = checkmap(L, "get");
    uint32_t id = omidof(map, L->base + 1);
    if (id == 0)
        lua_pushnil(L);
    else
        luaA_pushvalue(L, luaH_getnum(omtable(map, OM_VALUES), int(id)));
    return 1;
}

static int orderedmapset(lua_State* L)
{
    LuauObject* map = checkmap(L, "set");
    luaL_checkany(L, 3);
    uint32_t id = omidof(map, L->base + 1);
    if (id == 0)
    {
        id = omnewentry(L, map, 2);
        omplace(L, map, id, omnumber(map, OM_COUNT) + 1);
    }
    omsetvalue(L, map, id, L->base + 2);
    return 0;
}

static int orderedmaphas(lua_State* L)
{
    LuauObject* map = checkmap(L, "has");
    lua_pushboolean(L, omidof(map, L->base + 1) != 0);
    return 1;
}

static int orderedmapremove(lua_State* L)
{
    LuauObject* map = checkmap(L, "remove");
    uint32_t id = omidof(map, L->base + 1);
    if (id == 0)
    {
        lua_pushnil(L);
        return 1;
    }

    LuaTable* values = omtable(map, OM_VALUES);
    luaA_pushvalue(L, luaH_getnum(values, int(id)));

    uint32_t count = omnumber(map, OM_COUNT);
    uint32_t position = omids(map, OM_POSITIONS)[id - 1];
    uint32_t* order = omids(map, OM_ORDER);
    memmove(order + position - 1, order + position, (count - position) * sizeof(uint32_t));
    omsetnumber(map, OM_COUNT, count - 1);
    omrenumber(map, position, count - 1);

    setnilvalue(luaH_setnum(L, omtable(map, OM_KEYS), int(id)));
    setnilvalue(luaH_setnum(L, values, int(id)));
    setnilvalue(luaH_set(L, omtable(map, OM_IDS), L->base + 1));

    uint32_t freecount = omnumber(map, OM_FREECOUNT);
    omreserve(L, map, OM_FREE, freecount + 1)[freecount] = id;
    omsetnumber(map, OM_FREECOUNT, freecount + 1);
    return 1;
}

static int orderedmapinsert(lua_State* L)
{
    LuauObject* map = checkmap(L, "insert");
    luaL_checkany(L, 4);
    uint32_t id = omidof(map, L->base + 2);
    if (id == 0)
    {
        uint32_t position = checkposition(L, 2, omnumber(map, OM_COUNT) + 1);
        id = omnewentry(L, map, 3);
        omplace(L, map, id, position);
    }
    else
    {
        ommove(map, id, checkposition(L, 2, omnumber(map, OM_COUNT)));
    }
    omsetvalue(L, map, id, L->base + 3);
    return 0;
}

static int orderedmapmove(lua_State* L)
{
    LuauObject* map = checkmap(L, "move");
    uint32_t id = omcheckid(L, map, 2);
    ommove(map, id, checkposition(L, 3, omnumber(map, OM_COUNT)));
    return 0;
}

// movebefore and moveafter: the key at stack 2 goes right before or right after the one at stack 3
static int ommovenext(lua_State* L, const char* method, bool after)
{
    LuauObject* map = checkmap(L, method);
    uint32_t id = omcheckid(L, map, 2);
    uint32_t* positions = omids(map, OM_POSITIONS);
    uint32_t from = positions[id - 1];
    uint32_t target = positions[omcheckid(L, map, 3) - 1];

    if (after && from > target)
        target++;
    else if (!after && from < target)
        target--;

    ommove(map, id, target);
    return 0;
}

static int orderedmapmovebefore(lua_State* L)
{
    return ommovenext(L, "movebefore", false);
}

static int orderedmapmoveafter(lua_State* L)
{
    return ommovenext(L, "moveafter", true);
}

static int orderedmapswap(lua_State* L)
{
    LuauObject* map = checkmap(L, "swap");
    uint32_t a = omcheckid(L, map, 2);
    uint32_t b = omcheckid(L, map, 3);
    uint32_t* positions = omids(map, OM_POSITIONS);
    uint32_t* order = omids(map, OM_ORDER);
    uint32_t apos = positions[a - 1];
    uint32_t bpos = positions[b - 1];
    order[apos - 1] = b;
    order[bpos - 1] = a;
    positions[a - 1] = bpos;
    positions[b - 1] = apos;
    return 0;
}

// Calls the comparator at stack 2 with the keys of entries `a` and `b`
static bool omcomesbefore(lua_State* L, LuauObject* map, uint32_t a, uint32_t b)
{
    LuaTable* keys = omtable(map, OM_KEYS);
    lua_pushvalue(L, 2);
    luaA_pushvalue(L, luaH_getnum(keys, int(a)));
    luaA_pushvalue(L, luaH_getnum(keys, int(b)));
    lua_call(L, 2, 1);
    bool result = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return result;
}

// A stable bottom-up merge sort of `count` ids through `scratch`, which holds as many. It never reads outside either
// array, whatever the comparator answers.
static void omsortids(lua_State* L, LuauObject* map, uint32_t* ids, uint32_t* scratch, uint32_t count)
{
    for (uint32_t width = 1; width < count; width *= 2)
    {
        for (uint32_t left = 0; left < count; left += 2 * width)
        {
            uint32_t middle = left + width < count ? left + width : count;
            uint32_t right = left + 2 * width < count ? left + 2 * width : count;
            uint32_t i = left;
            uint32_t j = middle;
            uint32_t out = left;
            while (i < middle && j < right)
                scratch[out++] = omcomesbefore(L, map, ids[j], ids[i]) ? ids[j++] : ids[i++];
            while (i < middle)
                scratch[out++] = ids[i++];
            while (j < right)
                scratch[out++] = ids[j++];
        }
        memcpy(ids, scratch, count * sizeof(uint32_t));
    }
}

static int orderedmapsort(lua_State* L)
{
    LuauObject* map = checkmap(L, "sort");
    luaL_checktype(L, 2, LUA_TFUNCTION);
    uint32_t count = omnumber(map, OM_COUNT);

    // The comparator can change the map, so the ids are sorted in a copy the stack keeps alive
    Buffer* work = luaB_newbuffer(L, size_t(count) * 2 * sizeof(uint32_t));
    luaC_threadbarrier(L);
    setbufvalue(L, L->top, work);
    incr_top(L);

    uint32_t* ids = reinterpret_cast<uint32_t*>(work->data);
    memcpy(ids, omids(map, OM_ORDER), count * sizeof(uint32_t));
    omsortids(L, map, ids, ids + count, count);

    if (omnumber(map, OM_COUNT) != count)
        luaL_error(L, "orderedmap changed while it was being sorted");

    memcpy(omids(map, OM_ORDER), ids, count * sizeof(uint32_t));
    omrenumber(map, 1, count);
    return 0;
}

static int orderedmapindex(lua_State* L)
{
    LuauObject* map = checkmap(L, "index");
    uint32_t id = omidof(map, L->base + 1);
    if (id == 0)
        lua_pushnil(L);
    else
        lua_pushnumber(L, omids(map, OM_POSITIONS)[id - 1]);
    return 1;
}

// Pushes the key and value at `position`, or two nils when there's no entry there
static int ompushentry(lua_State* L, LuauObject* map, double position)
{
    if (!isposition(position, omnumber(map, OM_COUNT)))
    {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    uint32_t id = omids(map, OM_ORDER)[uint32_t(position) - 1];
    luaA_pushvalue(L, luaH_getnum(omtable(map, OM_KEYS), int(id)));
    luaA_pushvalue(L, luaH_getnum(omtable(map, OM_VALUES), int(id)));
    return 2;
}

static int orderedmapat(lua_State* L)
{
    LuauObject* map = checkmap(L, "at");
    return ompushentry(L, map, luaL_checknumber(L, 2));
}

static int orderedmaplen(lua_State* L)
{
    LuauObject* map = checkmap(L, "__len");
    lua_pushnumber(L, omnumber(map, OM_COUNT));
    return 1;
}

// The iterator `__iter` returns: given the previous key, the next key and value in order
static int orderedmapnext(lua_State* L)
{
    LuauObject* map = checkmap(L, "__iter");
    uint32_t position = 1;
    if (!lua_isnoneornil(L, 2))
    {
        uint32_t id = omidof(map, L->base + 1);
        if (id == 0)
            luaL_error(L, "orderedmap lost key %s while it was being iterated", pushshown(L, 2));
        position = omids(map, OM_POSITIONS)[id - 1] + 1;
    }
    return ompushentry(L, map, double(position));
}

static int orderedmapiter(lua_State* L)
{
    checkmap(L, "__iter");
    lua_pushcfunction(L, orderedmapnext, "next");
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

static const luaR_NativeMember kOrderedMapMembers[] = {
    {"keys", NULL, kPrivateField},
    {"values", NULL, kPrivateField},
    {"ids", NULL, kPrivateField},
    {"order", NULL, kPrivateField},
    {"positions", NULL, kPrivateField},
    {"free", NULL, kPrivateField},
    {"count", NULL, kPrivateField},
    {"freecount", NULL, kPrivateField},
    {"nextid", NULL, kPrivateField},
    {"get", orderedmapget, 0},
    {"set", orderedmapset, 0},
    {"has", orderedmaphas, 0},
    {"remove", orderedmapremove, 0},
    {"insert", orderedmapinsert, 0},
    {"move", orderedmapmove, 0},
    {"movebefore", orderedmapmovebefore, 0},
    {"moveafter", orderedmapmoveafter, 0},
    {"swap", orderedmapswap, 0},
    {"sort", orderedmapsort, 0},
    {"index", orderedmapindex, 0},
    {"at", orderedmapat, 0},
    {"__len", orderedmaplen, 0},
    {"__iter", orderedmapiter, 0},
};

// vecdeque: a double-ended queue on a ring buffer, the array part of a table. The entry `i` places from the front
// (from 0) is in slot (head + i) % capacity + 1, computed without a division (vdwrap).

enum VecDequeField
{
    VD_ITEMS,    // table: the ring, slots 1 to capacity
    VD_HEAD,     // the slot of the front entry, from 0
    VD_COUNT,    // number of entries
    VD_CAPACITY, // number of slots
    VD_ELEMENTTYPE,
};

// A new or cleared ring has this many slots
static const uint32_t kInitialSlots = 8;

static uint32_t vdnumber(LuauObject* dq, VecDequeField field)
{
    return uint32_t(nvalue(&dq->members[field]));
}

static void vdsetnumber(LuauObject* dq, VecDequeField field, uint32_t value)
{
    setnvalue(&dq->members[field], double(value));
}

static LuauObject* checkdeque(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, builtinclass(L, LUWU_BUILTIN_VECDEQUE), method);
}

// `ringindex` % `capacity` for a `ringindex` under twice the capacity, which every slot computation stays within:
// one compare instead of an integer division
static uint32_t vdwrap(uint32_t ringindex, uint32_t capacity)
{
    LUAU_ASSERT(ringindex < 2 * capacity);
    return ringindex >= capacity ? ringindex - capacity : ringindex;
}

// The table slot of the entry `offset` places from the front
static int vdslot(LuauObject* dq, uint32_t offset)
{
    return int(vdwrap(vdnumber(dq, VD_HEAD) + offset, vdnumber(dq, VD_CAPACITY))) + 1;
}

// Replaces the ring with one of `capacity` slots, holding the entries in order from slot 1
static void vdresize(lua_State* L, LuauObject* dq, uint32_t capacity)
{
    LuaTable* ring = luaH_new(L, int(capacity), 0);
    if (ttistable(&dq->members[VD_ITEMS]))
    {
        LuaTable* old = hvalue(&dq->members[VD_ITEMS]);
        for (uint32_t i = 0; i < vdnumber(dq, VD_COUNT); i++)
            setobj2t(L, luaH_setnum(L, ring, int(i + 1)), luaH_getnum(old, vdslot(dq, i)));
    }

    TValue value;
    sethvalue(L, &value, ring);
    setfield(L, dq, VD_ITEMS, &value);
    vdsetnumber(dq, VD_HEAD, 0);
    vdsetnumber(dq, VD_CAPACITY, capacity);
}

static void vdreserveone(lua_State* L, LuauObject* dq)
{
    uint32_t capacity = vdnumber(dq, VD_CAPACITY);
    if (vdnumber(dq, VD_COUNT) == capacity)
        vdresize(L, dq, capacity * 2);
}

static void vdstore(lua_State* L, LuauObject* dq, int slot, const TValue* value)
{
    LuaTable* ring = hvalue(&dq->members[VD_ITEMS]);
    setobj2t(L, luaH_setnum(L, ring, slot), value);
    luaC_barriert(L, ring, value);
}

static int vecdequepushback(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "pushback");
    luaL_checkany(L, 2);
    checkelement(L, dq, VD_ELEMENTTYPE, 2);
    vdreserveone(L, dq);
    uint32_t count = vdnumber(dq, VD_COUNT);
    vdstore(L, dq, vdslot(dq, count), L->base + 1);
    vdsetnumber(dq, VD_COUNT, count + 1);
    return 0;
}

static int vecdequepushfront(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "pushfront");
    luaL_checkany(L, 2);
    checkelement(L, dq, VD_ELEMENTTYPE, 2);
    vdreserveone(L, dq);
    uint32_t capacity = vdnumber(dq, VD_CAPACITY);
    vdsetnumber(dq, VD_HEAD, vdwrap(vdnumber(dq, VD_HEAD) + capacity - 1, capacity));
    vdstore(L, dq, vdslot(dq, 0), L->base + 1);
    vdsetnumber(dq, VD_COUNT, vdnumber(dq, VD_COUNT) + 1);
    return 0;
}

// Pushes the entry `offset` places from the front and clears its slot
static void vdtake(lua_State* L, LuauObject* dq, uint32_t offset)
{
    TValue* slot = luaH_setnum(L, hvalue(&dq->members[VD_ITEMS]), vdslot(dq, offset));
    luaA_pushvalue(L, slot);
    setnilvalue(slot);
}

static int vecdequepopback(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "popback");
    uint32_t count = vdnumber(dq, VD_COUNT);
    if (count == 0)
    {
        lua_pushnil(L);
        return 1;
    }

    vdtake(L, dq, count - 1);
    vdsetnumber(dq, VD_COUNT, count - 1);
    return 1;
}

static int vecdequepopfront(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "popfront");
    uint32_t count = vdnumber(dq, VD_COUNT);
    if (count == 0)
    {
        lua_pushnil(L);
        return 1;
    }

    vdtake(L, dq, 0);
    vdsetnumber(dq, VD_HEAD, vdwrap(vdnumber(dq, VD_HEAD) + 1, vdnumber(dq, VD_CAPACITY)));
    vdsetnumber(dq, VD_COUNT, count - 1);
    return 1;
}

// Pushes the entry at `position` (from 1 at the front), or nil when there's none
static int vdpushat(lua_State* L, LuauObject* dq, double position)
{
    if (!isposition(position, vdnumber(dq, VD_COUNT)))
    {
        lua_pushnil(L);
        return 1;
    }

    luaA_pushvalue(L, luaH_getnum(hvalue(&dq->members[VD_ITEMS]), vdslot(dq, uint32_t(position) - 1)));
    return 1;
}

static int vecdequefront(lua_State* L)
{
    return vdpushat(L, checkdeque(L, "front"), 1);
}

static int vecdequeback(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "back");
    return vdpushat(L, dq, vdnumber(dq, VD_COUNT));
}

static int vecdequeat(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "at");
    return vdpushat(L, dq, luaL_checknumber(L, 2));
}

static int vecdequeset(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "set");
    luaL_checkany(L, 3);
    uint32_t position = checkposition(L, 2, vdnumber(dq, VD_COUNT));
    checkelement(L, dq, VD_ELEMENTTYPE, 3);
    vdstore(L, dq, vdslot(dq, position - 1), L->base + 2);
    return 0;
}

static int vecdequeclear(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "clear");
    vdsetnumber(dq, VD_COUNT, 0);
    vdresize(L, dq, kInitialSlots);
    return 0;
}

static int vecdequelen(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "__len");
    lua_pushnumber(L, vdnumber(dq, VD_COUNT));
    return 1;
}

// The iterator `__iter` returns: given the previous position, the next position and its entry
static int vecdequenext(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "__iter");
    double position = lua_isnoneornil(L, 2) ? 1 : luaL_checknumber(L, 2) + 1;
    if (!isposition(position, vdnumber(dq, VD_COUNT)))
        return 0;

    lua_pushnumber(L, position);
    return 1 + vdpushat(L, dq, position);
}

static int vecdequeiter(lua_State* L)
{
    checkdeque(L, "__iter");
    lua_pushcfunction(L, vecdequenext, "next");
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

static int vecdequeinit(lua_State* L)
{
    LuauObject* dq = objectvalue(L->base);
    vdsetnumber(dq, VD_COUNT, 0);
    vdresize(L, dq, kInitialSlots);
    containerfill(L, dq, vecdequepushback, "pushback");
    return 0;
}

static int vecdequetostring(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "__tostring");
    return containertostring(L, dq, vdnumber(dq, VD_COUNT), vecdequeat);
}

static const luaR_NativeMember kVecDequeMembers[] = {
    {"items", NULL, kPrivateField},
    {"head", NULL, kPrivateField},
    {"count", NULL, kPrivateField},
    {"capacity", NULL, kPrivateField},
    {"elementtype", NULL, kPrivateField},
    {"pushfront", vecdequepushfront, 0},
    {"pushback", vecdequepushback, 0},
    {"popfront", vecdequepopfront, 0},
    {"popback", vecdequepopback, 0},
    {"front", vecdequefront, 0},
    {"back", vecdequeback, 0},
    {"at", vecdequeat, 0},
    {"set", vecdequeset, 0},
    {"clear", vecdequeclear, 0},
    {"__len", vecdequelen, 0},
    {"__iter", vecdequeiter, 0},
    {"__tostring", vecdequetostring, 0},
};

// vec: a growable array, entries 1 to count of a table's array part. The vec grows the array part itself, so no
// entry is ever in the hash part, which a generic for loop relies on (luaR_collectionitems).

enum VecField
{
    VEC_ITEMS, // table: entries 1 to count
    VEC_COUNT, // number of entries
    VEC_ELEMENTTYPE,
};

static LuauObject* checkvec(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, builtinclass(L, LUWU_BUILTIN_VEC), method);
}

static LuaTable* vecitems(LuauObject* vec)
{
    return hvalue(&vec->members[VEC_ITEMS]);
}

static uint32_t veccount(LuauObject* vec)
{
    return uint32_t(nvalue(&vec->members[VEC_COUNT]));
}

static void vecsetcount(LuauObject* vec, uint32_t count)
{
    setnvalue(&vec->members[VEC_COUNT], double(count));
}

// Makes room for entries 1 to `size` in the items' array part, doubling it as it grows
static void vecreserve(lua_State* L, LuauObject* vec, uint32_t size)
{
    LuaTable* items = vecitems(vec);
    if (size <= uint32_t(items->sizearray))
        return;

    uint32_t grown = std::max(uint32_t(items->sizearray) * 2, uint32_t(4));
    luaH_resizearray(L, items, int(std::max(size, grown)));
}

static void vecstore(lua_State* L, LuauObject* vec, uint32_t position, const TValue* value)
{
    vecreserve(L, vec, position);
    LuaTable* items = vecitems(vec);
    setobj2t(L, luaH_setnum(L, items, int(position)), value);
    luaC_barriert(L, items, value);
}

// Moves entries `first` to `last` by `delta` positions, one at a time in the order that overwrites nothing still needed
static void vecshift(lua_State* L, LuauObject* vec, uint32_t first, uint32_t last, int delta)
{
    LuaTable* items = vecitems(vec);
    for (uint32_t i = 0; i + first <= last; i++)
    {
        uint32_t position = delta > 0 ? last - i : first + i;
        setobj2t(L, luaH_setnum(L, items, int(position) + delta), luaH_getnum(items, int(position)));
    }
}

static void vecreset(lua_State* L, LuauObject* vec)
{
    TValue items;
    sethvalue(L, &items, luaH_new(L, 0, 0));
    setfield(L, vec, VEC_ITEMS, &items);
    vecsetcount(vec, 0);
}

static int vecpush(lua_State* L)
{
    LuauObject* vec = checkvec(L, "push");
    luaL_checkany(L, 2);
    checkelement(L, vec, VEC_ELEMENTTYPE, 2);
    uint32_t count = veccount(vec) + 1;
    vecstore(L, vec, count, L->base + 1);
    vecsetcount(vec, count);
    return 0;
}

static int vecpop(lua_State* L)
{
    LuauObject* vec = checkvec(L, "pop");
    uint32_t count = veccount(vec);
    if (count == 0)
    {
        lua_pushnil(L);
        return 1;
    }

    TValue* last = luaH_setnum(L, vecitems(vec), int(count));
    luaA_pushvalue(L, last);
    setnilvalue(last);
    vecsetcount(vec, count - 1);
    return 1;
}

// Pushes the entry at `position`, or nil when there's none
static int vecpushat(lua_State* L, LuauObject* vec, double position)
{
    if (isposition(position, veccount(vec)))
        luaA_pushvalue(L, luaH_getnum(vecitems(vec), int(position)));
    else
        lua_pushnil(L);
    return 1;
}

static int vecat(lua_State* L)
{
    LuauObject* vec = checkvec(L, "at");
    return vecpushat(L, vec, luaL_checknumber(L, 2));
}

static int vecset(lua_State* L)
{
    LuauObject* vec = checkvec(L, "set");
    luaL_checkany(L, 3);
    uint32_t position = checkposition(L, 2, veccount(vec));
    checkelement(L, vec, VEC_ELEMENTTYPE, 3);
    vecstore(L, vec, position, L->base + 2);
    return 0;
}

static int vecinsert(lua_State* L)
{
    LuauObject* vec = checkvec(L, "insert");
    luaL_checkany(L, 3);
    uint32_t count = veccount(vec);
    uint32_t position = checkposition(L, 2, count + 1);
    checkelement(L, vec, VEC_ELEMENTTYPE, 3);
    vecreserve(L, vec, count + 1);
    vecshift(L, vec, position, count, 1);
    vecstore(L, vec, position, L->base + 2);
    vecsetcount(vec, count + 1);
    return 0;
}

static int vecremove(lua_State* L)
{
    LuauObject* vec = checkvec(L, "remove");
    uint32_t count = veccount(vec);
    uint32_t position = checkposition(L, 2, count);
    luaA_pushvalue(L, luaH_getnum(vecitems(vec), int(position)));
    vecshift(L, vec, position + 1, count, -1);
    setnilvalue(luaH_setnum(L, vecitems(vec), int(count)));
    vecsetcount(vec, count - 1);
    return 1;
}

static int vecclear(lua_State* L)
{
    vecreset(L, checkvec(L, "clear"));
    return 0;
}

static int veclen(lua_State* L)
{
    LuauObject* vec = checkvec(L, "__len");
    lua_pushnumber(L, veccount(vec));
    return 1;
}

// The iterator `__iter` returns: given the previous position, the next position and its entry
static int vecnext(lua_State* L)
{
    LuauObject* vec = checkvec(L, "__iter");
    double position = lua_isnoneornil(L, 2) ? 1 : luaL_checknumber(L, 2) + 1;
    if (!isposition(position, veccount(vec)))
        return 0;

    lua_pushnumber(L, position);
    return 1 + vecpushat(L, vec, position);
}

static int veciter(lua_State* L)
{
    checkvec(L, "__iter");
    lua_pushcfunction(L, vecnext, "next");
    lua_pushvalue(L, 1);
    lua_pushnil(L);
    return 3;
}

static int vecinit(lua_State* L)
{
    LuauObject* vec = objectvalue(L->base);
    vecreset(L, vec);
    containerfill(L, vec, vecpush, "push");
    return 0;
}

static int vectostring(lua_State* L)
{
    LuauObject* vec = checkvec(L, "__tostring");
    return containertostring(L, vec, veccount(vec), vecat);
}

static const luaR_NativeMember kVecMembers[] = {
    {"items", NULL, kPrivateField},
    {"count", NULL, kPrivateField},
    {"elementtype", NULL, kPrivateField},
    {"push", vecpush, 0},
    {"pop", vecpop, 0},
    {"at", vecat, 0},
    {"set", vecset, 0},
    {"insert", vecinsert, 0},
    {"remove", vecremove, 0},
    {"clear", vecclear, 0},
    {"__len", veclen, 0},
    {"__iter", veciter, 0},
    {"__tostring", vectostring, 0},
};

// option and result. A full option is a `some` object and an empty one is the `none` primitive itself, so an empty
// option allocates nothing. `none` gets `get` and `expect` through its type's metatable, whose `__index` only runs when
// code indexes `none`, which is otherwise an error: nothing else `none` does gets slower. A result is an `ok` or an
// `err` object. Each holds its value in its one field.

// The one field of a `some`, `ok` or `err`
static const int kPayload = 0;

template<LuwuBuiltinClass Kind>
static LuauObject* checkpayload(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, builtinclass(L, Kind), method);
}

// `__init` for `ok` and `err`, which hold any value
static int payloadinit(lua_State* L)
{
    setfield(L, objectvalue(L->base), kPayload, L->base + 1);
    return 0;
}

// `__init` runs under the class's C constructor, so its error is raised at the Lua code constructing
static int someinit(lua_State* L)
{
    if (lua_gettop(L) < 2 || isnovalue(L->base + 1))
    {
        char message[LUA_BUFFERSIZE];
        int type = lua_gettop(L) < 2 ? LUA_TNIL : ttype(L->base + 1);
        snprintf(message, sizeof(message), "option.some needs a value, but you passed %s", getstr(L->global->ttname[type]));
        luaG_runerroratlua(L, message);
    }
    return payloadinit(L);
}

// `get` and `expect` of a `some` or an `ok`: the value
template<LuwuBuiltinClass Kind>
static int payloadget(lua_State* L)
{
    luaA_pushvalue(L, &checkpayload<Kind>(L, "get")->members[kPayload]);
    return 1;
}

template<LuwuBuiltinClass Kind>
static int payloadexpect(lua_State* L)
{
    LuauObject* holder = checkpayload<Kind>(L, "expect");
    luaL_optstring(L, 2, NULL);
    luaA_pushvalue(L, &holder->members[kPayload]);
    return 1;
}

template<LuwuBuiltinClass Kind>
static int payloadeq(lua_State* L)
{
    LuauObject* holder = checkpayload<Kind>(L, "__eq");
    const TValue* other = L->base + 1;
    bool equal = ttisobject(other) && objectvalue(other)->lclass == holder->lclass &&
                 equalobj(L, &holder->members[kPayload], &objectvalue(other)->members[kPayload]);
    lua_pushboolean(L, equal);
    return 1;
}

// `__tostring`: the constructor call, `option.some(1)`
template<LuwuBuiltinClass Kind>
static int payloadtostring(lua_State* L)
{
    LuauObject* holder = checkpayload<Kind>(L, "__tostring");
    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, Kind == LUWU_BUILTIN_SOME ? "option." : "result.");
    luaL_addstring(&b, getstr(holder->lclass->name));
    luaL_addchar(&b, '(');
    luaA_pushvalue(L, &holder->members[kPayload]);
    addshowntop(&b);
    luaL_addchar(&b, ')');
    luaL_pushresult(&b);
    return 1;
}

// `err` of an `ok`: there's no error
static int okerr(lua_State* L)
{
    checkpayload<LUWU_BUILTIN_OK>(L, "err");
    lua_pushsymnone(L);
    return 1;
}

// `get` of an `err`: there's no value
static int errget(lua_State* L)
{
    checkpayload<LUWU_BUILTIN_ERR>(L, "get");
    lua_pushsymnone(L);
    return 1;
}

static int errerr(lua_State* L)
{
    luaA_pushvalue(L, &checkpayload<LUWU_BUILTIN_ERR>(L, "err")->members[kPayload]);
    return 1;
}

// `expect` of an `err` raises: the message and the error, or without a message the error itself, which keeps an error
// that isn't a string intact
static int errexpect(lua_State* L)
{
    LuauObject* failed = checkpayload<LUWU_BUILTIN_ERR>(L, "expect");
    const char* message = luaL_optstring(L, 2, NULL);
    luaA_pushvalue(L, &failed->members[kPayload]);
    if (!message)
        lua_error(L);

    luaL_error(L, "%s: %s", message, luaL_tolstring(L, -1, NULL));
}

static const luaR_NativeMember kSomeMembers[] = {
    {"value", NULL, kPrivateField},
    {"get", payloadget<LUWU_BUILTIN_SOME>, 0},
    {"expect", payloadexpect<LUWU_BUILTIN_SOME>, 0},
    {"__eq", payloadeq<LUWU_BUILTIN_SOME>, 0},
    {"__tostring", payloadtostring<LUWU_BUILTIN_SOME>, 0},
};

static const luaR_NativeMember kOkMembers[] = {
    {"value", NULL, kPrivateField},
    {"get", payloadget<LUWU_BUILTIN_OK>, 0},
    {"expect", payloadexpect<LUWU_BUILTIN_OK>, 0},
    {"err", okerr, 0},
    {"__eq", payloadeq<LUWU_BUILTIN_OK>, 0},
    {"__tostring", payloadtostring<LUWU_BUILTIN_OK>, 0},
};

static const luaR_NativeMember kErrMembers[] = {
    {"error", NULL, kPrivateField},
    {"get", errget, 0},
    {"expect", errexpect, 0},
    {"err", errerr, 0},
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

LuaTable* luaR_collectionitems(lua_State* L, LuauObject* object)
{
    LuauClass** builtins = L->global->builtinclasses;
    if (object->lclass == builtins[LUWU_BUILTIN_VEC])
        return vecitems(object);

    if (object->lclass != builtins[LUWU_BUILTIN_VECDEQUE])
        return NULL;

    // A ring starting past slot 1 is rotated to start there, which keeps every entry's position
    LuaTable* ring = hvalue(&object->members[VD_ITEMS]);
    uint32_t head = vdnumber(object, VD_HEAD);
    if (head != 0)
    {
        std::rotate(ring->array, ring->array + head, ring->array + vdnumber(object, VD_CAPACITY));
        vdsetnumber(object, VD_HEAD, 0);
    }
    return ring;
}

// Builds a class, records it as builtin `kind` and stores it in the library table on top of the stack
template<size_t N>
static void addclass(lua_State* L, LuwuBuiltinClass kind, const char* name, const luaR_NativeMember (&members)[N], lua_CFunction init)
{
    L->global->builtinclasses[kind] = luaR_newnativeclass(L, name, members, int(N), init);
    lua_setfield(L, -2, name);
}

int luaopen_collections(lua_State* L)
{
    lua_createtable(L, 0, 8);
    addclass(L, LUWU_BUILTIN_PAIR, "pair", kPairMembers, tupleinit);
    addclass(L, LUWU_BUILTIN_TRIPLE, "triple", kTripleMembers, tupleinit);
    addclass(L, LUWU_BUILTIN_QUAD, "quad", kQuadMembers, tupleinit);
    addclass(L, LUWU_BUILTIN_ORDEREDMAP, "orderedmap", kOrderedMapMembers, orderedmapinit);
    addclass(L, LUWU_BUILTIN_VEC, "vec", kVecMembers, vecinit);
    addclass(L, LUWU_BUILTIN_VECDEQUE, "vecdeque", kVecDequeMembers, vecdequeinit);

    lua_createtable(L, 0, 2);
    addclass(L, LUWU_BUILTIN_SOME, "some", kSomeMembers, someinit);
    lua_pushsymnone(L);
    lua_setfield(L, -2, "none");
    lua_setfield(L, -2, "option");

    lua_createtable(L, 0, 2);
    addclass(L, LUWU_BUILTIN_OK, "ok", kOkMembers, payloadinit);
    addclass(L, LUWU_BUILTIN_ERR, "err", kErrMembers, payloadinit);
    lua_setfield(L, -2, "result");

    setnonemetatable(L);

    lua_pushvalue(L, -1);
    lua_setglobal(L, LUA_COLLECTIONSLIBNAME);
    return 1;
}
