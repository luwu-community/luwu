// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
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

#include <string.h>

// Continuation for luaR_createobject: runs after a custom __init returns (possibly across a yield),
// leaving the freshly-constructed object as the constructor's single result. See luaR_createobject.
static int luaR_createobjectcont(lua_State* L, int status);

LuauClass* luaR_newclass(
    lua_State* L,
    TString* name,
    uint32_t numberofinstancemembers,
    uint32_t numberofstaticmembers,
    bool hasmemberdefaults
)
{
    LUAU_ASSERT(L->global->GCthreshold == SIZE_MAX && "GC must be paused");
    LuauClass* classdef = luaM_newgco(L, LuauClass, sizeof(LuauClass), L->activememcat);
    luaC_init(L, classdef, LUA_TCLASS);

    // Every allocation below can raise LUA_ERRMEM, and the half-built class is still swept afterwards
    // (luaR_freeclass). So every field luaR_freeclass frees is given a valid value here, before the
    // first allocation.
    classdef->name = name;
    classdef->staticmembers = NULL;
    classdef->memberstooffset = NULL;
    classdef->offsettomember = NULL;
    classdef->metatable = NULL;
    classdef->instancemetatable = NULL;
    classdef->numberofinstancemembers = numberofinstancemembers;
    classdef->numberofallmembers = numberofinstancemembers + numberofstaticmembers;
    classdef->hascustominit = false;
    classdef->initoffset = 0;
    classdef->hasprimaryinit = false;
    classdef->memberflags = NULL;
    classdef->hasprivatemembers = false;
    classdef->hasconstmembers = false;
    classdef->haspoddefaultsfn = false;
    classdef->poddefaultsoffset = 0;
    classdef->memberdefaults = NULL;

    classdef->offsettomember = luaM_newarray(L, classdef->numberofallmembers, TString*, classdef->memcat);
    for (uint32_t i = 0; i < classdef->numberofallmembers; i++)
        classdef->offsettomember[i] = NULL;

    classdef->memberflags = luaM_newarray(L, classdef->numberofallmembers, uint8_t, classdef->memcat);
    memset(classdef->memberflags, 0, classdef->numberofallmembers);

    classdef->staticmembers = luaM_newarray(L, numberofstaticmembers, TValue, classdef->memcat);
    // Initialize static members to nil, otherwise we may read uninitialized memory.
    for (uint32_t i = 0; i < numberofstaticmembers; i++)
        setnilvalue(&classdef->staticmembers[i]);

    if (hasmemberdefaults)
    {
        classdef->memberdefaults = luaM_newarray(L, numberofinstancemembers, TValue, classdef->memcat);
        for (uint32_t i = 0; i < numberofinstancemembers; i++)
            setnilvalue(&classdef->memberdefaults[i]);
    }

    classdef->memberstooffset = luaH_new(L, 0, classdef->numberofallmembers);

    // Initialize the metatable of the _class value_, which for now only
    // contains an __call entry for the class constructor.
    classdef->metatable = luaH_new(L, 0, 1);

    // The constructor can outlive the class (`debug.info` hands it out from inside `__init`), so its
    // debug name is a string it holds as an upvalue rather than memory owned by the class.
    static const char kCtorSuffix[] = "() constructor";
    TString* debugnamestr = luaS_bufstart(L, name->len + sizeof(kCtorSuffix) - 1);
    memcpy(debugnamestr->data, getstr(name), name->len);
    memcpy(debugnamestr->data + name->len, kCtorSuffix, sizeof(kCtorSuffix) - 1);
    debugnamestr = luaS_buffinish(L, debugnamestr);

    // We should probably pass an empty table here rather than the global
    // environment. (The constructor itself reads no globals.)
    Closure* constructor = luaF_newCclosure(L, 1, L->gt);
    setsvalue(L, &constructor->c.upvals[0], debugnamestr);
    constructor->c.f = luaR_createobject;
    // points into upvalue 1: an embedder replacing that upvalue with lua_setupvalue leaves this dangling
    constructor->c.debugname = getstr(debugnamestr);
    // a continuation makes construction yieldable: a custom __init may call coroutine.yield (or a
    // yielding C function), suspending mid-construction and resuming into luaR_createobjectcont
    constructor->c.cont = luaR_createobjectcont;
    TValue* dest = luaH_setstr(L, classdef->metatable, L->global->tmname[TM_CALL]);
    LUAU_ASSERT(ttisnil(dest));
    setclvalue(L, dest, constructor);
    classdef->metatable->readonly = true;

    return classdef;
}

void luaR_sealclassshape(lua_State* L, LuauClass* classdef)
{
    for (uint32_t i = 0; i < classdef->numberofallmembers; i++)
    {
        LUAU_ASSERT(classdef->offsettomember[i]);
        classdef->hasprivatemembers |= (classdef->memberflags[i] & LBC_CLASSMEMBER_PRIVATE) != 0;
        classdef->hasconstmembers |= (classdef->memberflags[i] & LBC_CLASSMEMBER_CONST) != 0;
    }

    // `__init` is never readable by name, from anywhere: calling it on a constructed object would
    // re-run construction on it, which may reassign its `const` fields. Construction reads it by
    // offset, so it is unaffected. Every class has an `__init` member: a custom or primary one from the
    // shape, or the one the loader reserves.
    //
    // The ban costs other members nothing. `__init` has no name in `offsettomember`, so no cached-slot
    // fast path (which compares the key against that name) ever resolves it; a read by name always
    // takes a hash lookup through `memberstooffset`, and every such path runs luaR_checkprivateaccessfast,
    // which raises on LBC_CLASSMEMBER_INITBLOCKED.
    const TValue* initoffset = luaH_getstr(classdef->memberstooffset, luaS_newlstr(L, "__init", 6));
    LUAU_ASSERT(!ttisnil(initoffset));
    classdef->initoffset = uint32_t(nvalue(initoffset));
    LUAU_ASSERT(classdef->initoffset >= classdef->numberofinstancemembers && classdef->initoffset < classdef->numberofallmembers);
    classdef->memberflags[classdef->initoffset] |= LBC_CLASSMEMBER_INITBLOCKED;
    classdef->offsettomember[classdef->initoffset] = NULL;

    classdef->memberstooffset->readonly = true;
}

size_t luaR_classsize(const LuauClass* classdef)
{
    uint32_t numberofstaticmembers = classdef->numberofallmembers - classdef->numberofinstancemembers;

    // The "object" itself ...
    return sizeof(LuauClass) +
           // ... plus the method closures, each a `TValue` wide ...
           (numberofstaticmembers * sizeof(TValue)) +
           // ... plus a string pointer and a flags byte for each method or property ...
           (classdef->numberofallmembers * (sizeof(TString*) + sizeof(uint8_t))) +
           // ... plus the constant field defaults, when the class carries them.
           (classdef->memberdefaults ? classdef->numberofinstancemembers * sizeof(TValue) : 0);
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
    // deviaze: Since we're eating the expense of a prop on every proto to keep track of class ownership
    // for ncg lowering let's just use that prop.

    // Covers all closures lexically scoped inside a private access owning closure
    // as well (luaR_stampownerclass stamps every proto nested in a method).
    return !cl->isC && cl->l.p->ownerclass == classdef;
}

// Finds the Luwu code on whose behalf a VM-internal C function is acting: the nearest Lua frame,
// looking through any C frames above it. NULL means there is none, i.e. native code drove this
// through the C API.
//
// This exists for `luaR_createobject`, which is the class's `__call` metamethod and therefore performs
// construction *on behalf of whoever called the class*. Its immediate caller is not that: for
// `pcall(SomeClass, ...)` it is `pcall`, and a builtin's C frame is not authority for anything. Note
// this is not the rule for an access a C function performs itself -- see luaR_checkprivateaccess.
static const Closure* luaR_callinglua(lua_State* L)
{
    for (CallInfo* ci = L->ci; ci > L->base_ci; ci--)
        if (isLua(ci))
            return clvalue(ci->func);

    return NULL;
}

void luaR_checkprivateaccess(lua_State* L, const TValue* key, const LuauClass* classdef, const Closure* cl, uint32_t offset)
{
    // deviaze: unlike upstream, we reject all user code calls to __init. We may relax this if a usecase appears later.
    // (.__init reinits open huge cans of soundness holes around const and private ownership laundering)
    if (LUAU_UNLIKELY((classdef->memberflags[offset] & LBC_CLASSMEMBER_INITBLOCKED) != 0))
        luaG_blockedinitaccesserror(L, classdef->name);

    if ((classdef->memberflags[offset] & LBC_CLASSMEMBER_PRIVATE) == 0)
        return;

    // deviaze: critically, embedders using C api (native code) should be allowed to bypass private field restrictions;
    // if they choose to expose unchecked apis (that don't check lua_getmemberaccess) to luwu code that bypasses private access
    // that's up to them. if a runtime allows users in luwu to load arbitrary C/Rust/whatever code that can access the luwu C API
    // all bets are off (atp private access is the least of their worries): they shouldn't assume any private access is sandboxed from users.
    // for embedder provided code written in luwu, embedders could try to use debug apis to see
    // if any suspicious luwu/c stack frames exist and reject suspicious ones via runtime error from within truly private code.
    
    // A builtin doesn't count as native code here: Luwu code reaching a member through one (pcall, xpcall, coroutine lib fns)
    // is still checked (see luaR_callinglua).
    if (!cl || cl->isC)
        return;

    if (luaR_closureownsprivateaccess(classdef, cl))
        return;

    luaG_privateaccesserror(L, key, classdef->name);
}

void luaR_checkprivateconstructor(lua_State* L, const LuauClass* classdef, const Closure* cl)
{
    LUAU_ASSERT(luaR_hasprivateconstructor(classdef));

    // See luaR_checkprivateaccess for why native code is trusted.
    if (!cl || cl->isC || luaR_closureownsprivateaccess(classdef, cl))
        return;

    luaG_privateconstructorerror(L, classdef->name);
}

void luaR_checkconstassign(lua_State* L, const TValue* key, const LuauObject* object, const Closure* cl, uint32_t offset)
{
    const LuauClass* classdef = object->lclass;

    if ((classdef->memberflags[offset] & LBC_CLASSMEMBER_CONST) == 0)
        return;

    // deviaze: embedder code isn't allowed to bypass `const` access (unlike private access) because
    // allowing such could lead to UB in future const optimizations.
    // also if you're using `const` just to have embedder-only-assignable fields please just use userdata
    if (!cl || !luaR_closureisinit(classdef, cl))
        luaG_constassignerror(L, key, classdef->name);

    // `__init` constructs the object in its `self` parameter, which is register 0 of its frame. This
    // holds for a vararg `__init` too: its fixed parameters are moved up, and its frame base moves with
    // them. Any other object of the class is already constructed.
    LUAU_ASSERT(isLua(L->ci) && clvalue(L->ci->func) == cl);
    const TValue* self = L->ci->base;
    bool writesownself = ttisobject(self) && objectvalue(self) == object;

    if (!writesownself)
        luaG_constassignnotselferror(L, key, classdef->name);
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
        LUAU_ASSERT(classdef->initoffset == offsetint);
        classdef->hascustominit = true;
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

        // Nothing outside the VM can reach this table (getmetatable returns nil for objects), but it
        // is the operator path's source of truth, so keep it locked like the class's own metatable.
        // luaH_setstr above ignores `readonly`, so later metamethods of this class still land.
        classdef->instancemetatable->readonly = true;
    }
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

// luaR_applyobjectfieldsslow with the reads made on behalf of `accessor` (NULL: native code).
static void luaR_applyobjectfieldsas(lua_State* L, LuauClass* classdef, LuauObject* object, const TValue* arg, const Closure* accessor)
{
    // `Point(nil)` constructs the same object as `Point()`.
    if (ttisnil(arg))
        return;

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
        luaV_gettablefor(L, &source, &key, L->top - 1, accessor);
        const TValue* value = L->top - 1;

        // A field absent from the argument (or nil) keeps its default.
        if (!ttisnil(value))
        {
            setobj(L, &object->members[idx], value);
            // An __index call can run the collector, so `object` may already be black. Once the next
            // field's lookup overwrites the stack slot, this member is the value's only reference, so
            // it needs the barrier.
            luaC_barrier(L, object, value);
        }
    }

    L->top--;
}

void luaR_applyobjectfieldsslow(lua_State* L, LuauClass* classdef, LuauObject* object, const TValue* arg)
{
    luaR_applyobjectfieldsas(L, classdef, object, arg, isLua(L->ci) ? clvalue(L->ci->func) : NULL);
}

// Initializes the object with the POD constructor, from the user-provided table at `args` mapping
// expected fields to values. Since classes can have 0 fields that need to be initialized we also allow
// Class() here as well (if class actually had fields they will be nil, or their default).
//
// Field defaults come from one of two places: constant defaults are serialized into the class shape
// and copied by luaR_newobject, while a class with any non-constant default (`= {}`, a call, ...)
// calls its synthesized `__defaults` closure here, since those have to be re-evaluated on every
// construction. Only the latter pays for a `lua_call` here.
void luaR_initpodobject(lua_State* L, LuauClass* classdef, LuauObject* object, StkId args, int nargs, const Closure* accessor)
{
    if (nargs > 1)
        luaL_error(
            L,
            "the default constructor for constructing a '%s' expected zero or one arguments "
            "(table mapping field names to values or nothing if class has 0 fields), got an incorrect number of arguments",
            getstr(classdef->name)
        );

    // `__defaults` and `__index` can reallocate the stack
    ptrdiff_t argsslot = savestack(L, args);

    if (classdef->haspoddefaultsfn)
    {
        uint32_t nresults = classdef->numberofinstancemembers;
        luaL_checkstack(L, int(nresults) + 1, "class field defaults");

        setobj2s(L, L->top, &classdef->staticmembers[classdef->poddefaultsoffset - classdef->numberofinstancemembers]);
        L->top++;
        lua_call(L, 0, int(nresults));

        StkId results = L->top - nresults;
        for (uint32_t idx = 0; idx < nresults; idx++)
            setobj(L, &object->members[idx], &results[idx]);

        L->top -= nresults;

        // `object` can be black by now, and the argument's __index calls below can run the collector
        // before the barrier at the end.
        luaC_barrierfast(L, object);
    }

    // assume class has 0 fields to initialize or user wants all fields to be nil (or their default)
    if (nargs == 0)
        return;

    const TValue* arg = restorestack(L, argsslot);

    // The argument is a plain field bag in every realistic case, so read it with a direct string
    // lookup; only a table carrying a metatable (or a non-table) needs the generic __index-aware path.
    if (ttistable(arg) && hvalue(arg)->metatable == NULL)
    {
        luaR_applyobjectfields(L, classdef, object, hvalue(arg));
        // Preserve the GC invariant, moving barrier back once after writing multiple objects (similar to SETLIST)
        luaC_barrierfast(L, object);
    }
    else
    {
        luaR_applyobjectfieldsas(L, classdef, object, arg, accessor);
    }
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
    // of constructing objects. The interpreter reads them through `members`; native code addresses
    // them from the object pointer (LUAR_OBJECT_MEMBERS_OFFSET).
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
    //
    // Construction happens on behalf of whoever called the class, so authority is the nearest Lua
    // frame rather than the frame directly below: for `pcall(SomeClass, ...)` that frame is `pcall`,
    // and a builtin is not authority for anything. Native code with no Lua frame under it at all is
    // trusted, same as an access it performs itself. That authority decides both whether a private
    // constructor may be called and whether the POD constructor may read an argument's private fields.
    const Closure* constructing = luaR_callinglua(L);

    if (luaR_hasprivateconstructor(classdef))
        luaR_checkprivateconstructor(L, classdef, constructing);

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

    luaR_initpodobject(L, classdef, object, L->base + 1, numargs - 1, constructing);

    return 1;
}

static int luaR_createobjectcont(lua_State* L, int status)
{
    // __init was called with zero expected results, so the object we constructed is left on top of
    // the stack (see luaR_createobject); return it as the constructor's single result.
    return 1;
}

void luaR_freeclass(lua_State* L, LuauClass* classdef, lua_Page* page)
{
    // Any of these is NULL when an allocation in luaR_newclass failed before it.
    uint32_t numberofstaticmembers = classdef->numberofallmembers - classdef->numberofinstancemembers;

    if (classdef->staticmembers)
        luaM_freearray(L, classdef->staticmembers, numberofstaticmembers, TValue, classdef->memcat);
    if (classdef->offsettomember)
        luaM_freearray(L, classdef->offsettomember, classdef->numberofallmembers, TString*, classdef->memcat);
    if (classdef->memberflags)
        luaM_freearray(L, classdef->memberflags, classdef->numberofallmembers, uint8_t, classdef->memcat);
    if (classdef->memberdefaults)
        luaM_freearray(L, classdef->memberdefaults, classdef->numberofinstancemembers, TValue, classdef->memcat);
    luaM_freegco(L, classdef, sizeof(LuauClass), classdef->memcat, page);
}

void luaR_freeobject(lua_State* L, LuauObject* object, lua_Page* page)
{
    luaM_freegco(L, object, luaR_objectsize(object->numberofmembers), object->memcat, page);
}
