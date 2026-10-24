// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "lobject.h"

// Kind 0 is VM-owned, 1..14 are external tags, and 15 is a slice.
enum
{
    LUA_BSTORE_KIND_MASK = 15,
    LUA_BSTORE_SLICE = LUA_BSTORE_KIND_MASK,
    LUA_BSTORE_IMMUTABLE = 1 << 4,
    LUA_BSTORE_HASH_VALID = 1 << 5,
    LUA_BSTORE_CSTR_COPY = 1 << 6,
    LUA_BSTORE_TERMINATED = 1 << 7,
    LUA_BSTORE_MAX_EXTERNAL_TAGS = LUA_BSTORE_SLICE - 1,
};

typedef struct alignas(8) BackingStore
{
    CommonHeader;
    // Low to high: 4-bit kind, immutability, hash validity, C-string copy, termination.
    // Termination describes this exact range, independently of ownership and mutability.
    uint8_t flagsandtag;
    uint32_t length;
    union
    {
        struct
        {
            uint32_t hash;
            // Slice offset into its root. Once a slice caches a C string, its copy carries the offset instead.
            uint32_t aux;
        } h;
        BackingStore* cstrcopy; // traced terminated copy, also holds this store's hash
    };
} BackingStore;

// The common header must be first in every allocation.
typedef struct OwnedBackingStore
{
    BackingStore header;
    alignas(8) char bytes[1];
} OwnedBackingStore;

typedef struct alignas(8) ExternalBackingStore
{
    BackingStore header;
    void* data;
} ExternalBackingStore;

// The offset lives in the header's spare hash half; see BackingStore::h.aux.
typedef struct alignas(8) SliceBackingStore
{
    BackingStore header;
    BackingStore* root; // ultimate owner; nested slices combine offsets
} SliceBackingStore;

typedef struct BackingStoreState
{
    lua_BackingStoreFree externalcallbacks[LUA_BSTORE_MAX_EXTERNAL_TAGS];
    uint32_t externaluserdatasizes[LUA_BSTORE_MAX_EXTERNAL_TAGS];
} BackingStoreState;

static_assert(offsetof(BackingStore, tt) == offsetof(GCheader, tt) &&
    offsetof(BackingStore, marked) == offsetof(GCheader, marked) &&
    offsetof(BackingStore, memcat) == offsetof(GCheader, memcat), "Backing stores must begin with the common GC header");
static_assert(sizeof(BackingStore) == 16, "Backing-store headers must remain compact");
static_assert(offsetof(OwnedBackingStore, header) == 0 && offsetof(ExternalBackingStore, header) == 0 &&
    offsetof(SliceBackingStore, header) == 0, "GC headers must begin each allocation");
static_assert(offsetof(OwnedBackingStore, bytes) == sizeof(BackingStore), "Owned payload follows the header");
static_assert(sizeof(ExternalBackingStore) == ((sizeof(BackingStore) + sizeof(void*) + 7) / 8) * 8,
    "External prefixes must contain only the header and data pointer");
static_assert(sizeof(SliceBackingStore) == ((sizeof(BackingStore) + sizeof(void*) + 7) / 8) * 8,
    "Slices must contain only the header and root");

LUAI_FUNC void luaB_initbackingstorestate(lua_State* L);

// Up to 14 fresh registrations per VM, returning tags 1..14. Inline userdata is 8-byte aligned.
// The non-null cleanup callback must not raise, allocate Lua objects, run Lua, or resurrect the store.
LUAI_FUNC uint32_t luaB_registerbackingstoretype(lua_State* L, lua_BackingStoreFree freecb, size_t userdatasize);

// Internal factories do not run a GC step or root their result. Root it before an operation that can collect.
// Owned bytes are zero-initialized; termination reserves a NUL outside the logical length.
LUAI_FUNC BackingStore* luaB_newbackingstore(lua_State* L, size_t len, bool mutable_, bool terminated);
// Failures leave ownership with the caller; the public API transfers it. Copies the tag's userdata size
// from userdata, which may be NULL only for zero-sized userdata.
// If terminated, data must be non-null and data[len] must remain a readable NUL for the store's lifetime.
LUAI_FUNC BackingStore* luaB_newexternalbackingstore(
    lua_State* L, void* data, size_t len, uint32_t tag, bool mutable_, bool terminated, const void* userdata);
// Always makes an independent owned copy with the requested mutability and termination.
LUAI_FUNC BackingStore* luaB_copybackingstore(lua_State* L, const BackingStore* source, bool mutable_, bool terminated);
// Zero-copy bounded range, inheriting mutability and retaining the ultimate owner through tracing.
LUAI_FUNC BackingStore* luaB_slicebackingstore(lua_State* L, BackingStore* source, size_t offset, size_t len);

// Collector-only cleanup; does not dereference edges, which may already have been swept.
LUAI_FUNC void luaB_freebackingstore(lua_State* L, BackingStore* store, struct lua_Page* page);
LUAI_FUNC size_t luaB_backingstoresize(lua_State* L, const BackingStore* store);
LUAI_FUNC void* luaB_backingstoreuserdata(lua_State* L, const BackingStore* store);
// Immutable stores cache even a zero hash; mutable stores always hash the current bytes.
LUAI_FUNC unsigned int luaB_backingstorehash(lua_State* L, BackingStore* store);
// Returns a terminated representation: self if terminated, otherwise a cached immutable copy
// for immutable sources or a fresh immutable snapshot for mutable sources. Does not run a GC step;
// root the result before GC. Allocation failure leaves the source and any cached hash unchanged.
LUAI_FUNC BackingStore* luaB_backingstorecstr(lua_State* L, BackingStore* store);

inline bool luaB_backingstoreismutable(const BackingStore* store)
{
    return (store->flagsandtag & LUA_BSTORE_IMMUTABLE) == 0;
}

// Guaranteed termination of the original bytes; a cached C-string copy does not count.
inline bool luaB_backingstoreisterminated(const BackingStore* store)
{
    return (store->flagsandtag & LUA_BSTORE_TERMINATED) != 0;
}

inline bool luaB_backingstoreisexternal(const BackingStore* store)
{
    uint8_t kind = store->flagsandtag & LUA_BSTORE_KIND_MASK;
    return kind != 0 && kind != LUA_BSTORE_SLICE;
}

inline bool luaB_backingstoreisslice(const BackingStore* store)
{
    return (store->flagsandtag & LUA_BSTORE_KIND_MASK) == LUA_BSTORE_SLICE;
}

// Registered tag of an external owner, or 0 for VM-owned stores and slices (not their root's tag).
inline uint32_t luaB_backingstoretag(const BackingStore* store)
{
    return luaB_backingstoreisexternal(store) ? store->flagsandtag & LUA_BSTORE_KIND_MASK : 0;
}

// Ultimate owner: a slice's root, otherwise the store itself. Owners are never slices.
inline BackingStore* luaB_backingstoreroot(const BackingStore* store)
{
    return luaB_backingstoreisslice(store) ? reinterpret_cast<const SliceBackingStore*>(store)->root : const_cast<BackingStore*>(store);
}

inline size_t luaB_backingstoreoffset(const BackingStore* store)
{
    if (!luaB_backingstoreisslice(store))
        return 0;
    // Caching a C string replaces the hash half with the copy pointer; the copy keeps the offset.
    return (store->flagsandtag & LUA_BSTORE_CSTR_COPY) ? store->cstrcopy->h.aux : store->h.aux;
}

// Borrowed immutable, terminated representation, or NULL; never creates a copy.
inline BackingStore* luaB_backingstorecachedcstr(const BackingStore* store)
{
    return (store->flagsandtag & LUA_BSTORE_CSTR_COPY) ? store->cstrcopy : NULL;
}

inline bool luaB_backingstorehashvalid(const BackingStore* store)
{
    return (store->flagsandtag & LUA_BSTORE_HASH_VALID) != 0;
}

inline size_t luaB_backingstorelen(const BackingStore* store)
{
    return store->length;
}

inline void* luaB_backingstoredata(const BackingStore* store, size_t* len)
{
    if (len)
        *len = store->length;
    const BackingStore* owner = luaB_backingstoreroot(store);
    char* bytes = luaB_backingstoreisexternal(owner) ? static_cast<char*>(reinterpret_cast<const ExternalBackingStore*>(owner)->data)
                                                     : const_cast<char*>(reinterpret_cast<const OwnedBackingStore*>(owner)->bytes);
    return bytes + luaB_backingstoreoffset(store);
}
