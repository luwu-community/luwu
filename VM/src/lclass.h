// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "lmem.h"
#include "lobject.h"
#include "lstate.h"
#include "lbytecode.h"
#include "ltable.h"

// An instance's members are allocated inline, immediately after the LuauObject header, so a
// construction costs one GC allocation rather than two. Everything that allocates, frees or
// GC-accounts for an object must agree on this size.
inline size_t luaR_objectsize(uint32_t nummembers)
{
    return sizeof(LuauObject) + size_t(nummembers) * sizeof(TValue);
}

// Luwu bundles: a bundle's allocation, its values followed by its key (LuauBundleKey)
inline size_t luaR_bundleallocsize(uint32_t size)
{
    return luaR_objectsize(size) + sizeof(LuauBundleKey);
}

// Luwu Native Classes (rfcs/native-classes.md): the allocation of an object with native data, its members followed by
// a LuauNativeData header and `nativesize` bytes
inline size_t luaR_nativeallocsize(uint32_t nummembers, uint32_t nativesize)
{
    return luaR_objectsize(nummembers) + sizeof(LuauNativeData) + nativesize;
}

// Luwu Native Classes (rfcs/native-classes.md): the header of the native data that follows `object`'s members, or NULL
// when it has none
inline LuauNativeData* luaR_nativedata(LuauObject* object)
{
    if (object->storagekind != LUAR_STORAGE_NATIVE)
        return NULL;
    return reinterpret_cast<LuauNativeData*>(luaR_members(object) + object->numberofmembers);
}

inline const LuauNativeData* luaR_nativedata(const LuauObject* object)
{
    return luaR_nativedata(const_cast<LuauObject*>(object));
}

// Luwu Native Classes (rfcs/native-classes.md): the embedder's bytes in `object`, or NULL when it has no native data
inline void* luaR_objectdata(LuauObject* object)
{
    LuauNativeData* native = luaR_nativedata(object);
    return native ? native + 1 : NULL;
}

// The size of `object`'s allocation, which a bundle's key or native data makes larger than its members need
inline size_t luaR_objectallocsize(const LuauObject* object)
{
    if (object->storagekind == LUAR_STORAGE_BUNDLE)
        return luaR_bundleallocsize(object->numberofmembers);
    if (const LuauNativeData* native = luaR_nativedata(object))
        return luaR_nativeallocsize(object->numberofmembers, native->size);
    return luaR_objectsize(object->numberofmembers);
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

/**
 * Luwu Destructuring (rfcs/destructuring.md): makes objects of `classdef` destructurable positionally with `count`
 * positions, for a builder to fill in `positionalmembers` before luaR_sealclassshape. Called once, for a class with a
 * primary constructor.
 */
LUAI_FUNC void luaR_newpositionalmembers(lua_State* L, LuauClass* classdef, int count);

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

// Luwu Classes (rfcs/classes): stores the fields `arg`, the POD constructor's argument, names into `object`. May run an
// `__index` metamethod, so callers protect it like a call.
LUAI_FUNC void luaR_applyobjectargument(lua_State* L, LuauClass* classdef, LuauObject* object, const TValue* arg);

/**
 * The POD constructor for the C API (lua_newobject), where nothing it calls may yield: runs `classdef`'s
 * `__implements` (its trait fields) and `__defaults` if it has them, then applies the fields of
 * `args[0]` (see luaR_applyobjectfields). `nargs` is the number of constructor arguments at `args`; 0,
 * or 1 that is nil, applies nothing, and more than 1 raises. Private fields of an object argument are
 * read with the access rights of `accessor`, the Lua closure constructing (NULL for native code).
 * `object` must be anchored where the collector can see it; it gets every write barrier it needs.
 * luaR_createobject does the same work in steps that may yield.
 */
LUAI_FUNC void luaR_initpodobject(lua_State* L, LuauClass* classdef, LuauObject* object, StkId args, int nargs, const Closure* accessor);

/**
 * Returns true if `cl` is `classdef`'s own `__init` closure specifically (stricter than
 * luaR_closureownsprivateaccess, which accepts any method of the class), or one of its trait initializers
 * (luaR_closureistraitinit).
 */
LUAI_FUNC bool luaR_closureisinit(const LuauClass* classdef, const Closure* cl);

// Luwu Traits (rfcs/classes/traits.md): true if `cl` is one of `classdef`'s copies of its traits' `__traitinit`, the only
// closures that may write a final field (luaR_closureisinit accepts them too, for const fields)
LUAI_FUNC bool luaR_closureistraitinit(const LuauClass* classdef, const Closure* cl);

// Luwu Classes (rfcs/classes): true if `cl` is a function only the VM calls during construction (`__defaults`, `__implements`,
// `__traitinit`, `__needs` and the trait initializer runner). lua_getinfo never hands one out: their code trusts that
// construction is what called them, with an object of their class.
LUAI_FUNC bool luaR_isvminternalfunction(lua_State* L, const Closure* cl);

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

// Luwu opaque values: the metafield that marks a table or userdata opaque when its value is truthy. The name isn't
// final, so nothing else spells it.
#define LUWU_OPAQUE_MARK "__opaque"

// Luwu opaque values: whether `value` is opaque, a class or object of a class marked with lua_setclassopaque, or a table
// or userdata whose metatable has a truthy LUWU_OPAQUE_MARK. An opaque value hides what it holds from error records and
// reflection. Only error paths call this: it looks the metafield up by name.
LUAI_FUNC bool luaR_isopaque(lua_State* L, const TValue* value);

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

// Luwu Classes (rfcs/classes): makes `fn`, a function the compiler built from the class declaration, what a call on the
// class runs (LuauClass::constructor)
LUAI_FUNC void luaR_setconstructor(lua_State* L, LuauClass* classdef, const TValue* fn);

// Luwu Classes (rfcs/classes): prepares a call on the class in `func`, with its arguments up to `argtop`, to run the
// class's constructor in its place. Raises for a private constructor called from outside the class, and for a class
// without `__init` given more than one argument. Returns false for a class without a constructor.
LUAI_FUNC bool luaR_prepareclasscall(lua_State* L, StkId func, StkId argtop);

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
// The interpreter passes `pc`, which is saved before a check that can raise, so the error points at the instruction
// being run (and a `try` around it catches it). Other callers have saved it already.
LUAU_FORCEINLINE void luaR_checkprivateaccessfast(
    lua_State* L,
    const TValue* key,
    const LuauClass* classdef,
    const Closure* cl,
    uint32_t offset,
    const Instruction* pc = NULL
)
{
    if (LUAU_UNLIKELY((classdef->memberflags[offset] & (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_INITBLOCKED)) != 0))
    {
        if (pc)
            L->ci->savedpc = pc;
        luaR_checkprivateaccess(L, key, classdef, cl, offset);
    }
}

LUAU_FORCEINLINE void luaR_checkconstassignfast(
    lua_State* L,
    const TValue* key,
    const LuauObject* object,
    const Closure* cl,
    uint32_t offset,
    const Instruction* pc = NULL
)
{
    if (LUAU_UNLIKELY((object->lclass->memberflags[offset] & LBC_CLASSMEMBER_CONST) != 0))
    {
        if (pc)
            L->ci->savedpc = pc;
        luaR_checkconstassign(L, key, object, cl, offset);
    }
}

// The lookup luaR_classvalueoffset makes for each kind of key
inline const TValue* luaR_membermapget(LuaTable* map, const TValue* key)
{
    return luaH_get(map, key);
}

inline const TValue* luaR_membermapget(LuaTable* map, TString* key)
{
    return luaH_getstr(map, key);
}

/**
 * Luwu Traits (rfcs/classes/traits.md): the offset of `classdef`'s member `key` as a read through the class value finds
 * it, or nil. A static member may share its name with an object field: `memberstooffset` then has the field and
 * `staticshadows` the static. The second lookup only happens where the first found an object field, which can't be read
 * through the class value.
 */
template<typename Key>
LUAU_FORCEINLINE const TValue* luaR_classvalueoffset(const LuauClass* classdef, Key key)
{
    const TValue* offset = luaR_membermapget(classdef->memberstooffset, key);
    bool objectfield = !ttisnil(offset) && uint32_t(nvalue(offset)) < classdef->numberofinstancemembers;

    if (LUAU_UNLIKELY(objectfield && classdef->staticshadows))
    {
        const TValue* shadow = luaR_membermapget(classdef->staticshadows, key);
        if (!ttisnil(shadow))
            return shadow;
    }

    return offset;
}

/**
 * Luwu Traits (rfcs/classes/traits.md): maps the member `name` to `offset` in `map`, a members-to-offset table being
 * filled in offset order. A static member (`isstatic`) whose name an object field already has goes in `*shadows` instead,
 * which is created when it's first needed (see LuauClass::staticshadows).
 */
LUAI_FUNC void luaR_mapmembername(
    lua_State* L,
    LuaTable* map,
    LuaTable** shadows,
    TString* name,
    uint32_t offset,
    bool isstatic,
    uint32_t numberofinstancemembers
);

/**
 * Add a new class member to `classdef` named `name` and with value `method`. As the naming implies
 * we only support methods today.
 */
LUAI_FUNC void luaR_addclassmember(lua_State* L, LuauClass* classdef, TString* name, TValue* method);

// Luwu Traits (rfcs/classes/traits.md): gives the field `name` on the class value (LBC_CLASSMEMBER_CLASSFIELD) its value, as
// the class statement runs (LBC_NEWCLASSMEMBER_FIELD)
LUAI_FUNC void luaR_addclassfield(lua_State* L, LuauClass* classdef, TString* name, const TValue* value);

// Luwu Traits (rfcs/classes/traits.md): adds the metamethod `name` of the class or trait value itself to its metatable, as
// the class statement runs (LBC_NEWCLASSMEMBER_VALUEMETAMETHOD): `tostring(Cat)` calls a class's `function __tostring(Self)`
// with the class. A Lua function is the class's code, for its `Self` check and private access.
LUAI_FUNC void luaR_addvaluemetamethod(lua_State* L, LuauClass* classdef, TString* name, const TValue* fn);

/**
 * Luwu Traits (rfcs/classes/traits.md): writes the field at `offset` of the class value `classdef`, `Cat.species = v`.
 * Raises unless it's a field on the class value (LBC_CLASSMEMBER_CLASSFIELD) that isn't const, and checks private
 * access against `cl`. `key` is the field's name, for messages.
 */
LUAI_FUNC void luaR_setclassfield(lua_State* L, LuauClass* classdef, const TValue* key, const Closure* cl, uint32_t offset, const TValue* value);

// Luwu Traits (rfcs/classes/traits.md): raised by a write through an object to a field on its class value
LUAI_FUNC l_noret luaR_sharedfieldwriteerror(lua_State* L, const LuauObject* object, const TValue* key);

// Luwu Traits (rfcs/classes/traits.md): "function" or "field" when `name` is a public member of `classdef` that only the
// class value can read (`static`, or a function without `self`), and NULL otherwise
LUAI_FUNC const char* luaR_staticmemberkind(const LuauClass* classdef, TString* name);

LUAI_FUNC void luaR_freeclass(lua_State* L, LuauClass* classdef, lua_Page* page);

// Luwu builtin classes: a member of a class built in C++. A field has no `method`; `flags` are LBC_CLASSMEMBER_* bits, which
// place a member the way a class body's modifiers do. A name the language gives a meaning follows its entry in
// Luau/ClassMemberNames.h: a metamethod goes on the objects, or with LBC_CLASSMEMBER_STATIC on the class value when the
// registry allows it there (`__tostring`).
struct luaR_NativeMember
{
    const char* name;
    lua_CFunction method;
    uint16_t flags;
    // A method's frameless version for the interpreter, or NULL
    luaR_FastMethod fast;
};

/**
 * Luwu builtin classes: builds a class from C++, the way the loader builds one from bytecode, and leaves it on the
 * stack. `members` lists the fields first, in the order of their offsets, then the methods and metamethods, and the
 * static methods (LBC_CLASSMEMBER_STATIC) last. `init` is the class's `__init`, called with the new object and the
 * constructor's arguments; without one, the class constructs like a class without `__init`. Names must outlive the
 * state: methods use them as their debug names.
 */
LUAI_FUNC LuauClass* luaR_newnativeclass(lua_State* L, const char* name, const luaR_NativeMember* members, int count, lua_CFunction init);

// Luwu builtin classes: a builtin class value's `__tostring`, which prints it like a primitive, by name and without an
// address. Builtin libraries list it as a static member (luaR_NativeMember).
LUAI_FUNC int luaR_builtinclasstostring(lua_State* L);

/**
 * Luwu Native Classes (rfcs/native-classes.md): gives every object of `classdef` constructed from now on `size` bytes of
 * native data, zeroed, whose destructor is the one set for `tag` (lua_setobjectdtor). The destructor runs once, when the
 * object is freed, whether or not its construction finished.
 */
LUAI_FUNC void luaR_setclassnativedata(LuauClass* classdef, uint32_t size, int tag);

/**
 * Luwu builtin classes: the object a C method of `classdef` was called on, its first argument. Raises the error a
 * Luwu method's prologue raises (luaG_selfclasserror) when that isn't an object of `classdef`.
 */
LUAI_FUNC LuauObject* luaR_checknativeself(lua_State* L, const LuauClass* classdef, const char* method);

// Luwu builtin classes: an `__init` that fills the object's fields from its arguments in order. A field with no
// argument for it stays nil.
LUAI_FUNC int luaR_fieldsinit(lua_State* L);

// Luwu builtin classes: pops the value on top of the stack and adds it to `b` the way a constructor call in `__tostring`
// shows it: strings quoted, everything else as `tostring` gives it
LUAI_FUNC void luaR_addshowntop(struct luaL_Strbuf* b);

// Luwu try expressions: a new `Some`, `Ok` or `Err` (`kind`) holding `value`, for the C API and `try`. Raises if the
// library isn't open.
LUAI_FUNC LuauObject* luaR_newpayload(lua_State* L, LuwuBuiltinClass kind, const TValue* value);

// Luwu try expressions: LOP_WRAPRESULT. Replaces the value in `ra` with an `Ok` holding it, or with `caught` an `Err`
// holding the error in `ra` and where it was raised in `ra + 1` to `ra + 3`. Doesn't run the collector.
LUAI_FUNC void luaR_wrapresult(lua_State* L, StkId ra, bool caught);

// Luwu try expressions: the value a `Some`, `Ok` or `Err` holds, or NULL for any other value
LUAI_FUNC const TValue* luaR_payload(lua_State* L, const TValue* value);

// Luwu structured runtime errors: writes the `kind` positions of `record`, an `Error.Runtime`'s and an
// `Error.NotIndexable`'s, from its `subkind` as the strings the variants hold. Call it while the record is a GC root.
LUAI_FUNC void luaR_fillerrorsubkind(lua_State* L, LuwuErrorRecord& record);

// Luwu structured runtime errors: adds the message of a recorded error to `b`, without its position.
// luaR_fillerrorsubkind must have filled it.
LUAI_FUNC void luaR_adderrorrecord(struct luaL_Strbuf* b, const LuwuErrorRecord& record);

// Luwu structured runtime errors: a new object of the recorded error's variant, `Error.NotIndexable(...)` and the
// others, or NULL when the result library (luaopen_result) isn't open. The record stays as it is.
LUAI_FUNC LuauObject* luaR_newerrorvariant(lua_State* L);

// Luwu structured runtime errors: raises error variant `kind` with the `nargs` values on top of the stack as its
// positions, at the nearest Luwu code. `pcall` gets its message and `try` an object of the variant.
LUAI_FUNC l_noret luaR_raisevariant(lua_State* L, LuwuBuiltinClass kind, int nargs);

/**
 * Luwu collections: sets up a generic for loop over the object at `ra` when it's a builtin container, and returns
 * false otherwise. The loop calls neither `__iter` nor anything per step: `ra` becomes a boolean, which FORGLOOP hands
 * to luaR_collectionnext, and `ra + 1` the container. `ra + 2` is the loop's state:
 *  - a `Vec` or `VecDeque` gets `ra` = true and the number of entries visited, as light userdata like a table loop's
 *    index (luaR_storagenext);
 *  - an `OrderedMap` gets `ra` = false and the previous key, nil at the start. `ra`'s extra field keeps that key's
 *    position, so a step only looks the key up when the entry there changed.
 */
LUAI_FUNC bool luaR_collectionprep(lua_State* L, StkId ra);

/**
 * Luwu collections: one step of a loop luaR_collectionprep set up. Writes the next key and value to `ra + 3` and
 * `ra + 4` and updates the state in `ra + 2`, or returns false at the end. It visits what `__iter` would, and raises the
 * same errors.
 */
LUAI_FUNC bool luaR_collectionnext(lua_State* L, StkId ra);

// Luwu collections: the storage block of a `Vec` or `VecDeque`, or NULL before its first value. Its `__init` sets the
// object's storage kind and leaves the storage member nil. Native code reads light userdata there as a block.
LUAU_FORCEINLINE LuauStorage* luaR_storage(LuauObject* object)
{
    if ((object->storagekind & LUAR_STORAGE_CONTAINER) == 0)
        return NULL;

    const TValue* member = &luaR_members(object)[LUAR_STORAGE_MEMBER];
    return ttislightuserdata(member) ? static_cast<LuauStorage*>(pvalue(member)) : NULL;
}

LUAU_FORCEINLINE const LuauStorage* luaR_storage(const LuauObject* object)
{
    return luaR_storage(const_cast<LuauObject*>(object));
}

// Luwu collections: the size of a storage block with `capacity` slots
LUAU_FORCEINLINE size_t luaR_storagesize(uint32_t capacity)
{
    return offsetof(LuauStorage, items) + size_t(capacity) * sizeof(TValue);
}

// The memory `object` holds: its allocation and its storage block (luaR_storage), if it has one
LUAU_FORCEINLINE size_t luaR_objectmemory(const LuauObject* object)
{
    const LuauStorage* storage = luaR_storage(object);
    return luaR_objectallocsize(object) + (storage ? luaR_storagesize(storage->capacity) : 0);
}

// Luwu bundles: whether `object` is a bundle
LUAU_FORCEINLINE bool luaR_isbundle(const LuauObject* object)
{
    return object->storagekind == LUAR_STORAGE_BUNDLE;
}

// Luwu bundles: the key that follows a bundle's values
LUAU_FORCEINLINE LuauBundleKey* luaR_bundlekey(LuauObject* object)
{
    LUAU_ASSERT(luaR_isbundle(object));
    return reinterpret_cast<LuauBundleKey*>(luaR_members(object) + object->numberofmembers);
}

LUAU_FORCEINLINE const LuauBundleKey* luaR_bundlekey(const LuauObject* object)
{
    return luaR_bundlekey(const_cast<LuauObject*>(object));
}

// Luwu collections: the slot of the entry `offset` places from the front of a `Vec` or `VecDeque`
LUAU_FORCEINLINE TValue* luaR_storageentry(LuauStorage* storage, uint32_t offset)
{
    LUAU_ASSERT(offset < storage->count);
    return &storage->items[(storage->head + offset) & (storage->capacity - 1)];
}

// Luwu collections: one step of a loop over a `Vec` or `VecDeque`, the case of luaR_collectionnext that FORGLOOP runs
// inline
LUAU_FORCEINLINE bool luaR_storagenext(lua_State* L, LuauObject* container, StkId ra)
{
    LuauStorage* storage = luaR_storage(container);
    uint32_t visited = uint32_t(reinterpret_cast<uintptr_t>(pvalue(ra + 2)));
    if (!storage || visited >= storage->count)
        return false;

    // Like a table loop's index, so native code can update only the low 32 bits
    setpvalue(ra + 2, reinterpret_cast<void*>(uintptr_t(visited + 1)), LU_TAG_ITERATOR);
    setnvalue(ra + 3, double(visited + 1));
    // note: a plain copy, since setobj2s needs lgc.h
    *(ra + 4) = *luaR_storageentry(storage, visited);
    return true;
}

// Luwu collections: the length `#` gives `object` without calling `__len`, or -1 when its class has no such length. A
// `Vec` and a `VecDeque` read their storage block, an `OrderedMap` its length field (LuauClass::lengthfield).
LUAU_FORCEINLINE double luaR_objectlength(const LuauObject* object)
{
    if (object->storagekind & LUAR_STORAGE_CONTAINER)
    {
        const LuauStorage* storage = luaR_storage(object);
        return storage ? double(storage->count) : 0;
    }
    if (luaR_isbundle(object))
        return double(object->numberofmembers);

    uint32_t field = object->lclass->lengthfield;
    return field == LUAR_NO_LENGTH_FIELD ? -1 : nvalue(&luaR_members(object)[field]);
}

/**
 * The `__call` metamethod of every class, which constructs a class without a compiled constructor (one built from C)
 * and calls a trait's `__create`. A class with a constructor never reaches it (luaR_prepareclasscall). This is
 * written as a Lua API function and expects the stack to be:
 *
 *  [ BASE ]
 *  - A class value
 *  - The constructor arguments (for the POD constructor, an optional indexable value)
 *
 * This function checks a private constructor, allocates a new object and then either calls a custom
 * `__init` with the arguments or runs the POD constructor's steps (`__implements`, `__defaults`, the
 * argument's fields). Every step may yield; the object stays on this function's stack until it's
 * complete, and is the single result.
 */
LUAI_FUNC int luaR_createobject(lua_State* L);

LUAI_FUNC void luaR_freeobject(lua_State* L, LuauObject* object, lua_Page* page);

// Luwu Traits (rfcs/classes/traits.md): raises when `classdef` hasn't finished implementing its traits (traitspending).
// Every construction path checks it.
LUAI_FUNC void luaR_checktraitsimplemented(lua_State* L, const LuauClass* classdef);

// Luwu Traits (rfcs/classes/traits.md): raised by every attempt to construct a trait.
LUAI_FUNC l_noret luaR_traitconstructionerror(lua_State* L, const LuauClass* trait);

// Luwu Traits (rfcs/classes/traits.md): the `__create` that calling `trait` calls; raises when it has none.
LUAI_FUNC const TValue* luaR_traitcreate(lua_State* L, const LuauClass* trait);

/**
 * Luwu Traits (rfcs/classes/traits.md): makes `classdef` implement the `n` traits at `listed`, which are followed on the
 * stack by how many trait arguments each `implements` entry passes (LBC_NEWCLASSMEMBER_IMPLEMENTS). Runs once, when
 * the class statement finishes, before any object of the class exists:
 *  - attaches the listed traits and every trait without parameters their `needs` lists imply;
 *  - appends the traits' provided fields after the class's own and gives the class a copy of every trait function it
 *    doesn't define (luaR_addclassmember stamps the copy as the class's method). The copy is the compiler's when it
 *    made one (luaR_addcompiledtraitcopy);
 *  - raises when a member is provided twice, a final function is redefined, or an expectation isn't met by the
 *    finished class (presence, field or function, access specifier, `const`);
 *  - copies the traits' constant field defaults into the class's, and gives the class a copy of each trait's
 *    `__traitinit` for the fields that have to be computed per construction (luaR_nexttraitinit).
 * May call the traits' `__needs` functions.
 */
LUAI_FUNC void luaR_implementtraits(lua_State* L, LuauClass* classdef, StkId listed, uint32_t n);

/**
 * Luwu Traits (rfcs/classes/traits.md): records `copy` as `classdef`'s copy of `trait`'s function `name`
 * (LBC_NEWCLASSMEMBER_TRAITCOPY). The compiler made it from the trait's source, which is in the same file, so
 * luaR_implementtraits uses it instead of copying the trait's function at runtime. Ignored once the class has
 * implemented its traits.
 */
LUAI_FUNC void luaR_addcompiledtraitcopy(lua_State* L, LuauClass* classdef, const LuauClass* trait, TString* name, const TValue* copy);

/**
 * Luwu Traits (rfcs/classes/traits.md): one run of LOP_INITTRAITS, which computes the fields an object gets from its
 * class's traits that don't have a constant default (those are already in place, see luaR_newobject). `state` holds how
 * many initializers have run, then the object and the class's `nargs` `implements` arguments. Lays out the call of the
 * class's copy of the next trait's `__traitinit` at `frame` and returns true, or, once every initializer has run, puts
 * back the overridden constant fields and returns false.
 */
LUAI_FUNC bool luaR_nexttraitinit(lua_State* L, StkId state, StkId frame, int nargs);

/**
 * Luwu Traits (rfcs/classes/traits.md): records `value` as `classdef`'s qualified implementation of the member `name` of
 * the trait `trait`. `form` is the LOP_NEWCLASSMEMBER form that says what `value` is: a function (`function Json.name`,
 * LBC_NEWCLASSMEMBER_QUALIFIED), the class's object slot for each object's value of a scoped field (`Animal.items = {}`,
 * LBC_NEWCLASSMEMBER_QUALIFIEDFIELD), or the value of a scoped field on the class (`shared Animal.kind = "cat"`,
 * LBC_NEWCLASSMEMBER_QUALIFIEDSHARED, and LBC_NEWCLASSMEMBER_QUALIFIEDSTATIC). Raises when the trait's member is the other
 * kind or has another placement; luaR_implementtraits checks the rest. Ignored once the class has implemented its traits.
 */
LUAI_FUNC void luaR_addqualified(lua_State* L, LuauClass* classdef, const TValue* trait, TString* name, const TValue* value, int form);

/**
 * Luwu Traits (rfcs/classes/traits.md): sets `ra` to the class's version of the member `name` of `trait`, for
 * `obj:Trait:name()` and `Class:Trait.name` (LOP_GETQUALIFIED). `receiver` is an object or a class. Raises when it
 * doesn't implement the trait, or the trait has no such function.
 */
LUAI_FUNC void luaR_getqualified(lua_State* L, StkId ra, const TValue* receiver, const TValue* trait, TString* name);

/**
 * Luwu Traits (rfcs/classes/traits.md): writes `value` to the scoped field `name` of the trait `trait`, for
 * `obj:Trait.name = value` (LOP_SETQUALIFIED). A per-object field is written through an object, and a `scoped shared` or
 * `scoped static` one through the class. Raises when `receiver` doesn't implement the trait, the field is const, or it's
 * written through the wrong receiver.
 */
LUAI_FUNC void luaR_setqualified(lua_State* L, const TValue* receiver, const TValue* trait, TString* name, const TValue* value);

// Luwu Traits (rfcs/classes/traits.md): whether `classdef` implements `trait`, listed or implied through `needs`.
LUAI_FUNC bool luaR_implements(const LuauClass* classdef, const LuauClass* trait);

// Luwu bundles: a bundle is a table key by its values. luaR_keyhash gives equal bundles equal hashes, and luaR_keyequal
// compares their values raw (no `__eq`), nested bundles by their values too.
LUAI_FUNC unsigned int luaR_keyhash(const LuauObject* bundle);
LUAI_FUNC bool luaR_keyequal(const LuauObject* a, const LuauObject* b);

// Luwu bundles: whether comparing `bundle` by value could take too long, past a weight limit. Such a bundle is only equal
// to itself.
LUAI_FUNC bool luaR_bundletooheavy(const LuauObject* bundle);

/**
 * Luwu bundles: a new bundle of `size` values, of the class for its size. Its values are unset: fill all of them, with
 * nothing in between that could run the collector, then call luaR_sealbundle. Raises when `size` is past
 * LUWU_BUNDLE_LIMIT or the bundle library isn't open. `what` names the operation in that error.
 */
LUAI_FUNC LuauObject* luaR_newbundleuninit(lua_State* L, int size, const char* what);
// Luwu bundles: computes the key of a bundle luaR_newbundleuninit made, once its values are set
LUAI_FUNC void luaR_sealbundle(LuauObject* bundle);
// Luwu bundles: the hash a bundle holding the `n` values at `values` would get, for a lookup that doesn't build it. False
// when that bundle would be too heavy to compare by value, so it would equal no key.
LUAI_FUNC bool luaR_bundlekeyprobe(const TValue* values, uint32_t n, unsigned int* hash);
// Luwu bundles: whether `bundle`, a table key, equals a bundle holding the `n` values at `values` whose hash is `hash`
LUAI_FUNC bool luaR_bundlekeyequals(const LuauObject* bundle, unsigned int hash, const TValue* values, uint32_t n);

// Luwu bundles: the number of values in the bundle `value`, or -1 when it isn't a bundle
LUAI_FUNC int luaR_bundlesize(const TValue* value);
// Luwu bundles: `bundle[key]` into `val`, a position by number. Raises for any other key or a position out of range.
LUAI_FUNC void luaR_getbundleposition(lua_State* L, const LuauObject* bundle, const TValue* key, StkId val);
// Luwu bundles: raises for an assignment to a bundle, naming `with` for a position
LUAI_FUNC l_noret luaR_bundleassignerror(lua_State* L, const TValue* key);

// A member's offset is cached in its instruction's 8-bit C operand (the slot the fast paths check). A
// larger offset doesn't fit: it is never cached, and that member is looked up by name every time.
#define LUAR_MAX_CACHED_MEMBER_SLOT 0xff

#define luaR_checkoffsetinbounds(object, offset) (offset < (object)->lclass->numberofallmembers)

#define luaR_lookupmemberatoffset(object, offset) \
    (LUAU_ASSERT(luaR_checkoffsetinbounds(object, offset)), \
     offset < (object)->lclass->numberofinstancemembers ? &luaR_members(object)[offset] \
                                                      : &(object)->lclass->staticmembers[offset - object->lclass->numberofinstancemembers])
