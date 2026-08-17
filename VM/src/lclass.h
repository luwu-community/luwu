// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "lmem.h"
#include "lobject.h"
#include "lbytecode.h"

// LuauClass::memberflags entries use LBC_CLASSMEMBER_PRIVATE / LBC_CLASSMEMBER_CONST (see
// Luau/Bytecode.h), the same bits the compiler serializes into LBC_CONSTANT_CLASS_SHAPE.

/**
 * Allocate and return a new class object.
 * @param name The name of this class. This does not have to be unique within a program.
 * @param memberstooffset A table mapping member names to their offset within the class
 * @param offsettomember An array of length `numberofinstancemembers + numberofstaticmembers` where
 * each entry is the name of the member at the specified offset.
 * @param memberflags An array of length `numberofinstancemembers + numberofstaticmembers`, parallel
 * to `offsettomember`, of LBC_CLASSMEMBER_* bits for each member. Ownership is transferred to the
 * new class object.
 * @param numberofinstancemembers The number of instance members (fields) this class has.
 * @param numberofstaticmembers The number of static members (only methods today) this class has.
 */
LUAI_FUNC LuauClass* luaR_newclass(
    lua_State* L,
    TString* name,
    LuaTable* memberstooffset,
    TString** offsettomember,
    uint8_t* memberflags,
    uint32_t numberofinstancemembers,
    uint32_t numberofstaticmembers
);

/**
 * Returns true if `cl` is one of `classobject`'s own method closures (including `__init`), ie
 * code that is lexically part of the class's own definition block. Used to allow private-member
 * access, and (via luaR_closureisinit) const-member writes.
 *
 * We check closure identity (not source location or Proto) because a class's method closures are
 * created exactly once, when the class statement itself runs, and closure identity can't be
 * spoofed by calling a method via `.`-syntax with a mismatched `self` (e.g.
 * `SomeClass.method(notAnInstance)`) -- the field access still happens from within that same
 * closure regardless of what `self` was passed in.
 */
LUAI_FUNC bool luaR_closureownsprivateaccess(const LuauClass* classobject, const Closure* cl);

/**
 * Returns true if `cl` is `classobject`'s own `__init` closure specifically (stricter than
 * luaR_closureownsprivateaccess, which accepts any method of the class).
 */
LUAI_FUNC bool luaR_closureisinit(const LuauClass* classobject, const Closure* cl);

/**
 * Errors (via luaG_privateaccesserror) if the member at `offset` is private and `cl` is not one
 * of `classobject`'s own methods. `key` is only used for the error message.
 *
 * Callers should only call this when `classobject->hasprivatemembers` is set, so that public
 * access from outside the class (the common case) costs nothing beyond that one flag check.
 */
LUAI_FUNC void luaR_checkprivateaccess(lua_State* L, const TValue* key, const LuauClass* classobject, const Closure* cl, uint32_t offset);

/**
 * Errors (via luaG_constassignerror) if the member at `offset` is const and `cl` is not
 * `classobject`'s own `__init` closure. `key` is only used for the error message.
 *
 * Callers should only call this when `classobject->hasconstmembers` is set.
 */
LUAI_FUNC void luaR_checkconstassign(lua_State* L, const TValue* key, const LuauClass* classobject, const Closure* cl, uint32_t offset);

/**
 * Add a new class member to `classobject` named `name` and with value `method`. As the naming implies
 * we only support methods today.
 */
LUAI_FUNC void luaR_addclassmember(lua_State* L, LuauClass* classobject, TString* name, TValue* method);

LUAI_FUNC void luaR_freeclass(lua_State* L, LuauClass* classobject, lua_Page* page);

/**
 * Callback for creating class instances. This is written as a Lua API function and expects the stack to be:
 *
 *  [ BASE ]
 *  - A class object
 *  - An optional indexable value
 *
 * This function will allocate a new class instance, iterate over the instance members of the class object,
 * initialize each class instance member with the result of indexing into the value, and then assign the
 * value to the top of the stack. If the indexable is not present, all members are initialized to `nil`.
 */
LUAI_FUNC int luaR_createobject(lua_State* L);

LUAI_FUNC void luaR_freeobject(lua_State* L, LuauObject* classinstance, lua_Page* page);

#define luaR_checkoffsetinbounds(inst, offset) (offset < (inst)->lclass->numberofallmembers)

#define luaR_lookupmemberatoffset(inst, offset) \
    (LUAU_ASSERT(luaR_checkoffsetinbounds(inst, offset)), \
     offset < (inst)->lclass->numberofinstancemembers ? &(inst)->members[offset] \
                                                      : &(inst)->lclass->staticmembers[offset - inst->lclass->numberofinstancemembers])
