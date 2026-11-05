// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lualib.h"

#include "lapi.h"
#include "lbuffer.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
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
    setobj(L, &luaR_members(object)[field], value);
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
void luaR_addshowntop(luaL_Strbuf* b)
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

// Vec and VecDeque hold one type of value: the first value one gets fixes it, and a value of any other type raises.
// nil never goes in. A type is a primitive type, an object's class, or a builtin enum.
//
// Luwu try expressions: builtin enums are types whose values come from several variants, classes or primitives: an
// option is a `Some` object or `none`, a result an `Ok` or an `Err` object. A container of an enum takes all of its
// variants. Enums aren't user facing yet.

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

// Fixes the element type of `storage` to the type of `value`: its builtin enum, its class, or its type tag
static void setelementtype(lua_State* L, LuauStorage* storage, const TValue* value)
{
    BuiltinEnum builtinenum = enumof(L, value);
    storage->elementtag = builtinenum != NOT_AN_ENUM ? LUAR_STORAGE_ENUM : uint8_t(ttype(value));
    storage->elementenum = uint8_t(builtinenum);
    storage->elementclass = builtinenum == NOT_AN_ENUM && ttisobject(value) ? objectvalue(value)->lclass : NULL;
}

// Whether `value` is of the element type of `storage`. It takes one compare unless the type is an object's class or a
// builtin enum. An enum's variants never match a class or a primitive element type, since setelementtype gives their
// values the enum and `none` is an option.
static bool hastype(lua_State* L, const LuauStorage* storage, const TValue* value)
{
    if (storage->elementtag == LUAR_STORAGE_ENUM)
        return enumof(L, value) == BuiltinEnum(storage->elementenum);
    return ttype(value) == storage->elementtag && (storage->elementtag != LUA_TOBJECT || objectvalue(value)->lclass == storage->elementclass);
}

// Whether a fast method can put `value` in a container with `storage`: it has the element type. Everything else goes
// through checkelement in the C method, the first value included.
static bool fitselement(lua_State* L, const LuauStorage* storage, const TValue* value)
{
    return storage && hastype(L, storage, value);
}

// Writes how an error names the values of the element type of `storage`: "strings", "'Player' objects", "options"
static void addtypeplural(luaL_Strbuf* b, const LuauStorage* storage)
{
    if (storage->elementclass)
    {
        luaL_addchar(b, '\'');
        luaL_addstring(b, getstr(storage->elementclass->name));
        luaL_addstring(b, "' objects");
        return;
    }

    if (storage->elementtag == LUAR_STORAGE_ENUM)
    {
        luaL_addstring(b, kEnumPlurals[storage->elementenum]);
        return;
    }

    const char* name = getstr(b->L->global->ttname[storage->elementtag]);
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

// Checks the value at stack `idx` can be the first in `container`: anything but nil
static void checkfirstelement(lua_State* L, LuauObject* container, int idx)
{
    if (ttisnil(L->base + idx - 1))
        luaL_error(L, "this %s can't accept nil", getstr(container->lclass->name));
}

// Checks the value at stack `idx` can go in `container`, whose block is `storage`
static void checkelement(lua_State* L, LuauObject* container, const LuauStorage* storage, int idx)
{
    const TValue* value = L->base + idx - 1;
    if (hastype(L, storage, value))
        return;

    const char* containername = getstr(container->lclass->name);

    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, "this ");
    luaL_addstring(&b, containername);
    luaL_addstring(&b, " can only accept ");
    addtypeplural(&b, storage);
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

// `__tostring`: the call that builds an equal container, `Vec({1, 2})`, or `Vec()` when it's empty. `at` reads an
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
        luaR_addshowntop(&b);
    }
    luaL_addstring(&b, "})");
    luaL_pushresult(&b);
    return 1;
}

// Luwu bundles: a fixed number of values, each position with its own type, immutable. One class per size, 0 to
// LUWU_BUNDLE_MAX; sizes 2 to 4 are named Pair, Triple and Quad. The fields are `first` to `twelfth`, public and const.

static const char* const kBundleFieldNames[LUWU_BUNDLE_MAX] = {
    "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth", "tenth", "eleventh", "twelfth",
};

static const char* const kBundleClassNames[LUWU_BUNDLE_MAX + 1] = {
    "Bundle0", "Bundle1", "Pair", "Triple", "Quad", "Bundle5", "Bundle6",
    "Bundle7", "Bundle8", "Bundle9", "Bundle10", "Bundle11", "Bundle12",
};

static LuauClass* bundleclass(lua_State* L, int size)
{
    return builtinclass(L, LuwuBuiltinClass(LUWU_BUILTIN_BUNDLE0 + size));
}

// The number of values in the bundle `value`, or -1 when it isn't a bundle
static int bundlesize(lua_State* L, const TValue* value)
{
    if (!ttisobject(value))
        return -1;

    const LuauObject* object = objectvalue(value);
    int size = int(object->numberofmembers);
    return size <= LUWU_BUNDLE_MAX && object->lclass == bundleclass(L, size) ? size : -1;
}

// The bundle at stack `idx`; raises unless it's one
static LuauObject* checkbundlearg(lua_State* L, int idx)
{
    if (bundlesize(L, luaA_toobject(L, idx)) < 0)
        luaL_typeerror(L, idx, "bundle");
    return objectvalue(luaA_toobject(L, idx));
}

// A new bundle of `size` values, pushed on the stack. Its values are unset: the caller fills all of them before anything
// can run the collector.
static LuauObject* pushnewbundle(lua_State* L, int size)
{
    luaC_threadbarrier(L);
    luaD_checkstack(L, 1);
    LuauObject* bundle = luaR_newobjectuninit(L, bundleclass(L, size));
    setobjectvalue(L, L->top, bundle);
    L->top++;
    return bundle;
}

// The range of positions `first` to `last` (stack `idx` and `idx + 1`, defaulting to the whole bundle) of a bundle of
// `size` values. It can be empty, from `n + 1` to `n`; raises when it reaches past either end.
static void checkrange(lua_State* L, int idx, int size, int* first, int* last)
{
    double from = luaL_optnumber(L, idx, 1);
    double to = luaL_optnumber(L, idx + 1, size);
    bool whole = from == double(int64_t(from)) && to == double(int64_t(to));
    if (!whole || from < 1 || to > size || to < from - 1)
        luaL_error(L, "range %s to %s is out of range 1 to %d", pushshown(L, idx), pushshown(L, idx + 1), size);
    *first = int(from);
    *last = int(to);
}

template<int Size>
static LuauObject* checkbundle(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, bundleclass(L, Size), method);
}

// `__init` is only ever called by construction, with a new object of the class: it can't be read as a member. `Err`
// uses it too, for its error and position fields.
int luaR_fieldsinit(lua_State* L)
{
    LuauObject* bundle = objectvalue(L->base);
    int nargs = lua_gettop(L) - 1;
    for (uint32_t i = 0; i < bundle->numberofmembers && int(i) < nargs; i++)
        setfield(L, bundle, i, L->base + 1 + i);
    return 0;
}

template<int Size>
static int bundleeq(lua_State* L)
{
    LuauObject* bundle = checkbundle<Size>(L, "__eq");
    const TValue* other = L->base + 1;
    bool equal = ttisobject(other) && objectvalue(other)->lclass == bundle->lclass;
    for (uint32_t i = 0; equal && i < bundle->numberofmembers; i++)
        equal = equalobj(L, &luaR_members(bundle)[i], &luaR_members(objectvalue(other))[i]);
    lua_pushboolean(L, equal);
    return 1;
}

// `<` and `<=` compare position by position: the first pair of values that differ decides, by their own `<`. Equal
// bundles are `<=` and not `<`. Bundles of different sizes don't compare.
template<int Size, bool OrEqual>
static int bundlecompare(lua_State* L)
{
    LuauObject* bundle = checkbundle<Size>(L, OrEqual ? "__le" : "__lt");
    const TValue* other = L->base + 1;
    if (!ttisobject(other) || objectvalue(other)->lclass != bundle->lclass)
        luaG_ordererror(L, L->base, other, OrEqual ? TM_LE : TM_LT);

    // a bundle is immutable and the stack anchors both, so pointers to their values stay valid while `<` runs Luau code
    const TValue* left = luaR_members(bundle);
    const TValue* right = luaR_members(objectvalue(other));
    for (int i = 0; i < Size; i++)
    {
        if (!equalobj(L, &left[i], &right[i]))
        {
            lua_pushboolean(L, luaV_lessthan(L, &left[i], &right[i]));
            return 1;
        }
    }

    lua_pushboolean(L, OrEqual);
    return 1;
}

template<int Size>
static int bundlelen(lua_State* L)
{
    checkbundle<Size>(L, "__len");
    lua_pushinteger(L, Size);
    return 1;
}

template<int Size>
static int bundletostring(lua_State* L)
{
    LuauObject* bundle = checkbundle<Size>(L, "__tostring");
    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    luaL_addstring(&b, getstr(bundle->lclass->name));
    luaL_addchar(&b, '(');
    for (uint32_t i = 0; i < bundle->numberofmembers; i++)
    {
        if (i > 0)
            luaL_addstring(&b, ", ");
        luaA_pushvalue(L, &luaR_members(bundle)[i]);
        luaR_addshowntop(&b);
    }
    luaL_addchar(&b, ')');
    luaL_pushresult(&b);
    return 1;
}

// Every slot in order, `nil` included: a bundle's size is fixed. Starting a loop allocates nothing because `__iter` is
// also the iterator it returns. The VM calls `__iter` with the bundle alone, and the iterator with the bundle and the
// previous position.
template<int Size>
static int bundleiter(lua_State* L)
{
    LuauObject* bundle = checkbundle<Size>(L, "__iter");
    if (lua_gettop(L) == 1)
    {
        luaA_pushvalue(L, L->ci->func);
        lua_pushvalue(L, 1);
        lua_pushnil(L);
        return 3;
    }

    double position = lua_isnil(L, 2) ? 1 : luaL_checknumber(L, 2) + 1;
    int index = int(position);
    if (index != position || index < 1 || uint32_t(index) > bundle->numberofmembers)
        return 0;

    lua_pushnumber(L, position);
    luaA_pushvalue(L, &luaR_members(bundle)[index - 1]);
    return 2;
}

// `b:sub(i, j)`: a new bundle of positions `i` to `j`
template<int Size>
static int bundlesub(lua_State* L)
{
    LuauObject* bundle = checkbundle<Size>(L, "sub");
    int first, last;
    checkrange(L, 2, Size, &first, &last);

    LuauObject* result = pushnewbundle(L, last - first + 1);
    for (int i = first; i <= last; i++)
        setobj(L, &luaR_members(result)[i - first], &luaR_members(bundle)[i - 1]);
    luaC_checkGC(L);
    return 1;
}

// `b:with(i, v)`: a copy of the bundle with position `i` holding `v`
template<int Size>
static int bundlewith(lua_State* L)
{
    LuauObject* bundle = checkbundle<Size>(L, "with");
    uint32_t position = checkposition(L, 2, Size);
    luaL_checkany(L, 3);

    LuauObject* result = pushnewbundle(L, Size);
    for (int i = 0; i < Size; i++)
        setobj(L, &luaR_members(result)[i], &luaR_members(bundle)[i]);
    setobj(L, &luaR_members(result)[position - 1], L->base + 2);
    luaC_checkGC(L);
    return 1;
}

// The class of `Size` values: its fields, then its methods
template<int Size>
static void addbundleclass(lua_State* L)
{
    luaR_NativeMember members[LUWU_BUNDLE_MAX + 8] = {};
    int count = 0;
    for (int i = 0; i < Size; i++)
        members[count++] = {kBundleFieldNames[i], NULL, LBC_CLASSMEMBER_CONST, NULL};

    members[count++] = {"sub", bundlesub<Size>, 0, NULL};
    members[count++] = {"with", bundlewith<Size>, 0, NULL};
    members[count++] = {"__eq", bundleeq<Size>, 0, NULL};
    members[count++] = {"__lt", bundlecompare<Size, false>, 0, NULL};
    members[count++] = {"__le", bundlecompare<Size, true>, 0, NULL};
    members[count++] = {"__len", bundlelen<Size>, 0, NULL};
    members[count++] = {"__iter", bundleiter<Size>, 0, NULL};
    members[count++] = {"__tostring", bundletostring<Size>, 0, NULL};

    LuauClass* classdef = luaR_newnativeclass(L, kBundleClassNames[Size], members, count, luaR_fieldsinit);
    classdef->keybyvalue = true;
    L->global->builtinclasses[LUWU_BUILTIN_BUNDLE0 + Size] = classdef;
    lua_pop(L, 1);
}

static void addbundleclasses(lua_State* L)
{
    static_assert(LUWU_BUNDLE_MAX == 12, "add a class for each size");
    addbundleclass<0>(L);
    addbundleclass<1>(L);
    addbundleclass<2>(L);
    addbundleclass<3>(L);
    addbundleclass<4>(L);
    addbundleclass<5>(L);
    addbundleclass<6>(L);
    addbundleclass<7>(L);
    addbundleclass<8>(L);
    addbundleclass<9>(L);
    addbundleclass<10>(L);
    addbundleclass<11>(L);
    addbundleclass<12>(L);
}

// `bundle.pack(...)`: a bundle of every argument, nil included
static int bundlepack(lua_State* L)
{
    int size = lua_gettop(L);
    if (size > LUWU_BUNDLE_MAX)
        luaL_error(L, "bundle.pack takes at most %d values, but got %d", LUWU_BUNDLE_MAX, size);

    LuauObject* bundle = pushnewbundle(L, size);
    for (int i = 0; i < size; i++)
        setobj(L, &luaR_members(bundle)[i], L->base + i);
    luaC_checkGC(L);
    return 1;
}

// `bundle.unpack(b, i, j)`: the values at positions `i` to `j`, the whole bundle by default
static int bundleunpack(lua_State* L)
{
    LuauObject* bundle = checkbundlearg(L, 1);
    int first, last;
    checkrange(L, 2, int(bundle->numberofmembers), &first, &last);

    int count = last - first + 1;
    luaL_checkstack(L, count, "bundle.unpack");
    for (int i = first; i <= last; i++)
        luaA_pushvalue(L, &luaR_members(bundle)[i - 1]);
    return count;
}

// `bundle.concat(a, b, ...)`: one bundle of every argument's values, in order
static int bundleconcat(lua_State* L)
{
    int nargs = lua_gettop(L);
    int size = 0;
    for (int i = 1; i <= nargs; i++)
        size += int(checkbundlearg(L, i)->numberofmembers);
    if (size > LUWU_BUNDLE_MAX)
        luaL_error(L, "bundle.concat makes at most %d values, but these hold %d", LUWU_BUNDLE_MAX, size);

    LuauObject* result = pushnewbundle(L, size);
    int position = 0;
    for (int i = 1; i <= nargs; i++)
    {
        const LuauObject* part = objectvalue(luaA_toobject(L, i));
        for (uint32_t j = 0; j < part->numberofmembers; j++)
            setobj(L, &luaR_members(result)[position++], &luaR_members(part)[j]);
    }
    luaC_checkGC(L);
    return 1;
}

static const luaL_Reg kBundleFunctions[] = {
    {"pack", bundlepack},
    {"unpack", bundleunpack},
    {"concat", bundleconcat},
    {NULL, NULL},
};

static const uint8_t kPrivateField = LBC_CLASSMEMBER_PRIVATE;

// OrderedMap: a map in an order the user decides. Every entry has an id that stays the same while it exists, the
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
    return hvalue(&luaR_members(map)[field]);
}

static uint32_t* omids(LuauObject* map, OrderedMapField field)
{
    return reinterpret_cast<uint32_t*>(bufvalue(&luaR_members(map)[field])->data);
}

static uint32_t omnumber(LuauObject* map, OrderedMapField field)
{
    return uint32_t(nvalue(&luaR_members(map)[field]));
}

static void omsetnumber(LuauObject* map, OrderedMapField field, uint32_t value)
{
    setnvalue(&luaR_members(map)[field], double(value));
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
    Buffer* buffer = bufvalue(&luaR_members(map)[field]);
    uint32_t capacity = buffer->len / sizeof(uint32_t);
    if (ids <= capacity)
        return reinterpret_cast<uint32_t*>(buffer->data);

    uint32_t grown = capacity * 2 > ids ? capacity * 2 : ids;
    if (size_t(grown) * sizeof(uint32_t) > MAX_BUFFER_SIZE)
        luaL_error(L, "OrderedMap can't hold more than %d entries", int(MAX_BUFFER_SIZE / sizeof(uint32_t)));

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
        luaL_error(L, "OrderedMap has no key %s", pushshown(L, idx));
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
        luaL_error(L, "OrderedMap changed while it was being sorted");

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
// The id of the entry at `position`, or 0 when there's none
static uint32_t omidat(LuauObject* map, double position)
{
    return isposition(position, omnumber(map, OM_COUNT)) ? omids(map, OM_ORDER)[uint32_t(position) - 1] : 0;
}

static int ompushentry(lua_State* L, LuauObject* map, double position)
{
    uint32_t id = omidat(map, position);
    if (id == 0)
    {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

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

// The position after the key at stack `idx`, which a loop gave last; raises when the map no longer has it
static uint32_t omnextposition(lua_State* L, LuauObject* map, int idx)
{
    uint32_t id = omidof(map, L->base + idx - 1);
    if (id == 0)
        luaL_error(L, "OrderedMap lost key %s while it was being iterated", pushshown(L, idx));
    return omids(map, OM_POSITIONS)[id - 1] + 1;
}

// The iterator `__iter` returns: given the previous key, the next key and value in order. A generic for loop doesn't
// call it: it runs luaR_collectionnext instead.
static int orderedmapnext(lua_State* L)
{
    LuauObject* map = checkmap(L, "__iter");
    uint32_t position = lua_isnoneornil(L, 2) ? 1 : omnextposition(L, map, 2);
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

// OrderedMap's fast methods (luaR_FastMethod). `set` leaves a new key to the C method, which may grow the map.

static int orderedmapgetfast(lua_State* L, LuauObject* map, StkId res, StkId args, int nargs)
{
    if (nargs < 1)
        return -1;

    uint32_t id = omidof(map, args);
    if (id == 0)
        setnilvalue(res);
    else
        setobj2s(L, res, luaH_getnum(omtable(map, OM_VALUES), int(id)));
    return 1;
}

static int orderedmaphasfast(lua_State* L, LuauObject* map, StkId res, StkId args, int nargs)
{
    if (nargs < 1)
        return -1;

    setbvalue(res, omidof(map, args) != 0);
    return 1;
}

static int orderedmapsetfast(lua_State* L, LuauObject* map, StkId res, StkId args, int nargs)
{
    uint32_t id = nargs >= 2 ? omidof(map, args) : 0;
    if (id == 0)
        return -1;

    // The key's value slot can only be written in place when the values table already has it
    LuaTable* values = omtable(map, OM_VALUES);
    const TValue* slot = luaH_getnum(values, int(id));
    if (slot == luaO_nilobject)
        return -1;

    setobj2t(L, const_cast<TValue*>(slot), args + 1);
    luaC_barriert(L, values, args + 1);
    return 0;
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
    {"get", orderedmapget, 0, orderedmapgetfast},
    {"set", orderedmapset, 0, orderedmapsetfast},
    {"has", orderedmaphas, 0, orderedmaphasfast},
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

// Vec and VecDeque store their entries in a storage block (LuauStorage) from the first value on. Its capacity is a power
// of two. The entry `i` places from the front (from 0) is in slot (head + i) & (capacity - 1) (luaR_storageentry): a
// VecDeque's entries wrap around the block's end, and a Vec's start at slot 0 with head 0.

// A container holds at most this many entries, as many as a table's array part
static const uint32_t kMaxEntries = 1u << 26;

// A new or cleared block has this many slots
static const uint32_t kInitialSlots = 4;

static uint32_t countof(const LuauObject* container)
{
    const LuauStorage* storage = luaR_storage(container);
    return storage ? storage->count : 0;
}

// The slot of the entry `offset` places from the front, which may be past the last entry
static TValue* storageslot(LuauStorage* storage, uint32_t offset)
{
    LUAU_ASSERT(offset < storage->capacity);
    return &storage->items[(storage->head + offset) & (storage->capacity - 1)];
}

static void setstorage(LuauObject* container, LuauStorage* storage)
{
    setpvalue(&luaR_members(container)[LUAR_STORAGE_MEMBER], storage, 0);
}

// `__init`'s first step: a container of `kind` without a block
static void initstorage(LuauObject* container, LuwuStorageKind kind)
{
    setnilvalue(&luaR_members(container)[LUAR_STORAGE_MEMBER]);
    container->storagekind = uint8_t(kind);
}

// The block of an empty container, made for its first value at stack `idx`, which fixes the element type
static LuauStorage* newstorage(lua_State* L, LuauObject* container, int idx)
{
    checkfirstelement(L, container, idx);

    LuauStorage* storage = static_cast<LuauStorage*>(luaM_new_(L, luaR_storagesize(kInitialSlots), container->memcat));
    storage->count = 0;
    storage->capacity = kInitialSlots;
    storage->head = 0;
    storage->padding = 0;
    setelementtype(L, storage, L->base + idx - 1);
    setstorage(container, storage);
    if (storage->elementclass)
        luaC_objbarrier(L, container, storage->elementclass);
    return storage;
}

// Gives the block `capacity` slots, enough for every entry. The entries don't move: when the block grows,
// the ones that wrapped around its old end move after it.
static LuauStorage* resizestorage(lua_State* L, LuauObject* container, uint32_t capacity)
{
    LuauStorage* storage = luaR_storage(container);
    uint32_t old = storage->capacity;
    LUAU_ASSERT(capacity >= storage->count && (capacity & (capacity - 1)) == 0);
    LUAU_ASSERT(capacity > old || storage->count == 0);

    storage = static_cast<LuauStorage*>(luaM_realloc_(L, storage, luaR_storagesize(old), luaR_storagesize(capacity), container->memcat));
    storage->capacity = capacity;
    if (storage->count == 0)
        storage->head = 0;
    else if (storage->head + storage->count > old)
        memcpy(storage->items + old, storage->items, (storage->head + storage->count - old) * sizeof(TValue));

    setstorage(container, storage);
    return storage;
}

// The block of `container`, with room for `size` entries
static LuauStorage* reservestorage(lua_State* L, LuauObject* container, uint32_t size)
{
    LuauStorage* storage = luaR_storage(container);
    if (size <= storage->capacity)
        return storage;
    if (size > kMaxEntries)
        luaL_error(L, "%s can't hold more than %d entries", getstr(container->lclass->name), int(kMaxEntries));

    uint32_t capacity = storage->capacity;
    while (capacity < size)
        capacity *= 2;
    return resizestorage(L, container, capacity);
}

// Checks the value at stack `idx` can go in `container`, and returns its block with room for one more entry
static LuauStorage* storageforone(lua_State* L, LuauObject* container, int idx)
{
    LuauStorage* storage = luaR_storage(container);
    if (!storage)
        return newstorage(L, container, idx);

    checkelement(L, container, storage, idx);
    return reservestorage(L, container, storage->count + 1);
}

static void storeentry(lua_State* L, LuauObject* container, TValue* slot, const TValue* value)
{
    setobj(L, slot, value);
    luaC_barrier(L, container, value);
}

// Empties `container` without changing its element type, and shrinks a grown block back
static void clearstorage(lua_State* L, LuauObject* container)
{
    LuauStorage* storage = luaR_storage(container);
    if (!storage)
        return;

    storage->count = 0;
    storage->head = 0;
    if (storage->capacity > kInitialSlots)
        resizestorage(L, container, kInitialSlots);
}

// Pushes the entry at `position` (from 1 at the front), or nil when there's none
static int pushentryat(lua_State* L, LuauObject* container, double position)
{
    if (isposition(position, countof(container)))
        luaA_pushvalue(L, luaR_storageentry(luaR_storage(container), uint32_t(position) - 1));
    else
        lua_pushnil(L);
    return 1;
}

// `popback` of a Vec or VecDeque: takes the last entry, or nil when there's none
static int popback(lua_State* L, LuauObject* container)
{
    LuauStorage* storage = luaR_storage(container);
    if (!storage || storage->count == 0)
    {
        lua_pushnil(L);
        return 1;
    }

    luaA_pushvalue(L, luaR_storageentry(storage, storage->count - 1));
    storage->count--;
    return 1;
}

// The iterator `__iter` returns: given the previous position, the next position and its entry. A generic for loop
// doesn't call it: it steps with luaR_storagenext instead.
static int containernext(lua_State* L, LuauObject* container)
{
    double position = lua_isnoneornil(L, 2) ? 1 : luaL_checknumber(L, 2) + 1;
    if (!isposition(position, countof(container)))
        return 0;

    lua_pushnumber(L, position);
    return 1 + pushentryat(L, container, position);
}

// Writes the entry at `position` (from 1 at the front) to `res`, or nil when there's none, for a fast method
static int entryatfast(lua_State* L, LuauObject* container, double position, StkId res)
{
    if (!isposition(position, countof(container)))
    {
        setnilvalue(res);
        return 1;
    }

    setobj2s(L, res, luaR_storageentry(luaR_storage(container), uint32_t(position) - 1));
    return 1;
}

// Whether a fast method can add `value` to `container`: it has the element type, and there's room without growing
static bool fitsonemore(lua_State* L, LuauObject* container, StkId args, int nargs)
{
    const LuauStorage* storage = luaR_storage(container);
    return nargs >= 1 && fitselement(L, storage, args) && storage->count < storage->capacity;
}

// Whether a fast method can store `args + 1` at the position `args`
static bool fitsset(lua_State* L, LuauObject* container, StkId args, int nargs)
{
    return nargs >= 2 && ttisnumber(args) && isposition(nvalue(args), countof(container)) &&
           fitselement(L, luaR_storage(container), args + 1);
}

// VecDeque: a double-ended queue on a ring. Pushing at the front moves the head back one slot.

static LuauObject* checkdeque(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, builtinclass(L, LUWU_BUILTIN_VECDEQUE), method);
}

static int vecdequepushback(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "pushback");
    luaL_checkany(L, 2);
    LuauStorage* storage = storageforone(L, dq, 2);
    storeentry(L, dq, storageslot(storage, storage->count), L->base + 1);
    storage->count++;
    return 0;
}

static int vecdequepushfront(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "pushfront");
    luaL_checkany(L, 2);
    LuauStorage* storage = storageforone(L, dq, 2);
    storage->head = (storage->head - 1) & (storage->capacity - 1);
    storeentry(L, dq, storageslot(storage, 0), L->base + 1);
    storage->count++;
    return 0;
}

static int vecdequepopback(lua_State* L)
{
    return popback(L, checkdeque(L, "popback"));
}

static int vecdequepopfront(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "popfront");
    LuauStorage* storage = luaR_storage(dq);
    if (!storage || storage->count == 0)
    {
        lua_pushnil(L);
        return 1;
    }

    luaA_pushvalue(L, luaR_storageentry(storage, 0));
    storage->head = (storage->head + 1) & (storage->capacity - 1);
    storage->count--;
    return 1;
}

static int vecdequefront(lua_State* L)
{
    return pushentryat(L, checkdeque(L, "front"), 1);
}

static int vecdequeback(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "back");
    return pushentryat(L, dq, countof(dq));
}

static int vecdequeat(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "at");
    return pushentryat(L, dq, luaL_checknumber(L, 2));
}

static int vecdequeset(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "set");
    luaL_checkany(L, 3);
    uint32_t position = checkposition(L, 2, countof(dq));
    LuauStorage* storage = luaR_storage(dq);
    checkelement(L, dq, storage, 3);
    storeentry(L, dq, luaR_storageentry(storage, position - 1), L->base + 2);
    return 0;
}

static int vecdequeclear(lua_State* L)
{
    clearstorage(L, checkdeque(L, "clear"));
    return 0;
}

static int vecdequelen(lua_State* L)
{
    lua_pushnumber(L, countof(checkdeque(L, "__len")));
    return 1;
}

static int vecdequenext(lua_State* L)
{
    return containernext(L, checkdeque(L, "__iter"));
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
    initstorage(dq, LUAR_STORAGE_RING);
    containerfill(L, dq, vecdequepushback, "pushback");
    return 0;
}

static int vecdequetostring(lua_State* L)
{
    LuauObject* dq = checkdeque(L, "__tostring");
    return containertostring(L, dq, countof(dq), vecdequeat);
}

// VecDeque's fast methods (luaR_FastMethod) handle the common case and leave the rest to the C methods: a first or
// mistyped value, growing, a missing argument and a position that isn't a number.

static int vecdequepushbackfast(lua_State* L, LuauObject* dq, StkId res, StkId args, int nargs)
{
    if (!fitsonemore(L, dq, args, nargs))
        return -1;

    LuauStorage* storage = luaR_storage(dq);
    storeentry(L, dq, storageslot(storage, storage->count), args);
    storage->count++;
    return 0;
}

static int vecdequepushfrontfast(lua_State* L, LuauObject* dq, StkId res, StkId args, int nargs)
{
    if (!fitsonemore(L, dq, args, nargs))
        return -1;

    LuauStorage* storage = luaR_storage(dq);
    storage->head = (storage->head - 1) & (storage->capacity - 1);
    storeentry(L, dq, storageslot(storage, 0), args);
    storage->count++;
    return 0;
}

// `popback` of a Vec or VecDeque
static int popbackfast(lua_State* L, LuauObject* container, StkId res, StkId args, int nargs)
{
    LuauStorage* storage = luaR_storage(container);
    if (!storage || storage->count == 0)
    {
        setnilvalue(res);
        return 1;
    }

    setobj2s(L, res, luaR_storageentry(storage, storage->count - 1));
    storage->count--;
    return 1;
}

static int vecdequepopfrontfast(lua_State* L, LuauObject* dq, StkId res, StkId args, int nargs)
{
    LuauStorage* storage = luaR_storage(dq);
    if (!storage || storage->count == 0)
    {
        setnilvalue(res);
        return 1;
    }

    setobj2s(L, res, luaR_storageentry(storage, 0));
    storage->head = (storage->head + 1) & (storage->capacity - 1);
    storage->count--;
    return 1;
}

static int vecdequefrontfast(lua_State* L, LuauObject* dq, StkId res, StkId args, int nargs)
{
    return entryatfast(L, dq, 1, res);
}

static int vecdequebackfast(lua_State* L, LuauObject* dq, StkId res, StkId args, int nargs)
{
    return entryatfast(L, dq, countof(dq), res);
}

static int containeratfast(lua_State* L, LuauObject* container, StkId res, StkId args, int nargs)
{
    if (nargs < 1 || !ttisnumber(args))
        return -1;
    return entryatfast(L, container, nvalue(args), res);
}

static int containersetfast(lua_State* L, LuauObject* container, StkId res, StkId args, int nargs)
{
    if (!fitsset(L, container, args, nargs))
        return -1;

    storeentry(L, container, luaR_storageentry(luaR_storage(container), uint32_t(nvalue(args)) - 1), args + 1);
    return 0;
}

static const luaR_NativeMember kVecDequeMembers[] = {
    {"storage", NULL, kPrivateField},
    {"pushfront", vecdequepushfront, 0, vecdequepushfrontfast},
    {"pushback", vecdequepushback, 0, vecdequepushbackfast},
    {"popfront", vecdequepopfront, 0, vecdequepopfrontfast},
    {"popback", vecdequepopback, 0, popbackfast},
    {"front", vecdequefront, 0, vecdequefrontfast},
    {"back", vecdequeback, 0, vecdequebackfast},
    {"at", vecdequeat, 0, containeratfast},
    {"set", vecdequeset, 0, containersetfast},
    {"clear", vecdequeclear, 0},
    {"__len", vecdequelen, 0},
    {"__iter", vecdequeiter, 0},
    {"__tostring", vecdequetostring, 0},
};

// Vec: a growable array. Its head stays 0, so position `p` is slot `p - 1`.

static LuauObject* checkvec(lua_State* L, const char* method)
{
    return luaR_checknativeself(L, builtinclass(L, LUWU_BUILTIN_VEC), method);
}

static int vecpush(lua_State* L)
{
    LuauObject* vec = checkvec(L, "push");
    luaL_checkany(L, 2);
    LuauStorage* storage = storageforone(L, vec, 2);
    storeentry(L, vec, &storage->items[storage->count], L->base + 1);
    storage->count++;
    return 0;
}

static int vecpop(lua_State* L)
{
    return popback(L, checkvec(L, "pop"));
}

static int vecat(lua_State* L)
{
    LuauObject* vec = checkvec(L, "at");
    return pushentryat(L, vec, luaL_checknumber(L, 2));
}

static int vecset(lua_State* L)
{
    LuauObject* vec = checkvec(L, "set");
    luaL_checkany(L, 3);
    uint32_t position = checkposition(L, 2, countof(vec));
    LuauStorage* storage = luaR_storage(vec);
    checkelement(L, vec, storage, 3);
    storeentry(L, vec, &storage->items[position - 1], L->base + 2);
    return 0;
}

static int vecinsert(lua_State* L)
{
    LuauObject* vec = checkvec(L, "insert");
    luaL_checkany(L, 3);
    uint32_t position = checkposition(L, 2, countof(vec) + 1);
    LuauStorage* storage = storageforone(L, vec, 3);
    TValue* slot = &storage->items[position - 1];
    memmove(slot + 1, slot, (storage->count - position + 1) * sizeof(TValue));
    storeentry(L, vec, slot, L->base + 2);
    storage->count++;
    return 0;
}

static int vecremove(lua_State* L)
{
    LuauObject* vec = checkvec(L, "remove");
    uint32_t position = checkposition(L, 2, countof(vec));
    LuauStorage* storage = luaR_storage(vec);
    TValue* slot = &storage->items[position - 1];
    luaA_pushvalue(L, slot);
    memmove(slot, slot + 1, (storage->count - position) * sizeof(TValue));
    storage->count--;
    return 1;
}

static int vecclear(lua_State* L)
{
    clearstorage(L, checkvec(L, "clear"));
    return 0;
}

static int veclen(lua_State* L)
{
    lua_pushnumber(L, countof(checkvec(L, "__len")));
    return 1;
}

static int vecnext(lua_State* L)
{
    return containernext(L, checkvec(L, "__iter"));
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
    initstorage(vec, LUAR_STORAGE_VEC);
    containerfill(L, vec, vecpush, "push");
    return 0;
}

static int vectostring(lua_State* L)
{
    LuauObject* vec = checkvec(L, "__tostring");
    return containertostring(L, vec, countof(vec), vecat);
}

// Vec's fast methods (luaR_FastMethod) handle the common case and leave the rest to the C methods: a first or mistyped
// value, growing, a missing argument and a position that isn't a number.

static int vecpushfast(lua_State* L, LuauObject* vec, StkId res, StkId args, int nargs)
{
    if (!fitsonemore(L, vec, args, nargs))
        return -1;

    LuauStorage* storage = luaR_storage(vec);
    storeentry(L, vec, &storage->items[storage->count], args);
    storage->count++;
    return 0;
}

static const luaR_NativeMember kVecMembers[] = {
    {"storage", NULL, kPrivateField},
    {"push", vecpush, 0, vecpushfast},
    {"pop", vecpop, 0, popbackfast},
    {"at", vecat, 0, containeratfast},
    {"set", vecset, 0, containersetfast},
    {"insert", vecinsert, 0},
    {"remove", vecremove, 0},
    {"clear", vecclear, 0},
    {"__len", veclen, 0},
    {"__iter", veciter, 0},
    {"__tostring", vectostring, 0},
};

bool luaR_collectionprep(lua_State* L, StkId ra)
{
    LuauObject* object = objectvalue(ra);
    LuauClass** builtins = L->global->builtinclasses;
    if (object->lclass == builtins[LUWU_BUILTIN_ORDEREDMAP])
    {
        setobj2s(L, ra + 1, ra);
        setnilvalue(ra + 2);
        setbvalue(ra, false);
        ra->extra[0] = 0;
        return true;
    }

    if (object->lclass != builtins[LUWU_BUILTIN_VEC] && object->lclass != builtins[LUWU_BUILTIN_VECDEQUE])
        return false;

    setobj2s(L, ra + 1, ra);
    setpvalue(ra + 2, reinterpret_cast<void*>(uintptr_t(0)), LU_TAG_ITERATOR);
    setbvalue(ra, true);
    return true;
}

// Raises the error a loop's step gets when the map lost the key it gave last. The step runs in the loop's Lua frame,
// where nothing can be pushed, so the C iterator raises it from a frame of its own, as when a loop called it.
static l_noret omlostkey(lua_State* L, StkId ra)
{
    Closure* iterator = luaF_newCclosure(L, 0, L->gt);
    iterator->c.f = orderedmapnext;
    iterator->c.debugname = "next";

    // note: like FORGLOOP's call, this writes past top, which the stack always has room for
    setclvalue(L, ra + 3, iterator);
    setobj2s(L, ra + 4, ra + 1);
    setobj2s(L, ra + 5, ra + 2);
    L->top = ra + 6;
    luaD_call(L, ra + 3, 0);
    LUAU_ASSERT(!"orderedmapnext raises for a lost key");
    LUAU_UNREACHABLE();
}

bool luaR_collectionnext(lua_State* L, StkId ra)
{
    if (bvalue(ra))
        return luaR_storagenext(L, objectvalue(ra + 1), ra);

    LuauObject* map = objectvalue(ra + 1);
    LUAU_ASSERT(map->lclass == builtinclass(L, LUWU_BUILTIN_ORDEREDMAP));

    // The previous key's position, which the last step kept in the loop marker's extra field. When the entry there is
    // still that key, nothing moved it and the hash lookup isn't needed.
    uint32_t previous = 0;
    if (!ttisnil(ra + 2))
    {
        previous = uint32_t(ra->extra[0]);
        uint32_t id = omidat(map, double(previous));
        if (id == 0 || !luaO_rawequalObj(luaH_getnum(omtable(map, OM_KEYS), int(id)), ra + 2))
        {
            id = omidof(map, ra + 2);
            if (id == 0)
                omlostkey(L, ra);
            previous = omids(map, OM_POSITIONS)[id - 1];
        }
    }

    uint32_t position = previous + 1;
    uint32_t id = omidat(map, double(position));
    if (id == 0)
        return false;

    setobj2s(L, ra + 3, luaH_getnum(omtable(map, OM_KEYS), int(id)));
    setobj2s(L, ra + 4, luaH_getnum(omtable(map, OM_VALUES), int(id)));
    setobj2s(L, ra + 2, ra + 3);
    ra->extra[0] = int(position);
    return true;
}

// Builds a class, records it as builtin `kind` and stores it in the library table on top of the stack. An `OrderedMap`
// passes the field holding its length as `lengthfield`, for `#` to read; `Vec` and `VecDeque` read their storage.
template<size_t N>
static void addclass(
    lua_State* L,
    LuwuBuiltinClass kind,
    const char* name,
    const luaR_NativeMember (&members)[N],
    lua_CFunction init,
    uint32_t lengthfield = LUAR_NO_LENGTH_FIELD
)
{
    LuauClass* classdef = luaR_newnativeclass(L, name, members, int(N), init);
    classdef->lengthfield = lengthfield;
    L->global->builtinclasses[kind] = classdef;
    lua_setfield(L, -2, name);
}

int luaopen_collections(lua_State* L)
{
    lua_createtable(L, 0, 3);
    addclass(L, LUWU_BUILTIN_ORDEREDMAP, "OrderedMap", kOrderedMapMembers, orderedmapinit, OM_COUNT);
    addclass(L, LUWU_BUILTIN_VEC, "Vec", kVecMembers, vecinit);
    addclass(L, LUWU_BUILTIN_VECDEQUE, "VecDeque", kVecDequeMembers, vecdequeinit);

    lua_pushvalue(L, -1);
    lua_setglobal(L, LUA_COLLECTIONSLIBNAME);
    return 1;
}

int luaopen_bundle(lua_State* L)
{
    addbundleclasses(L);

    lua_createtable(L, 0, 6);
    luaL_register(L, NULL, kBundleFunctions);
    for (int size = 2; size <= 4; size++)
    {
        luaA_pushclass(L, bundleclass(L, size));
        lua_setfield(L, -2, kBundleClassNames[size]);
    }

    lua_pushvalue(L, -1);
    lua_setglobal(L, LUA_BUNDLELIBNAME);
    return 1;
}
