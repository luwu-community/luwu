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
#include "Luau/LuwuFlags.h"

LUAU_FASTFLAG(LuwuNonePrimitive)
LUAU_FASTFLAG(DebugLuwuBundles)
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
// `owner` and `offset` are private: `get` and `__tostring` read by them. A bundle's position has no offset, and `get`
// reads it by `position`. Only the VM constructs one: its constructor is private, so `class.of(member)` can't make another.
enum ClassMemberField
{
    kMemberName,
    kMemberValue,
    kMemberIsConst,
    kMemberIsFinal,
    kMemberVisibility,
    kMemberPosition,
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

// Whether the ClassMember `member` describes a field (or a bundle's position) rather than a method
static bool memberisfield(const LuauObject* member)
{
    const TValue* fields = luaR_members(member);
    if (!ttisnumber(&fields[kMemberOffset]))
        return true;
    return uint32_t(nvalue(&fields[kMemberOffset])) < classvalue(&fields[kMemberOwner])->numberofinstancemembers;
}

// member:get(object): the member's value in `object`, which must be an object of the member's class
static int classmember_get(lua_State* L)
{
    const TValue* fields = luaR_members(luaR_checknativeself(L, classmemberclass(L), "get"));
    const LuauClass* owner = classvalue(&fields[kMemberOwner]);

    const TValue* object = luaA_toobject(L, 2);
    bool ofOwner = object && ttisobject(object) && objectvalue(object)->lclass == owner;
    if (!ofOwner)
        luaL_typeerror(L, 2, getstr(owner->name));

    // a member of BigBundle can be given a smaller bundle, which raises here
    if (!ttisnumber(&fields[kMemberOffset]))
    {
        lua_checkstack(L, 1);
        luaR_getbundleposition(L, objectvalue(object), &fields[kMemberPosition], L->top);
        incr_top(L);
        return 1;
    }

    uint32_t offset = uint32_t(nvalue(&fields[kMemberOffset]));
    if (offset < owner->numberofinstancemembers)
        luaA_pushvalue(L, &luaR_members(objectvalue(object))[offset]);
    else
        luaA_pushvalue(L, &owner->staticmembers[offset - owner->numberofinstancemembers]);
    return 1;
}

// tostring(member): the member as a class body declares it, without its type: `public const x = 1`, `public x`,
// `public final function f`. A bundle's position prints as `public const 1 = 5`.
static int classmember_tostring(lua_State* L)
{
    LuauObject* member = luaR_checknativeself(L, classmemberclass(L), "__tostring");
    const TValue* fields = luaR_members(member);
    bool isfield = memberisfield(member);

    luaL_Strbuf buffer;
    luaL_buffinit(L, &buffer);
    luaL_addstring(&buffer, svalue(&fields[kMemberVisibility]));
    luaL_addchar(&buffer, ' ');
    if (bvalue(&fields[kMemberIsFinal]))
        luaL_addstring(&buffer, "final ");
    if (bvalue(&fields[kMemberIsConst]))
        luaL_addstring(&buffer, "const ");
    if (!isfield)
        luaL_addstring(&buffer, "function ");
    luaL_addstring(&buffer, svalue(&fields[kMemberName]));

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
    {"position", nullptr, LBC_CLASSMEMBER_CONST, nullptr},
    {"owner", nullptr, LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST, nullptr},
    {"offset", nullptr, LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST, nullptr},
    {"get", classmember_get, 0, nullptr},
    {"__tostring", classmember_tostring, 0, nullptr},
};

// The position of the member at `offset` of `cls` among its primary constructor's parameters, from 1, or 0 when it isn't
// one of them
static int positionof(const LuauClass* cls, uint32_t offset)
{
    for (int i = 0; i < cls->numberofpositional; i++)
        if (cls->positionalmembers[i] == offset)
            return i + 1;

    return 0;
}

// The number of positions class reflection lists for `cls`, read through `object` when it's set, or -1 when `cls` isn't
// a bundle class. Through the class only the fixed sizes have positions: a BigBundle's size is its object's.
static int bundlepositions(lua_State* L, const LuauClass* cls, const LuauObject* object)
{
    if (object)
        return luaR_isbundle(object) ? int(object->numberofmembers) : -1;

    for (int size = 0; size <= LUWU_BUNDLE_FIXED_MAX; size++)
        if (L->global->builtinclasses[LUWU_BUILTIN_BUNDLE0 + size] == cls)
            return size;

    return L->global->builtinclasses[LUWU_BUILTIN_BIGBUNDLE] == cls ? 0 : -1;
}

// What a new ClassMember says about its member
struct MemberInfo
{
    TString* name = nullptr;
    bool isconst = false;
    bool isfinal = false;
    bool isprivate = false;
    // from 1 for a primary constructor's parameter or a bundle's position, 0 for any other member
    int position = 0;
    // a bundle's position has none
    bool hasoffset = true;
    uint32_t offset = 0;
    // the value for `Some(value)`, or null for `none`
    const TValue* value = nullptr;
};

// Sets field `field` of the new `member`. A new object is white, but filling it allocates, so each store has a barrier.
static void setmemberfield(lua_State* L, LuauObject* member, ClassMemberField field, const TValue* value)
{
    setobj2class(L, &luaR_members(member)[field], value);
    luaC_barrier(L, member, value);
}

// Pushes a new ClassMember of `cls` saying `info`. The strings are the class's own and interned literals, so the member
// itself and a `Some` are all that's allocated.
static void pushnewmember(lua_State* L, LuauClass* cls, const MemberInfo& info)
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

    TValue value;
    setsvalue(L, &value, info.name);
    setmemberfield(L, member, kMemberName, &value);

    setbvalue(&value, info.isconst);
    setmemberfield(L, member, kMemberIsConst, &value);
    setbvalue(&value, info.isfinal);
    setmemberfield(L, member, kMemberIsFinal, &value);
    if (info.position > 0)
    {
        setnvalue(&value, double(info.position));
        setmemberfield(L, member, kMemberPosition, &value);
    }

    setsvalue(L, &value, info.isprivate ? luaS_newliteral(L, "private") : luaS_newliteral(L, "public"));
    setmemberfield(L, member, kMemberVisibility, &value);

    setclassvalue(L, &value, cls);
    setmemberfield(L, member, kMemberOwner, &value);
    if (info.hasoffset)
    {
        setnvalue(&value, double(info.offset));
        setmemberfield(L, member, kMemberOffset, &value);
    }

    if (!info.value)
    {
        setsymnonevalue(&value);
        setmemberfield(L, member, kMemberValue, &value);
        return;
    }

    luaA_pushvalue(L, info.value);
    lua_pushsome(L, -1);
    setmemberfield(L, member, kMemberValue, L->top - 1);
    L->top -= 2;
}

// Pushes a new ClassMember for the member at `offset` of `cls`. Through an object its value is `Some(value)`; a field
// read through the class has none.
static void pushnewclassmember(lua_State* L, LuauClass* cls, uint32_t offset, const LuauObject* object)
{
    uint8_t flags = cls->memberflags[offset];
    bool isfield = offset < cls->numberofinstancemembers;

    MemberInfo info;
    info.name = cls->offsettomember[offset];
    info.isconst = isfield && (flags & LBC_CLASSMEMBER_CONST) != 0;
    info.isfinal = (flags & LBC_CLASSMEMBER_FINAL) != 0;
    info.isprivate = (flags & LBC_CLASSMEMBER_PRIVATE) != 0;
    info.position = isfield ? positionof(cls, offset) : 0;
    info.offset = offset;
    if (!isfield)
        info.value = &cls->staticmembers[offset - cls->numberofinstancemembers];
    else if (object)
        info.value = &luaR_members(object)[offset];

    pushnewmember(L, cls, info);
}

// Pushes a new ClassMember for `position` of the bundle class `cls`: a public const field named after the position.
// Through a bundle its value is `Some(value)`; through the class it has none.
static void pushnewbundleposition(lua_State* L, LuauClass* cls, int position, const LuauObject* object)
{
    // the name stays on the stack until the member holds it, so allocating the member can't collect it
    lua_checkstack(L, 1);
    lua_pushfstring(L, "%d", position);

    MemberInfo info;
    info.name = tsvalue(L->top - 1);
    info.isconst = true;
    info.position = position;
    info.hasoffset = false;
    if (object)
        info.value = &luaR_members(object)[position - 1];

    pushnewmember(L, cls, info);
    lua_remove(L, -2);
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

// Pushes the ClassMember `cls`'s reflection cache holds at `key`, after `pushnew` creates it the first time
template<typename PushNew>
static void pushcachedmember(lua_State* L, LuauClass* cls, int key, PushNew pushnew)
{
    pushreflection(L, cls);
    if (lua_rawgeti(L, -1, key) == LUA_TNIL)
    {
        lua_pop(L, 1);
        pushnew();
        lua_pushvalue(L, -1);
        lua_rawseti(L, -3, key);
    }
    lua_remove(L, -2);
}

// Pushes the ClassMember of the member at `offset` of `cls`: through the class it's created once and kept
static void pushclassmember(lua_State* L, LuauClass* cls, uint32_t offset, const LuauObject* object)
{
    if (object)
        return pushnewclassmember(L, cls, offset, object);

    pushcachedmember(L, cls, int(offset) + 1, [&]{ pushnewclassmember(L, cls, offset, NULL); });
}

// Pushes the ClassMember of `position` of the bundle class `cls`. Through the class it's kept like pushclassmember's,
// under the negative position.
static void pushbundleposition(lua_State* L, LuauClass* cls, int position, const LuauObject* object)
{
    if (object)
        return pushnewbundleposition(L, cls, position, object);

    pushcachedmember(L, cls, -position, [&]{ pushnewbundleposition(L, cls, position, NULL); });
}

// The position `name` names in a bundle of `size` positions, or 0 when it names none: `1` to `size`, without a sign or
// leading zeros, like `b.1`
static int parseposition(const char* name, int size)
{
    if (name[0] < '1' || name[0] > '9')
        return 0;

    int position = 0;
    for (const char* digit = name; *digit; digit++)
    {
        if (*digit < '0' || *digit > '9')
            return 0;
        position = position * 10 + (*digit - '0');
        if (position > size)
            return 0;
    }
    return position;
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
// member, or it's private and the caller isn't the class's code. A bundle's positions are members `"1"` to `"n"`.
static int class_member(lua_State* L)
{
    const LuauObject* object = NULL;
    LuauClass* cls = checkclassorobject(L, &object);
    const char* name = luaL_checkstring(L, 2);

    if (int position = parseposition(name, bundlepositions(L, cls, object)))
    {
        pushbundleposition(L, cls, position, object);
        return 1;
    }

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
// them, then the others in declaration order. Private ones only with `withprivate`. A bundle lists its positions.
static void pushfieldlist(lua_State* L, LuauClass* cls, const LuauObject* object, bool withprivate)
{
    int positions = bundlepositions(L, cls, object);
    if (positions >= 0)
    {
        lua_createtable(L, positions, 0);
        for (int position = 1; position <= positions; position++)
        {
            pushbundleposition(L, cls, position, object);
            lua_rawseti(L, -2, position);
        }
        lua_setreadonly(L, -1, true);
        return;
    }

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
        if (positionof(cls, offset) == 0)
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

    // Luwu: upstream Luau's name for `class.of`, for Luau modules with class compat. One library serves every module in
    // the state, so Luwu modules can reach it too; the type checker keeps each module to its own language's names.
    if (Luau::luauClassCompat())
    {
        lua_pushcfunction(L, class_of, "classof");
        lua_setfield(L, -2, "classof");
    }


    // Luwu class reflection (rfcs/classes): a ClassMember's modifiers are a bundle and its value an option
    if (FFlag::DebugLuwuBundles && FFlag::DebugLuwuTryExpressions && FFlag::LuwuNonePrimitive)
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