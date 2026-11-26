# Native Classes

> **WIP: agent-written draft.** While the Luwu community grows, `integration` temporarily accepts agent-written RFCs as specifications for
> experimental features ([AI policy](https://luwu.org/development/ai-policy.html)). This RFC must be redesigned and rewritten primarily by humans
> before it's accepted.

Status: Proposed

FFlag: DebugLuwuBuiltinClasses (the builtin classes this builds on)

## Summary

Embedders can define real Luwu classes from C, and from Rust through mluau, instead of exposing their types as userdata. A native class is a class
like any other at runtime: its objects have fixed member slots, methods are found by NAMECALL's slot cache, public fields are read without an
`__index` call, and `class.is`, `class.of`, traits and destructuring work on it. Its methods are C functions, and an object can carry native data
that the embedder frees when the object is collected.

## Motivation

Embedders expose most of their API as userdata today. A userdata is opaque: every method call goes through `__namecall` or `__index` into C, a field
read is a function call, and the type checker sees an extern type that can't implement a trait or be destructured. That cost is right for a handle
to something Luwu shouldn't see, like a socket or a child process. It's the wrong cost for plain data, like a file size, a duration, a path or the
result of running a process.

seal shows the problem. A directory listing builds a table per entry and allocates around fifteen closures for each one. `FileSize` and `Duration`
are userdata around a single number, so `size.bytes` calls into Rust. None of these types can implement a Luwu trait, and the editor can't tell a
`FileSize` from any other extern type.

Luwu already builds its own library types this way. `Vec`, `VecDeque`, `OrderedMap` and bundles are classes written in C++ through an internal
builder (`luaR_newnativeclass`), and they run at the speed of user classes. This RFC gives embedders the same builder through the public C API.

## Design

### Defining a class

An embedder describes the members and calls `lua_newclass`, which leaves the class on the stack. Fields come first, then methods. A method is any
function on the stack, so closures with upvalues and continuations work the same as for `lua_pushcclosurek`:

```c
static const lua_ClassMember fileSizeMembers[] = {
    {"bytes", LUA_CLASSMEMBER_FIELD | LUA_CLASSMEMBER_CONST},
    {"kilobytes", LUA_CLASSMEMBER_METHOD},
    {"__tostring", LUA_CLASSMEMBER_METHOD},
};

lua_pushcfunction(L, fileSizeKilobytes, "kilobytes");
lua_pushcfunction(L, fileSizeToString, "__tostring");
lua_newclass(L, "FileSize", "seal", NULL, fileSizeMembers, 3, 0);
```

The methods are popped in member order. The class is a value like any other: the embedder stores it in a library table, returns it from `require`
or keeps a reference to it with `lua_ref`.

A method without `self` is a static, called as `FileSize.fromKilobytes(4)`. Metamethods are members with their usual names, under the same rules as
user classes: `__index` and `__newindex` aren't allowed.

### `typeof` ids

Every native class has a name, a namespace and an id, and `typeof` of its objects is the namespace and the id joined with a `.`. A `NULL` id is
the class's name, so the class above gives `typeof(size) == "seal.FileSize"`. The id is only what `typeof` reports: `class.name` returns the name.

- The name must be a valid Luwu identifier that isn't a keyword (`FileSize`, not `"cats. meow"`), the same as a class declared in Luwu.
- The namespace and the id are each ASCII letters, digits, `.` and spaces. Neither may be empty or made up of only dots.
- Only the joined id matters: `("seal.time", "Duration")` and `("seal", "time.Duration")` both give `seal.time.Duration`.
- A joined id is unique within a VM. Registering one a second time raises.
- An embedder can't set `__type` on a native class or its objects. The `typeof` id is the only name it reports.
- An invalid name, namespace or id raises from `lua_newclass`.

Builtin classes have no namespace. They act like primitives and choose their own `typeof` names (`"vecdeque"`, `"bundle"`). A native `typeof` id
always contains a `.`, so a new builtin class can never take a name an embedder already uses. Anything Luwu itself builds through this API is an
opt-in module rather than part of the language, and it gets a namespace like any embedder.

`typeof` isn't how code tells classes apart. Use `x is FileSize`, or a pattern on wherever the embedder exposes the class value. seal's userdata are
narrowed by `typeof` today only because userdata have nothing better.

### Construction

A class constructs through its `__init` member, which the embedder writes as a C function taking the new object and the call's arguments. Calling
the class from Luwu runs it. `lua_newobject` constructs one from C. A class without `__init` takes the POD constructor's table argument, as a user
class does.

A class's registered `__init` may assign its `const` fields. Today `luaR_checkconstassign` trusts no C code with `const`, which would leave a native
class with no way to set one. The check would accept the C function the class registered as `__init`, the way it accepts a Luwu `__init`.

### Native data

An object can carry bytes the embedder owns. `lua_newclass` takes their size, and every object of the class has that many bytes after its members,
zeroed when the object is made. The embedder reads them with `lua_toobjectdata`, which returns `NULL` for a value without data of that tag:

```c
typedef struct FileSize
{
    uint64_t bytes;
} FileSize;

FileSize* size = (FileSize*)lua_toobjectdata(L, 1, FILESIZE_TAG);
```

A class with native data has a tag, like a userdata tag. `lua_setobjectdtor(L, tag, dtor)` sets the function that runs when an object of that tag is
freed, and `lua_setobjectmark` sets one that marks the references the data holds. The tag is kept on the object, since the collector can free a
class before its objects.

The destructor runs once for every object of the class, when the collector frees it or the state is closed. It also runs for an object whose
construction raised, which is why the data starts zeroed. Like a userdata destructor, it runs during a sweep and must not call into Luwu.

### Checking `self`

A method checks its receiver with `luaL_checkobject(L, 1, tag)`. It raises Luwu's usual `self` error when the value isn't an object of the class
with that tag, and returns the native data otherwise. The check compares one byte on the object.

### Traits

A native class can implement Luwu traits. `lua_newclass` takes the traits from the stack and the arguments of any trait that has parameters, then
runs `luaR_implementtraits` as a class statement does. A trait's `__implemented` hook runs for a native class too.

### Types

The checker learns a native class from the embedder's definition file, which declares it as a class:

```luwu
declare class FileSize
    __type = "seal.FileSize"
    const bytes: number
    function kilobytes(self): number
end
```

`declare class type` takes the same `__type` line, for a class whose value the embedder doesn't expose.

Definition files already read classes with `private` and `const` members. A declaration can drift from the class the embedder builds, so the
Analysis C API gets a check that compares a declared class with a live one. It reports any member that's missing, added, or declared with other
flags. seal would run it in its own tests.

### mluau

mluau gets a `LuwuClass` builder next to `UserData`. A Rust type implements a trait that lists its fields and methods, and the builder stores the
Rust value in the object's native data. It reuses mluau's method closures, its `TypeId` checks and its borrow tracking, and Rust's `Drop` runs from
the destructor. A Rust field getter has no class equivalent: it becomes a stored field set at construction, or a method.
`Lua::new_native_class(name, namespace)` builds a class whose id is its name, as in `lua.new_native_class("Duration", "seal")`.
`Lua::new_native_class_with_id(name, namespace, id)` sets the id.

## C API

```c
typedef struct lua_ClassMember
{
    const char* name;
    int flags; // LUA_CLASSMEMBER_*
} lua_ClassMember;

#define LUA_CLASSMEMBER_FIELD 0
#define LUA_CLASSMEMBER_METHOD 1
#define LUA_CLASSMEMBER_CONST 2
#define LUA_CLASSMEMBER_PRIVATE 4

// Uses `name` if `typeofid` is `NULL`
LUA_API void lua_newclass(
    lua_State* L,
    const char* name,
    const char* typeofnamespace,
    const char* typeofid,
    const lua_ClassMember* members,
    int count,
    size_t nativesize
);
LUA_API void lua_setclasstag(lua_State* L, int idx, int tag);
LUA_API void* lua_toobjectdata(lua_State* L, int idx, int tag);
LUA_API void lua_setobjectdtor(lua_State* L, int tag, lua_Destructor dtor);
LUA_API lua_Destructor lua_getobjectdtor(lua_State* L, int tag);
LUALIB_API void* luaL_checkobject(lua_State* L, int narg, int tag);
```

The public flags are separate from the bytecode's `LBC_CLASSMEMBER_*` bits, which stay free to change. Fast methods, which the builtin classes use
to run without a call frame, stay internal.

## Compatibility

This only adds C API. Luau code can't tell a native class from a user class, and nothing changes for userdata. Upstream Luau has no equivalent.

## Drawbacks

- C code bypasses `private` today, so every embedder's C code can read every class's private fields. Two embedders sharing a state aren't isolated
  from each other.
- A class belongs to one `lua_State` and its threads. An embedder with several states, like seal's thread library, builds the class once per state,
  and an object from one state fails `luaL_checkobject` in another.
- An object points at its class for life. A host that reloads its classes leaves older objects on the older class.
- APIs that read like fields today through getters, like seal's `DateTime`, change shape when they move to a class.

## Alternatives

- **A private userdata field** holding the embedder's data works today with no VM change. It costs a second allocation and an indirection per object.
- **Keep userdata, and make `__namecall` faster.** It keeps every type opaque to the checker and to traits.
- **Hybrid classes**, a Luwu class whose heavy methods are declared in Luwu and implemented in Rust, would put the class's shape in Luwu source. It
  could build on this RFC later.

## Unresolved questions

- Whether the registered C `__init` may assign `const` fields, which the VM refuses for C code today.
- Static values, like `Duration.ZERO`: today a class's static members can only be functions.
- How `implements` arguments are passed to `lua_newclass`.
- Whether the member order (fields first) and the `__init` slot are part of the ABI, or hidden behind the builder.
