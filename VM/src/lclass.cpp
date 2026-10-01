// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details

#include "lclass.h"

#include "ldebug.h"
#include "ldo.h"
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
#include "lbytecode.h"

#include <stdio.h>
#include <string.h>

LUAU_FASTFLAG(LuauCIProto)

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
    classdef->istrait = false;
    classdef->traitspending = false;
    classdef->numberofownmembers = numberofinstancemembers;
    classdef->traits = NULL;
    classdef->numtraits = 0;
    classdef->traitinits = NULL;
    classdef->numtraitinits = 0;
    classdef->numdirecttraitinits = 0;
    classdef->traitdefaults = NULL;

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

    // Luwu Traits (rfcs/classes/traits.md): only a trait's own functions can be read through it (see luaR_addclassmember). Its fields only exist
    // in the objects of implementing classes, an expected function only in those classes, and `__traitinit`/`__needs`
    // are the VM's. Blocking a member sends every read of it to luaR_checkprivateaccess, the same way `__init` is
    // blocked, which raises the trait's error.
    if (classdef->istrait)
    {
        TString* traitinit = luaS_newliteral(L, "__traitinit");
        TString* needs = luaS_newliteral(L, "__needs");

        for (uint32_t i = 0; i < classdef->numberofallmembers; i++)
        {
            TString* name = classdef->offsettomember[i];
            bool isfunction = i >= classdef->numberofinstancemembers;
            bool readable = isfunction && name && name != traitinit && name != needs &&
                            !(classdef->memberflags[i] & LBC_CLASSMEMBER_EXPECTED);

            if (!readable)
                classdef->memberflags[i] |= LBC_CLASSMEMBER_INITBLOCKED;
        }

        classdef->hasprivatemembers = true;
    }

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
           // ... plus the constant field defaults, when the class carries them ...
           (classdef->memberdefaults ? classdef->numberofinstancemembers * sizeof(TValue) : 0) +
           // ... plus what it keeps about the traits it implements.
           (classdef->numtraits * sizeof(LuauClass*)) + (classdef->numtraitinits * sizeof(TValue));
}

bool luaR_closureisinit(const LuauClass* classdef, const Closure* cl)
{
    if (cl->isC)
        return false;

    if (classdef->hascustominit)
    {
        const TValue* v = &classdef->staticmembers[classdef->initoffset - classdef->numberofinstancemembers];
        if (ttisfunction(v) && clvalue(v) == cl)
            return true;
    }

    return luaR_closureistraitinit(classdef, cl);
}

bool luaR_closureistraitinit(const LuauClass* classdef, const Closure* cl)
{
    // The class's copies of its traits' `__traitinit` initialize the trait fields, const and final ones included. The
    // class's `__inittraits`, which only calls them, doesn't count.
    for (uint32_t i = 0; i < classdef->numtraitinits; i++)
    {
        bool isinittraits = classdef->numtraitinits > classdef->numdirecttraitinits && i == classdef->numdirecttraitinits;
        if (!isinittraits && clvalue(&classdef->traitinits[i]) == cl)
            return true;
    }

    return false;
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
    {
        // Luwu Traits (rfcs/classes/traits.md): the members of a trait that can't be read through it (see luaR_sealclassshape)
        if (classdef->istrait)
        {
            const char* name = ttisstring(key) ? svalue(key) : "?";

            if (offset < classdef->numberofinstancemembers && ttisstring(key))
            {
                TValue trait;
                setclassvalue(L, &trait, const_cast<LuauClass*>(classdef));
                luaG_instancefieldonclasserror(L, &trait, key);
            }

            if (classdef->memberflags[offset] & LBC_CLASSMEMBER_EXPECTED)
                luaG_runerror(
                    L, "cannot read '%s' of trait '%s': it is expected, not defined", name, getstr(classdef->name)
                );

            if (offset == classdef->initoffset)
                luaG_runerror(L, "trait '%s' has no '__init'", getstr(classdef->name));

            // the functions only the VM calls (see luaR_sealclassshape)
            luaG_runerror(L, "cannot read '%s' of trait '%s'", name, getstr(classdef->name));
        }

        luaG_blockedinitaccesserror(L, classdef->name);
    }

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

    // Luwu Traits (rfcs/classes/traits.md): a trait's own code -- its `__create` and its statics -- may construct the classes that implement it,
    // which is what lets a factory trait be the only way to make them. A trait's methods only ever run as each class's
    // copy, which belongs to that class instead.
    const LuauClass* owner = cl->l.p->ownerclass;
    if (owner && owner->istrait && luaR_implements(classdef, owner))
        return;

    luaG_privateconstructorerror(L, classdef->name);
}

void luaR_checkconstassign(lua_State* L, const TValue* key, const LuauObject* object, const Closure* cl, uint32_t offset)
{
    const LuauClass* classdef = object->lclass;

    if ((classdef->memberflags[offset] & LBC_CLASSMEMBER_CONST) == 0)
        return;

    // Luwu Traits (rfcs/classes/traits.md): a final field belongs to its trait, so only the trait's initializer writes it,
    // not the class's `__init`
    bool isfinal = (classdef->memberflags[offset] & LBC_CLASSMEMBER_FINAL) != 0;
    if (isfinal && (!cl || !luaR_closureistraitinit(classdef, cl)))
        luaG_runerror(
            L, "'%s' is a final field of '%s' and can't be assigned", ttisstring(key) ? svalue(key) : "?", getstr(classdef->name)
        );

    // deviaze: embedder code isn't allowed to bypass `const` access (unlike private access) because
    // allowing such could lead to UB in future const optimizations.
    // also if you're using `const` just to have embedder-only-assignable fields please just use userdata
    if (!cl || (!isfinal && !luaR_closureisinit(classdef, cl)))
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

// Luwu Traits (rfcs/classes/traits.md): the function `Trait.method` reads as. Its upvalues are the trait, the method's name and the trait's
// own closure (which luaR_implementtraits copies into implementing classes). It calls the receiver's class's copy of
// the trait's method, even when the class overrides it: `Trait.method(obj)` is how an override reaches the default.
static int luaR_traitmethod(lua_State* L)
{
    Closure* dispatcher = clvalue(L->ci->func);
    const LuauClass* trait = classvalue(&dispatcher->c.upvals[0]);
    TString* name = tsvalue(&dispatcher->c.upvals[1]);
    int nargs = lua_gettop(L);

    const TValue* receiver = nargs > 0 ? L->base : NULL;
    bool implements = receiver && ttisobject(receiver) && luaR_implements(objectvalue(receiver)->lclass, trait);

    if (!implements)
        luaL_error(
            L,
            "'%s.%s' expects an object implementing '%s', got %s",
            getstr(trait->name),
            getstr(name),
            getstr(trait->name),
            receiver ? luaT_objtypename(L, receiver) : "no value"
        );

    // The class implements the trait, so it has a function of this name: its own, or its copy of the trait's
    // (luaR_implementtraits refuses a class field that clashes with it).
    const LuauClass* cls = objectvalue(receiver)->lclass;
    const TValue* offset = luaH_getstr(cls->memberstooffset, name);
    LUAU_ASSERT(!ttisnil(offset) && uint32_t(nvalue(offset)) >= cls->numberofinstancemembers);
    uint32_t memberoffset = uint32_t(nvalue(offset));

    // The call reads the class's member on behalf of the Luwu code calling through the trait: a class may define the
    // method as private, and calling through the trait doesn't launder that. The trait's own code is the exception, as
    // for private constructors (luaR_checkprivateconstructor): the method is the trait's, whoever defines it.
    const Closure* caller = luaR_callinglua(L);
    bool traitsowncode = caller && !caller->isC && caller->l.p->ownerclass == trait;
    if (!traitsowncode)
    {
        TValue key;
        setsvalue(L, &key, name);
        luaR_checkprivateaccess(L, &key, cls, caller, memberoffset);
    }

    // The class's slot holds its copy of the default, unless the class overrides it: then the copy is in traitdefaults
    const TValue* fn = &cls->staticmembers[memberoffset - cls->numberofinstancemembers];
    if (cls->traitdefaults)
    {
        const TValue* overridden = luaH_get(cls->traitdefaults, &dispatcher->c.upvals[2]);
        if (!ttisnil(overridden))
            fn = overridden;
    }

    luaL_checkstack(L, 1, "trait method call");
    luaC_threadbarrier(L);

    for (StkId arg = L->top; arg > L->base; arg--)
        setobj2s(L, arg, arg - 1);
    setobj2s(L, L->base, fn);
    L->top++;

    return luaL_callyieldable(L, nargs, LUA_MULTRET);
}

static int luaR_traitmethodcont(lua_State* L, int status)
{
    // the callee's results replaced it and its arguments, from the bottom of this frame
    return lua_gettop(L);
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

    // Luwu Traits (rfcs/classes/traits.md): a trait's method is read through the trait as a dispatcher to the receiver's class, which keeps
    // the trait's own closure for luaR_implementtraits to copy. A static is read as itself.
    if (classdef->istrait && (classdef->memberflags[offsetint] & LBC_CLASSMEMBER_TAKESSELF))
    {
        Closure* dispatcher = luaF_newCclosure(L, 3, L->gt);
        setclassvalue(L, &dispatcher->c.upvals[0], classdef);
        setsvalue(L, &dispatcher->c.upvals[1], name);
        setobj(L, &dispatcher->c.upvals[2], value);
        dispatcher->c.f = luaR_traitmethod;
        dispatcher->c.cont = luaR_traitmethodcont;
        // points into upvalue 2, which the dispatcher holds for its whole life
        dispatcher->c.debugname = getstr(name);

        setclvalue(L, &classdef->staticmembers[offsetint - classdef->numberofinstancemembers], dispatcher);
        luaC_objbarrier(L, classdef, dispatcher);
        return;
    }

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

// Luwu Traits (rfcs/classes/traits.md): a constructor's table can't set a final field, which only its trait sets
static void luaR_checknotfinal(lua_State* L, const LuauClass* classdef, uint32_t idx)
{
    if (classdef->memberflags[idx] & LBC_CLASSMEMBER_FINAL)
        luaG_runerror(
            L, "'%s' is a final field of '%s' and can't be assigned", getstr(classdef->offsettomember[idx]), getstr(classdef->name)
        );
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
        {
            luaR_checknotfinal(L, classdef, idx);
            setobj(L, &object->members[idx], value);
        }
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
            luaR_checknotfinal(L, classdef, idx);
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
static void luaR_initpodobjectny(lua_State* L, LuauClass* classdef, LuauObject* object, StkId args, int nargs, const Closure* accessor)
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
        // the fields of the traits the class implements come after its own, and were initialized by luaR_inittraitfields
        uint32_t nresults = classdef->numberofownmembers;
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

// Initializes the object without letting `__defaults` or an argument's `__index` yield.
//
// The constructor (luaR_createobject) has a continuation so that `__init` can yield, and that makes every call its frame
// makes yieldable. A yield doesn't stop the C code that made the call: the call returns, and the C code carries on as if
// it had finished, reading results out of whatever the stack then holds. Both calls here run in the middle of filling
// the object, so they must not yield. One C call level more than the thread was resumed with makes lua_yield refuse;
// an error unwinds that level with the rest (luaD_pcall and resume restore nCcalls).
void luaR_initpodobject(lua_State* L, LuauClass* classdef, LuauObject* object, StkId args, int nargs, const Closure* accessor)
{
    L->nCcalls++;
    luaR_initpodobjectny(L, classdef, object, args, nargs, accessor);
    L->nCcalls--;
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

    luaR_checktraitsimplemented(L, classdef);

    // Luwu Traits (rfcs/classes/traits.md): calling a trait calls its `__create` in place of the trait, with the same arguments, and returns
    // its result, whatever it is
    if (classdef->istrait)
    {
        setobj2s(L, L->base, luaR_traitcreate(L, classdef));
        return luaL_callyieldable(L, lua_gettop(L) - 1, 1);
    }

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

    // Luwu Traits (rfcs/classes/traits.md): trait fields are initialized before the class's own defaults and `__init`
    if (classdef->traitinits)
        luaR_inittraitfields(L, classdef, object, L->base + 1, numargs - 1);

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

    if (classdef->traits)
        luaM_freearray(L, classdef->traits, classdef->numtraits, LuauClass*, classdef->memcat);
    if (classdef->traitinits)
        luaM_freearray(L, classdef->traitinits, classdef->numtraitinits, TValue, classdef->memcat);

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

// Luwu Traits (rfcs/classes/traits.md)

void luaR_checktraitsimplemented(lua_State* L, const LuauClass* classdef)
{
    if (classdef->traitspending)
        luaG_runerror(L, "class '%s' can't be constructed until its traits are implemented", getstr(classdef->name));
}

l_noret luaR_traitconstructionerror(lua_State* L, const LuauClass* trait)
{
    luaG_runerror(L, "trait '%s' can't be called: it has no '__create'", getstr(trait->name));
}

// The static member of `classdef` named `name`, or NULL when it has none.
static const TValue* luaR_findstaticmember(lua_State* L, const LuauClass* classdef, const char* name)
{
    const TValue* offset = luaH_getstr(classdef->memberstooffset, luaS_new(L, name));
    if (ttisnil(offset))
        return NULL;

    uint32_t offsetint = uint32_t(nvalue(offset));
    if (offsetint < classdef->numberofinstancemembers)
        return NULL;

    return &classdef->staticmembers[offsetint - classdef->numberofinstancemembers];
}

const TValue* luaR_traitcreate(lua_State* L, const LuauClass* trait)
{
    const TValue* create = luaR_findstaticmember(L, trait, "__create");
    if (!create || !ttisfunction(create))
        luaR_traitconstructionerror(L, trait);

    return create;
}

// A trait with parameters is never implied by a `needs` list: only the class passes trait arguments, so it has to list
// the trait itself. `__traitinit` takes `self`, then the trait's parameters.
static bool luaR_traithasparameters(lua_State* L, const LuauClass* trait)
{
    const TValue* init = luaR_findstaticmember(L, trait, "__traitinit");
    return init && ttisfunction(init) && !clvalue(init)->isC && clvalue(init)->l.p->numparams > 1;
}

// A deep copy of `p` and every proto nested in it. The copy owns its instructions, so the member slot caches in them
// only ever see the one class the copy belongs to. Strings are immutable and shared. The copy is white and nothing
// here runs a GC step, so it survives until the caller anchors it.
static Proto* luaR_copyproto(lua_State* L, Proto* p)
{
    Proto* c = luaF_newproto(L);

    c->nups = p->nups;
    c->numparams = p->numparams;
    c->is_vararg = p->is_vararg;
    c->maxstacksize = p->maxstacksize;
    c->flags = p->flags;
    c->source = p->source;
    c->debugname = p->debugname;
    c->userdata = p->userdata;
    c->linegaplog2 = p->linegaplog2;
    c->linedefined = p->linedefined;
    c->bytecodeid = p->bytecodeid;
    c->funid = p->funid;
    c->cost = p->cost;

    // Each size is set right after its array exists, so a failed allocation leaves a proto luaF_freeproto can free. An
    // empty array is NULL, which memcpy may not be given even for zero bytes.
    c->code = luaM_newarray(L, p->sizecode, Instruction, c->memcat);
    if (p->sizecode > 0)
        memcpy(c->code, p->code, sizeof(Instruction) * p->sizecode);
    c->sizecode = p->sizecode;
    c->codeentry = c->code;

    // A breakpoint replaces its instruction with LOP_BREAK, which reads the original opcode back from `debuginsn`. The
    // code was copied with any breakpoints in it, so the copy needs the original opcodes too.
    if (p->debuginsn)
    {
        c->debuginsn = luaM_newarray(L, p->sizecode, uint8_t, c->memcat);
        memcpy(c->debuginsn, p->debuginsn, p->sizecode);
    }

    c->k = luaM_newarray(L, p->sizek, TValue, c->memcat);
    for (int i = 0; i < p->sizek; i++)
        setnilvalue(&c->k[i]);
    c->sizek = p->sizek;

    c->p = luaM_newarray(L, p->sizep, Proto*, c->memcat);
    for (int i = 0; i < p->sizep; i++)
        c->p[i] = NULL;
    c->sizep = p->sizep;

    for (int i = 0; i < p->sizep; i++)
        c->p[i] = luaR_copyproto(L, p->p[i]);

    for (int i = 0; i < p->sizek; i++)
    {
        const TValue* k = &p->k[i];

        // DUPCLOSURE's closure constants are closures of this proto's children, which the copy has its own of
        int child = -1;
        if (ttisfunction(k) && !clvalue(k)->isC)
        {
            for (int j = 0; j < p->sizep && child < 0; j++)
                if (p->p[j] == clvalue(k)->l.p)
                    child = j;
        }

        if (child < 0)
        {
            setobj(L, &c->k[i], k);
            continue;
        }

        Closure* original = clvalue(k);
        Closure* copy = luaF_newLclosure(L, original->nupvalues, original->env, c->p[child]);
        for (int u = 0; u < original->nupvalues; u++)
            setobj(L, &copy->l.uprefs[u], &original->l.uprefs[u]);
        copy->preload = original->preload;
        setclvalue(L, &c->k[i], copy);
    }

    if (p->lineinfo)
    {
        c->lineinfo = luaM_newarray(L, p->sizelineinfo, uint8_t, c->memcat);
        memcpy(c->lineinfo, p->lineinfo, p->sizelineinfo);
        c->sizelineinfo = p->sizelineinfo;
        c->abslineinfo = (int*)(c->lineinfo + ((uint8_t*)p->abslineinfo - p->lineinfo));
    }

    c->locvars = luaM_newarray(L, p->sizelocvars, LocVar, c->memcat);
    if (p->sizelocvars > 0)
        memcpy(c->locvars, p->locvars, sizeof(LocVar) * p->sizelocvars);
    c->sizelocvars = p->sizelocvars;

    c->upvalues = luaM_newarray(L, p->sizeupvalues, TString*, c->memcat);
    if (p->sizeupvalues > 0)
        memcpy(c->upvalues, p->upvalues, sizeof(TString*) * p->sizeupvalues);
    c->sizeupvalues = p->sizeupvalues;

    if (p->typeinfo)
    {
        c->typeinfo = luaM_newarray(L, p->sizetypeinfo, uint8_t, c->memcat);
        memcpy(c->typeinfo, p->typeinfo, p->sizetypeinfo);
        c->sizetypeinfo = p->sizetypeinfo;
    }

    if (p->feedbackvec)
    {
        c->feedbackvec = luaM_newarray(L, p->feedbackvecsize, FeedbackVectorSlot, c->memcat);
        memcpy(c->feedbackvec, p->feedbackvec, sizeof(FeedbackVectorSlot) * p->feedbackvecsize);
        c->feedbackvecsize = p->feedbackvecsize;
    }

    return c;
}

// A trait function's copy for one implementing class: a new closure over a copy of its proto, sharing its upvalues.
static Closure* luaR_copytraitfunction(lua_State* L, const Closure* cl)
{
    LUAU_ASSERT(!cl->isC);

    Proto* p = luaR_copyproto(L, cl->l.p);
    Closure* copy = luaF_newLclosure(L, cl->nupvalues, cl->env, p);
    for (int i = 0; i < cl->nupvalues; i++)
        setobj(L, &copy->l.uprefs[i], &cl->l.uprefs[i]);
    copy->preload = cl->preload;

    return copy;
}

// Members of a trait that implementing classes don't get: the functions the VM calls itself (the trait's `__init` slot
// has no name at all, see luaR_sealclassshape).
static bool luaR_istraitinternal(lua_State* L, TString* name)
{
    return name == luaS_newliteral(L, "__traitinit") || name == luaS_newliteral(L, "__needs");
}

static const char* luaR_memberkind(bool isfield)
{
    return isfield ? "field" : "function";
}

static const char* luaR_visibilityname(uint8_t flags)
{
    return (flags & LBC_CLASSMEMBER_PRIVATE) ? "private" : "public";
}

// How deep a chain of `needs` luaR_checkneedscycles follows before giving up, like a C call depth limit
#define LUAR_MAX_NEEDS_DEPTH 200

// The marks luaR_checkneedscycles stores in its `state` table, which holds numbers
static const double LUAR_NEEDS_VISITING = 1;
static const double LUAR_NEEDS_DONE = 2;

// Raises for traits that need each other, naming every trait in the cycle in the order it goes: "traits 'A', 'B' and
// 'C' are codependent. ..." Analysis words its error the same way (codependentTraitsMessage).
static l_noret luaR_codependenterror(lua_State* L, LuauClass* const* cycle, int count)
{
    char names[512] = {0};
    for (int i = 0; i < count; i++)
    {
        size_t used = strlen(names);
        const char* separator = "";
        if (i + 1 == count)
            separator = " and ";
        else if (i > 0)
            separator = ", ";

        snprintf(names + used, sizeof(names) - used, "%s'%s'", separator, getstr(cycle[i]->name));
    }

    luaG_runerror(
        L,
        "traits %s are codependent. This is an unhealthy relationship; consider merging these traits or factoring out common "
        "members into a new trait",
        names
    );
}

// Raises when `trait` is part of a cycle of `needs`. Traits that need each other are always implemented together, so
// they should be one trait. `edges` maps each trait to an array of the traits it needs; `state` marks the traits the
// search is inside of (LUAR_NEEDS_VISITING) and the ones already cleared (LUAR_NEEDS_DONE). `path` holds the traits the
// search is inside of, `trait` at `depth`, so a cycle can be named in full.
static void luaR_checkneedscycles(lua_State* L, LuaTable* edges, LuaTable* state, LuauClass* trait, LuauClass** path, int depth)
{
    if (depth >= LUAR_MAX_NEEDS_DEPTH)
        luaG_runerror(L, "trait '%s' needs traits nested more than %d deep", getstr(trait->name), LUAR_MAX_NEEDS_DEPTH);

    TValue key;
    setclassvalue(L, &key, trait);

    const TValue* list = luaH_get(edges, &key);
    if (!ttistable(list))
        return;

    path[depth] = trait;
    setnvalue(luaH_set(L, state, &key), LUAR_NEEDS_VISITING);

    for (int i = 1; i <= luaH_getn(hvalue(list)); i++)
    {
        LuauClass* needed = classvalue(luaH_getnum(hvalue(list), i));

        TValue neededkey;
        setclassvalue(L, &neededkey, needed);
        const TValue* neededstate = luaH_get(state, &neededkey);

        if (ttisnumber(neededstate) && nvalue(neededstate) == LUAR_NEEDS_DONE)
            continue;

        if (ttisnumber(neededstate))
        {
            if (needed == trait)
                luaG_runerror(L, "trait '%s' needs itself", getstr(trait->name));

            // `needed` is on the path, since the search is inside of it: the cycle runs from there to here
            int start = depth;
            while (start > 0 && path[start] != needed)
                start--;

            luaR_codependenterror(L, path + start, depth - start + 1);
        }

        luaR_checkneedscycles(L, edges, state, needed, path, depth + 1);
    }

    setnvalue(luaH_set(L, state, &key), LUAR_NEEDS_DONE);
}

// What a value that should be a trait is instead, for an error: "nil", "none", "true", "class 'Foo'", "a table",
// "an object". The single-valued types are named by their value, with no article.
static void luaR_describenontrait(lua_State* L, const TValue* t, char* buf, size_t size)
{
    if (ttisnil(t))
        snprintf(buf, size, "nil");
    else if (ttissymnone(t))
        snprintf(buf, size, "none");
    else if (ttisboolean(t))
        snprintf(buf, size, "%s", bvalue(t) ? "true" : "false");
    else if (ttisclass(t))
        snprintf(buf, size, "class '%s'", getstr(classvalue(t)->name));
    else
    {
        const char* name = luaT_objtypename(L, t);
        // strchr also finds the terminator, so an empty name is checked first
        bool startsWithVowel = name[0] != 0 && strchr("aeiouAEIOU", name[0]) != nullptr;
        snprintf(buf, size, "%s %s", startsWithVowel ? "an" : "a", name);
    }
}

// The name entry `i` of the running class statement's `implements` list was read from ("global 'Item'",
// "global 'mod.Item'", "field 'Item'", and with debug level 2 "upvalue 'Item'" or "local 'Item'"), found in the
// instruction that loaded its register. False when it wasn't read from a name the bytecode keeps. Only called for an
// error.
static bool luaR_implementsentryname(lua_State* L, uint32_t i, char* buf, size_t size)
{
    buf[0] = 0;

    if (!isLua(L->ci))
        return false;

    // the proto the frame is running, which `savedpc` points into: the closure's may have been promoted since it started
    Proto* p = FFlag::LuauCIProto ? L->ci->p : clvalue(L->ci->func)->l.p;
    const Instruction* implements = L->ci->savedpc - 2;

    bool inside = implements >= p->code && implements < p->code + p->sizecode;
    if (!inside || LUAU_INSN_OP(*implements) != LOP_NEWCLASSMEMBER || LUAU_INSN_B(*implements) != LBC_NEWCLASSMEMBER_IMPLEMENTS)
        return false;

    uint32_t reg = LUAU_INSN_C(*implements) + i;

    // the last instruction before the list that names the entry's register in operand A loaded it: the list's
    // expressions are plain names and indexing, with no branches between them
    const Instruction* loader = NULL;
    for (const Instruction* pc = p->code; pc < implements;)
    {
        int op = p->debuginsn ? p->debuginsn[pc - p->code] : LUAU_INSN_OP(*pc);
        if (LUAU_INSN_A(*pc) == reg)
            loader = pc;
        pc += Luau::getOpLength(LuauOpcode(op));
    }

    if (!loader)
        return false;

    int op = p->debuginsn ? p->debuginsn[loader - p->code] : LUAU_INSN_OP(*loader);

    if (op == LOP_GETGLOBAL || op == LOP_GETTABLEKS)
    {
        const TValue* k = &p->k[loader[1]];
        if (ttisstring(k))
            snprintf(buf, size, "%s '%s'", op == LOP_GETGLOBAL ? "global" : "field", svalue(k));
    }
    else if (op == LOP_GETIMPORT)
    {
        // the import path's names, as constant indices packed into the aux word (see BytecodeBuilder::getImportId)
        uint32_t aux = loader[1];
        uint32_t count = aux >> 30;
        uint32_t ids[3] = {(aux >> 20) & 1023, (aux >> 10) & 1023, aux & 1023};

        char path[256] = {0};
        for (uint32_t n = 0; n < count && n < 3; n++)
        {
            const TValue* k = ids[n] < uint32_t(p->sizek) ? &p->k[ids[n]] : NULL;
            if (!k || !ttisstring(k))
                return false;

            size_t used = strlen(path);
            snprintf(path + used, sizeof(path) - used, "%s%s", n > 0 ? "." : "", svalue(k));
        }

        snprintf(buf, size, "global '%s'", path);
    }
    else if (op == LOP_GETUPVAL)
    {
        int up = LUAU_INSN_B(*loader);
        if (up < p->sizeupvalues && p->upvalues[up])
            snprintf(buf, size, "upvalue '%s'", getstr(p->upvalues[up]));
    }
    else if (op == LOP_MOVE)
    {
        uint32_t source = LUAU_INSN_B(*loader);
        int pcindex = int(loader - p->code);

        for (int v = 0; v < p->sizelocvars; v++)
        {
            const LocVar& local = p->locvars[v];
            if (local.reg == source && local.startpc <= pcindex && pcindex < local.endpc)
                snprintf(buf, size, "local '%s'", getstr(local.varname));
        }
    }

    return buf[0] != 0;
}

// Pushes the traits a class implements onto the stack, the listed ones first, then the ones their `needs` lists imply,
// each once. Returns how many it pushed. Raises for a cycle of `needs`.
static int luaR_collecttraits(lua_State* L, LuauClass* classdef, ptrdiff_t listedslot, uint32_t n)
{
    luaD_checkstack(L, int(n) + 3);

    // every trait's `needs`, for luaR_checkneedscycles
    LuaTable* edges = luaH_new(L, 0, int(n));
    sethvalue(L, L->top, edges);
    L->top++;

    LuaTable* seen = luaH_new(L, 0, int(n));
    sethvalue(L, L->top, seen);
    L->top++;

    int first = cast_int(L->top - L->base);

    for (uint32_t i = 0; i < n; i++)
    {
        const TValue* t = restorestack(L, listedslot) + i;

        if (!ttisclass(t) || !classvalue(t)->istrait)
        {
            char what[160];
            luaR_describenontrait(L, t, what, sizeof(what));

            char entry[320];
            if (luaR_implementsentryname(L, i, entry, sizeof(entry)))
                luaG_runerror(L, "class '%s' implements %s, which is %s, not a trait", getstr(classdef->name), entry, what);

            luaG_runerror(
                L, "class '%s' can only implement traits, but entry %d of its 'implements' list is %s", getstr(classdef->name), int(i + 1), what
            );
        }

        if (!ttisnil(luaH_get(seen, t)))
            luaG_runerror(L, "class '%s' lists trait '%s' more than once", getstr(classdef->name), getstr(classvalue(t)->name));

        setbvalue(luaH_set(L, seen, t), 1);
        setobj2s(L, L->top, t);
        L->top++;
    }

    // The queue is the stack itself: each trait's needed traits are appended after the traits already in it.
    for (int cur = first; cur < cast_int(L->top - L->base); cur++)
    {
        LuauClass* trait = classvalue(L->base + cur);
        const TValue* needs = luaR_findstaticmember(L, trait, "__needs");

        if (!needs)
            continue;

        luaD_checkstack(L, 1);
        int resultsbase = cast_int(L->top - L->base);
        setobj2s(L, L->top, needs);
        L->top++;
        luaD_callny(L, L->top - 1, LUA_MULTRET);

        int end = cast_int(L->top - L->base);
        int keep = resultsbase;

        // No allocation below runs a collection step, but the call above may have blackened both tables.
        LuaTable* neededlist = luaH_new(L, end - resultsbase, 0);
        edges = hvalue(L->base + first - 2);
        TValue* edge = luaH_set(L, edges, L->base + cur);
        sethvalue(L, edge, neededlist);
        luaC_barriert(L, edges, L->base + cur);
        luaC_barriert(L, edges, edge);

        for (int r = resultsbase; r < end; r++)
        {
            const TValue* needed = L->base + r;
            seen = hvalue(L->base + first - 1);

            if (ttisclass(needed) && !classvalue(needed)->istrait)
                luaG_runerror(
                    L,
                    "trait '%s' can't need class '%s': traits can only need other traits",
                    getstr(classvalue(L->base + cur)->name),
                    getstr(classvalue(needed)->name)
                );

            if (!ttisclass(needed))
            {
                char what[160];
                luaR_describenontrait(L, needed, what, sizeof(what));
                luaG_runerror(L, "trait '%s' needs %s, which is not a trait", getstr(classvalue(L->base + cur)->name), what);
            }

            setobj2t(L, luaH_setnum(L, neededlist, r - resultsbase + 1), needed);

            if (!ttisnil(luaH_get(seen, needed)))
                continue;

            // A needed trait with parameters has to be listed by the class, with its arguments. Only Analysis reports
            // one that isn't; at runtime the members it would have provided are simply missing.
            if (luaR_traithasparameters(L, classvalue(needed)))
                continue;

            setbvalue(luaH_set(L, seen, needed), 1);
            luaC_barriert(L, seen, needed);
            setobj2s(L, L->base + keep, needed);
            keep++;
        }

        L->top = L->base + keep;
    }

    int numtraits = cast_int(L->top - L->base) - first;
    luaD_checkstack(L, 1);
    LuaTable* state = luaH_new(L, 0, numtraits);
    sethvalue(L, L->top, state);
    L->top++;

    LuauClass* path[LUAR_MAX_NEEDS_DEPTH];
    for (int i = 0; i < numtraits; i++)
        luaR_checkneedscycles(L, hvalue(L->base + first - 2), state, classvalue(L->base + first + i), path, 0);

    L->top--;
    return numtraits;
}

// The offset of `trait`'s member `name`, which it has
static uint32_t luaR_traitmemberoffset(const LuauClass* trait, TString* name)
{
    const TValue* offset = luaH_getstr(trait->memberstooffset, name);
    LUAU_ASSERT(ttisnumber(offset));
    return uint32_t(nvalue(offset));
}

// Whether `from` needs `to`, directly or through the traits it needs. `edges` maps each trait to an array of the traits
// it needs (luaR_collecttraits, which refused cycles and chains deeper than LUAR_MAX_NEEDS_DEPTH, so this ends at that
// depth). `visited` holds the traits already searched, so a graph with many diamonds is walked once.
static bool luaR_traitneeds(lua_State* L, LuaTable* edges, LuaTable* visited, LuauClass* from, LuauClass* to)
{
    TValue key;
    setclassvalue(L, &key, from);
    setbvalue(luaH_set(L, visited, &key), 1);

    const TValue* list = luaH_get(edges, &key);
    if (!ttistable(list))
        return false;

    for (int i = 1; i <= luaH_getn(hvalue(list)); i++)
    {
        LuauClass* needed = classvalue(luaH_getnum(hvalue(list), i));
        if (needed == to)
            return true;

        TValue neededkey;
        setclassvalue(L, &neededkey, needed);
        if (ttisnil(luaH_get(visited, &neededkey)) && luaR_traitneeds(L, edges, visited, needed, to))
            return true;
    }

    return false;
}

// Of the traits in `providers` (an array, in the order the class attaches them) that all provide `name`, the one whose
// member the class gets: the one that needs every other. Raises when there is none, when a field would be overridden,
// or when an override breaks what the overridden trait promised (it is `final`, or public where the override isn't).
static LuauClass* luaR_overridingtrait(lua_State* L, LuaTable* edges, LuaTable* providers, const LuauClass* classdef, TString* name)
{
    int count = luaH_getn(providers);
    auto providerat = [&](int i)
    {
        return classvalue(luaH_getnum(providers, i));
    };

    LuauClass* winner = NULL;
    for (int i = 1; i <= count && !winner; i++)
    {
        bool needsall = true;
        for (int j = 1; j <= count && needsall; j++)
            needsall = i == j || luaR_traitneeds(L, edges, luaH_new(L, 0, 0), providerat(i), providerat(j));

        if (needsall)
            winner = providerat(i);
    }

    // the clash reported names the first two providers neither of which needs the other
    if (!winner)
    {
        for (int i = 1; i <= count; i++)
        {
            for (int j = i + 1; j <= count; j++)
            {
                bool related = luaR_traitneeds(L, edges, luaH_new(L, 0, 0), providerat(i), providerat(j)) ||
                               luaR_traitneeds(L, edges, luaH_new(L, 0, 0), providerat(j), providerat(i));
                if (related)
                    continue;

                const LuauClass* a = providerat(i);
                const LuauClass* b = providerat(j);
                if (luaR_traitmemberoffset(a, name) < a->numberofinstancemembers)
                    luaG_runerror(L, "traits '%s' and '%s' both provide '%s'", getstr(a->name), getstr(b->name), getstr(name));

                luaG_runerror(
                    L,
                    "traits '%s' and '%s' both provide '%s' and neither needs the other; define '%s' in class '%s' to choose",
                    getstr(a->name),
                    getstr(b->name),
                    getstr(name),
                    getstr(name),
                    getstr(classdef->name)
                );
            }
        }
    }

    LUAU_ASSERT(winner);
    uint32_t winneroff = luaR_traitmemberoffset(winner, name);
    uint8_t winnerflags = winner->memberflags[winneroff];

    for (int i = 1; i <= count; i++)
    {
        const LuauClass* overridden = providerat(i);
        if (overridden == winner)
            continue;

        uint32_t off = luaR_traitmemberoffset(overridden, name);
        uint8_t flags = overridden->memberflags[off];

        if (off < overridden->numberofinstancemembers || winneroff < winner->numberofinstancemembers)
            luaG_runerror(L, "trait '%s' can't redefine '%s': fields of trait '%s' can't be overridden", getstr(winner->name), getstr(name), getstr(overridden->name));

        if (flags & LBC_CLASSMEMBER_FINAL)
            luaG_runerror(L, "'%s' is final in trait '%s' and can't be overridden", getstr(name), getstr(overridden->name));

        if ((flags ^ winnerflags) & LBC_CLASSMEMBER_PRIVATE)
            luaG_runerror(
                L,
                "'%s' must be %s in trait '%s' to override it from trait '%s'",
                getstr(name),
                luaR_visibilityname(flags),
                getstr(winner->name),
                getstr(overridden->name)
            );
    }

    return winner;
}

// "missing field 'x' required for 'C' to implement 'T'"; `name` is NULL for a constructor
static l_noret luaR_traitmissingerror(
    lua_State* L,
    const char* kind,
    TString* name,
    const LuauClass* classdef,
    const LuauClass* trait
)
{
    if (!name)
        luaG_runerror(L, "missing %s required for '%s' to implement '%s'", kind, getstr(classdef->name), getstr(trait->name));

    luaG_runerror(L, "missing %s '%s' required for '%s' to implement '%s'", kind, getstr(name), getstr(classdef->name), getstr(trait->name));
}

// "'x' must be public for 'C' to implement 'T'"
static l_noret luaR_traitrequirementerror(
    lua_State* L,
    TString* name,
    const char* requirement,
    const LuauClass* classdef,
    const LuauClass* trait
)
{
    luaG_runerror(L, "'%s' must be %s for '%s' to implement '%s'", getstr(name), requirement, getstr(classdef->name), getstr(trait->name));
}

// Raises unless every expectation of the traits is met by the class being implemented, whoever provides the member: the
// class itself, or another trait. `added` is luaR_implementtraits' map from each name a trait provides to its provider,
// or `false` for an optional function nobody defines. Runs before the class is changed at all, so a class that fails
// here is left as it was, still unconstructible (traitspending), and a later run of its statement starts over cleanly.
static void luaR_checktraitexpectations(lua_State* L, LuauClass* classdef, StkId traits, int numtraits, LuaTable* added)
{
    uint32_t ownninst = classdef->numberofinstancemembers;

    for (int i = 0; i < numtraits; i++)
    {
        LuauClass* trait = classvalue(traits + i);

        // An expected `__init` asks for a constructor, explicit or a primary constructor's, so that the trait's code can
        // construct the class with the arguments it expects (Analysis checks those). It may be private: the trait's own
        // code may call a private constructor of a class implementing it. `__init` has no name in `offsettomember`
        // (see luaR_sealclassshape), so it isn't met in the loop below.
        bool expectsinit = (trait->memberflags[trait->initoffset] & LBC_CLASSMEMBER_EXPECTED) != 0;
        if (expectsinit && !classdef->hascustominit)
            luaR_traitmissingerror(L, "constructor", NULL, classdef, trait);

        for (uint32_t off = 0; off < trait->numberofallmembers; off++)
        {
            TString* name = trait->offsettomember[off];
            uint8_t expected = trait->memberflags[off];

            if (!name || !(expected & LBC_CLASSMEMBER_EXPECTED))
                continue;

            bool isfield = off < trait->numberofinstancemembers;
            bool foundisfield = false;
            bool isplaceholder = false;
            uint8_t flags = 0;

            const TValue* own = luaH_getstr(classdef->memberstooffset, name);
            const TValue* provider = luaH_getstr(added, name);

            if (!ttisnil(own))
            {
                uint32_t foundoff = uint32_t(nvalue(own));
                foundisfield = foundoff < ownninst;
                flags = classdef->memberflags[foundoff];
            }
            else if (ttisclass(provider))
            {
                // the member as it will be placed: access and constness carry over from the providing trait
                LuauClass* from = classvalue(provider);
                uint32_t foundoff = uint32_t(nvalue(luaH_getstr(from->memberstooffset, name)));
                foundisfield = foundoff < from->numberofinstancemembers;
                flags = from->memberflags[foundoff] & (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST);
            }
            else if (ttisboolean(provider))
            {
                isplaceholder = true;
            }
            else
            {
                luaR_traitmissingerror(L, luaR_memberkind(isfield), name, classdef, trait);
            }

            if (foundisfield != isfield)
                luaR_traitrequirementerror(L, name, isfield ? "a field" : "a function", classdef, trait);

            if (isplaceholder && !(expected & LBC_CLASSMEMBER_OPTIONAL))
                luaR_traitmissingerror(L, "function", name, classdef, trait);

            if (!isplaceholder && ((flags ^ expected) & LBC_CLASSMEMBER_PRIVATE))
                luaR_traitrequirementerror(L, name, luaR_visibilityname(expected), classdef, trait);

            if (isfield && ((flags ^ expected) & LBC_CLASSMEMBER_CONST))
                luaR_traitrequirementerror(L, name, (expected & LBC_CLASSMEMBER_CONST) ? "const" : "non-const", classdef, trait);
        }
    }
}

void luaR_implementtraits(lua_State* L, LuauClass* classdef, StkId listed, uint32_t n)
{
    LUAU_ASSERT(!classdef->istrait);

    // The chunk that declares the class ran again. The class constant is shared, and already implements its traits.
    if (!classdef->traitspending)
        return;

    ptrdiff_t listedslot = savestack(L, listed);
    ptrdiff_t oldtop = savestack(L, L->top);

    int numtraits = luaR_collecttraits(L, classdef, listedslot, n);
    int first = cast_int(L->top - L->base) - numtraits;

    // Collecting called each trait's `__needs`, which can run anything, including this class's statement again: that
    // run implemented the traits already, and implementing twice would grow the layout under its objects.
    if (!classdef->traitspending)
    {
        L->top = restorestack(L, oldtop);
        return;
    }

    auto traitat = [&](int i)
    {
        return classvalue(L->base + first + i);
    };

    // Which trait provided each member name the class gets from a trait; `false` for an optional expected function that
    // nothing defines, which still gets a (nil) slot so reading it gives nil.
    luaD_checkstack(L, 2);
    LuaTable* added = luaH_new(L, 0, 8);
    sethvalue(L, L->top, added);
    L->top++;

    uint32_t ownninst = classdef->numberofinstancemembers;
    uint32_t ownnstatic = classdef->numberofallmembers - ownninst;
    uint32_t newfields = 0;
    uint32_t newstatics = 0;

    // Every trait that provides each name, for choosing the one whose member the class gets
    LuaTable* providers = luaH_new(L, 0, 8);
    sethvalue(L, L->top, providers);
    L->top++;

    // Pass 1: the provided fields and the defined functions of every trait, by name
    for (int i = 0; i < numtraits; i++)
    {
        LuauClass* trait = traitat(i);

        for (uint32_t off = 0; off < trait->numberofallmembers; off++)
        {
            TString* name = trait->offsettomember[off];
            uint8_t flags = trait->memberflags[off];
            bool isfield = off < trait->numberofinstancemembers;

            // a factory's `__create` belongs to the trait alone
            bool provided = name && !(flags & LBC_CLASSMEMBER_EXPECTED) && !luaR_istraitinternal(L, name) && name != luaS_newliteral(L, "__create");
            if (!provided)
                continue;

            const TValue* own = luaH_getstr(classdef->memberstooffset, name);
            if (!ttisnil(own))
            {
                bool ownisfield = uint32_t(nvalue(own)) < ownninst;

                if (isfield || ownisfield)
                    luaG_runerror(
                        L, "class '%s' can't declare '%s': trait '%s' already provides it", getstr(classdef->name), getstr(name), getstr(trait->name)
                    );

                if (flags & LBC_CLASSMEMBER_FINAL)
                    luaG_runerror(L, "'%s' is final in trait '%s' and can't be overridden", getstr(name), getstr(trait->name));

                // whether the function is public is part of what the trait promises about every class implementing it
                uint8_t ownflags = classdef->memberflags[uint32_t(nvalue(own))];
                if ((ownflags ^ flags) & LBC_CLASSMEMBER_PRIVATE)
                    luaR_traitrequirementerror(L, name, luaR_visibilityname(flags), classdef, trait);

                // the class's own function overrides the trait's default
                continue;
            }

            TValue* list = luaH_setstr(L, providers, name);
            if (!ttistable(list))
                sethvalue(L, list, luaH_new(L, 2, 0));

            LuaTable* names = hvalue(list);
            setclassvalue(L, luaH_setnum(L, names, luaH_getn(names) + 1), trait);
        }
    }

    // Pass 1b: which trait's member the class gets for each name. A trait's function overrides the function of a trait
    // it needs, so the provider that needs every other provider wins; with none, the class has to choose by defining
    // the function itself. Fields are never overridden.
    LuaTable* edges = hvalue(L->base + first - 2);

    for (int i = 0; i < numtraits; i++)
    {
        LuauClass* trait = traitat(i);

        for (uint32_t off = 0; off < trait->numberofallmembers; off++)
        {
            TString* name = trait->offsettomember[off];
            const TValue* list = name ? luaH_getstr(providers, name) : luaO_nilobject;
            bool undecided = ttistable(list) && ttisnil(luaH_getstr(added, name));
            if (!undecided)
                continue;

            LuauClass* winner = luaR_overridingtrait(L, edges, hvalue(list), classdef, name);
            bool isfield = luaR_traitmemberoffset(winner, name) < winner->numberofinstancemembers;

            setclassvalue(L, luaH_setstr(L, added, name), winner);

            if (isfield)
                newfields++;
            else
                newstatics++;
        }
    }

    // Pass 2: an optional expected function that neither the class nor any trait defines still gets a slot
    for (int i = 0; i < numtraits; i++)
    {
        LuauClass* trait = traitat(i);

        for (uint32_t off = trait->numberofinstancemembers; off < trait->numberofallmembers; off++)
        {
            TString* name = trait->offsettomember[off];
            uint8_t flags = trait->memberflags[off];
            bool optional = name && (flags & LBC_CLASSMEMBER_EXPECTED) && (flags & LBC_CLASSMEMBER_OPTIONAL);

            bool definedElsewhere = optional && (!ttisnil(luaH_getstr(classdef->memberstooffset, name)) || !ttisnil(luaH_getstr(added, name)));
            if (!optional || definedElsewhere)
                continue;

            setbvalue(luaH_setstr(L, added, name), 0);
            newstatics++;
        }
    }

    luaR_checktraitexpectations(L, classdef, L->base + first, numtraits, added);

    uint32_t newninst = ownninst + newfields;
    uint32_t newnstatic = ownnstatic + newstatics;
    uint32_t newall = newninst + newnstatic;

    TString** newnames = luaM_newarray(L, newall, TString*, classdef->memcat);
    uint8_t* newflags = luaM_newarray(L, newall, uint8_t, classdef->memcat);
    TValue* newstaticmembers = luaM_newarray(L, newnstatic, TValue, classdef->memcat);
    // A trait's constant field defaults are in its shape, like a class's; they join the class's so that luaR_newobject
    // writes them with the rest and construction doesn't compute them
    bool anytraitconstdefault = false;
    for (int i = 0; i < numtraits && !anytraitconstdefault; i++)
    {
        LuauClass* trait = traitat(i);

        for (uint32_t off = 0; off < trait->numberofinstancemembers && !anytraitconstdefault; off++)
        {
            const TValue* provider = trait->offsettomember[off] ? luaH_getstr(added, trait->offsettomember[off]) : NULL;
            bool providedhere = provider && ttisclass(provider) && classvalue(provider) == trait;
            anytraitconstdefault = providedhere && (trait->memberflags[off] & LBC_CLASSMEMBER_CONSTDEFAULT) != 0;
        }
    }

    bool needsdefaults = classdef->memberdefaults || anytraitconstdefault;
    TValue* newdefaults = needsdefaults ? luaM_newarray(L, newninst, TValue, classdef->memcat) : NULL;

    memset(newflags, 0, newall);

    for (uint32_t i = 0; i < newall; i++)
        newnames[i] = NULL;

    for (uint32_t i = 0; i < newnstatic; i++)
        setnilvalue(&newstaticmembers[i]);

    // The class's own members keep their order: its fields first, where the compiler put them, then its functions,
    // shifted past the trait fields.
    for (uint32_t off = 0; off < ownninst; off++)
    {
        newnames[off] = classdef->offsettomember[off];
        newflags[off] = classdef->memberflags[off];
    }

    for (uint32_t s = 0; s < ownnstatic; s++)
    {
        newnames[newninst + s] = classdef->offsettomember[ownninst + s];
        newflags[newninst + s] = classdef->memberflags[ownninst + s];
        setobj(L, &newstaticmembers[s], &classdef->staticmembers[s]);
    }

    if (newdefaults)
    {
        for (uint32_t off = 0; off < newninst; off++)
        {
            if (off < ownninst && classdef->memberdefaults)
            {
                setobj(L, &newdefaults[off], &classdef->memberdefaults[off]);
            }
            else
            {
                setnilvalue(&newdefaults[off]);
            }
        }
    }

    // The trait members, in the order pass 1 and 2 counted them
    uint32_t nextfield = ownninst;
    uint32_t nextstatic = newninst + ownnstatic;

    for (int i = 0; i < numtraits; i++)
    {
        LuauClass* trait = traitat(i);

        for (uint32_t off = 0; off < trait->numberofallmembers; off++)
        {
            TString* name = trait->offsettomember[off];
            if (!name)
                continue;

            // only the trait that `added` credits with the name places it
            const TValue* provider = luaH_getstr(added, name);
            uint8_t flags = trait->memberflags[off];
            bool isexpected = (flags & LBC_CLASSMEMBER_EXPECTED) != 0;
            bool providedhere = ttisclass(provider) && classvalue(provider) == trait && !isexpected;
            // pass 2 marks an optional function nobody defines with `false`
            bool placeholderhere = ttisboolean(provider) && isexpected && (flags & LBC_CLASSMEMBER_OPTIONAL);
            if (!providedhere && !placeholderhere)
                continue;

            // an optional function's placeholder is placed once, by the first trait expecting it
            if (ttisboolean(provider))
                setclassvalue(L, luaH_setstr(L, added, name), trait);

            bool isfield = off < trait->numberofinstancemembers;
            uint32_t target = isfield ? nextfield++ : nextstatic++;

            newnames[target] = name;
            // access and constness carry over; the other bits describe the member's role in the trait
            // (and a field's finality)
            uint8_t carried = LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST | (isfield ? LBC_CLASSMEMBER_FINAL : 0);
            newflags[target] = trait->memberflags[off] & carried;
            luaC_objbarrier(L, classdef, name);

            if (isfield && (flags & LBC_CLASSMEMBER_CONSTDEFAULT))
            {
                setobj(L, &newdefaults[target], &trait->memberdefaults[off]);
                luaC_barrier(L, classdef, &newdefaults[target]);
            }
        }
    }

    LUAU_ASSERT(nextfield == newninst && nextstatic == newall);

    LuaTable* newmap = luaH_new(L, 0, int(newall));
    TString* initname = luaS_newliteral(L, "__init");
    uint32_t newinitoffset = classdef->initoffset + newfields;

    for (uint32_t off = 0; off < newall; off++)
    {
        TString* name = off == newinitoffset ? initname : newnames[off];
        LUAU_ASSERT(name);
        setnvalue(luaH_setstr(L, newmap, name), double(off));
    }

    // Install the new layout. No object of this class exists yet: the class statement is still running.
    luaM_freearray(L, classdef->offsettomember, classdef->numberofallmembers, TString*, classdef->memcat);
    luaM_freearray(L, classdef->memberflags, classdef->numberofallmembers, uint8_t, classdef->memcat);
    luaM_freearray(L, classdef->staticmembers, ownnstatic, TValue, classdef->memcat);
    if (classdef->memberdefaults)
        luaM_freearray(L, classdef->memberdefaults, ownninst, TValue, classdef->memcat);

    classdef->offsettomember = newnames;
    classdef->memberflags = newflags;
    classdef->staticmembers = newstaticmembers;
    classdef->memberdefaults = newdefaults;
    classdef->numberofinstancemembers = newninst;
    classdef->numberofallmembers = newall;
    classdef->memberstooffset = newmap;
    luaC_objbarrier(L, classdef, newmap);
    classdef->initoffset = newinitoffset;

    if (classdef->haspoddefaultsfn)
        classdef->poddefaultsoffset += newfields;

    // A default becomes the class's own method: a copy stamped with the class, so its `self` check, private access and
    // slot caches all belong to this class (luaR_addclassmember).
    for (int i = 0; i < numtraits; i++)
    {
        LuauClass* trait = traitat(i);

        for (uint32_t off = trait->numberofinstancemembers; off < trait->numberofallmembers; off++)
        {
            TString* name = trait->offsettomember[off];
            if (!name || (trait->memberflags[off] & LBC_CLASSMEMBER_EXPECTED))
                continue;

            const TValue* provider = luaH_getstr(added, name);
            if (!ttisclass(provider) || classvalue(provider) != trait)
                continue;

            // a method's slot holds its dispatcher (see luaR_addclassmember), which holds the trait's closure
            const TValue* fn = &trait->staticmembers[off - trait->numberofinstancemembers];
            LUAU_ASSERT(ttisfunction(fn));
            if (clvalue(fn)->isC)
                fn = &clvalue(fn)->c.upvals[2];
            LUAU_ASSERT(ttisfunction(fn) && !clvalue(fn)->isC);

            luaD_checkstack(L, 1);
            setclvalue(L, L->top, luaR_copytraitfunction(L, clvalue(fn)));
            L->top++;
            luaR_addclassmember(L, classdef, name, L->top - 1);
            L->top--;
        }
    }

    // A default the class overrides still gets a copy stamped with the class, for `Trait.method(obj)` to run
    for (int i = 0; i < numtraits; i++)
    {
        LuauClass* trait = traitat(i);

        for (uint32_t off = trait->numberofinstancemembers; off < trait->numberofallmembers; off++)
        {
            TString* name = trait->offsettomember[off];
            if (!name || (trait->memberflags[off] & LBC_CLASSMEMBER_EXPECTED))
                continue;

            // only a method reads as a dispatcher; a static is called as itself
            const TValue* fn = &trait->staticmembers[off - trait->numberofinstancemembers];
            if (!ttisfunction(fn) || !clvalue(fn)->isC)
                continue;

            // pass 1 credits the trait with every default the class doesn't define itself
            const TValue* provider = luaH_getstr(added, name);
            if (ttisclass(provider) && classvalue(provider) == trait)
                continue;

            const TValue* traitfn = &clvalue(fn)->c.upvals[2];
            LUAU_ASSERT(ttisfunction(traitfn) && !clvalue(traitfn)->isC);

            if (!classdef->traitdefaults)
            {
                classdef->traitdefaults = luaH_new(L, 0, 1);
                luaC_objbarrier(L, classdef, classdef->traitdefaults);
            }

            Closure* copy = luaR_copytraitfunction(L, clvalue(traitfn));
            luaD_checkstack(L, 1);
            setclvalue(L, L->top, copy);
            L->top++;
            luaR_stampownerclass(L, copy->l.p, classdef);

            TValue* slot = luaH_set(L, classdef->traitdefaults, traitfn);
            setclvalue(L, slot, copy);
            luaC_barriert(L, classdef->traitdefaults, L->top - 1);
            L->top--;
        }
    }

    classdef->traits = luaM_newarray(L, numtraits, LuauClass*, classdef->memcat);
    classdef->numtraits = uint32_t(numtraits);

    for (int i = 0; i < numtraits; i++)
    {
        classdef->traits[i] = traitat(i);
        luaC_objbarrier(L, classdef, traitat(i));
    }

    // What construction calls for the trait fields that aren't constants (luaR_inittraitfields): the class's copy of each
    // trait's `__traitinit`. A copy writes the fields by name, so its slot caches learn this class's offsets. The
    // copies of traits whose `implements` entry passes arguments are called by the class's `__inittraits`, which
    // evaluates the arguments; the others are called directly. Each copy is anchored on the stack until it is stored.
    int directbase = cast_int(L->top - L->base);
    int numdirect = 0;
    int numcalled = 0;

    for (int pass = 0; pass < 2; pass++)
    {
        for (int i = 0; i < numtraits; i++)
        {
            LuauClass* trait = traitat(i);

            // only the listed traits, which come first, have arguments
            bool hasargs = uint32_t(i) < n && nvalue(restorestack(L, listedslot) + n + i) > 0;
            if (hasargs != (pass == 1))
                continue;

            const TValue* init = luaR_findstaticmember(L, trait, "__traitinit");
            if (!init)
                continue;

            LUAU_ASSERT(ttisfunction(init) && !clvalue(init)->isC);
            Closure* copy = luaR_copytraitfunction(L, clvalue(init));

            luaD_checkstack(L, 1);
            setclvalue(L, L->top, copy);
            L->top++;
            luaR_stampownerclass(L, copy->l.p, classdef);

            if (pass == 0)
                numdirect++;
            else
                numcalled++;
        }
    }

    if (numdirect + numcalled > 0)
    {
        // `__inittraits` goes between the two groups
        uint32_t total = uint32_t(numdirect + (numcalled > 0 ? 1 + numcalled : 0));
        TValue* inits = luaM_newarray(L, total, TValue, classdef->memcat);

        StkId copies = L->base + directbase;
        for (int i = 0; i < numdirect; i++)
            setobj(L, &inits[i], copies + i);

        if (numcalled > 0)
        {
            const TValue* inittraits = luaR_findstaticmember(L, classdef, "__inittraits");
            LUAU_ASSERT(inittraits && ttisfunction(inittraits));
            setobj(L, &inits[numdirect], inittraits);

            for (int i = 0; i < numcalled; i++)
                setobj(L, &inits[numdirect + 1 + i], copies + numdirect + i);
        }

        classdef->traitinits = inits;
        classdef->numtraitinits = total;
        classdef->numdirecttraitinits = uint32_t(numdirect);

        for (uint32_t i = 0; i < total; i++)
            luaC_barrier(L, classdef, &inits[i]);
    }

    for (uint32_t off = 0; off < classdef->numberofallmembers; off++)
    {
        classdef->hasprivatemembers |= (classdef->memberflags[off] & LBC_CLASSMEMBER_PRIVATE) != 0;
        classdef->hasconstmembers |= (classdef->memberflags[off] & LBC_CLASSMEMBER_CONST) != 0;
    }

    classdef->memberstooffset->readonly = true;
    classdef->traitspending = false;

    L->top = restorestack(L, oldtop);
}

void luaR_inittraitfields(lua_State* L, LuauClass* classdef, LuauObject* object, StkId args, int nargs)
{
    LUAU_ASSERT(classdef->traitinits);

    ptrdiff_t argsslot = savestack(L, args);
    ptrdiff_t oldtop = savestack(L, L->top);
    uint32_t numdirect = classdef->numdirecttraitinits;

    luaC_threadbarrier(L);

    for (uint32_t i = 0; i < numdirect; i++)
    {
        luaD_checkstack(L, 2);
        StkId fn = L->top;
        setobj2s(L, fn, &classdef->traitinits[i]);
        setobjectvalue(L, fn + 1, object);
        L->top = fn + 2;
        luaD_callny(L, fn, 0);
    }

    // `__inittraits(self, copies..., constructor arguments...)`
    uint32_t numcalled = classdef->numtraitinits - numdirect;
    if (numcalled > 0)
    {
        luaD_checkstack(L, int(numcalled) + 1 + nargs);
        StkId fn = L->top;
        setobj2s(L, fn, &classdef->traitinits[numdirect]);
        setobjectvalue(L, fn + 1, object);

        for (uint32_t i = 1; i < numcalled; i++)
            setobj2s(L, fn + 1 + i, &classdef->traitinits[numdirect + i]);

        StkId ctorargs = restorestack(L, argsslot);
        for (int i = 0; i < nargs; i++)
            setobj2s(L, fn + 1 + numcalled + i, ctorargs + i);

        L->top = fn + 1 + numcalled + nargs;
        luaD_callny(L, fn, 0);
    }

    L->top = restorestack(L, oldtop);
}

bool luaR_implements(const LuauClass* classdef, const LuauClass* trait)
{
    for (uint32_t i = 0; i < classdef->numtraits; i++)
        if (classdef->traits[i] == trait)
            return true;

    return false;
}
