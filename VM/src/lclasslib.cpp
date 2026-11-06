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
// `owner` and `offset` are private: `get` reads by them. The objects can't be constructed.
enum ClassMemberField
{
    kMemberName,
    kMemberValue,
    kMemberModifiers,
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

// member:get(object): the member's value in `object`, which must be an object of the member's class
static int classmember_get(lua_State* L)
{
    LuauObject* member = luaR_checknativeself(L, classmemberclass(L), "get");
    const TValue* fields = luaR_members(member);
    const LuauClass* owner = classvalue(&fields[kMemberOwner]);
    uint32_t offset = uint32_t(nvalue(&fields[kMemberOffset]));

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

static const luaR_NativeMember kClassMemberMembers[] = {
    {"name", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"value", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"modifiers", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"visibility", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"positional", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"owner", nullptr, LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST, nullptr},
    {"offset", nullptr, LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST, nullptr},
    {"get", classmember_get, 0, nullptr},
};

// Whether the member at `offset` of `cls` is one of its primary constructor's parameters
static bool ispositional(const LuauClass* cls, uint32_t offset)
{
    for (int i = 0; i < cls->numberofpositional; i++)
        if (cls->positionalmembers[i] == offset)
            return true;

    return false;
}

// Stores the value on top of the stack into field `field` of `member`, and pops it
static void setmemberfield(lua_State* L, LuauObject* member, ClassMemberField field)
{
    TValue* slot = &luaR_members(member)[field];
    setobj2class(L, slot, L->top - 1);
    luaC_barrier(L, member, L->top - 1);
    L->top--;
}

// Pushes the member's modifiers: a bundle of "const" and "final" as the member has them, or `none`
static void pushmodifiers(lua_State* L, const LuauClass* cls, uint32_t offset)
{
    uint8_t flags = cls->memberflags[offset];
    bool isfield = offset < cls->numberofinstancemembers;
    int count = 0;

    if (isfield && (flags & LBC_CLASSMEMBER_CONST))
    {
        lua_pushliteral(L, "const");
        count++;
    }
    if (flags & LBC_CLASSMEMBER_FINAL)
    {
        lua_pushliteral(L, "final");
        count++;
    }

    if (count == 0)
        lua_pushsymnone(L);
    else
        lua_packbundle(L, count);
}

// Pushes a new ClassMember for the member at `offset` of `cls`. Through an object its value is `Some(value)`; a field
// read through the class has none.
static void pushclassmember(lua_State* L, LuauClass* cls, uint32_t offset, const LuauObject* object)
{
    LuauClass* membercls = classmemberclass(L);
    LUAU_ASSERT(membercls);

    lua_checkstack(L, 4);
    LuauObject* member = luaR_newobjectuninit(L, membercls);
    for (uint32_t i = 0; i < member->numberofmembers; i++)
        setnilvalue(&luaR_members(member)[i]);
    setobjectvalue(L, L->top, member);
    incr_top(L);

    bool isfield = offset < cls->numberofinstancemembers;

    lua_pushstring(L, getstr(cls->offsettomember[offset]));
    setmemberfield(L, member, kMemberName);

    if (isfield && object)
        luaA_pushvalue(L, &luaR_members(object)[offset]);
    else if (!isfield)
        luaA_pushvalue(L, &cls->staticmembers[offset - cls->numberofinstancemembers]);

    if (isfield && !object)
    {
        lua_pushsymnone(L);
    }
    else
    {
        lua_pushsome(L, -1);
        lua_remove(L, -2);
    }
    setmemberfield(L, member, kMemberValue);

    pushmodifiers(L, cls, offset);
    setmemberfield(L, member, kMemberModifiers);

    if (cls->memberflags[offset] & LBC_CLASSMEMBER_PRIVATE)
        lua_pushliteral(L, "private");
    else
        lua_pushliteral(L, "public");
    setmemberfield(L, member, kMemberVisibility);

    lua_pushboolean(L, isfield && ispositional(cls, offset));
    setmemberfield(L, member, kMemberPositional);

    luaA_pushclass(L, cls);
    setmemberfield(L, member, kMemberOwner);

    lua_pushnumber(L, double(offset));
    setmemberfield(L, member, kMemberOffset);
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

// class.fields(x): the fields of the class or object `x` as ClassMembers: a primary constructor's parameters in the order
// it takes them, then the others in declaration order. Private ones are listed only for the class's own code.
static int class_members(lua_State* L)
{
    const LuauObject* object = NULL;
    LuauClass* cls = checkclassorobject(L, &object);
    bool readsprivate = callerreadsprivate(L, cls);

    lua_createtable(L, int(cls->numberofinstancemembers), 0);
    int list = lua_gettop(L);
    int count = 0;

    auto add = [&](uint32_t offset)
    {
        if ((cls->memberflags[offset] & LBC_CLASSMEMBER_PRIVATE) && !readsprivate)
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
        L->global->builtinclasses[LUWU_BUILTIN_CLASSMEMBER] =
            luaR_newnativeclass(L, "ClassMember", kClassMemberMembers, int(sizeof(kClassMemberMembers) / sizeof(kClassMemberMembers[0])), classmember_init);
        lua_setfield(L, -2, "ClassMember");

        lua_pushcfunction(L, class_member, "member");
        lua_setfield(L, -2, "member");
        lua_pushcfunction(L, class_members, "fields");
        lua_setfield(L, -2, "fields");
    }

    return 1;
}