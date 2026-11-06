#include "lapi.h"
#include "lbytecode.h"
#include "lclass.h"
#include "lobject.h"
#include "lstring.h"
#include "lua.h"
#include "lualib.h"
#include "lstate.h"
#include "ldo.h"
#include "lgc.h"
#include "ltable.h"

#include <string.h>

#include "Luau/Common.h"

LUAU_FASTFLAG(LuwuNonePrimitive)
LUAU_FASTFLAG(DebugLuwuCollections)
LUAU_FASTFLAG(DebugLuwuTryExpressions)

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

// Luwu Traits (rfcs/classes/traits.md): class.implements(value, trait) -> value is an object whose class implements the
// trait, listed or implied through `needs`
static int class_implements(lua_State* L)
{
    luaL_checkany(L, 1);
    luaL_checktype(L, 2, LUA_TCLASS);
    const TValue* value = luaA_toobject(L, 1);
    const LuauClass* trait = classvalue(luaA_toobject(L, 2));

    if (!trait->istrait)
        luaL_error(L, "class.implements expects a trait, but '%s' is a class; use class.isinstance", getstr(trait->name));

    lua_pushboolean(L, ttisobject(value) && luaR_implements(objectvalue(value)->lclass, trait));
    return 1;
}

static int class_of(lua_State* L)
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

        const TValue* value = object ? &luaR_members(object)[offset] : NULL;
        if (value && !ttisnil(value))
            luaA_pushvalue(L, value);
        else if (FFlag::LuwuNonePrimitive)
            lua_pushsymnone(L);
        else
            lua_pushnil(L);

        lua_rawset(L, -3);
    }

    lua_pushboolean(L, complete);
    return 2;
}

// Luwu class reflection (rfcs/classes): `ClassMember`, one member of a class as `class.member` and `class.fields` give it.
// `owner` and `offset` are private: `get` and `__tostring` read by them. Only the VM constructs one: its constructor is
// private, so `class.of(member)` can't make another.
enum ClassMemberField
{
    kMemberName,
    kMemberValue,
    kMemberIsConst,
    kMemberIsFinal,
    kMemberVisibility,
    kMemberPositional,
    kMemberOwner,
    kMemberOffset,
};

static int classmember_init(lua_State* L)
{
    luaL_error(L, "ClassMember objects come from class.member and class.fields");
}

static LuauClass* classmemberclass(lua_State* L)
{
    return L->global->builtinclasses[LUWU_BUILTIN_CLASSMEMBER];
}

// The class and offset of the member a ClassMember describes
static const LuauClass* memberowner(const LuauObject* member, uint32_t* offset)
{
    const TValue* fields = luaR_members(member);
    *offset = uint32_t(nvalue(&fields[kMemberOffset]));
    return classvalue(&fields[kMemberOwner]);
}

// member:get(object): the member's value in `object`, which must be an object of the member's class
static int classmember_get(lua_State* L)
{
    uint32_t offset = 0;
    const LuauClass* owner = memberowner(luaR_checknativeself(L, classmemberclass(L), "get"), &offset);

    const TValue* object = luaA_toobject(L, 2);
    bool ofOwner = object && ttisobject(object) && objectvalue(object)->lclass == owner;
    if (!ofOwner)
        luaL_typeerror(L, 2, getstr(owner->name));

    if (offset < owner->numberofinstancemembers)
        luaA_pushvalue(L, &luaR_members(objectvalue(object))[offset]);
    else
        luaA_pushvalue(L, &owner->staticmembers[offset - owner->numberofinstancemembers]);
    return 1;
}

// tostring(member): the member as a class body declares it, without its type: `public const x = 1`, `public x`,
// `public final function f`
static int classmember_tostring(lua_State* L)
{
    LuauObject* member = luaR_checknativeself(L, classmemberclass(L), "__tostring");
    uint32_t offset = 0;
    const LuauClass* owner = memberowner(member, &offset);
    const TValue* fields = luaR_members(member);
    uint8_t flags = owner->memberflags[offset];
    bool isfield = offset < owner->numberofinstancemembers;

    luaL_Strbuf buffer;
    luaL_buffinit(L, &buffer);
    luaL_addstring(&buffer, (flags & LBC_CLASSMEMBER_PRIVATE) ? "private " : "public ");
    if (flags & LBC_CLASSMEMBER_FINAL)
        luaL_addstring(&buffer, "final ");
    if (isfield && (flags & LBC_CLASSMEMBER_CONST))
        luaL_addstring(&buffer, "const ");
    if (!isfield)
        luaL_addstring(&buffer, "function ");
    luaL_addstring(&buffer, getstr(owner->offsettomember[offset]));

    // a field read through an object has a value; `none` is the empty option
    if (isfield && !ttissymnone(&fields[kMemberValue]))
    {
        luaL_addstring(&buffer, " = ");
        luaA_pushvalue(L, &fields[kMemberValue]);
        lua_getpayload(L, -1);
        if (lua_type(L, -1) == LUA_TSTRING)
        {
            size_t length = 0;
            const char* text = lua_tolstring(L, -1, &length);
            luaL_addchar(&buffer, '"');
            luaL_addlstring(&buffer, text, length);
            luaL_addchar(&buffer, '"');
            lua_pop(L, 1);
        }
        else
        {
            luaL_tolstring(L, -1, nullptr);
            lua_remove(L, -2);
            luaL_addvalue(&buffer);
        }
        lua_pop(L, 2);
    }

    luaL_pushresult(&buffer);
    return 1;
}

static const luaR_NativeMember kClassMemberMembers[] = {
    {"name", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"value", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"isconst", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"isfinal", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"visibility", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"positional", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"owner", nullptr, LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST, nullptr},
    {"offset", nullptr, LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST, nullptr},
    {"get", classmember_get, 0, nullptr},
    {"__tostring", classmember_tostring, 0, nullptr},
};

// Whether the member at `offset` of `cls` is one of its primary constructor's parameters
static bool ispositional(const LuauClass* cls, uint32_t offset)
{
    for (int i = 0; i < cls->numberofpositional; i++)
        if (cls->positionalmembers[i] == offset)
            return true;

    return false;
}

// Sets field `field` of the new `member`. A new object is white, but filling it allocates, so each store has a barrier.
static void setmemberfield(lua_State* L, LuauObject* member, ClassMemberField field, const TValue* value)
{
    setobj2class(L, &luaR_members(member)[field], value);
    luaC_barrier(L, member, value);
}

// Pushes a new ClassMember for the member at `offset` of `cls`. Through an object its value is `Some(value)`; a field
// read through the class has none. The strings are the class's own and interned literals, so the member itself and a
// `Some` are all that's allocated.
static void pushnewclassmember(lua_State* L, LuauClass* cls, uint32_t offset, const LuauObject* object)
{
    LuauClass* membercls = classmemberclass(L);
    LUAU_ASSERT(membercls);

    lua_checkstack(L, 3);
    LuauObject* member = luaR_newobjectuninit(L, membercls);
    TValue* fields = luaR_members(member);
    for (uint32_t i = 0; i < member->numberofmembers; i++)
        setnilvalue(&fields[i]);
    setobjectvalue(L, L->top, member);
    incr_top(L);

    uint8_t flags = cls->memberflags[offset];
    bool isfield = offset < cls->numberofinstancemembers;
    TValue value;

    setsvalue(L, &value, cls->offsettomember[offset]);
    setmemberfield(L, member, kMemberName, &value);

    setbvalue(&value, isfield && (flags & LBC_CLASSMEMBER_CONST) != 0);
    setmemberfield(L, member, kMemberIsConst, &value);
    setbvalue(&value, (flags & LBC_CLASSMEMBER_FINAL) != 0);
    setmemberfield(L, member, kMemberIsFinal, &value);
    setbvalue(&value, isfield && ispositional(cls, offset));
    setmemberfield(L, member, kMemberPositional, &value);

    setsvalue(L, &value, (flags & LBC_CLASSMEMBER_PRIVATE) ? luaS_newliteral(L, "private") : luaS_newliteral(L, "public"));
    setmemberfield(L, member, kMemberVisibility, &value);

    setclassvalue(L, &value, cls);
    setmemberfield(L, member, kMemberOwner, &value);
    setnvalue(&value, double(offset));
    setmemberfield(L, member, kMemberOffset, &value);

    if (isfield && !object)
    {
        setsymnonevalue(&value);
        setmemberfield(L, member, kMemberValue, &value);
        return;
    }

    if (isfield)
        luaA_pushvalue(L, &luaR_members(object)[offset]);
    else
        luaA_pushvalue(L, &cls->staticmembers[offset - cls->numberofinstancemembers]);
    lua_pushsome(L, -1);
    setmemberfield(L, member, kMemberValue, L->top - 1);
    L->top -= 2;
}

// Pushes `cls`'s cache of its members read through the class (LuauClass::reflection), creating it the first time: the
// ClassMember of each member by offset, and the lists `class.fields` gives with and without private fields
static void pushreflection(lua_State* L, LuauClass* cls)
{
    if (!cls->reflection)
    {
        cls->reflection = luaH_new(L, 0, 2);
        luaC_objbarrier(L, cls, cls->reflection);
    }

    lua_checkstack(L, 1);
    sethvalue(L, L->top, cls->reflection);
    incr_top(L);
}

// Pushes the ClassMember of the member at `offset` of `cls`: through the class it's created once and kept
static void pushclassmember(lua_State* L, LuauClass* cls, uint32_t offset, const LuauObject* object)
{
    if (object)
        return pushnewclassmember(L, cls, offset, object);

    pushreflection(L, cls);
    if (lua_rawgeti(L, -1, int(offset) + 1) == LUA_TNIL)
    {
        lua_pop(L, 1);
        pushnewclassmember(L, cls, offset, NULL);
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, int(offset) + 1);
    }
    lua_remove(L, -2);
}

// The class and object at argument 1, a class or an object of it
static LuauClass* checkclassorobject(lua_State* L, const LuauObject** object)
{
    const TValue* value = luaA_toobject(L, 1);
    *object = NULL;

    if (value && ttisclass(value))
        return classvalue(value);
    if (value && ttisobject(value))
    {
        *object = objectvalue(value);
        return (*object)->lclass;
    }

    luaL_typeerror(L, 1, "class or object");
}

// Whether the code calling into the class library may read `cls`'s private members: the class's methods, and the copies
// of trait defaults the class runs
static bool callerreadsprivate(lua_State* L, const LuauClass* cls)
{
    const Closure* caller = luaR_callinglua(L);
    return caller && luaR_closureownsprivateaccess(cls, caller);
}

// class.member(x, name): the member `name` of the class or object `x`, as a ClassMember. Raises when the class has no such
// member, or it's private and the caller isn't the class's code.
static int class_member(lua_State* L)
{
    const LuauObject* object = NULL;
    LuauClass* cls = checkclassorobject(L, &object);
    const char* name = luaL_checkstring(L, 2);

    const TValue* offset = luaH_getstr(cls->memberstooffset, luaS_new(L, name));
    bool member = !ttisnil(offset) && strcmp(name, "__init") != 0;
    if (!member)
        luaL_error(L, "class '%s' has no member '%s'", getstr(cls->name), name);

    uint32_t off = uint32_t(nvalue(offset));
    if ((cls->memberflags[off] & LBC_CLASSMEMBER_PRIVATE) && !callerreadsprivate(L, cls))
        luaL_error(L, "'%s' is private to class '%s'", name, getstr(cls->name));

    pushclassmember(L, cls, off, object);
    return 1;
}

// Pushes a new frozen list of the fields of `cls` as ClassMembers: a primary constructor's parameters in the order it takes
// them, then the others in declaration order. Private ones only with `withprivate`.
static void pushfieldlist(lua_State* L, LuauClass* cls, const LuauObject* object, bool withprivate)
{
    lua_createtable(L, int(cls->numberofinstancemembers), 0);
    int list = lua_gettop(L);
    int count = 0;

    auto add = [&](uint32_t offset)
    {
        if ((cls->memberflags[offset] & LBC_CLASSMEMBER_PRIVATE) && !withprivate)
            return;

        pushclassmember(L, cls, offset, object);
        lua_rawseti(L, list, ++count);
    };

    for (int i = 0; i < cls->numberofpositional; i++)
        add(cls->positionalmembers[i]);

    for (uint32_t offset = 0; offset < cls->numberofinstancemembers; offset++)
    {
        if (!ispositional(cls, offset))
            add(offset);
    }

    lua_setreadonly(L, list, true);
}

// class.fields(x): the fields of the class or object `x` as ClassMembers (pushfieldlist). Private ones are listed only for
// the class's own code. Through the class, the list and its members are the same every call.
static int class_members(lua_State* L)
{
    const LuauObject* object = NULL;
    LuauClass* cls = checkclassorobject(L, &object);
    bool withprivate = callerreadsprivate(L, cls);

    if (object)
    {
        pushfieldlist(L, cls, object, withprivate);
        return 1;
    }

    const char* key = withprivate ? "all" : "public";
    pushreflection(L, cls);
    if (lua_rawgetfield(L, -1, key) == LUA_TNIL)
    {
        lua_pop(L, 1);
        pushfieldlist(L, cls, NULL, withprivate);
        lua_pushvalue(L, -1);
        lua_rawsetfield(L, -3, key);
    }
    return 1;
}


static int class_name(lua_State* L)
{
    const char* name = lua_getclassname(L, 1);
    luaL_argexpected(L, name, 1, "class or object");
    lua_pushstring(L, name);
    return 1;
}

static const luaL_Reg classlib[] = {
    {"isinstance", class_isinstance},
    {"implements", class_implements},
    {"of", class_of},
    {"name", class_name},
    {"fields", class_fields},
    {nullptr, nullptr},
};

/*
** Open class library
*/
int luaopen_class(lua_State* L)
{
    luaL_register(L, LUA_CLASSLIBNAME, classlib);

    // Luwu class reflection (rfcs/classes): a ClassMember's modifiers are a bundle and its value an option
    if (FFlag::DebugLuwuCollections && FFlag::DebugLuwuTryExpressions && FFlag::LuwuNonePrimitive)
    {
        LuauClass* classmember =
            luaR_newnativeclass(L, "ClassMember", kClassMemberMembers, int(sizeof(kClassMemberMembers) / sizeof(kClassMemberMembers[0])), classmember_init);
        classmember->memberflags[classmember->initoffset] |= LBC_CLASSMEMBER_PRIVATE;
        classmember->hasprivatemembers = true;
        L->global->builtinclasses[LUWU_BUILTIN_CLASSMEMBER] = classmember;
        lua_pop(L, 1);

        lua_pushcfunction(L, class_member, "member");
        lua_setfield(L, -2, "member");
        lua_pushcfunction(L, class_members, "fields");
        lua_setfield(L, -2, "fields");
    }

    return 1;
}