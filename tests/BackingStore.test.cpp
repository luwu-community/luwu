// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lua.h"
#include "lualib.h"
#include "lbackingstore.h"
#include "lapi.h"
#include "ldo.h"
#include "lgc.h"
#include "lstate.h"
#include "lstring.h"
#include "doctest.h"
#include "ScopedFlags.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#ifndef LUWU_BACKINGSTORE_VM_ONLY
#include "luacode.h"
#include "luacodegen.h"
#include "Luau/CodeGen.h"
#endif


LUAU_FASTFLAG(LuwuManagedReferences2)

namespace
{
using State = std::unique_ptr<lua_State, void (*)(lua_State*)>;
State state() { return State(luaL_newstate(), lua_close); }
void collect(lua_State* L) { lua_gc(L, LUA_GCCOLLECT, 0); luaC_validate(L); }
struct Owner { int* calls; };
void drop(lua_State*, void*, size_t, void* ud) { ++*static_cast<Owner*>(ud)->calls; }
uint32_t tag(lua_State* L) { return lua_registerbackingstoretype(L, drop, sizeof(Owner)); }
// The store at a stack index, for checking internals behind the index-based C API.
BackingStore* at(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    REQUIRE(o != nullptr);
    REQUIRE(ttisbackingstore(o));
    return backingstorevalue(o);
}
// Each helper pushes through the C API and returns the pushed store.
BackingStore* newstore(lua_State* L, size_t len, bool mutable_, bool terminated)
{
    lua_newbackingstore(L, len, mutable_, terminated);
    return at(L, -1);
}
BackingStore* external(lua_State* L, void* data, size_t len, uint32_t t, int* calls, bool terminated = false, bool mutable_ = false)
{
    Owner owner{calls};
    lua_newexternalbackingstore(L, data, len, t, mutable_, terminated, &owner);
    return at(L, -1);
}
BackingStore* newslice(lua_State* L, int idx, size_t offset, size_t len)
{
    lua_slicebackingstore(L, idx, offset, len);
    return at(L, -1);
}
BackingStore* pushcstr(lua_State* L, int idx)
{
    lua_backingstorecstr(L, idx, nullptr);
    return at(L, -1);
}
struct FailingAllocator
{
    bool fail = false;
    static void* alloc(void* ud, void* ptr, size_t, size_t size)
    {
        if (!size) { std::free(ptr); return nullptr; }
        return static_cast<FailingAllocator*>(ud)->fail ? nullptr : std::realloc(ptr, size);
    }
};
struct Operation
{
    int idx = 1;
    uint32_t t = 0;
    size_t len = 0;
    void* data = nullptr;
    const void* userdata = nullptr;
    bool terminated = false;
    enum Kind { Owned, External, Slice, Copy, CStrStore, Register } kind = Owned;
    static void run(lua_State* L, void* ud)
    {
        Operation& op = *static_cast<Operation*>(ud);
        switch (op.kind)
        {
        case Owned: lua_newbackingstore(L, op.len, 0, 0); break;
        case External: lua_newexternalbackingstore(L, op.data, op.len, op.t, 0, op.terminated, op.userdata); break;
        case Slice: lua_slicebackingstore(L, op.idx, 1, op.len); break;
        case Copy: lua_copybackingstore(L, op.idx, 0, op.terminated); break;
        case CStrStore: lua_backingstorecstr(L, op.idx, nullptr); break;
        case Register: lua_registerbackingstoretype(L, drop, op.len); break;
        }
    }
};
int run(lua_State* L, Operation& op) { return luaD_rawrunprotected(L, Operation::run, &op); }
// Packs the C API predicates so a check covers every property at once.
enum { Mutable = 1, Terminated = 2, Slice = 4, External = 8 };
int flags(lua_State* L, int idx)
{
    return (lua_isbackingstoremutable(L, idx) ? Mutable : 0) | (lua_isbackingstoreterminated(L, idx) ? Terminated : 0) |
           (lua_isbackingstoreslice(L, idx) ? Slice : 0) | (lua_isbackingstoreexternal(L, idx) ? External : 0);
}
#ifndef LUWU_BACKINGSTORE_VM_ONLY
void execute(lua_State* L, const char* source, bool native)
{
    size_t size = 0;
    char* bytecode = luau_compile(source, strlen(source), nullptr, &size);
    int status = luau_load(L, "=backingstores", bytecode, size, 0);
    std::free(bytecode);
    REQUIRE(status == LUA_OK);
    if (native)
    {
        auto result = Luau::CodeGen::compile(L, -1, {});
        REQUIRE(result.result == Luau::CodeGen::CodeGenCompilationResult::Success);
        REQUIRE(clvalue(luaA_toobject(L, -1))->l.p->execdata != nullptr);
    }
    status = lua_pcall(L, 0, 0, 0);
    if (status != LUA_OK)
        INFO(lua_tostring(L, -1));
    REQUIRE(status == LUA_OK);
}
#endif
}

TEST_SUITE("BackingStores")
{
TEST_CASE("Compact GC allocations and stack constructors")
{
    auto L = state(); REQUIRE(L);
    if (sizeof(void*) == 8)
    {
        CHECK(sizeof(BackingStore) == 16);
        CHECK(sizeof(OwnedBackingStore) == 24);
        CHECK(sizeof(ExternalBackingStore) == 24);
        CHECK(sizeof(SliceBackingStore) == 24);
    }
    CHECK(sizeof(TKey) == sizeof(TValue));
    CHECK(sizeof(LuaNode) == 2 * sizeof(TValue));
    // Owned stores are sized exactly; an empty unterminated one is just the header.
    CHECK(luaB_backingstoresize(L.get(), newstore(L.get(), 0, 0, 0)) == sizeof(BackingStore));
    CHECK(luaB_backingstoresize(L.get(), newstore(L.get(), 0, 0, 1)) == sizeof(BackingStore) + 1);
    CHECK(luaB_backingstoresize(L.get(), newstore(L.get(), 3, 0, 0)) == sizeof(BackingStore) + 3);
    collect(L.get());
    lua_settop(L.get(), 0);
    auto bytes = static_cast<char*>(lua_newbackingstore(L.get(), 100, 1, 0));
    auto store = at(L.get(), -1);
    CHECK(lua_gettop(L.get()) == 1);
    CHECK(lua_type(L.get(), -1) == LUA_TBACKINGSTORE);
    CHECK(lua_isbackingstore(L.get(), -1));
    size_t len = 0;
    CHECK(lua_tobackingstore(L.get(), -1, &len) == bytes);
    CHECK(len == 100);
    CHECK(lua_topointer(L.get(), -1) == store);
    CHECK(flags(L.get(), -1) == Mutable);
    CHECK(lua_backingstoretag(L.get(), -1) == 0);
    CHECK(lua_getbackingstoreuserdata(L.get(), -1) == nullptr);
    CHECK(lua_backingstoreoffset(L.get(), -1) == 0);
    lua_getbackingstoreroot(L.get(), -1);
    CHECK(at(L.get(), -1) == store);
    lua_pop(L.get(), 1);
    CHECK(lua_touserdata(L.get(), -1) == nullptr);
    CHECK_FALSE(lua_isuserdata(L.get(), -1));
    CHECK(lua_tobackingstore(L.get(), 2, nullptr) == nullptr);
    lua_pushnumber(L.get(), 1);
    CHECK_FALSE(lua_isbackingstore(L.get(), -1));
    CHECK(lua_tobackingstore(L.get(), -1, nullptr) == nullptr);
    CHECK(lua_getbackingstoreuserdata(L.get(), -1) == nullptr);
    CHECK(lua_backingstoretag(L.get(), -1) == 0);
    CHECK(flags(L.get(), -1) == 0);
    CHECK(lua_backingstoreoffset(L.get(), -1) == 0);
    lua_pop(L.get(), 1);
    CHECK(std::strcmp(lua_typename(L.get(), LUA_TBACKINGSTORE), "userdata") == 0);
    for (size_t i = 0; i < len; ++i) CHECK(bytes[i] == 0);
    bytes[42] = 77;
    collect(L.get());
    CHECK(bytes[42] == 77);
    CHECK(lua_getmetatable(L.get(), -1) == 0);
}

TEST_CASE("Stack and refpool roots govern external lifetime")
{
    ScopedFastFlag refs(FFlag::LuwuManagedReferences2, true);
    int calls = 0;
    auto L = state();
    char bytes[] = "hello";
    auto store = external(L.get(), bytes, 5, tag(L.get()), &calls);
    CHECK(reinterpret_cast<uintptr_t>(lua_getbackingstoreuserdata(L.get(), -1)) % 8 == 0);
    collect(L.get()); CHECK(calls == 0);
    int ref = lua_refpool(L.get(), -1);
    lua_pop(L.get(), 1);
    collect(L.get()); CHECK(calls == 0);
    CHECK(lua_getrefpool(L.get(), ref) == LUA_TBACKINGSTORE);
    CHECK(at(L.get(), -1) == store);
    lua_pop(L.get(), 1);
    lua_unrefpool(L.get(), ref);
    collect(L.get()); CHECK(calls == 1);
    collect(L.get()); CHECK(calls == 1);
}

TEST_CASE("Pushing a slice's root roots it without stepping GC")
{
    int calls = 0;
    auto L = state();
    char bytes[] = "hello";
    uint32_t t = tag(L.get());
    external(L.get(), bytes, 5, t, &calls);
    newslice(L.get(), 1, 1, 3);
    lua_remove(L.get(), 1);
    // Collection is due, but the root must be pushed before the collector runs.
    L->global->GCthreshold = 0;
    CHECK(lua_backingstoreoffset(L.get(), -1) == 1);
    lua_getbackingstoreroot(L.get(), -1);
    CHECK(L->global->GCthreshold == 0);
    CHECK(lua_gettop(L.get()) == 2);
    CHECK(lua_tobackingstore(L.get(), -1, nullptr) == bytes);
    CHECK(lua_backingstoretag(L.get(), -1) == t);
    lua_remove(L.get(), 1);
    collect(L.get());
    CHECK(calls == 0);
    lua_pop(L.get(), 1);
    collect(L.get());
    CHECK(calls == 1);
}

TEST_CASE("Constructing on a black coroutine preserves the GC barrier")
{
    int calls = 0;
    auto L = state();
    lua_State* thread = lua_newthread(L.get());
    uint32_t t = tag(L.get());
    lua_newtable(L.get());
    for (int i = 0; i < 3000; ++i)
    {
        lua_newtable(L.get());
        lua_rawseti(L.get(), -2, i + 1);
    }
    collect(L.get());
    bool black = false;
    for (int i = 0; i < 10000; ++i)
    {
        lua_gc(L.get(), LUA_GCSTEP, 1);
        if (keepinvariant(L->global) && isblack(obj2gco(thread)))
        {
            black = true;
            break;
        }
    }
    REQUIRE(black);
    char bytes[] = "abc";
    auto store = external(thread, bytes, 3, t, &calls);
    CHECK_FALSE(isblack(obj2gco(thread)));
    CHECK(lua_gettop(thread) == 1);
    luaC_validate(L.get());
    collect(L.get());
    CHECK(calls == 0);
    CHECK(luaB_backingstoredata(store, NULL) == bytes);
    lua_pop(thread, 1);
    collect(L.get());
    CHECK(calls == 1);
}

TEST_CASE("Nested slices trace their ultimate owner and cached C strings")
{
    int calls = 0;
    auto L = state();
    char bytes[] = {'a','b','c','d','e','f'};
    uint32_t t = tag(L.get());
    auto root = external(L.get(), bytes, sizeof(bytes), t, &calls);
    auto first = newslice(L.get(), 1, 1, 4);
    auto last = newslice(L.get(), 2, 1, 2);
    CHECK(luaB_backingstoreroot(last) == root);
    CHECK(luaB_backingstoreroot(root) == root);
    CHECK(luaB_backingstoreoffset(root) == 0);
    CHECK(luaB_backingstoreoffset(first) == 1);
    CHECK(luaB_backingstoreoffset(last) == 2);
    for (int idx = 1; idx <= 3; ++idx)
    {
        CHECK(lua_backingstoreoffset(L.get(), idx) == size_t(idx - 1));
        lua_getbackingstoreroot(L.get(), idx);
        CHECK(at(L.get(), -1) == root);
        CHECK(lua_backingstoretag(L.get(), -1) == t);
        lua_pop(L.get(), 1);
    }
    CHECK(flags(L.get(), 1) == External);
    CHECK(flags(L.get(), 3) == Slice);
    CHECK(lua_backingstoretag(L.get(), 1) == t);
    CHECK(lua_backingstoretag(L.get(), 3) == 0);
    CHECK(lua_gettop(L.get()) == 3);
    CHECK(luaB_backingstoredata(last, NULL) == bytes + 2);
    CHECK(luaB_backingstorelen(last) == 2);
    CHECK(lua_getbackingstoreuserdata(L.get(), 3) == nullptr);
    unsigned hash = luaB_backingstorehash(L.get(), last);
    size_t len = 0;
    const char* str = lua_backingstorecstr(L.get(), 3, &len);
    BackingStore* cstr = at(L.get(), -1);
    CHECK(len == 2);
    CHECK(std::strcmp(str, "cd") == 0);
    REQUIRE(luaB_backingstorecachedcstr(last) == cstr);
    CHECK(luaB_backingstoreoffset(cstr) == 0);
    // The cached copy takes over the union, but the slice keeps its offset and data pointer.
    CHECK(luaB_backingstoreoffset(last) == 2);
    CHECK(lua_backingstoreoffset(L.get(), 3) == 2);
    lua_getbackingstoreroot(L.get(), 3);
    lua_pop(L.get(), 1);
    auto nested = newslice(L.get(), 3, 1, 1);
    CHECK(luaB_backingstoreroot(nested) == root);
    CHECK(luaB_backingstoreoffset(nested) == 3);
    CHECK(luaB_backingstoredata(nested, NULL) == bytes + 3);
    lua_pop(L.get(), 1);
    CHECK(lua_backingstoretag(L.get(), 4) == 0);
    CHECK(lua_backingstoreoffset(L.get(), 4) == 0);
    lua_getbackingstoreroot(L.get(), 4);
    CHECK(at(L.get(), -1) == cstr);
    lua_pop(L.get(), 1);
    CHECK(luaB_backingstorehashvalid(cstr));
    CHECK(luaB_backingstorehash(L.get(), last) == hash);
    CHECK(luaB_backingstoredata(last, NULL) == bytes + 2);
    lua_pop(L.get(), 1);
    lua_replace(L.get(), 1);
    lua_settop(L.get(), 1);
    for (int i = 0; i < 3; ++i)
    {
        collect(L.get());
        CHECK(calls == 0);
        CHECK(luaB_backingstoreroot(last) == root);
        CHECK(luaB_backingstorecstr(L.get(), last) == cstr);
        CHECK(luaB_backingstoredata(last, NULL) == bytes + 2);
        CHECK(luaB_backingstoreoffset(last) == 2);
        CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(cstr, NULL)), "cd") == 0);
    }
    CHECK(lua_backingstoreoffset(L.get(), 1) == 2);
    lua_getbackingstoreroot(L.get(), 1);
    lua_remove(L.get(), 1);
    collect(L.get());
    CHECK(calls == 0);
    CHECK(at(L.get(), -1) == root);
    lua_pop(L.get(), 1);
    collect(L.get()); CHECK(calls == 1);
}

TEST_CASE("C API reserves a terminator outside the logical length")
{
    auto L = state();
    for (int mutable_ : {0, 1})
    {
        for (size_t len : {size_t(0), size_t(8), size_t(16), size_t(255)})
        {
            char* data = static_cast<char*>(lua_newbackingstore(L.get(), len, mutable_, 1));
            auto store = at(L.get(), -1);
            size_t logical = SIZE_MAX;
            CHECK(lua_tobackingstore(L.get(), -1, &logical) == data);
            CHECK(logical == len);
            CHECK(lua_gettop(L.get()) == 1);
            CHECK(flags(L.get(), -1) == (Terminated | (mutable_ ? Mutable : 0)));
            CHECK(data[len] == '\0');
            CHECK(luaB_backingstoreisterminated(store));
            CHECK(lua_backingstoretag(L.get(), -1) == 0);
            memset(data, 'x', len);
            CHECK(data[len] == '\0');
            if (!mutable_)
            {
                CHECK(luaB_backingstorecstr(L.get(), store) == store);
                CHECK(luaB_backingstorecachedcstr(store) == nullptr);
            }
            collect(L.get());
            CHECK(data[len] == '\0');
            lua_pop(L.get(), 1);
            collect(L.get());
        }
    }
}

TEST_CASE("Slicing a slice composes offsets, bounds, and mutability against its source")
{
    auto L = state();
    char* rootbytes = static_cast<char*>(lua_newbackingstore(L.get(), 10, 1, 0));
    memcpy(rootbytes, "0123456789", 10);
    auto root = at(L.get(), 1);
    char* outer = static_cast<char*>(lua_slicebackingstore(L.get(), 1, 2, 6));
    char* inner = static_cast<char*>(lua_slicebackingstore(L.get(), 2, 1, 3));
    CHECK(outer == rootbytes + 2);
    CHECK(inner == rootbytes + 3);
    size_t len = 0;
    CHECK(lua_tobackingstore(L.get(), 3, &len) == inner);
    CHECK(len == 3);
    CHECK(memcmp(inner, "345", 3) == 0);
    CHECK(flags(L.get(), 3) == (Mutable | Slice));
    // The nested slice points straight at the ultimate root, not the intermediate slice.
    CHECK(luaB_backingstoreroot(at(L.get(), 3)) == root);
    CHECK(lua_backingstoreoffset(L.get(), 3) == 3);
    lua_getbackingstoreroot(L.get(), 3);
    CHECK(at(L.get(), -1) == root);
    lua_pop(L.get(), 1);

    // Bounds are relative to the source slice, even where the root has more bytes.
    Operation op;
    op.kind = Operation::Slice;
    op.idx = 2;
    op.len = 6; // [1, 7) of a 6-byte slice
    CHECK(run(L.get(), op) == LUA_ERRRUN);
    lua_settop(L.get(), 3);
    op.len = 5; // [1, 6) ends exactly at the slice's end
    CHECK(run(L.get(), op) == LUA_OK);
    CHECK(lua_tobackingstore(L.get(), -1, &len) == rootbytes + 3);
    CHECK(len == 5);
    lua_pop(L.get(), 1);
    CHECK(lua_slicebackingstore(L.get(), 3, 3, 0) == inner + 3);
    CHECK(lua_tobackingstore(L.get(), -1, &len) == inner + 3);
    CHECK(len == 0);
    lua_pop(L.get(), 1);

    // Writes through a nested slice land in the root and show through every view.
    inner[0] = 'X';
    CHECK(rootbytes[3] == 'X');
    CHECK(outer[1] == 'X');

    // Neither the intermediate slice nor the root's own slot is needed to keep the bytes alive.
    lua_remove(L.get(), 2);
    lua_remove(L.get(), 1);
    collect(L.get());
    CHECK(lua_tobackingstore(L.get(), 1, &len) == inner);
    CHECK(memcmp(inner, "X45", 3) == 0);
    CHECK(lua_backingstoreoffset(L.get(), 1) == 3);
    lua_getbackingstoreroot(L.get(), 1);
    CHECK(at(L.get(), -1) == root);
    lua_pop(L.get(), 1);

    // A third level keeps composing from the root.
    CHECK(lua_slicebackingstore(L.get(), 1, 2, 1) == inner + 2);
    CHECK(lua_backingstoreoffset(L.get(), 2) == 5);
    lua_getbackingstoreroot(L.get(), 2);
    lua_settop(L.get(), 0);
    collect(L.get());

    // Immutable roots produce immutable slices at every level.
    lua_newbackingstore(L.get(), 4, 0, 0);
    lua_slicebackingstore(L.get(), 1, 1, 3);
    lua_slicebackingstore(L.get(), 2, 1, 1);
    CHECK(flags(L.get(), 2) == Slice);
    CHECK(flags(L.get(), 3) == Slice);
    collect(L.get());
}

TEST_CASE("Termination belongs to the exact immutable range")
{
    auto L = state();
    auto owned = newstore(L.get(), 5, 0, 1);
    memcpy(luaB_backingstoredata(owned, NULL), "ab\0cd", 5);
    auto end = newslice(L.get(), 1, 3, 2);
    CHECK_FALSE(luaB_backingstoreisterminated(end));
    CHECK_FALSE(lua_isbackingstoreterminated(L.get(), 2));
    CHECK(luaB_backingstorecstr(L.get(), end) == end);
    CHECK(luaB_backingstoreisterminated(end));
    CHECK(lua_isbackingstoreterminated(L.get(), 2));
    CHECK(luaB_backingstoredata(end, NULL) == static_cast<char*>(luaB_backingstoredata(owned, NULL)) + 3);
    CHECK(luaB_backingstorecachedcstr(end) == nullptr);
    auto interior = newslice(L.get(), 1, 0, 2);
    CHECK(luaB_backingstorecstr(L.get(), interior) == interior);
    CHECK(luaB_backingstoredata(interior, NULL) == luaB_backingstoredata(owned, NULL));
    CHECK(luaB_backingstorecachedcstr(interior) == nullptr);
    auto unterm = newstore(L.get(), 2, 0, 0);
    memcpy(luaB_backingstoredata(unterm, NULL), "xy", 2);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(luaB_backingstorecstr(L.get(), unterm), NULL)), "xy") == 0);
    CHECK(luaB_backingstorecachedcstr(unterm) != nullptr);
    CHECK_FALSE(luaB_backingstoreisterminated(unterm));
    CHECK(luaB_backingstoreisterminated(luaB_backingstorecachedcstr(unterm)));
    CHECK(static_cast<char*>(luaB_backingstoredata(luaB_backingstorecachedcstr(unterm), NULL))[2] == 0);
    collect(L.get());
}

TEST_CASE("Mutable hashing reads current bytes and immutable caches survive union transition")
{
    auto L = state();
    auto mut = newstore(L.get(), 4, 1, 0);
    memcpy(luaB_backingstoredata(mut, NULL), "abcd", 4);
    CHECK(luaB_backingstorehash(L.get(), mut) == luaS_hash("abcd", 4));
    memcpy(luaB_backingstoredata(mut, NULL), "efgh", 4);
    CHECK(luaB_backingstorehash(L.get(), mut) == luaS_hash("efgh", 4));
    CHECK_FALSE(luaB_backingstorehashvalid(mut));
    auto imm = newstore(L.get(), 4, 0, 0);
    memcpy(luaB_backingstoredata(imm, NULL), "ijkl", 4);
    unsigned hash = luaB_backingstorehash(L.get(), imm);
    CHECK(luaB_backingstorehashvalid(imm));
    luaB_backingstorecstr(L.get(), imm);
    // Only the copy holding the hash claims it once the union holds the copy pointer.
    CHECK_FALSE(luaB_backingstorehashvalid(imm));
    CHECK(luaB_backingstorehashvalid(luaB_backingstorecachedcstr(imm)));
    CHECK(luaB_backingstorehash(L.get(), luaB_backingstorecachedcstr(imm)) == hash);
    CHECK(luaB_backingstorehash(L.get(), imm) == hash);
    auto snapshot = pushcstr(L.get(), 1);
    CHECK_FALSE(luaB_backingstoreismutable(snapshot));
    CHECK(snapshot != mut);
    CHECK(std::memcmp(luaB_backingstoredata(snapshot, NULL), "efgh", 4) == 0);
    CHECK(luaB_backingstorecachedcstr(mut) == nullptr);
    CHECK_FALSE(luaB_backingstorehashvalid(mut));
}

TEST_CASE("Empty external stores and zero-sized userdata")
{
    auto L = state();
    uint32_t t = lua_registerbackingstoretype(L.get(), [](lua_State*, void*, size_t, void*) {}, 0);
    CHECK(lua_newexternalbackingstore(L.get(), nullptr, 0, t, 0, 0, nullptr) == nullptr);
    auto store = at(L.get(), -1);
    CHECK(lua_isbackingstore(L.get(), -1));
    CHECK(lua_tobackingstore(L.get(), -1, nullptr) == nullptr);
    CHECK(lua_getbackingstoreuserdata(L.get(), -1) == nullptr);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(luaB_backingstorecstr(L.get(), store), NULL)), "") == 0);
    auto slice = newslice(L.get(), 1, 0, 0);
    CHECK(luaB_backingstoredata(slice, NULL) == nullptr);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(luaB_backingstorecstr(L.get(), slice), NULL)), "") == 0);
    collect(L.get());
}

TEST_CASE("Terminated external stores reuse host bytes and preserve slice bounds")
{
    int calls = 0;
    auto L = state();
    char bytes[] = {'a', 'b', '\0', 'c', 'd', '\0'};
    auto root = external(L.get(), bytes, 5, tag(L.get()), &calls, true);
    unsigned hash = luaB_backingstorehash(L.get(), root);
    CHECK(luaB_backingstorecstr(L.get(), root) == root);
    CHECK(luaB_backingstorecachedcstr(root) == nullptr);
    CHECK(luaB_backingstorehash(L.get(), root) == hash);
    CHECK(hash == luaS_hash(bytes, 5));
    auto tail = newslice(L.get(), 1, 3, 2);
    CHECK(luaB_backingstorecstr(L.get(), tail) == tail);
    CHECK(luaB_backingstoredata(tail, NULL) == bytes + 3);
    CHECK(luaB_backingstorecachedcstr(tail) == nullptr);
    auto interior = newslice(L.get(), 1, 0, 2);
    CHECK(luaB_backingstorecstr(L.get(), interior) == interior);
    CHECK(luaB_backingstoredata(interior, NULL) == bytes);
    CHECK(luaB_backingstorecachedcstr(interior) == nullptr);
    auto unterm = newslice(L.get(), 1, 0, 1);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(luaB_backingstorecstr(L.get(), unterm), NULL)), "a") == 0);
    CHECK(luaB_backingstorecachedcstr(unterm) != nullptr);
    CHECK_FALSE(luaB_backingstoreisterminated(unterm));
    CHECK(luaB_backingstoreisterminated(luaB_backingstorecachedcstr(unterm)));
    collect(L.get());
    CHECK(calls == 0);
    CHECK(luaB_backingstorecstr(L.get(), root) == root);
    lua_settop(L.get(), 0);
    collect(L.get());
    CHECK(calls == 1);
}

TEST_CASE("External termination supports empty stores and preserves mutability")
{
    int calls = 0;
    auto L = state();
    uint32_t t = tag(L.get());
    for (int mutable_ : {0, 1})
    {
        for (size_t len : {size_t(0), size_t(3)})
        {
            char bytes[] = {'a', 'b', 'c', '\0'};
            bytes[len] = '\0';
            auto store = external(L.get(), bytes, len, t, &calls, true, mutable_ != 0);
            CHECK(lua_backingstoretag(L.get(), 1) == t);
            size_t logical = SIZE_MAX;
            CHECK(lua_tobackingstore(L.get(), 1, &logical) == bytes);
            CHECK(logical == len);
            CHECK(flags(L.get(), 1) == (External | Terminated | (mutable_ ? Mutable : 0)));
            logical = SIZE_MAX;
            CHECK(lua_backingstorecstr(L.get(), 1, &logical) == bytes);
            CHECK(logical == len);
            auto result = at(L.get(), -1);
            CHECK(result == store);
            CHECK(luaB_backingstoreismutable(result) == (mutable_ != 0));
            CHECK(static_cast<char*>(luaB_backingstoredata(result, NULL))[len] == '\0');
            CHECK(luaB_backingstorecachedcstr(store) == nullptr);
            lua_settop(L.get(), 0);
            collect(L.get());
        }
    }
    CHECK(calls == 4);
}

TEST_CASE("Mutable C-string stores reuse terminators and snapshot unterminated bytes")
{
    auto L = state();
    for (int terminated : {0, 1})
    {
        auto source = newstore(L.get(), 4, 1, terminated);
        char* writable = static_cast<char*>(luaB_backingstoredata(source, NULL));
        memcpy(writable, "a\0bc", 4);
        auto first = pushcstr(L.get(), 1);
        CHECK(lua_gettop(L.get()) == 2);
        CHECK((first == source) == (terminated != 0));
        CHECK(luaB_backingstoreismutable(first) == (terminated != 0));
        CHECK(luaB_backingstorelen(first) == 4);
        CHECK(std::memcmp(luaB_backingstoredata(first, NULL), "a\0bc", 4) == 0);
        CHECK(static_cast<char*>(luaB_backingstoredata(first, NULL))[4] == '\0');
        CHECK(luaB_backingstorecstr(L.get(), first) == first);
        writable[0] = 'z';
        auto second = pushcstr(L.get(), 1);
        CHECK((second == first) == (terminated != 0));
        CHECK(std::memcmp(luaB_backingstoredata(second, NULL), "z\0bc", 4) == 0);
        CHECK(std::memcmp(luaB_backingstoredata(first, NULL), terminated ? "z\0bc" : "a\0bc", 4) == 0);
        CHECK(luaB_backingstorecachedcstr(source) == nullptr);
        CHECK_FALSE(luaB_backingstorehashvalid(source));
        lua_remove(L.get(), 1);
        collect(L.get());
        CHECK(std::memcmp(luaB_backingstoredata(first, NULL), terminated ? "z\0bc" : "a\0bc", 4) == 0);
        CHECK(std::memcmp(luaB_backingstoredata(second, NULL), "z\0bc", 4) == 0);
        lua_settop(L.get(), 0);
        collect(L.get());
    }
}

TEST_CASE("Mutable slice snapshots survive root mutation and external cleanup")
{
    int calls = 0;
    auto L = state();
    char bytes[] = "hello";
    auto root = external(L.get(), bytes, 5, tag(L.get()), &calls, true, true);
    auto slice = newslice(L.get(), 1, 1, 3);
    REQUIRE(luaB_backingstoreismutable(slice));
    CHECK(flags(L.get(), 2) == (Mutable | Slice));
    auto first = pushcstr(L.get(), 2);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(first, NULL)), "ell") == 0);
    bytes[1] = 'X';
    auto second = pushcstr(L.get(), 2);
    CHECK(first != second);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(second, NULL)), "Xll") == 0);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(first, NULL)), "ell") == 0);
    CHECK(luaB_backingstorecachedcstr(slice) == nullptr);
    CHECK(luaB_backingstorecachedcstr(root) == nullptr);
    lua_remove(L.get(), 1);
    lua_remove(L.get(), 1);
    collect(L.get());
    CHECK(calls == 1);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(first, NULL)), "ell") == 0);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(second, NULL)), "Xll") == 0);
}

TEST_CASE("A returned immutable C-string cache can outlive its source")
{
    ScopedFastFlag refs(FFlag::LuwuManagedReferences2, true);
    int calls = 0;
    auto L = state();
    char bytes[] = {'a', 'b', 'c'};
    auto source = external(L.get(), bytes, 3, tag(L.get()), &calls);
    unsigned hash = luaB_backingstorehash(L.get(), source);
    auto result = pushcstr(L.get(), 1);
    REQUIRE(result != source);
    CHECK(result == luaB_backingstorecachedcstr(source));
    CHECK(luaB_backingstorehashvalid(result));
    CHECK(luaB_backingstorehash(L.get(), result) == hash);
    CHECK(pushcstr(L.get(), 1) == result);
    int ref = lua_refpool(L.get(), -1);
    lua_settop(L.get(), 0);
    collect(L.get());
    CHECK(calls == 1);
    CHECK(lua_getrefpool(L.get(), ref) == LUA_TBACKINGSTORE);
    CHECK(at(L.get(), -1) == result);
    CHECK(luaB_backingstorecstr(L.get(), result) == result);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(result, NULL)), "abc") == 0);
    lua_pop(L.get(), 1);
    lua_unrefpool(L.get(), ref);
    collect(L.get());
}

TEST_CASE("C API roots an existing terminated slice as its result")
{
    int calls = 0;
    auto L = state();
    char bytes[] = "abcd";
    external(L.get(), bytes, 4, tag(L.get()), &calls, true);
    auto slice = newslice(L.get(), 1, 1, 3);
    CHECK(pushcstr(L.get(), 2) == slice);
    CHECK(lua_gettop(L.get()) == 3);
    lua_replace(L.get(), 1);
    lua_settop(L.get(), 1);
    collect(L.get());
    CHECK(calls == 0);
    CHECK(at(L.get(), -1) == slice);
    CHECK(luaB_backingstoredata(slice, NULL) == bytes + 1);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(slice, NULL)), "bcd") == 0);
    lua_pop(L.get(), 1);
    collect(L.get());
    CHECK(calls == 1);
}

TEST_CASE("Empty mutable C-string snapshots are distinct terminated stores")
{
    auto L = state();
    auto source = newstore(L.get(), 0, 1, 0);
    auto first = pushcstr(L.get(), 1);
    auto second = pushcstr(L.get(), 1);
    CHECK(first != source);
    CHECK(second != first);
    CHECK(luaB_backingstorelen(first) == 0);
    CHECK(luaB_backingstorelen(second) == 0);
    CHECK_FALSE(luaB_backingstoreismutable(first));
    CHECK_FALSE(luaB_backingstoreismutable(second));
    CHECK(static_cast<char*>(luaB_backingstoredata(first, NULL))[0] == '\0');
    CHECK(static_cast<char*>(luaB_backingstoredata(second, NULL))[0] == '\0');
    CHECK(luaB_backingstorecachedcstr(source) == nullptr);
    collect(L.get());
}

TEST_CASE("Explicit copies are independent even for already terminated stores")
{
    auto L = state();
    for (bool sourceMutable : {false, true})
    {
        for (bool copyMutable : {false, true})
        {
            for (bool terminated : {false, true})
            {
                auto source = newstore(L.get(), 4, sourceMutable, 1);
                memcpy(luaB_backingstoredata(source, NULL), "a\0bc", 4);
                REQUIRE(luaB_backingstorecstr(L.get(), source) == source);
                void* bytes = lua_copybackingstore(L.get(), 1, copyMutable, terminated);
                auto copy = at(L.get(), -1);
                CHECK(lua_gettop(L.get()) == 2);
                CHECK(bytes == luaB_backingstoredata(copy, NULL));
                CHECK(copy != source);
                CHECK(bytes != luaB_backingstoredata(source, NULL));
                CHECK(luaB_backingstorelen(copy) == 4);
                CHECK(flags(L.get(), 2) ==
                      ((copyMutable ? Mutable : 0) | (terminated ? Terminated : 0)));
                CHECK(lua_backingstoretag(L.get(), 2) == 0);
                CHECK(luaB_backingstoreroot(copy) == copy);
                CHECK(lua_backingstoreoffset(L.get(), 2) == 0);
                lua_getbackingstoreroot(L.get(), 2);
                CHECK(at(L.get(), -1) == copy);
                lua_pop(L.get(), 1);
                CHECK(luaB_backingstorecachedcstr(copy) == nullptr);
                CHECK(luaB_backingstorecachedcstr(source) == nullptr);
                CHECK(std::memcmp(bytes, "a\0bc", 4) == 0);
                if (terminated)
                    CHECK(static_cast<char*>(bytes)[4] == '\0');
                if (copyMutable)
                {
                    static_cast<char*>(bytes)[0] = 'X';
                    CHECK(static_cast<char*>(luaB_backingstoredata(source, NULL))[0] == 'a');
                }
                if (sourceMutable)
                {
                    static_cast<char*>(luaB_backingstoredata(source, NULL))[2] = 'Y';
                    CHECK(static_cast<char*>(bytes)[2] == 'b');
                }
                lua_remove(L.get(), 1);
                collect(L.get());
                CHECK(std::memcmp(bytes, copyMutable ? "X\0bc" : "a\0bc", 4) == 0);
                lua_pop(L.get(), 1);
                collect(L.get());
            }
        }
    }
}

TEST_CASE("Copying a nested external slice detaches its owner and offset")
{
    int calls = 0;
    auto L = state();
    char bytes[] = "0123456789";
    external(L.get(), bytes, 10, tag(L.get()), &calls, true, true);
    newslice(L.get(), 1, 2, 6);
    auto nested = newslice(L.get(), 2, 1, 3);
    CHECK(luaB_backingstoreoffset(nested) == 3);
    lua_copybackingstore(L.get(), 3, 0, 1);
    auto copy = at(L.get(), -1);
    CHECK(copy != nested);
    CHECK(luaB_backingstoreoffset(copy) == 0);
    CHECK(luaB_backingstoreroot(copy) == copy);
    CHECK(flags(L.get(), -1) == Terminated);
    CHECK(lua_backingstoretag(L.get(), -1) == 0);
    CHECK(lua_getbackingstoreuserdata(L.get(), -1) == nullptr);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(copy, NULL)), "345") == 0);
    bytes[3] = 'X';
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(copy, NULL)), "345") == 0);
    lua_replace(L.get(), 1);
    lua_settop(L.get(), 1);
    collect(L.get());
    CHECK(calls == 1);
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(copy, NULL)), "345") == 0);
}

TEST_CASE("Copying empty external memory supports optional termination")
{
    auto L = state();
    uint32_t t = lua_registerbackingstoretype(L.get(), [](lua_State*, void*, size_t, void*) {}, 0);
    lua_newexternalbackingstore(L.get(), nullptr, 0, t, 0, 0, nullptr);
    newslice(L.get(), 1, 0, 0);
    for (int idx : {1, 2})
    {
        for (bool terminated : {false, true})
        {
            void* bytes = lua_copybackingstore(L.get(), idx, 0, terminated);
            auto copy = at(L.get(), -1);
            CHECK(copy != at(L.get(), idx));
            CHECK(bytes != nullptr);
            CHECK(luaB_backingstorelen(copy) == 0);
            CHECK(luaB_backingstoreoffset(copy) == 0);
            CHECK(flags(L.get(), -1) == (terminated ? Terminated : 0));
            CHECK(lua_backingstoretag(L.get(), -1) == 0);
            if (terminated)
                CHECK(static_cast<char*>(bytes)[0] == '\0');
            collect(L.get());
            lua_pop(L.get(), 1);
        }
    }
}

TEST_CASE("External termination rejects null data even for empty stores")
{
    auto L = state();
    int calls = 0;
    Owner owner{&calls};
    Operation op;
    op.kind = Operation::External;
    op.t = tag(L.get());
    op.terminated = true;
    op.userdata = &owner;
    CHECK(run(L.get(), op) == LUA_ERRRUN);
    // Validation errors after a valid tag still transfer ownership.
    CHECK(calls == 1);
    lua_settop(L.get(), 0);
    collect(L.get());
    CHECK(calls == 1);
}

TEST_CASE("Fresh external registrations and bounds errors")
{
    auto L = state();
    for (uint32_t i = 1; i <= 14; ++i)
        CHECK(lua_registerbackingstoretype(L.get(), drop, sizeof(Owner)) == i);
    Operation op; op.kind = Operation::Register;
    CHECK(run(L.get(), op) == LUA_ERRRUN);
    lua_settop(L.get(), 0);
    op.kind = Operation::Owned; op.len = size_t(1 << 30) + 1;
    CHECK(run(L.get(), op) == LUA_ERRRUN);
    lua_settop(L.get(), 0);
    lua_newbackingstore(L.get(), 4, 0, 0);
    op.kind = Operation::Slice; op.idx = 1; op.len = SIZE_MAX;
    CHECK(run(L.get(), op) == LUA_ERRRUN);
}

TEST_CASE("External construction copies initial userdata and reports it on failure")
{
    auto L = state();
    int calls = 0;
    Owner owner{&calls};
    char bytes[] = "abc";
    CHECK(lua_newexternalbackingstore(L.get(), bytes, 3, tag(L.get()), 0, 0, &owner) == bytes);
    void* inlineuserdata = lua_getbackingstoreuserdata(L.get(), -1);
    CHECK(inlineuserdata != &owner);
    CHECK(memcmp(inlineuserdata, &owner, sizeof(owner)) == 0);
    lua_settop(L.get(), 0);
    collect(L.get());
    CHECK(calls == 1);

    static void* seen;
    seen = &seen;
    uint32_t t = lua_registerbackingstoretype(L.get(), [](lua_State*, void*, size_t, void* ud) { seen = ud; }, 0);
    Operation op;
    op.kind = Operation::External;
    op.t = t;
    op.terminated = true;
    op.userdata = &owner;
    CHECK(run(L.get(), op) == LUA_ERRRUN);
    // Zero-sized userdata is NULL on failure, matching what the callback sees after success.
    CHECK(seen == nullptr);
    lua_settop(L.get(), 0);
}

TEST_CASE("C API cannot install a backing-store metatable")
{
    auto L = state();
    lua_newbackingstore(L.get(), 0, 0, 0);
    struct SetMetatable
    {
        static void run(lua_State* L, void*)
        {
            lua_newtable(L);
            lua_setmetatable(L, 1);
        }
    };
    CHECK(luaD_rawrunprotected(L.get(), SetMetatable::run, nullptr) == LUA_ERRRUN);
    lua_settop(L.get(), 1);
    CHECK(lua_getmetatable(L.get(), 1) == 0);
    collect(L.get());
}

TEST_CASE("External construction frees host data when stack reservation fails")
{
    FailingAllocator allocator;
    State L(lua_newstate(FailingAllocator::alloc, &allocator), lua_close);
    REQUIRE(L);
    lua_gc(L.get(), LUA_GCSTOP, 0);
    int calls = 0;
    Owner owner{&calls};
    Operation op;
    op.kind = Operation::External;
    op.t = tag(L.get());
    op.userdata = &owner;
    // Force the next constructor's stack reservation to grow the stack.
    lua_rawcheckstack(L.get(), int(L->stack_last - L->top) - 1);
    while (L->top < L->stack_last - 1)
        lua_pushnil(L.get());
    allocator.fail = true;
    CHECK(run(L.get(), op) == LUA_ERRMEM);
    allocator.fail = false;
    CHECK(calls == 1);
    lua_settop(L.get(), 0);
    collect(L.get());
    CHECK(calls == 1);
}

TEST_CASE("GC heap enumeration exposes owner and C-string edges")
{
    auto L = state();
    auto root = newstore(L.get(), 4, 0, 0);
    memcpy(luaB_backingstoredata(root, NULL), "abcd", 4);
    auto slice = newslice(L.get(), 1, 1, 2);
    luaB_backingstorecstr(L.get(), slice);
    struct Graph { int nodes = 0; int roots = 0; int caches = 0; } graph;
    luaC_enumheap(L.get(), &graph,
        [](void* ud, void*, uint8_t tt, uint8_t, size_t size, const char*) {
            if (tt == LUA_TBACKINGSTORE) { ++static_cast<Graph*>(ud)->nodes; CHECK(size >= sizeof(BackingStore)); }
        },
        [](void* ud, void*, void*, const char* name) {
            if (!strcmp(name, "root")) ++static_cast<Graph*>(ud)->roots;
            if (!strcmp(name, "cstr")) ++static_cast<Graph*>(ud)->caches;
        });
    CHECK(graph.nodes == 3);
    CHECK(graph.roots == 1);
    CHECK(graph.caches == 1);
    collect(L.get());
}

TEST_CASE("C-string publication has an incremental write barrier")
{
    auto L = state();
    auto store = newstore(L.get(), 4, 0, 0);
    memcpy(luaB_backingstoredata(store, NULL), "abcd", 4);
    // Keep enough marking work queued to observe the store black before atomic.
    lua_newtable(L.get());
    for (int i = 0; i < 3000; ++i) { lua_newtable(L.get()); lua_rawseti(L.get(), -2, i + 1); }
    collect(L.get());
    bool black = false;
    for (int i = 0; i < 10000; ++i)
    {
        lua_gc(L.get(), LUA_GCSTEP, 1);
        if (keepinvariant(L->global) && isblack(reinterpret_cast<GCObject*>(store))) { black = true; break; }
    }
    REQUIRE(black);
    luaB_backingstorecstr(L.get(), store);
    REQUIRE(luaB_backingstorecachedcstr(store));
    CHECK_FALSE(iswhite(reinterpret_cast<GCObject*>(luaB_backingstorecachedcstr(store))));
    luaC_validate(L.get());
    collect(L.get());
    CHECK(std::strcmp(static_cast<const char*>(luaB_backingstoredata(luaB_backingstorecstr(L.get(), store), NULL)), "abcd") == 0);
}

TEST_CASE("Allocation failure frees external data with initial userdata and preserves cached hash")
{
    FailingAllocator allocator;
    State L(lua_newstate(FailingAllocator::alloc, &allocator), lua_close);
    REQUIRE(L);
    lua_gc(L.get(), LUA_GCSTOP, 0);
    auto source = newstore(L.get(), 20000, 0, 0);
    memset(luaB_backingstoredata(source, NULL), 'x', luaB_backingstorelen(source));
    unsigned hash = luaB_backingstorehash(L.get(), source);
    lua_rawcheckstack(L.get(), 10);
    Operation op;
    allocator.fail = true;
    op.kind = Operation::CStrStore;
    CHECK(run(L.get(), op) == LUA_ERRMEM);
    allocator.fail = false;
    lua_settop(L.get(), 1);
    CHECK(luaB_backingstorecachedcstr(source) == nullptr);
    CHECK(luaB_backingstorehash(L.get(), source) == hash);
    // Large inline userdata forces allocation even if small GC pages have free blocks.
    uint32_t t = lua_registerbackingstoretype(L.get(), drop, 20000);
    struct LargeUserdata { Owner owner; char padding[20000 - sizeof(Owner)]; };
    int calls = 0;
    std::unique_ptr<LargeUserdata> userdata(new LargeUserdata{{&calls}, {}});
    op.kind = Operation::External; op.t = t; op.data = luaB_backingstoredata(source, NULL); op.len = luaB_backingstorelen(source);
    op.userdata = userdata.get();
    allocator.fail = true;
    CHECK(run(L.get(), op) == LUA_ERRMEM);
    allocator.fail = false;
    CHECK(calls == 1);
    lua_settop(L.get(), 1);
    CHECK(luaB_backingstorehash(L.get(), source) == hash);
    collect(L.get());
}

TEST_CASE("Mutable snapshot allocation failure leaves source untouched")
{
    FailingAllocator allocator;
    State L(lua_newstate(FailingAllocator::alloc, &allocator), lua_close);
    REQUIRE(L);
    lua_gc(L.get(), LUA_GCSTOP, 0);
    auto source = newstore(L.get(), 20000, 1, 0);
    memset(luaB_backingstoredata(source, NULL), 'x', luaB_backingstorelen(source));
    lua_rawcheckstack(L.get(), 10);
    Operation op;
    op.kind = Operation::CStrStore;
    allocator.fail = true;
    CHECK(run(L.get(), op) == LUA_ERRMEM);
    allocator.fail = false;
    lua_settop(L.get(), 1);
    CHECK(luaB_backingstoreismutable(source));
    CHECK(luaB_backingstorecachedcstr(source) == nullptr);
    CHECK_FALSE(luaB_backingstorehashvalid(source));
    CHECK(static_cast<char*>(luaB_backingstoredata(source, NULL))[0] == 'x');
    CHECK(static_cast<char*>(luaB_backingstoredata(source, NULL))[19999] == 'x');
    collect(L.get());
}

TEST_CASE("C-string conversion reserves its stack slot before creating a cache")
{
    FailingAllocator allocator;
    State L(lua_newstate(FailingAllocator::alloc, &allocator), lua_close);
    REQUIRE(L);
    lua_gc(L.get(), LUA_GCSTOP, 0);
    auto source = newstore(L.get(), 4, 0, 0);
    memcpy(luaB_backingstoredata(source, NULL), "abcd", 4);
    unsigned hash = luaB_backingstorehash(L.get(), source);
    lua_rawcheckstack(L.get(), int(L->stack_last - L->top) - 1);
    while (L->top < L->stack_last - 1)
        lua_pushnil(L.get());
    Operation op;
    op.kind = Operation::CStrStore;
    allocator.fail = true;
    CHECK(run(L.get(), op) == LUA_ERRMEM);
    allocator.fail = false;
    lua_settop(L.get(), 1);
    CHECK(luaB_backingstorecachedcstr(source) == nullptr);
    CHECK(luaB_backingstorehash(L.get(), source) == hash);
    collect(L.get());
}

TEST_CASE("Copy allocation failure preserves the source and its cached representation")
{
    FailingAllocator allocator;
    State L(lua_newstate(FailingAllocator::alloc, &allocator), lua_close);
    REQUIRE(L);
    lua_gc(L.get(), LUA_GCSTOP, 0);
    auto source = newstore(L.get(), 20000, 0, 0);
    memset(luaB_backingstoredata(source, NULL), 'x', luaB_backingstorelen(source));
    unsigned hash = luaB_backingstorehash(L.get(), source);
    auto cached = pushcstr(L.get(), 1);
    lua_pop(L.get(), 1);
    lua_rawcheckstack(L.get(), 10);
    Operation op;
    op.kind = Operation::Copy;
    op.terminated = true;
    allocator.fail = true;
    CHECK(run(L.get(), op) == LUA_ERRMEM);
    allocator.fail = false;
    lua_settop(L.get(), 1);
    CHECK(luaB_backingstorecachedcstr(source) == cached);
    CHECK(luaB_backingstorehash(L.get(), source) == hash);
    CHECK_FALSE(luaB_backingstoreisterminated(source));
    CHECK(luaB_backingstoreisterminated(cached));
    CHECK(static_cast<char*>(luaB_backingstoredata(source, NULL))[0] == 'x');
    CHECK(static_cast<char*>(luaB_backingstoredata(source, NULL))[19999] == 'x');
    collect(L.get());
}

TEST_CASE("All memory categories release descriptor and external byte accounting")
{
    int calls = 0;
    auto L = state();
    auto t = tag(L.get());
    collect(L.get());
    size_t baseline = L->global->totalbytes;
    char bytes[16] = {};
    for (int category = 0; category < LUA_MEMORY_CATEGORIES; ++category)
    {
        lua_setmemcat(L.get(), category);
        size_t before = L->global->memcatbytes[category];
        auto store = external(L.get(), bytes, sizeof(bytes), t, &calls);
        CHECK(L->global->memcatbytes[category] == before + luaB_backingstoresize(L.get(), store) + sizeof(bytes));
        lua_pop(L.get(), 1);
        collect(L.get());
        CHECK(L->global->memcatbytes[category] == before);
    }
    CHECK(calls == LUA_MEMORY_CATEGORIES);
    CHECK(L->global->totalbytes == baseline);
}

TEST_CASE("Allocation hooks cannot change a backing store's charged memory category")
{
    int calls = 0;
    auto L = state();
    lua_gc(L.get(), LUA_GCSTOP, 0);
    auto source = newstore(L.get(), 4, 0, 0);
    auto t = tag(L.get());
    lua_rawcheckstack(L.get(), 10);
    collect(L.get());
    for (int kind = 0; kind < 3; ++kind)
    {
        CAPTURE(kind);
        const int charged = 41;
        const int changed = 73;
        size_t before = lua_totalbytes(L.get(), charged);
        size_t other = lua_totalbytes(L.get(), changed);
        lua_setmemcat(L.get(), charged);
        lua_callbacks(L.get())->onallocate = [](lua_State* L, size_t, size_t) { lua_setmemcat(L, 73); };
        BackingStore* store;
        if (kind == 0)
            store = newstore(L.get(), 16, 0, 1);
        else if (kind == 1)
            store = external(L.get(), luaB_backingstoredata(source, NULL), 4, t, &calls);
        else
            store = newslice(L.get(), 1, 1, 2);
        lua_callbacks(L.get())->onallocate = nullptr;
        lua_setmemcat(L.get(), 0);
        CHECK(store->memcat == charged);
        CHECK(lua_totalbytes(L.get(), charged) == before + luaB_backingstoresize(L.get(), store) + (kind == 1 ? 4 : 0));
        CHECK(lua_totalbytes(L.get(), changed) == other);
        lua_pop(L.get(), 1);
        collect(L.get());
        CHECK(lua_totalbytes(L.get(), charged) == before);
        CHECK(lua_totalbytes(L.get(), changed) == other);
    }
    CHECK(calls == 1);
}

TEST_CASE("Shutdown sweeps shared roots and caches without dependency ordering")
{
    ScopedFastFlag refs(FFlag::LuwuManagedReferences2, true);
    int calls = 0;
    char bytes[] = "hello world";
    {
        auto L = state();
        auto root = external(L.get(), bytes, 11, tag(L.get()), &calls);
        luaB_backingstorecstr(L.get(), root);
        for (int i = 0; i < 100; ++i)
        {
            auto slice = newslice(L.get(), 1, i % 5, 3);
            luaB_backingstorecstr(L.get(), slice);
            lua_refpool(L.get(), -1);
            lua_pop(L.get(), 1);
        }
        CHECK(calls == 0);
    }
    CHECK(calls == 1);
}

#ifndef LUWU_BACKINGSTORE_VM_ONLY
TEST_CASE("Backing-store Lua behavior in interpreter and native code")
{
    for (bool native : {false, true})
    {
        if (native && !luau_codegen_supported()) continue;
        auto L = state();
        luaL_openlibs(L.get());
        if (native) luau_codegen_create(L.get());
        auto a = newstore(L.get(), 4, 0, 0);
        lua_setglobal(L.get(), "a");
        lua_newbackingstore(L.get(), 4, 0, 0);
        lua_setglobal(L.get(), "b");
        lua_newtable(L.get());
        for (int i = 1; i <= 1000; ++i)
        {
            lua_newbackingstore(L.get(), 0, 0, 0);
            lua_rawseti(L.get(), -2, i);
        }
        lua_setglobal(L.get(), "keys");
        execute(L.get(), R"(
            assert(type(a) == "userdata" and typeof(a) == "userdata")
            assert(a == a and a ~= b and rawequal(a, a) and not rawequal(a, b))
            assert(a and not not a)
            assert(getmetatable(a) == nil)
            assert(string.find(tostring(a), "userdata"))
            assert(not pcall(function() return a.x end))
            assert(not pcall(function() a.x = 1 end))
            assert(not pcall(function() return #a end))
            assert(not pcall(function() return a + b end))
            assert(not pcall(function() return a() end))
            assert(not pcall(function() setmetatable(a, {}) end))
            local map = {}
            for i, key in keys do map[key] = i end
            for i, key in keys do assert(map[key] == i) end
            local count = 0
            for key, i in map do assert(keys[i] == key) count += 1 end
            assert(count == 1000)
            for i = 1, 500 do map[keys[i]] = nil end
            for i = 501, 1000 do assert(map[keys[i]] == i) end
            local t = { answer = 42 }
            for i = 1, 100 do assert(t.answer == 42) end
        )", native);
        CHECK_FALSE(lua_isbackingstore(L.get(), 1));
        lua_getglobal(L.get(), "a");
        CHECK(at(L.get(), -1) == a);
        collect(L.get());
    }
}

TEST_CASE("Weak table entries do not root backing stores")
{
    int calls = 0;
    auto L = state();
    luaL_openlibs(L.get());
    external(L.get(), nullptr, 0, tag(L.get()), &calls);
    lua_setglobal(L.get(), "value");
    execute(L.get(), "weak = setmetatable({}, {__mode='kv'}); weak[value] = value; value = nil", false);
    collect(L.get());
    CHECK(calls == 1);
    execute(L.get(), "assert(next(weak) == nil)", false);
}
#endif
}
