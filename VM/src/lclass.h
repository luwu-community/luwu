// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "lmem.h"
#include "lobject.h"
#include "lbytecode.h"

// An instance's members are allocated inline, immediately after the LuauObject header, so a
// construction costs one GC allocation rather than two. Everything that allocates, frees or
// GC-accounts for an object must agree on this size.
inline size_t luaR_objectsize(uint32_t nummembers)
{
    return sizeof(LuauObject) + size_t(nummembers) * sizeof(TValue);
}

// LuauClass::memberflags entries use LBC_CLASSMEMBER_PRIVATE / LBC_CLASSMEMBER_CONST (see
// Luau/Bytecode.h), the same bits the compiler serializes into LBC_CONSTANT_CLASS_SHAPE.

/**
 * Allocate and return a new class value with room for its members, for the loader to fill in. The
 * buffers start out as:
 *  - `offsettomember`: every entry NULL. `__init`'s entry stays NULL after luaR_sealclassshape too.
 *  - `memberflags`: every entry 0.
 *  - `memberstooffset`: empty.
 *  - `memberdefaults`: every entry nil when `hasmemberdefaults`, otherwise NULL.
 *  - `staticmembers`: every entry nil.
 * Call luaR_sealclassshape once they are filled.
 *
 * The class owns every buffer from the moment it is registered with the GC, so an allocation failure
 * anywhere in the loader leaves nothing to leak.
 * @param name The name of this class. This does not have to be unique within a program.
 * @param numberofinstancemembers The number of instance members (fields) this class has.
 * @param numberofstaticmembers The number of static members (methods, including `__init`) this class has.
 */
LUAI_FUNC LuauClass* luaR_newclass(
    lua_State* L,
    TString* name,
    uint32_t numberofinstancemembers,
    uint32_t numberofstaticmembers,
    bool hasmemberdefaults
);

/**
 * Finishes a class the loader has filled in (see luaR_newclass): derives the summary bits from
 * `memberflags`, blocks reading `__init` by name, and makes `memberstooffset` read-only.
 */
LUAI_FUNC void luaR_sealclassshape(lua_State* L, LuauClass* classdef);

// The number of bytes `classdef` owns outside its members-to-offset table, for GC accounting.
LUAI_FUNC size_t luaR_classsize(const LuauClass* classdef);

/**
 * Allocates an instance of `classdef` with every member set to its constant default (or nil), ready
 * for a constructor to apply arguments to. Does not run `__init` and does not check the GC threshold.
 */
LUAI_FUNC LuauObject* luaR_newobject(lua_State* L, LuauClass* classdef);

// Allocates an instance without initializing its members, for a caller that is about to write every
// one of them with nothing in between that could collect. See luaR_newobjectuninit's definition.
LUAI_FUNC LuauObject* luaR_newobjectuninit(lua_State* L, LuauClass* classdef);

/**
 * Copies fields named by `classdef`'s instance members out of the table `arg` into `object`, leaving
 * a member absent from the table (or explicitly nil) at whatever it already holds. This is the POD
 * constructor's table-of-fields form, `ClassName { field = value }`.
 */
LUAI_FUNC void luaR_applyobjectfields(lua_State* L, LuauClass* classdef, LuauObject* object, LuaTable* arg);

/**
 * As luaR_applyobjectfields, for an argument that is not a plain table: each field is read with the
 * generic indexing path, so an __index metamethod is honored, and a private field of an object is
 * read with the access rights of the running Lua frame. A nil argument applies nothing. May call back
 * into Lua. `object` must be anchored where the collector can see it.
 */
LUAI_FUNC void luaR_applyobjectfieldsslow(lua_State* L, LuauClass* classdef, LuauObject* object, const TValue* arg);

/**
 * The POD constructor: runs `classdef`'s `__defaults` if it has one, then applies the fields of
 * `args[0]` (see luaR_applyobjectfields). `nargs` is the number of constructor arguments at `args`; 0,
 * or 1 that is nil, applies nothing, and more than 1 raises. Private fields of an object argument are
 * read with the access rights of `accessor`, the Lua closure constructing (NULL for native code).
 * `object` must be anchored where the collector can see it; it gets every write barrier it needs.
 */
LUAI_FUNC void luaR_initpodobject(lua_State* L, LuauClass* classdef, LuauObject* object, StkId args, int nargs, const Closure* accessor);

/**
 * Returns true if `cl` is `classdef`'s own `__init` closure specifically (stricter than
 * luaR_closureownsprivateaccess, which accepts any method of the class).
 */
LUAI_FUNC bool luaR_closureisinit(const LuauClass* classdef, const Closure* cl);

/**
 * Returns true if `cl` is one of `classdef`'s own method closures (including `__init`), or a
 * closure lexically nested anywhere inside one -- ie code that is lexically part of the class's
 * own definition block. Used to authorize private-member access and private constructors.
 *
 * The check reads `cl`'s Proto::ownerclass rather than anything about the call. A class's method
 * protos, and every proto nested inside them, are stamped with the owning class exactly once, when
 * the class statement runs (see luaR_addclassmember and luaR_stampownerclass). Calling a method with
 * `.` syntax and a mismatched `self` (`SomeClass.method(notAnInstance)`) can't spoof it: the field
 * access still runs inside the same proto, whatever `self` is.
 */
LUAI_FUNC bool luaR_closureownsprivateaccess(const LuauClass* classdef, const Closure* cl);

/**
 * Errors (via luaG_privateaccesserror) if the member at `offset` is private and `cl` is not one
 * of `classdef`'s own methods, or (via luaG_blockedinitaccesserror) if it is `__init`
 * (LBC_CLASSMEMBER_INITBLOCKED), whoever `cl` is. `key` is only used for the error message.
 * Readers call it through luaR_checkprivateaccessfast.
 */
LUAI_FUNC void luaR_checkprivateaccess(lua_State* L, const TValue* key, const LuauClass* classdef, const Closure* cl, uint32_t offset);

// True when constructing `classdef` has to pass luaR_checkprivateconstructor.
LUAU_FORCEINLINE bool luaR_hasprivateconstructor(const LuauClass* classdef)
{
    return classdef->hascustominit && (classdef->memberflags[classdef->initoffset] & LBC_CLASSMEMBER_PRIVATE) != 0;
}

/**
 * The private-constructor check construction performs: errors if `classdef`'s custom `__init` is
 * private and `cl` is not one of `classdef`'s own methods. Unlike luaR_checkprivateaccess this ignores
 * LBC_CLASSMEMBER_INITBLOCKED, which restricts reading `__init`, not constructing with it. Callers
 * test luaR_hasprivateconstructor first.
 */
LUAI_FUNC void luaR_checkprivateconstructor(lua_State* L, const LuauClass* classdef, const Closure* cl);

/**
 * Errors if the member at `offset` of `object` is const, unless the writer is the object's class's
 * own `__init` (`cl`, the closure of the running frame L->ci) writing the object it is constructing,
 * its `self` parameter. `key` is only used for the error message.
 *
 * Callers should only call this when the class's `hasconstmembers` is set.
 */
LUAI_FUNC void luaR_checkconstassign(lua_State* L, const TValue* key, const LuauObject* object, const Closure* cl, uint32_t offset);

// Every member read that resolved its offset by name runs this. So does a read from a cached slot on a
// class with `hasprivatemembers`. It tests the member's own flag bits first, so the out-of-line
// authorization call only happens for members that are actually private, or for `__init`. `__init` is
// only ever resolved by name; see luaR_sealclassshape. Worth ~1.3ns per access, interpreted.
LUAU_FORCEINLINE void luaR_checkprivateaccessfast(
    lua_State* L,
    const TValue* key,
    const LuauClass* classdef,
    const Closure* cl,
    uint32_t offset
)
{
    if (LUAU_UNLIKELY((classdef->memberflags[offset] & (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_INITBLOCKED)) != 0))
        luaR_checkprivateaccess(L, key, classdef, cl, offset);
}

LUAU_FORCEINLINE void luaR_checkconstassignfast(
    lua_State* L,
    const TValue* key,
    const LuauObject* object,
    const Closure* cl,
    uint32_t offset
)
{
    if (LUAU_UNLIKELY((object->lclass->memberflags[offset] & LBC_CLASSMEMBER_CONST) != 0))
        luaR_checkconstassign(L, key, object, cl, offset);
}

/**
 * Add a new class member to `classdef` named `name` and with value `method`. As the naming implies
 * we only support methods today.
 */
LUAI_FUNC void luaR_addclassmember(lua_State* L, LuauClass* classdef, TString* name, TValue* method);

LUAI_FUNC void luaR_freeclass(lua_State* L, LuauClass* classdef, lua_Page* page);

/**
 * Callback for creating objects. This is written as a Lua API function and expects the stack to be:
 *
 *  [ BASE ]
 *  - A class value
 *  - The constructor arguments (for the POD constructor, an optional indexable value)
 *
 * This function checks a private constructor, allocates a new object and then either calls a custom
 * `__init` with the arguments (yieldably) or runs the POD constructor (luaR_initpodobject). The
 * object is the single result.
 */
LUAI_FUNC int luaR_createobject(lua_State* L);

LUAI_FUNC void luaR_freeobject(lua_State* L, LuauObject* object, lua_Page* page);

// A member's offset is cached in its instruction's 8-bit C operand (the slot the fast paths check). A
// larger offset doesn't fit: it is never cached, and that member is looked up by name every time.
#define LUAR_MAX_CACHED_MEMBER_SLOT 0xff

#define luaR_checkoffsetinbounds(object, offset) (offset < (object)->lclass->numberofallmembers)

#define luaR_lookupmemberatoffset(object, offset) \
    (LUAU_ASSERT(luaR_checkoffsetinbounds(object, offset)), \
     offset < (object)->lclass->numberofinstancemembers ? &(object)->members[offset] \
                                                      : &(object)->lclass->staticmembers[offset - object->lclass->numberofinstancemembers])
