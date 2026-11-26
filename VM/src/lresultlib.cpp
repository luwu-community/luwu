// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lualib.h"

#include "lapi.h"
#include "lclass.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "linteger.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltm.h"
#include "ltable.h"
#include "lnumutils.h"
#include "lvm.h"

#include <ctype.h>
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
static const uint16_t kPayloadField = LBC_CLASSMEMBER_CONST | LBC_CLASSMEMBER_POSITIONAL;

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
    {"__tostring", luaR_builtinclasstostring, LBC_CLASSMEMBER_STATIC},
};

static const luaR_NativeMember kOkMembers[] = {
    {"value", NULL, kPayloadField},
    {"ok", payloadget<LUWU_BUILTIN_OK>, 0, payloadgetfast},
    {"expect", payloadexpect<LUWU_BUILTIN_OK>, 0, payloadexpectfast},
    {"err", okerr, 0, nonefast},
    {"__eq", payloadeq<LUWU_BUILTIN_OK>, 0},
    {"__tostring", payloadtostring<LUWU_BUILTIN_OK>, 0},
    {"__tostring", luaR_builtinclasstostring, LBC_CLASSMEMBER_STATIC},
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
    {"__tostring", luaR_builtinclasstostring, LBC_CLASSMEMBER_STATIC},
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

// Luwu structured runtime errors: the builtin error classes in the global `Error`, such as `Error.NotIndexable`.
// `Error` is a frozen table until enums and nested classes exist. Each variant's fields are its positions, so
// `Error.Overflow(value, info)` matches `is Error.Overflow(got, mode)`. The VM and the libraries raise them through the
// error record (luaG_raiseerror), which `try` turns into an object of the variant and `pcall` into the variant's
// message. `__tostring` gives that message without the position.

// The `kind` of an `Error.Runtime`, by LuwuRuntimeKind
static const char* const kRuntimeKindNames[] = {
    "Other",
    "Type",
    "Member",
    "Library",
};

static_assert(sizeof(kRuntimeKindNames) / sizeof(kRuntimeKindNames[0]) == LUWU_RUNTIME__COUNT, "a name for every runtime kind");

// The `kind` of an `Error.NotIndexable` or an `Error.NotCallable`, by LuwuNameKind; LUWU_NAME_NONE has none
static const char* const kNameKindNames[] = {
    NULL,
    "Local",
    "Global",
    "Field",
    "Upvalue",
    "Method",
};

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

// Adds the name of the `integer.Mode` object `value` (`u8`), or `value` as `tostring` gives it when it isn't one
static void addmodename(luaL_Strbuf* b, const TValue* value)
{
    luaA_pushvalue(b->L, value);
    int mode = luaZ_tomode(b->L, -1);
    lua_pop(b->L, 1);
    if (mode >= 0)
        luaL_addstring(b, luaZ_modename(IntegerMode(mode)));
    else
        addvariantstring(b, value);
}

// Adds ": " and `value` as `tostring` gives it, unless it's nil
static void addvariantdetail(luaL_Strbuf* b, const TValue* value)
{
    if (ttisnil(value))
        return;
    luaL_addstring(b, ": ");
    addvariantstring(b, value);
}

// The positions of an error variant and what the error record knows about them besides their values
struct VariantFields
{
    const TValue* values;
    int count;
    // the record's subkind, 0 for an object of the variant
    uint8_t subkind;
    // the type names of positions that hold an opaque value's placeholder, or NULL
    TString* const* opaquetypes;

    const TValue* at(int index) const
    {
        return variantfield(values, count, index);
    }

    // The type name a message gives position `index`: the name an opaque value had, or the value's
    const char* type(lua_State* L, int index) const
    {
        if (opaquetypes && index < count && opaquetypes[index])
            return getstr(opaquetypes[index]);
        return luaT_objtypename(L, at(index));
    }

    bool isopaque(int index) const
    {
        return opaquetypes && index < count && opaquetypes[index];
    }
};

// Adds " (local 'foo')" when position `nameindex` holds a name and the next one how it was reached, `Local`, which the
// message gives in lowercase
static void addvariantname(luaL_Strbuf* b, const VariantFields& fields, int nameindex)
{
    const TValue* name = fields.at(nameindex);
    const TValue* kind = fields.at(nameindex + 1);
    if (!ttisstring(name) || !ttisstring(kind) || tsvalue(kind)->len == 0)
        return;

    luaL_addstring(b, " (");
    luaL_addchar(b, char(tolower(svalue(kind)[0])));
    luaL_addstring(b, svalue(kind) + 1);
    luaL_addstring(b, " '");
    luaL_addstring(b, svalue(name));
    luaL_addstring(b, "')");
}

// The longest string key an index error quotes; a longer one is named by its type
static const size_t kQuotedKeyLimit = 64;

// Adds "invalid argument #2 to 'rep'" or "missing argument #2" for the argument at `positionindex`, followed by the
// function's name. Returns false when the variant has no argument position, as when user code made it.
static bool addargumentposition(luaL_Strbuf* b, const VariantFields& fields, int positionindex, bool missing)
{
    const TValue* position = fields.at(positionindex);
    const TValue* functionname = fields.at(positionindex + 1);
    if (!ttisnumber(position))
        return false;

    char text[64];
    snprintf(text, sizeof(text), "%s argument #%d", missing ? "missing" : "invalid", int(nvalue(position)));
    luaL_addstring(b, text);
    if (ttisstring(functionname))
    {
        luaL_addstring(b, " to '");
        luaL_addstring(b, svalue(functionname));
        luaL_addchar(b, '\'');
    }
    return true;
}

// The message of an arithmetic, comparison, concatenation or length error (InvalidOperation), by the operator's name
static void addoperationmessage(luaL_Strbuf* b, lua_State* L, const VariantFields& fields)
{
    // a variant built without its operator
    if (!ttisstring(fields.at(0)))
    {
        luaL_addstring(b, "invalid operation");
        return;
    }

    const char* op = svalue(fields.at(0));
    const char* left = fields.type(L, 1);
    const char* right = fields.type(L, 2);
    char text[LUA_BUFFERSIZE];

    if (strcmp(op, "concat") == 0)
        snprintf(text, sizeof(text), "attempt to concatenate %s with %s", left, right);
    else if (strcmp(op, "len") == 0)
        snprintf(text, sizeof(text), "attempt to get length of a %s value", left);
    else if (strcmp(op, "lt") == 0 || strcmp(op, "le") == 0 || strcmp(op, "eq") == 0)
        snprintf(text, sizeof(text), "attempt to compare %s %s %s", left, op[1] == 't' ? "<" : op[1] == 'e' ? "<=" : "==", right);
    else if (strcmp(left, right) == 0)
        snprintf(text, sizeof(text), "attempt to perform arithmetic (%s) on %s", op, left);
    else
        snprintf(text, sizeof(text), "attempt to perform arithmetic (%s) on %s and %s", op, left, right);

    luaL_addstring(b, text);
}

// The message of a missing member (MissingMember). A record's subkind 1 says the class in `object` stands for one of
// its objects, which is how an opaque class's private field reads as missing.
static void addmissingmembermessage(luaL_Strbuf* b, lua_State* L, const VariantFields& fields)
{
    // a variant built without the member's name
    if (!ttisstring(fields.at(1)))
    {
        luaL_addstring(b, "missing member");
        return;
    }

    const TValue* object = fields.at(0);
    const char* name = svalue(fields.at(1));
    char text[LUA_BUFFERSIZE];

    const LuauClass* classdef = NULL;
    bool onobject = false;
    if (ttisclass(object))
    {
        classdef = classvalue(object);
        onobject = fields.subkind == 1;
    }
    else if (ttisobject(object))
    {
        classdef = objectvalue(object)->lclass;
        onobject = true;
    }

    // Luwu Traits (rfcs/classes/traits.md): the class has the member, but only for reads through the class value
    const char* statickind = onobject ? luaR_staticmemberkind(classdef, tsvalue(fields.at(1))) : NULL;

    if (!classdef)
        snprintf(text, sizeof(text), "this %s does not have a field named '%s'", fields.type(L, 0), name);
    else if (onobject && statickind)
        snprintf(
            text,
            sizeof(text),
            "objects of class '%s' can't reach its static %s '%s'; use it through the class, or declare it 'shared'",
            getstr(classdef->name),
            statickind,
            name
        );
    else if (onobject)
        snprintf(text, sizeof(text), "objects of class '%s' do not have a field or function named '%s'", getstr(classdef->name), name);
    // Luwu Traits (rfcs/classes/traits.md): a trait has no objects of its own to point at
    else if (classdef->istrait)
        snprintf(text, sizeof(text), "trait '%s' does not have a member named '%s'", getstr(classdef->name), name);
    else
        snprintf(
            text,
            sizeof(text),
            "class '%s' does not have a member named '%s'; did you mean to access a field of an object of this class instead?",
            getstr(classdef->name),
            name
        );

    luaL_addstring(b, text);
}

// The message of a write to something read-only (ReadOnly). A record's subkind says which: 0 a readonly table, 1 a const
// field written outside its class's `__init`, 2 one written by `__init` on another object.
static void addreadonlymessage(luaL_Strbuf* b, const VariantFields& fields)
{
    const TValue* target = fields.at(0);
    const TValue* name = fields.at(1);
    const LuauClass* classdef = ttisobject(target) ? objectvalue(target)->lclass : ttisclass(target) ? classvalue(target) : NULL;
    if (!classdef || !ttisstring(name))
    {
        luaL_addstring(b, "attempt to modify a readonly table");
        return;
    }

    const char* member = svalue(name);
    const char* owner = getstr(classdef->name);
    char text[LUA_BUFFERSIZE];
    if (fields.subkind == 2)
        snprintf(
            text,
            sizeof(text),
            "'%s' is a const member of '%s'; %s's '__init' can only assign it on the object it is constructing",
            member,
            owner,
            owner
        );
    else
        snprintf(text, sizeof(text), "'%s' is a const member of '%s' and cannot be assigned outside %s's '__init' constructor", member, owner, owner);
    luaL_addstring(b, text);
}

// Adds one of an `Error.Unreachable`'s invariants on a line of its own
static void addinvariant(luaL_Strbuf* b, const TValue* key, const TValue* value)
{
    luaL_addstring(b, "\n    ");
    addvariantvalue(b, key);
    luaL_addstring(b, " = ");
    addvariantvalue(b, value);
}

// Adds an `Error.Unreachable`'s invariants, a table of what held when it was raised. Showing a value can run its
// `__tostring`, which can change the table, so every step reads the table's parts again.
static void addinvariants(luaL_Strbuf* b, const TValue* invariants)
{
    if (ttisnil(invariants))
        return;

    luaL_addstring(b, "\ninvariants:");
    if (!ttistable(invariants))
    {
        luaL_addchar(b, ' ');
        addvariantvalue(b, invariants);
        return;
    }

    LuaTable* h = hvalue(invariants);
    for (int i = 0; i < h->sizearray; i++)
    {
        if (ttisnil(&h->array[i]))
            continue;
        TValue key;
        setnvalue(&key, i + 1);
        TValue value = h->array[i];
        addinvariant(b, &key, &value);
    }
    for (int i = 0; i < sizenode(h); i++)
    {
        LuaNode* node = gnode(h, i);
        if (ttisnil(gval(node)))
            continue;
        TValue key;
        getnodekey(b->L, &key, node);
        TValue value = *gval(node);
        addinvariant(b, &key, &value);
    }
}

// Adds "what was not found"-style messages: `subject` names what the variant is about when it's there, then the
// message after a colon
static void addsubjectmessage(luaL_Strbuf* b, const VariantFields& fields, const char* alone, const char* after)
{
    if (ttisnil(fields.at(0)))
    {
        luaL_addstring(b, alone);
    }
    else
    {
        addvariantvalue(b, fields.at(0));
        luaL_addstring(b, after);
    }
    addvariantdetail(b, fields.at(1));
}

// Adds the message of error variant `kind` to `b`
static void addvariantmessage(luaL_Strbuf* b, LuwuBuiltinClass kind, const VariantFields& fields)
{
    lua_State* L = b->L;
    const TValue* first = fields.at(0);
    const TValue* second = fields.at(1);

    switch (kind)
    {
    case LUWU_BUILTIN_ERR_RUNTIME:
        if (ttisnil(second))
            luaL_addstring(b, "runtime error");
        else
            addvariantstring(b, second);
        break;
    case LUWU_BUILTIN_ERR_NOTINDEXABLE:
    {
        const TValue* index = fields.at(3);
        luaL_addstring(b, "attempt to index ");
        luaL_addstring(b, fields.type(L, 2));
        addvariantname(b, fields, 0);
        luaL_addstring(b, " with ");
        if (ttisstring(index) && tsvalue(index)->len <= kQuotedKeyLimit && !fields.isopaque(3))
        {
            luaL_addchar(b, '\'');
            luaL_addstring(b, svalue(index));
            luaL_addchar(b, '\'');
        }
        else
        {
            luaL_addstring(b, fields.type(L, 3));
        }
        break;
    }
    case LUWU_BUILTIN_ERR_INVALIDKEY:
        if (ttisnil(second))
            luaL_addstring(b, "table index is nil");
        else if (ttisnumber(second) && luai_numisnan(nvalue(second)))
            luaL_addstring(b, "table index is NaN");
        else if (ttisvector(second) && luai_vecisnan(vvalue(second)))
            luaL_addstring(b, "table index contains NaN");
        else
            luaL_addstring(b, "invalid table index");
        break;
    case LUWU_BUILTIN_ERR_NOTCALLABLE:
        luaL_addstring(b, "attempt to call a ");
        luaL_addstring(b, fields.type(L, 2));
        luaL_addstring(b, " value");
        addvariantname(b, fields, 0);
        break;
    case LUWU_BUILTIN_ERR_INVALIDOPERATION:
        addoperationmessage(b, L, fields);
        break;
    case LUWU_BUILTIN_ERR_MISSINGMEMBER:
        addmissingmembermessage(b, L, fields);
        break;
    case LUWU_BUILTIN_ERR_READONLY:
        addreadonlymessage(b, fields);
        break;
    case LUWU_BUILTIN_ERR_DIVISIONBYZERO:
        luaL_addstring(b, "attempt to divide by zero");
        break;
    case LUWU_BUILTIN_ERR_NOTYIELDABLE:
        luaL_addstring(b, "attempt to yield across metamethod/C-call boundary");
        break;
    case LUWU_BUILTIN_ERR_STACKOVERFLOW:
        luaL_addstring(b, fields.subkind == 1 ? "C stack overflow" : "stack overflow");
        break;
    case LUWU_BUILTIN_ERR_INVALIDARGUMENT:
        if (addargumentposition(b, fields, 2, false))
        {
            luaL_addstring(b, " (");
            addvariantstring(b, second);
            luaL_addchar(b, ')');
        }
        else if (!ttisnil(second))
        {
            addvariantstring(b, second);
        }
        else
        {
            luaL_addstring(b, "invalid argument");
            if (!ttisnil(first))
            {
                luaL_addchar(b, ' ');
                addvariantvalue(b, first);
            }
        }
        break;
    case LUWU_BUILTIN_ERR_UNEXPECTEDTYPE:
    {
        // a missing argument is `none`
        bool missing = ttissymnone(fields.at(1));
        bool positioned = addargumentposition(b, fields, 2, missing);
        if (positioned)
            luaL_addstring(b, " (");
        // a variant built without the type it expected
        if (ttisnil(first))
        {
            luaL_addstring(b, "unexpected type");
            if (positioned)
                luaL_addchar(b, ')');
            break;
        }
        addvariantstring(b, first);
        luaL_addstring(b, " expected");
        if (!missing)
        {
            luaL_addstring(b, ", got ");
            luaL_addstring(b, fields.type(L, 1));
        }
        if (positioned)
            luaL_addchar(b, ')');
        break;
    }
    case LUWU_BUILTIN_ERR_ASSERTIONFAILED:
        if (ttisnil(first))
            luaL_addstring(b, "assertion failed!");
        else
            addvariantstring(b, first);
        break;
    case LUWU_BUILTIN_ERR_OVERFLOW:
    case LUWU_BUILTIN_ERR_UNDERFLOW:
    {
        bool overflow = kind == LUWU_BUILTIN_ERR_OVERFLOW;
        // a raiser that can't build the value, like a power too large to compute, leaves it nil
        if (ttisnil(first))
            luaL_addstring(b, "the result");
        else
            addvariantvalue(b, first);
        if (ttisnil(second))
        {
            luaL_addstring(b, overflow ? " overflowed" : " underflowed");
            break;
        }
        luaL_addstring(b, overflow ? " is too large for " : " is too small for ");
        addmodename(b, second);
        break;
    }
    case LUWU_BUILTIN_ERR_CANNOTBEPARSED:
        luaL_addstring(b, "couldn't parse");
        if (!ttisnil(first))
        {
            luaL_addchar(b, ' ');
            addvariantvalue(b, first);
        }
        addvariantdetail(b, second);
        break;
    case LUWU_BUILTIN_ERR_UNEXPECTEDFLOAT:
        luaL_addstring(b, "expected a whole number");
        if (!ttisnil(first))
        {
            luaL_addstring(b, ", got ");
            addvariantvalue(b, first);
        }
        break;
    case LUWU_BUILTIN_ERR_UNREACHABLE:
        luaL_addstring(b, "reached code that should be unreachable");
        addvariantdetail(b, first);
        addinvariants(b, second);
        break;
    case LUWU_BUILTIN_ERR_UNIMPLEMENTED:
        luaL_addstring(b, "not implemented yet");
        addvariantdetail(b, first);
        break;
    case LUWU_BUILTIN_ERR_UNSUPPORTED:
        luaL_addstring(b, "not supported");
        addvariantdetail(b, first);
        break;
    case LUWU_BUILTIN_ERR_NOTFOUND:
        addsubjectmessage(b, fields, "not found", " was not found");
        break;
    case LUWU_BUILTIN_ERR_ALREADYEXISTS:
        addsubjectmessage(b, fields, "already exists", " already exists");
        break;
    case LUWU_BUILTIN_ERR_PERMISSIONDENIED:
        luaL_addstring(b, "permission denied");
        if (!ttisnil(first))
        {
            luaL_addstring(b, " for ");
            addvariantvalue(b, first);
        }
        addvariantdetail(b, second);
        break;
    case LUWU_BUILTIN_ERR_OUTOFRANGE:
    {
        const TValue* low = fields.at(1);
        const TValue* high = fields.at(2);
        if (ttisnil(first))
        {
            luaL_addstring(b, "out of range");
        }
        else
        {
            addvariantvalue(b, first);
            luaL_addstring(b, " is out of range");
        }
        if (!ttisnil(low) && !ttisnil(high))
        {
            luaL_addstring(b, " (expected ");
            addvariantvalue(b, low);
            luaL_addstring(b, " to ");
            addvariantvalue(b, high);
            luaL_addchar(b, ')');
        }
        else if (!ttisnil(low) || !ttisnil(high))
        {
            luaL_addstring(b, ttisnil(low) ? " (expected at most " : " (expected at least ");
            addvariantvalue(b, ttisnil(low) ? high : low);
            luaL_addchar(b, ')');
        }
        break;
    }
    case LUWU_BUILTIN_ERR_INVALIDSTATE:
        luaL_addstring(b, "invalid state");
        addvariantdetail(b, first);
        break;
    case LUWU_BUILTIN_ERR_TIMEDOUT:
        luaL_addstring(b, "timed out");
        addvariantdetail(b, first);
        break;
    default:
        LUAU_ASSERT(!"not an error variant");
    }
}

template<LuwuBuiltinClass Kind>
static int varianttostring(lua_State* L)
{
    LuauObject* variant = luaR_checknativeself(L, builtinclass(L, Kind), "__tostring");
    VariantFields fields = {luaR_members(variant), int(variant->numberofmembers), 0, NULL};
    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    addvariantmessage(&b, Kind, fields);
    luaL_pushresult(&b);
    return 1;
}

void luaR_fillerrorsubkind(lua_State* L, LuwuErrorRecord& record)
{
    switch (record.variant)
    {
    case LUWU_BUILTIN_ERR_RUNTIME:
        LUAU_ASSERT(record.subkind < LUWU_RUNTIME__COUNT);
        setsvalue(L, &record.fields[0], luaS_new(L, kRuntimeKindNames[record.subkind]));
        break;
    case LUWU_BUILTIN_ERR_NOTINDEXABLE:
    case LUWU_BUILTIN_ERR_NOTCALLABLE:
        if (record.subkind != LUWU_NAME_NONE)
            setsvalue(L, &record.fields[1], luaS_new(L, kNameKindNames[record.subkind]));
        break;
    case LUWU_BUILTIN_ERR_INVALIDOPERATION:
        LUAU_ASSERT(record.subkind < TM_N);
        // the metamethod's name without its `__`, as the message has it
        setsvalue(L, &record.fields[0], luaS_new(L, luaT_eventname[record.subkind] + 2));
        break;
    default:
        break;
    }
}

void luaR_adderrorrecord(luaL_Strbuf* b, const LuwuErrorRecord& record)
{
    VariantFields fields = {record.fields, record.count, record.subkind, record.opaquetypes};
    addvariantmessage(b, LuwuBuiltinClass(record.variant), fields);
}

LuauObject* luaR_newerrorvariant(lua_State* L)
{
    LuwuErrorRecord& record = L->global->errorrecord;
    LUAU_ASSERT(record.active);
    LuauClass* classdef = builtinclass(L, LuwuBuiltinClass(record.variant));
    if (!classdef)
        return NULL;

    luaR_fillerrorsubkind(L, record);

    // A new object is white, so storing into it needs no barrier
    LuauObject* variant = luaR_newobjectuninit(L, classdef);
    TValue* members = luaR_members(variant);
    for (int i = 0; i < int(variant->numberofmembers); i++)
        setobj(L, &members[i], variantfield(record.fields, record.count, i));
    return variant;
}

// a variant's objects print through `varianttostring`, and the variant itself like every builtin class
#define LUWU_VARIANT_TOSTRING(kind) {"__tostring", varianttostring<kind>, 0}, {"__tostring", luaR_builtinclasstostring, LBC_CLASSMEMBER_STATIC}

static const luaR_NativeMember kRuntimeMembers[] = {
    {"kind", NULL, kPayloadField},
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_RUNTIME),
};

static const luaR_NativeMember kNotIndexableMembers[] = {
    {"name", NULL, kPayloadField},
    {"kind", NULL, kPayloadField},
    {"indexed", NULL, kPayloadField},
    {"index", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_NOTINDEXABLE),
};

static const luaR_NativeMember kInvalidKeyMembers[] = {
    {"indexed", NULL, kPayloadField},
    {"key", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_INVALIDKEY),
};

static const luaR_NativeMember kNotCallableMembers[] = {
    {"name", NULL, kPayloadField},
    {"kind", NULL, kPayloadField},
    {"called", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_NOTCALLABLE),
};

static const luaR_NativeMember kInvalidOperationMembers[] = {
    {"operator", NULL, kPayloadField},
    {"left", NULL, kPayloadField},
    {"right", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_INVALIDOPERATION),
};

static const luaR_NativeMember kMissingMemberMembers[] = {
    {"object", NULL, kPayloadField},
    {"name", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_MISSINGMEMBER),
};

static const luaR_NativeMember kReadOnlyMembers[] = {
    {"target", NULL, kPayloadField},
    {"name", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_READONLY),
};

static const luaR_NativeMember kDivisionByZeroMembers[] = {
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_DIVISIONBYZERO),
};

static const luaR_NativeMember kNotYieldableMembers[] = {
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_NOTYIELDABLE),
};

static const luaR_NativeMember kStackOverflowMembers[] = {
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_STACKOVERFLOW),
};

static const luaR_NativeMember kInvalidArgumentMembers[] = {
    {"value", NULL, kPayloadField},
    {"message", NULL, kPayloadField},
    {"position", NULL, kPayloadField},
    {"functionname", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_INVALIDARGUMENT),
};

static const luaR_NativeMember kUnexpectedTypeMembers[] = {
    {"expected", NULL, kPayloadField},
    {"value", NULL, kPayloadField},
    {"position", NULL, kPayloadField},
    {"functionname", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_UNEXPECTEDTYPE),
};

static const luaR_NativeMember kAssertionFailedMembers[] = {
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_ASSERTIONFAILED),
};

static const luaR_NativeMember kOverflowMembers[] = {
    {"value", NULL, kPayloadField},
    {"info", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_OVERFLOW),
};

static const luaR_NativeMember kUnderflowMembers[] = {
    {"value", NULL, kPayloadField},
    {"info", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_UNDERFLOW),
};

static const luaR_NativeMember kCannotBeParsedMembers[] = {
    {"text", NULL, kPayloadField},
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_CANNOTBEPARSED),
};

static const luaR_NativeMember kUnexpectedFloatMembers[] = {
    {"value", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_UNEXPECTEDFLOAT),
};

static const luaR_NativeMember kUnreachableMembers[] = {
    {"message", NULL, kPayloadField},
    {"invariants", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_UNREACHABLE),
};

static const luaR_NativeMember kUnimplementedMembers[] = {
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_UNIMPLEMENTED),
};

static const luaR_NativeMember kUnsupportedMembers[] = {
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_UNSUPPORTED),
};

static const luaR_NativeMember kNotFoundMembers[] = {
    {"what", NULL, kPayloadField},
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_NOTFOUND),
};

static const luaR_NativeMember kAlreadyExistsMembers[] = {
    {"what", NULL, kPayloadField},
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_ALREADYEXISTS),
};

static const luaR_NativeMember kPermissionDeniedMembers[] = {
    {"what", NULL, kPayloadField},
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_PERMISSIONDENIED),
};

static const luaR_NativeMember kOutOfRangeMembers[] = {
    {"value", NULL, kPayloadField},
    {"min", NULL, kPayloadField},
    {"max", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_OUTOFRANGE),
};

static const luaR_NativeMember kInvalidStateMembers[] = {
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_INVALIDSTATE),
};

static const luaR_NativeMember kTimedOutMembers[] = {
    {"message", NULL, kPayloadField},
    LUWU_VARIANT_TOSTRING(LUWU_BUILTIN_ERR_TIMEDOUT),
};

#undef LUWU_VARIANT_TOSTRING

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
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_RUNTIME, "Runtime", kRuntimeMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_NOTINDEXABLE, "NotIndexable", kNotIndexableMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_INVALIDKEY, "InvalidKey", kInvalidKeyMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_NOTCALLABLE, "NotCallable", kNotCallableMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_INVALIDOPERATION, "InvalidOperation", kInvalidOperationMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_MISSINGMEMBER, "MissingMember", kMissingMemberMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_READONLY, "ReadOnly", kReadOnlyMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_DIVISIONBYZERO, "DivisionByZero", kDivisionByZeroMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_NOTYIELDABLE, "NotYieldable", kNotYieldableMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_STACKOVERFLOW, "StackOverflow", kStackOverflowMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_INVALIDARGUMENT, "InvalidArgument", kInvalidArgumentMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNEXPECTEDTYPE, "UnexpectedType", kUnexpectedTypeMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_ASSERTIONFAILED, "AssertionFailed", kAssertionFailedMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_OVERFLOW, "Overflow", kOverflowMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNDERFLOW, "Underflow", kUnderflowMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_CANNOTBEPARSED, "CannotBeParsed", kCannotBeParsedMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNEXPECTEDFLOAT, "UnexpectedFloat", kUnexpectedFloatMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNREACHABLE, "Unreachable", kUnreachableMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNIMPLEMENTED, "Unimplemented", kUnimplementedMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_UNSUPPORTED, "Unsupported", kUnsupportedMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_NOTFOUND, "NotFound", kNotFoundMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_ALREADYEXISTS, "AlreadyExists", kAlreadyExistsMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_PERMISSIONDENIED, "PermissionDenied", kPermissionDeniedMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_OUTOFRANGE, "OutOfRange", kOutOfRangeMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_INVALIDSTATE, "InvalidState", kInvalidStateMembers),
    LUWU_ERROR_VARIANT(LUWU_BUILTIN_ERR_TIMEDOUT, "TimedOut", kTimedOutMembers),
};

#undef LUWU_ERROR_VARIANT

static_assert(
    sizeof(kErrorVariants) / sizeof(kErrorVariants[0]) == LUWU_BUILTIN_ERR_LAST - LUWU_BUILTIN_ERR_FIRST + 1,
    "a class for every error variant"
);

l_noret luaR_raisevariant(lua_State* L, LuwuBuiltinClass kind, int nargs)
{
    LUAU_ASSERT(nargs <= LUWU_ERROR_MAXFIELDS);
    luaG_raiseerror(L, kind, 0, LUWU_ERRORAT_NEARESTLUA, L->top - nargs, nargs);
}

// `unreachable(message, invariants?)` raises `Error.Unreachable(message, invariants)` at the caller
static int luaR_unreachable(lua_State* L)
{
    luaL_checkstring(L, 1);
    lua_settop(L, 2);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_UNREACHABLE, 0, LUWU_ERRORAT_CALLER, L->base, 2);
}

// `todo(message?)` raises `Error.Unimplemented(message)` at the caller
static int luaR_todo(lua_State* L)
{
    lua_settop(L, 1);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_UNIMPLEMENTED, 0, LUWU_ERRORAT_CALLER, L->base, 1);
}

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

    lua_pushcfunction(L, luaR_unreachable, "unreachable");
    lua_setglobal(L, "unreachable");
    lua_pushcfunction(L, luaR_todo, "todo");
    lua_setglobal(L, "todo");

    lua_pushsymnone(L);
    lua_setglobal(L, "None");

    setnonemetatable(L);
    return 0;
}
