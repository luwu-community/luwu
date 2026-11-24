// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lualib.h"

#include "lapi.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltm.h"
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

// The value of a `Some`, `Ok` or `Err`, readable as `value` (`error` for `Err`). Luwu Destructuring
// (rfcs/destructuring.md): `.(a, b)` reads it, and the rest of an `Err`'s fields, by position.
static const uint8_t kPayloadField = LBC_CLASSMEMBER_CONST | LBC_CLASSMEMBER_POSITIONAL;

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

// `__init` for `Some` and `Ok`, which hold any value, nil and `none` included. Without an argument they hold nil. The
// slot after the object isn't an argument then: it's stale stack the collector doesn't mark.
static int payloadinit(lua_State* L)
{
    LuauObject* holder = objectvalue(L->base);
    if (lua_gettop(L) < 2)
    {
        setnilvalue(&luaR_members(holder)[kPayload]);
        return 0;
    }

    setobj(L, &luaR_members(holder)[kPayload], L->base + 1);
    luaC_barrier(L, holder, L->base + 1);
    return 0;
}

LuauObject* luaR_newpayload(lua_State* L, LuwuBuiltinClass kind, const TValue* value)
{
    LuauClass* classdef = builtinclass(L, kind);
    if (!classdef)
        luaG_runerror(L, "open the result library (luaopen_result) before building an Ok, Err or Some");

    // A new object is white, so storing into it needs no barrier
    LuauObject* object = luaR_newobjectuninit(L, classdef);
    TValue* members = luaR_members(object);
    setobj(L, &members[kPayload], value);
    for (int i = kPayload + 1; i < int(object->numberofmembers); i++)
        setnilvalue(&members[i]);
    return object;
}

void luaR_wrapresult(lua_State* L, StkId ra, bool caught)
{
    LuauObject* result = luaR_newpayload(L, caught ? LUWU_BUILTIN_ERR : LUWU_BUILTIN_OK, ra);
    if (caught)
    {
        // the module, line and traceback fields follow the payload; a new object needs no barrier
        LUAU_ASSERT(result->numberofmembers >= kPayload + 4);
        TValue* members = luaR_members(result);
        for (int i = 1; i <= 3; i++)
            setobj(L, &members[kPayload + i], ra + i);
    }

    setobjectvalue(L, ra, result);
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

// Luwu error variants (rfcs/integers.md): the builtin error classes in the global `Error`, such as `Error.Overflow`.
// `Error` is a frozen table until the builtin `Error` trait exists. Each variant's fields are its positions, so
// `Error.Overflow(value, info)` matches `is Error.Overflow(got, mode)`. A library raises an
// object of one with luaR_raisevariant. `__tostring` gives the variant's message, which is also what's raised as a
// string when the result library isn't open.

// The position `index` of the `count` values at `fields`, or nil past them
static const TValue* variantfield(const TValue* fields, int count, int index)
{
    return index < count ? &fields[index] : luaO_nilobject;
}

// Adds `value` to `b` the way a constructor call in `__tostring` shows it
static void addvariantvalue(luaL_Strbuf* b, const TValue* value)
{
    luaA_pushvalue(b->L, value);
    luaR_addshowntop(b);
}

// Adds `value` to `b` as `tostring` gives it
static void addvariantstring(luaL_Strbuf* b, const TValue* value)
{
    luaA_pushvalue(b->L, value);
    luaL_tolstring(b->L, -1, NULL);
    lua_remove(b->L, -2);
    luaL_addvalue(b);
}

// Pushes the message of error variant `kind` whose positions are the `count` values at `fields`
static void pushvariantmessage(lua_State* L, LuwuBuiltinClass kind, const TValue* fields, int count)
{
    const TValue* first = variantfield(fields, count, 0);
    const TValue* second = variantfield(fields, count, 1);

    luaL_Strbuf b;
    luaL_buffinit(L, &b);

    switch (kind)
    {
    case LUWU_BUILTIN_ERR_OVERFLOW:
    case LUWU_BUILTIN_ERR_UNDERFLOW:
    {
        bool overflow = kind == LUWU_BUILTIN_ERR_OVERFLOW;
        // a raiser that can't build the value, like a power too large to compute, leaves it nil
        if (ttisnil(first))
            luaL_addstring(&b, "the result");
        else
            addvariantvalue(&b, first);
        if (ttisnil(second))
        {
            luaL_addstring(&b, overflow ? " overflowed" : " underflowed");
            break;
        }
        luaL_addstring(&b, overflow ? " is too large for " : " is too small for ");
        addvariantstring(&b, second);
        break;
    }
    case LUWU_BUILTIN_ERR_CANNOTBEPARSED:
        luaL_addstring(&b, "couldn't parse ");
        addvariantvalue(&b, first);
        if (!ttisnil(second))
        {
            luaL_addstring(&b, ": ");
            addvariantstring(&b, second);
        }
        break;
    case LUWU_BUILTIN_ERR_UNEXPECTEDFLOAT:
        luaL_addstring(&b, "expected a whole number, got ");
        addvariantvalue(&b, first);
        break;
    case LUWU_BUILTIN_ERR_INVALIDARGUMENT:
        if (!ttisnil(second))
        {
            addvariantstring(&b, second);
            break;
        }
        luaL_addstring(&b, "invalid argument");
        if (!ttisnil(first))
        {
            luaL_addchar(&b, ' ');
            addvariantvalue(&b, first);
        }
        break;
    case LUWU_BUILTIN_ERR_DIVISIONBYZERO:
        luaL_addstring(&b, "attempt to divide by zero");
        break;
    case LUWU_BUILTIN_ERR_INVALIDCOMPARISON:
        luaL_addstring(&b, "attempt to compare ");
        luaL_addstring(&b, luaT_objtypename(L, first));
        luaL_addstring(&b, " with ");
        luaL_addstring(&b, luaT_objtypename(L, second));
        break;
    case LUWU_BUILTIN_ERR_UNREACHABLE:
        luaL_addstring(&b, "reached code that should be unreachable");
        if (!ttisnil(first))
        {
            luaL_addstring(&b, ": ");
            addvariantstring(&b, first);
        }
        break;
    default:
        LUAU_ASSERT(!"not an error variant");
    }

    luaL_pushresult(&b);
}

template<LuwuBuiltinClass Kind>
static int varianttostring(lua_State* L)
{
    LuauObject* variant = luaR_checknativeself(L, builtinclass(L, Kind), "__tostring");
    pushvariantmessage(L, Kind, luaR_members(variant), int(variant->numberofmembers));
    return 1;
}

l_noret luaR_raisevariant(lua_State* L, LuwuBuiltinClass kind, int nargs)
{
    StkId first = L->top - nargs;
    LuauClass* classdef = builtinclass(L, kind);
    if (!classdef)
    {
        pushvariantmessage(L, kind, first, nargs);
        luaG_runerror(L, "%s", svalue(L->top - 1));
    }

    // A new object is white, so storing into it needs no barrier
    LuauObject* variant = luaR_newobjectuninit(L, classdef);
    TValue* members = luaR_members(variant);
    for (int i = 0; i < int(variant->numberofmembers); i++)
        setobj(L, &members[i], variantfield(first, nargs, i));

    setobjectvalue(L, first, variant);
    L->top = first + 1;
    luaD_throw(L, LUA_ERRRUN);
}

static const luaR_NativeMember kOverflowMembers[] = {
    {"value", NULL, kPayloadField},
    {"info", NULL, kPayloadField},
    {"__tostring", varianttostring<LUWU_BUILTIN_ERR_OVERFLOW>, 0},
};

static const luaR_NativeMember kUnderflowMembers[] = {
    {"value", NULL, kPayloadField},
    {"info", NULL, kPayloadField},
    {"__tostring", varianttostring<LUWU_BUILTIN_ERR_UNDERFLOW>, 0},
};

static const luaR_NativeMember kCannotBeParsedMembers[] = {
    {"text", NULL, kPayloadField},
    {"message", NULL, kPayloadField},
    {"__tostring", varianttostring<LUWU_BUILTIN_ERR_CANNOTBEPARSED>, 0},
};

static const luaR_NativeMember kUnexpectedFloatMembers[] = {
    {"value", NULL, kPayloadField},
    {"__tostring", varianttostring<LUWU_BUILTIN_ERR_UNEXPECTEDFLOAT>, 0},
};

static const luaR_NativeMember kInvalidArgumentMembers[] = {
    {"value", NULL, kPayloadField},
    {"message", NULL, kPayloadField},
    {"__tostring", varianttostring<LUWU_BUILTIN_ERR_INVALIDARGUMENT>, 0},
};

static const luaR_NativeMember kDivisionByZeroMembers[] = {
    {"__tostring", varianttostring<LUWU_BUILTIN_ERR_DIVISIONBYZERO>, 0},
};

static const luaR_NativeMember kInvalidComparisonMembers[] = {
    {"left", NULL, kPayloadField},
    {"right", NULL, kPayloadField},
    {"__tostring", varianttostring<LUWU_BUILTIN_ERR_INVALIDCOMPARISON>, 0},
};

static const luaR_NativeMember kUnreachableMembers[] = {
    {"message", NULL, kPayloadField},
    {"__tostring", varianttostring<LUWU_BUILTIN_ERR_UNREACHABLE>, 0},
};

struct ErrorVariant
{
    LuwuBuiltinClass kind;
    // The variant's name in `Error`
    const char* member;
    // The class's name, which `typeof` gives its objects
    const char* name;
    const luaR_NativeMember* members;
    int count;
};

#define LUWU_ERROR_VARIANT(kind, member, members) {kind, member, "Error." member, members, int(sizeof(members) / sizeof(members[0]))}

static const ErrorVariant kErrorVariants[] = {
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_OVERFLOW, "Overflow", kOverflowMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNDERFLOW, "Underflow", kUnderflowMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_CANNOTBEPARSED, "CannotBeParsed", kCannotBeParsedMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNEXPECTEDFLOAT, "UnexpectedFloat", kUnexpectedFloatMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_INVALIDARGUMENT, "InvalidArgument", kInvalidArgumentMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_DIVISIONBYZERO, "DivisionByZero", kDivisionByZeroMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_INVALIDCOMPARISON, "InvalidComparison", kInvalidComparisonMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNREACHABLE, "Unreachable", kUnreachableMembers),
};

#undef LUWU_ERROR_VARIANT


// Builds a class, records it as builtin `kind` and makes it the global `name`. `typeofname` is what `typeof` gives its
// objects.
template<size_t N>
static void addclass(
    lua_State* L,
    LuwuBuiltinClass kind,
    const char* name,
    const char* typeofname,
    const luaR_NativeMember (&members)[N],
    lua_CFunction init
)
{
    LuauClass* classdef = luaR_newnativeclass(L, name, members, int(N), init);
    classdef->typeofname = luaS_new(L, typeofname);
    L->global->builtinclasses[kind] = classdef;
    lua_setglobal(L, name);
}

int luaopen_result(lua_State* L)
{
    addclass(L, LUWU_BUILTIN_SOME, "Some", "some", kSomeMembers, payloadinit);
    addclass(L, LUWU_BUILTIN_OK, "Ok", "ok", kOkMembers, payloadinit);
    addclass(L, LUWU_BUILTIN_ERR, "Err", "err", kErrMembers, luaR_fieldsinit);

    lua_createtable(L, 0, int(sizeof(kErrorVariants) / sizeof(kErrorVariants[0])));
    for (const ErrorVariant& variant : kErrorVariants)
    {
        LuauClass* classdef = luaR_newnativeclass(L, variant.name, variant.members, variant.count, luaR_fieldsinit);
        classdef->typeofname = luaS_new(L, variant.name);
        L->global->builtinclasses[variant.kind] = classdef;
        lua_setfield(L, -2, variant.member);
    }
    lua_setreadonly(L, -1, true);
    lua_setglobal(L, "Error");

    lua_pushsymnone(L);
    lua_setglobal(L, "None");

    setnonemetatable(L);
    return 0;
}
