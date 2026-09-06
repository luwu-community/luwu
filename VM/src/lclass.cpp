// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details

#include "lclass.h"

#include "ldebug.h"
#include "lfunc.h"
#include "lgc.h"
#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lualib.h"
#include "lvm.h"

// Continuation for luaR_createobject: runs after a custom __init returns (possibly across a yield),
// leaving the freshly-constructed object as the constructor's single result. See luaR_createobject.
static int luaR_createobjectcont(lua_State* L, int status);

LuauClass* luaR_newclass(
    lua_State* L,
    TString* name,
    LuaTable* memberstooffset,
    TString** offsettomember,
    uint8_t* memberflags,
    uint32_t numberofinstancemembers,
    uint32_t numberofstaticmembers
)
{
    LUAU_ASSERT(L->global->GCthreshold == SIZE_MAX && "GC must be paused");
    LuauClass* classdef = luaM_newgco(L, LuauClass, sizeof(LuauClass), L->activememcat);
    luaC_init(L, classdef, LUA_TCLASS);
    classdef->name = name;

    classdef->staticmembers = luaM_newarray(L, numberofstaticmembers, TValue, classdef->memcat);
    // Initialize static members to nil, otherwise we may read uninitialized memory.
    for (uint32_t i = 0; i < numberofstaticmembers; i++)
        setnilvalue(&classdef->staticmembers[i]);

    classdef->memberstooffset = memberstooffset;
    classdef->offsettomember = offsettomember;
    classdef->memberdefaults = NULL;

    // Initialize the metatable of the _class value_, which for now only
    // contains an __call entry for the class constructor.
    classdef->metatable = luaH_new(L, 0, 1);
    // We should probably pass an empty table here rather than the global
    // environment.
    static const char kCtorSuffix[] = "() constructor";
    size_t namelen = strlen(getstr(name));
    size_t ctordebugnamelen = namelen + sizeof(kCtorSuffix); // includes the null terminator
    classdef->ctordebugname = luaM_newarray(L, ctordebugnamelen, char, classdef->memcat);
    memcpy(classdef->ctordebugname, getstr(name), namelen);
    memcpy(classdef->ctordebugname + namelen, kCtorSuffix, sizeof(kCtorSuffix));

    Closure* constructor = luaF_newCclosure(L, 0, L->gt);
    constructor->c.f = luaR_createobject;
    constructor->c.debugname = classdef->ctordebugname;
    // a continuation makes construction yieldable: a custom __init may call coroutine.yield (or a
    // yielding C function), suspending mid-construction and resuming into luaR_createobjectcont
    constructor->c.cont = luaR_createobjectcont;
    TValue* dest = luaH_setstr(L, classdef->metatable, L->global->tmname[TM_CALL]);
    LUAU_ASSERT(ttisnil(dest));
    setclvalue(L, dest, constructor);
    classdef->metatable->readonly = true;
    classdef->instancemetatable = NULL;

    classdef->numberofinstancemembers = numberofinstancemembers;
    classdef->numberofallmembers = numberofinstancemembers + numberofstaticmembers;
    classdef->hascustominit = false;
    classdef->hasprimaryinit = false;
    classdef->initoffset = 0;
    classdef->haspoddefaultsfn = false;
    classdef->poddefaultsoffset = 0;

    classdef->memberflags = memberflags;
    classdef->hasprivatemembers = false;
    classdef->hasconstmembers = false;
    classdef->hasdefaultmembers = false;
    for (uint32_t i = 0; i < classdef->numberofallmembers; i++)
    {
        classdef->hasprivatemembers |= (memberflags[i] & LBC_CLASSMEMBER_PRIVATE) != 0;
        classdef->hasconstmembers |= (memberflags[i] & LBC_CLASSMEMBER_CONST) != 0;
        classdef->hasdefaultmembers |= (memberflags[i] & LBC_CLASSMEMBER_HASDEFAULT) != 0;
    }

    return classdef;
}

bool luaR_closureisinit(const LuauClass* classdef, const Closure* cl)
{
    if (cl->isC || !classdef->hascustominit)
        return false;

    const TValue* v = &classdef->staticmembers[classdef->initoffset - classdef->numberofinstancemembers];
    return ttisfunction(v) && clvalue(v) == cl;
}

bool luaR_closureownsprivateaccess(const LuauClass* classdef, const Closure* cl)
{
    // Since we're adding a property to keep track of class ownership to every single function proto
    // for ncg lowering let's just use that prop. covers all closures lexically scoped inside
    // private access owning closure as well.
    return !cl->isC && cl->l.p->ownerclass == classdef;
}

// Finds the Luwu code on whose behalf a VM-internal C function is acting: the nearest Lua frame,
// looking through any C frames above it. NULL means there is none, i.e. native code drove this
// through the C API.
//
// This exists for `luaR_createobject`, which is the class's `__call` metamethod and therefore performs
// construction *on behalf of whoever called the class*. Its immediate caller is not that: for
// `pcall(SomeClass, ...)` it is `pcall`, and treating a builtin's C frame as authority is exactly what
// let a private constructor be laundered through `pcall`/`xpcall`. Note this is not the rule for an
// access a C function performs itself -- see luaR_checkprivateaccess.
static const Closure* luaR_callinglua(lua_State* L)
{
    for (CallInfo* ci = L->ci; ci > L->base_ci; ci--)
        if (isLua(ci))
            return clvalue(ci->func);

    return NULL;
}

void luaR_checkprivateaccess(lua_State* L, const TValue* key, const LuauClass* classdef, const Closure* cl, uint32_t offset)
{
    if ((classdef->memberflags[offset] & LBC_CLASSMEMBER_PRIVATE) == 0)
        return;

    // Native code performing the access itself is trusted, whether it is the embedder calling in or a
    // plugin Luwu code called: it holds the C API, and it holds the LuauObject pointer besides, so
    // `private` is not a boundary it could be held to. The restriction is on Luwu code -- including
    // Luwu code that reaches a member through a builtin, since a builtin is not an accessor of its own
    // (see luaR_callinglua).
    if (!cl || cl->isC)
        return;

    if (luaR_closureownsprivateaccess(classdef, cl))
        return;

    luaG_privateaccesserror(L, key, classdef->name);
}

void luaR_checkconstassign(lua_State* L, const TValue* key, const LuauClass* classdef, const Closure* cl, uint32_t offset)
{
    if ((classdef->memberflags[offset] & LBC_CLASSMEMBER_CONST) == 0)
        return;

    // See luaR_checkprivateaccess: native code doing the assignment itself is trusted.
    if (!cl || cl->isC)
        return;

    if (luaR_closureisinit(classdef, cl))
        return;

    luaG_constassignerror(L, key, classdef->name);
}

// Stamps `ownerclass` recursively on function proto `p` and every function proto lexically nested
// within it.
static void luaR_stampownerclass(lua_State* L, Proto* p, LuauClass* classdef)
{
    p->ownerclass = classdef;
    luaC_objbarrier(L, p, classdef);

    for (int i = 0; i < p->sizep; i++)
        luaR_stampownerclass(L, p->p[i], classdef);
}

void luaR_addclassmember(lua_State* L, LuauClass* classdef, TString* name, TValue* value)
{
    LUAU_ASSERT(classdef->staticmembers != nullptr);
    const TValue* offset = luaH_getstr(classdef->memberstooffset, name);
    const uint32_t offsetint = uint32_t(nvalue(offset));
    LUAU_ASSERT(offsetint >= classdef->numberofinstancemembers && offsetint < classdef->numberofallmembers);
    LUAU_ASSERT(ttisfunction(value) && value->value.gc->gch.tt == LUA_TFUNCTION);
    setobj2class(L, &classdef->staticmembers[offsetint - classdef->numberofinstancemembers], value);
    luaC_barrier(L, classdef, value);

    // Stamp this function/method's proto (and any function lexically within it) with info about
    // the owning class so NCG and interpreter can authorize private access from any function lexically
    // scoped within the class. 
    Closure* mcl = clvalue(value);
    if (!mcl->isC)
        luaR_stampownerclass(L, mcl->l.p, classdef);

    if (name == luaS_newlstr(L, "__init", 6))
    {
        classdef->hascustominit = true;
        classdef->initoffset = offsetint;
        // A primary constructor's `__init` only assigns fields from its parameters, so a construction
        // site is allowed to do that itself and skip the call (LOP_NEWOBJECT's FIELDS form).
        classdef->hasprimaryinit = (classdef->memberflags[offsetint] & LBC_CLASSMEMBER_PRIMARYINIT) != 0;
    }
    else if (name == luaS_newlstr(L, "__defaults", 10))
    {
        classdef->haspoddefaultsfn = true;
        classdef->poddefaultsoffset = offsetint;
    }

    // Only metamethods in the parser's allowlist are supported (see ALLOWED_METAMETHODS in Parser.cpp)
    bool isMetamethod = (name == luaS_newlstr(L, "__tostring", 10));
    for (int i = 0; i < TM_N && !isMetamethod; i++)
        isMetamethod = (name == L->global->tmname[i]);

    if (isMetamethod)
    {
        if (!classdef->instancemetatable)
        {
            classdef->instancemetatable = luaH_new(L, 0, 1);
            luaC_objbarrier(L, classdef, classdef->instancemetatable);
        }
        TValue* dest = luaH_setstr(L, classdef->instancemetatable, name);
        setobj2t(L, dest, value);
        luaC_barrier(L, classdef->instancemetatable, value);
    }
}

// Initializes the class instance (object) with POD constructor, with L->base + 1 being the stack location we expect
// the user-provided table matching expected fields to values to be. Since classes can have 0 fields that need to be
// initialized we also allow Class() here as well (if class actually had fields they will be nill)
//
// Field defaults come from one of two places: constant defaults are serialized into the class shape
// and copied straight out of classdef->memberdefaults, while a class with any non-constant default
// (`= {}`, a call, ...) still calls its synthesized `__defaults` closure, since those have to be
// re-evaluated on every construction. Only the latter pays for a `lua_call` here.
static void luaR_defaultinitinstancefields(lua_State* L, LuauClass* classdef, LuauObject* object, int numargs)
{
    if (classdef->haspoddefaultsfn)
    {
        setobj2s(L, L->top, &classdef->staticmembers[classdef->poddefaultsoffset - classdef->numberofinstancemembers]);
        L->top++;
        lua_call(L, 0, classdef->numberofinstancemembers);

        StkId results = L->top - classdef->numberofinstancemembers;
        for (uint32_t idx = 0; idx < classdef->numberofinstancemembers; idx++)
            setobj(L, &object->members[idx], &results[idx]);

        L->top -= classdef->numberofinstancemembers;
    }

    // Stack location to hold the table lookup result
    setnilvalue(L->top);
    L->top++;

    // The argument is a plain field bag in every realistic case, so read it with a direct string
    // lookup; only a table carrying a metatable (or a non-table) needs the generic __index-aware path.
    LuaTable* argtable = NULL;

    if (numargs == 2 && ttistable(L->base + 1))
    {
        LuaTable* candidate = hvalue(L->base + 1);

        if (candidate->metatable == NULL)
            argtable = candidate;
    }

    switch (numargs)
    {
    case 1:
        // assume class has 0 fields to initialize or user wants all fields to be nil (or their default)
        break;
    case 2:
        if (argtable)
        {
            luaR_applyobjectfields(L, classdef, object, argtable);
            break;
        }

        // by going over the expected instance members instead of the passed table we ensure
        // that users can't add arbitrary properties to the object within the default constructor
        for (uint32_t idx = 0; idx < classdef->numberofinstancemembers; idx++)
        {
            // A field absent from the table (or explicitly nil) keeps whatever's already in
            // object->members[idx] -- nil, or that field's default set above.
            TValue key;
            setsvalue(L, &key, classdef->offsettomember[idx]);
            luaV_gettable(L, L->base + 1, &key, L->top - 1);

            if (!ttisnil(L->top - 1))
                setobj(L, &object->members[idx], L->top - 1);
        }
        break;
    default:
        luaL_error(
            L,
            "the default constructor for constructing a '%s' expected zero or one arguments "
            "(table mapping field names to values or nothing if class has 0 fields), got an incorrect number of arguments",
            getstr(classdef->name)
        );
    }

    L->top--;
}

void luaR_applyobjectfields(lua_State* L, LuauClass* classdef, LuauObject* object, LuaTable* arg)
{
    for (uint32_t idx = 0; idx < classdef->numberofinstancemembers; idx++)
    {
        // by going over the expected instance members instead of the passed table we ensure
        // that users can't add arbitrary properties to the object
        const TValue* value = luaH_getstr(arg, classdef->offsettomember[idx]);

        // A field absent from the table (or explicitly nil) keeps whatever's already in
        // object->members[idx] -- nil, or that field's default.
        if (!ttisnil(value))
            setobj(L, &object->members[idx], value);
    }
}

void luaR_applyobjectfieldsslow(lua_State* L, LuauClass* classdef, LuauObject* object, const TValue* arg)
{
    // `arg` may live on the stack, and an __index metamethod can reallocate it, so work off a copy;
    // the original slot keeps the value alive for the GC.
    TValue source = *arg;

    luaL_checkstack(L, 1, "class constructor fields");
    setnilvalue(L->top);
    L->top++;

    for (uint32_t idx = 0; idx < classdef->numberofinstancemembers; idx++)
    {
        TValue key;
        setsvalue(L, &key, classdef->offsettomember[idx]);

        // L->top - 1 is recomputed every iteration because the lookup can move the stack
        luaV_gettable(L, &source, &key, L->top - 1);

        if (!ttisnil(L->top - 1))
            setobj(L, &object->members[idx], L->top - 1);
    }

    L->top--;
}

LuauObject* luaR_newobjectuninit(lua_State* L, LuauClass* classdef)
{
    // See luaR_newobject: the same allocation, minus initializing the members. Only for a caller that
    // fills every one of them immediately, with nothing in between that could trigger a GC step --
    // traverseobject reads them all.
    uint32_t nummembers = classdef->numberofinstancemembers;
    LuauObject* object = luaM_newgco(L, LuauObject, luaR_objectsize(nummembers), L->activememcat);
    luaC_init(L, object, LUA_TOBJECT);
    object->lclass = classdef;
    object->numberofmembers = nummembers;
    object->members = cast_to(TValue*, object + 1);

    return object;
}

LuauObject* luaR_newobject(lua_State* L, LuauClass* classdef)
{
    // The members live in the same allocation as the object itself (see luaR_objectsize): one GC
    // object per instance instead of two, which halves both the allocator traffic and the sweep cost
    // of constructing objects. `members` stays a real pointer field so every reader -- the
    // interpreter, native codegen's TRY_OBJECT_MEMBER_ADDR -- is unaffected.
    uint32_t nummembers = classdef->numberofinstancemembers;
    LuauObject* object = luaM_newgco(L, LuauObject, luaR_objectsize(nummembers), L->activememcat);
    luaC_init(L, object, LUA_TOBJECT);
    object->lclass = classdef;
    object->numberofmembers = nummembers;
    object->members = cast_to(TValue*, object + 1);

    // Initialize every member before anything can trigger a GC step, since traverseobject reads them
    // all. Constant defaults go straight in here, so the common case writes each member exactly once
    // rather than nil-filling and then overwriting.
    if (classdef->memberdefaults)
    {
        for (uint32_t idx = 0; idx < nummembers; idx++)
            setobj(L, &object->members[idx], &classdef->memberdefaults[idx]);
    }
    else
    {
        for (uint32_t idx = 0; idx < nummembers; idx++)
            setnilvalue(&object->members[idx]);
    }

    return object;
}

int luaR_createobject(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TCLASS);
    LuauClass* classdef = classvalue(L->base);

    // Ensure a private constructor is only callable from within its own class.
    if (classdef->hascustominit && classdef->hasprivatemembers &&
        (classdef->memberflags[classdef->initoffset] & LBC_CLASSMEMBER_PRIVATE))
    {
        // Construction happens on behalf of whoever called the class, so authority is the nearest Lua
        // frame rather than the frame directly below: for `pcall(SomeClass, ...)` that frame is
        // `pcall`, and a builtin is not authority for anything. Native code with no Lua frame under it
        // at all is trusted, same as an access it performs itself.
        const Closure* callercl = luaR_callinglua(L);

        TValue initname;
        setsvalue(L, &initname, classdef->offsettomember[classdef->initoffset]);
        luaR_checkprivateaccess(L, &initname, classdef, callercl, classdef->initoffset);
    }

    LuauObject* object = luaR_newobject(L, classdef);
    int numargs = lua_gettop(L);

    // Push the new object onto the stack. We do this prior to setting the
    // fields as we may reallocate the stack as part of indexing into the
    // second argument (if present).
    setobjectvalue(L, L->top, object);
    L->top++;
    int selfidx = lua_gettop(L);

    if (classdef->hascustominit)
    {
        // Build __init's call frame directly. lua_pushvalue re-checks the index and the GC thread
        // barrier on every single argument, which is most of the cost of constructing an object.
        // __init's offset is fixed when the class is created, so it's read straight out of the class
        // rather than interning "__init" and hash-looking it up on every construction.
        LUAU_ASSERT(classdef->initoffset >= classdef->numberofinstancemembers);
        luaL_checkstack(L, numargs + 1, "class constructor arguments");
        luaC_threadbarrier(L);

        // the stack may have moved, so everything below is recomputed from the current base
        StkId frame = L->top;
        setobj2s(L, frame, &classdef->staticmembers[classdef->initoffset - classdef->numberofinstancemembers]);
        setobj2s(L, frame + 1, L->base + selfidx - 1);

        for (int i = 1; i < numargs; i++)
            setobj2s(L, frame + 1 + i, L->base + i);

        L->top = frame + 1 + numargs;

        // Yieldable call: __init may suspend the coroutine (via coroutine.yield or a yielding C
        // function). On completion -- immediately or after a resume -- luaR_createobjectcont returns
        // the object. __init takes no results, so 'self' (selfidx) is left on top of the stack.
        return luaL_callyieldable(L, 1 + (numargs - 1), 0);
    }

    luaR_defaultinitinstancefields(L, classdef, object, numargs);

    // Preserve the GC invariant, moving barrier back once after writing multiple objects (similar to SETLIST)
    luaC_barrierfast(L, object);

    return 1;
}

static int luaR_createobjectcont(lua_State* L, int status)
{
    // __init was called with zero expected results, so the object we constructed is left on top of
    // the stack (see luaR_createobject); return it as the constructor's single result.
    return 1;
}

int luaR_defaultinit(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TOBJECT);
    LuauObject* object = objectvalue(L->base);
    LuauClass* classdef = object->lclass;
    int numargs = lua_gettop(L);

    if (classdef->memberdefaults)
    {
        for (uint32_t idx = 0; idx < classdef->numberofinstancemembers; idx++)
            setobj(L, &object->members[idx], &classdef->memberdefaults[idx]);
    }
    else
    {
        for (uint32_t idx = 0; idx < classdef->numberofinstancemembers; idx++)
            setnilvalue(&object->members[idx]);
    }

    luaR_defaultinitinstancefields(L, classdef, object, numargs);

    luaC_barrierfast(L, object);

    return 0;
}

void luaR_adddefaultinit(lua_State* L, LuauClass* classdef)
{
    const TValue* offset = luaH_getstr(classdef->memberstooffset, luaS_newlstr(L, "__init", 6));
    LUAU_ASSERT(!ttisnil(offset));
    const uint32_t offsetint = uint32_t(nvalue(offset));
    LUAU_ASSERT(offsetint >= classdef->numberofinstancemembers && offsetint < classdef->numberofallmembers);

    Closure* init = luaF_newCclosure(L, 0, L->gt);
    init->c.f = luaR_defaultinit;
    init->c.debugname = "__init";
    init->c.cont = NULL;

    TValue v;
    setclvalue(L, &v, init);
    setobj2class(L, &classdef->staticmembers[offsetint - classdef->numberofinstancemembers], &v);
    luaC_barrier(L, classdef, &v);
}

void luaR_setmemberdefaults(lua_State* L, LuauClass* classdef, TValue* defaults)
{
    LUAU_ASSERT(classdef->memberdefaults == NULL);
    classdef->memberdefaults = defaults;

    for (uint32_t idx = 0; idx < classdef->numberofinstancemembers; idx++)
        luaC_barrier(L, classdef, &defaults[idx]);
}

void luaR_freeclass(lua_State* L, LuauClass* classdef, lua_Page* page)
{
    luaM_freearray(
        L, classdef->staticmembers, classdef->numberofallmembers - classdef->numberofinstancemembers, TValue, classdef->memcat
    );
    luaM_freearray(L, classdef->offsettomember, classdef->numberofallmembers, TString*, classdef->memcat);
    luaM_freearray(L, classdef->memberflags, classdef->numberofallmembers, uint8_t, classdef->memcat);
    if (classdef->memberdefaults)
        luaM_freearray(L, classdef->memberdefaults, classdef->numberofinstancemembers, TValue, classdef->memcat);
    luaM_freearray(L, classdef->ctordebugname, strlen(classdef->ctordebugname) + 1, char, classdef->memcat);
    luaM_freegco(L, classdef, sizeof(LuauClass), classdef->memcat, page);
}

void luaR_freeobject(lua_State* L, LuauObject* object, lua_Page* page)
{
    luaM_freegco(L, object, luaR_objectsize(object->numberofmembers), object->memcat, page);
}
