// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "lbackingstore.h"

#include "lapi.h"
#include "ldebug.h"
#include "ldo.h"
#include "lgc.h"
#include "lmem.h"
#include "lstate.h"
#include "lstring.h"

#include <string.h>

static const size_t MAX_OWNED_BACKINGSTORE_SIZE = 1 << 30;
static const size_t MAX_BACKINGSTORE_SIZE = UINT32_MAX;

static uint32_t externalindex(lua_State* L, uint32_t tag)
{
    api_check(L, tag != 0 && tag <= LUA_BSTORE_MAX_EXTERNAL_TAGS);
    api_check(L, L->global->backingstores.externalcallbacks[tag - 1] != NULL);
    return tag - 1;
}

static uint32_t userdatasize(lua_State* L, const BackingStore* store)
{
    return L->global->backingstores.externaluserdatasizes[externalindex(L, luaB_backingstoretag(store))];
}

// Does not run a GC step; the result is unrooted.
static BackingStore* newstore(lua_State* L, size_t size, uint8_t flags, size_t len, uint32_t aux)
{
    uint8_t memcat = L->activememcat;
    BackingStore* store = luaM_newgco(L, BackingStore, size, memcat);
    luaC_init(L, store, LUA_TBACKINGSTORE);
    // Allocation hooks may change the active category after the allocation was charged.
    store->memcat = memcat;
    store->flagsandtag = flags;
    store->length = uint32_t(len);
    store->h.hash = 0;
    store->h.aux = aux;
    return store;
}

static uint8_t storeflags(bool mutable_, bool terminated)
{
    return (mutable_ ? 0 : LUA_BSTORE_IMMUTABLE) | (terminated ? LUA_BSTORE_TERMINATED : 0);
}

uint32_t luaB_registerbackingstoretype(lua_State* L, lua_BackingStoreFree freecb, size_t userdatasize)
{
    if (!freecb)
        luaG_runerror(L, "external backing store requires a free callback");
    if (userdatasize > MAX_OWNED_BACKINGSTORE_SIZE - sizeof(ExternalBackingStore))
        luaM_toobig(L, "backing store userdata too big", MAX_OWNED_BACKINGSTORE_SIZE);
    BackingStoreState* state = &L->global->backingstores;
    for (uint32_t i = 0; i < LUA_BSTORE_MAX_EXTERNAL_TAGS; ++i)
    {
        if (!state->externalcallbacks[i])
        {
            state->externalcallbacks[i] = freecb;
            state->externaluserdatasizes[i] = uint32_t(userdatasize);
            return i + 1;
        }
    }
    luaG_runerror(L, "too many backing store tags");
}

void* luaB_backingstoreuserdata(lua_State* L, const BackingStore* store)
{
    if (!luaB_backingstoreisexternal(store) || userdatasize(L, store) == 0)
        return NULL;
    return reinterpret_cast<char*>(const_cast<BackingStore*>(store)) + sizeof(ExternalBackingStore);
}

BackingStore* luaB_newbackingstore(lua_State* L, size_t len, bool mutable_, bool terminated)
{
    if (len > MAX_OWNED_BACKINGSTORE_SIZE)
        luaM_toobig(L, "backing store too big", MAX_OWNED_BACKINGSTORE_SIZE);
    size_t payload = len + (terminated ? 1 : 0);
    BackingStore* store = newstore(L, offsetof(OwnedBackingStore, bytes) + payload, storeflags(mutable_, terminated), len, 0);
    memset(reinterpret_cast<OwnedBackingStore*>(store)->bytes, 0, payload);
    return store;
}

BackingStore* luaB_newexternalbackingstore(
    lua_State* L, void* data, size_t len, uint32_t tag, bool mutable_, bool terminated, const void* userdata)
{
    uint32_t udsize = L->global->backingstores.externaluserdatasizes[externalindex(L, tag)];
    api_check(L, userdata != NULL || udsize == 0);
    if (!data && (len != 0 || terminated))
        luaG_runerror(L, "null backing store data");
    // Catches a missing terminator at creation; the host must keep it in place afterwards.
    api_check(L, !terminated || static_cast<const char*>(data)[len] == '\0');
    if (len > MAX_BACKINGSTORE_SIZE)
        luaM_toobig(L, "external backing store too big", MAX_BACKINGSTORE_SIZE);
    size_t size = sizeof(ExternalBackingStore) + udsize;
    size_t available = SIZE_MAX - L->global->totalbytes;
    if (available < size || len > available - size)
        luaG_runerror(L, "external backing store accounting overflow");

    // Allocation failure leaves the owner with the caller. No throwing work follows this allocation.
    BackingStore* store = newstore(L, size, uint8_t(tag) | storeflags(mutable_, terminated), len, 0);
    reinterpret_cast<ExternalBackingStore*>(store)->data = data;
    if (udsize)
        memcpy(reinterpret_cast<char*>(store) + sizeof(ExternalBackingStore), userdata, udsize);
    L->global->totalbytes += len;
    L->global->memcatbytes[store->memcat] += len;
    return store;
}

BackingStore* luaB_copybackingstore(lua_State* L, const BackingStore* source, bool mutable_, bool terminated)
{
    BackingStore* copy = luaB_newbackingstore(L, source->length, mutable_, terminated);
    if (source->length)
        memcpy(luaB_backingstoredata(copy, NULL), luaB_backingstoredata(source, NULL), source->length);
    return copy;
}

BackingStore* luaB_slicebackingstore(lua_State* L, BackingStore* source, size_t offset, size_t len)
{
    if (offset > source->length || len > source->length - offset)
        luaG_runerror(L, "backing store slice out of bounds");
    BackingStore* root = luaB_backingstoreroot(source);
    uint8_t flags = LUA_BSTORE_SLICE | (luaB_backingstoreismutable(root) ? 0 : LUA_BSTORE_IMMUTABLE);
    BackingStore* store = newstore(L, sizeof(SliceBackingStore), flags, len, uint32_t(offset + luaB_backingstoreoffset(source)));
    reinterpret_cast<SliceBackingStore*>(store)->root = root;
    return store;
}

size_t luaB_backingstoresize(lua_State* L, const BackingStore* store)
{
    if (luaB_backingstoreisslice(store))
        return sizeof(SliceBackingStore);
    if (luaB_backingstoreisexternal(store))
        return sizeof(ExternalBackingStore) + userdatasize(L, store);
    return offsetof(OwnedBackingStore, bytes) + store->length + (luaB_backingstoreisterminated(store) ? 1 : 0);
}

void luaB_freebackingstore(lua_State* L, BackingStore* store, lua_Page* page)
{
    size_t size = luaB_backingstoresize(L, store);
    if (luaB_backingstoreisexternal(store))
    {
        lua_BackingStoreFree callback = L->global->backingstores.externalcallbacks[externalindex(L, luaB_backingstoretag(store))];
        L->global->totalbytes -= store->length;
        L->global->memcatbytes[store->memcat] -= store->length;
        callback(L, luaB_backingstoredata(store, NULL), store->length, luaB_backingstoreuserdata(L, store));
    }
    // Edges belong to the collector; root/cache may already have been swept.
    luaM_freegco(L, store, size, store->memcat, page);
}

unsigned int luaB_backingstorehash(lua_State* L, BackingStore* store)
{
    (void)L;
    if (luaB_backingstoreismutable(store))
        return luaS_hash(static_cast<const char*>(luaB_backingstoredata(store, NULL)), store->length);
    // A cached C-string copy holds the hash, since the copy pointer occupies this store's hash.
    BackingStore* holder = luaB_backingstorecachedcstr(store);
    if (!holder)
        holder = store;
    if (!luaB_backingstorehashvalid(holder))
    {
        holder->h.hash = luaS_hash(static_cast<const char*>(luaB_backingstoredata(holder, NULL)), holder->length);
        holder->flagsandtag |= LUA_BSTORE_HASH_VALID;
    }
    return holder->h.hash;
}

BackingStore* luaB_backingstorecstr(lua_State* L, BackingStore* store)
{
    if (luaB_backingstoreisterminated(store))
        return store;
    bool immutable = !luaB_backingstoreismutable(store);
    if (immutable && luaB_backingstorecachedcstr(store))
        return store->cstrcopy;
    if (immutable && luaB_backingstoreisslice(store))
    {
        const char* bytes = static_cast<const char*>(luaB_backingstoredata(store, NULL));
        BackingStore* root = luaB_backingstoreroot(store);
        size_t offset = luaB_backingstoreoffset(store);
        size_t len = store->length;
        LUAU_ASSERT(offset <= root->length && len <= root->length - offset);
        size_t remaining = root->length - offset;
        // Interior endpoints are readable; the root endpoint requires a reserved terminator.
        if ((len < remaining || (luaB_backingstoreisterminated(root) && len == remaining)) && bytes[len] == '\0')
        {
            store->flagsandtag |= LUA_BSTORE_TERMINATED;
            return store;
        }
    }
    BackingStore* copy = luaB_copybackingstore(L, store, false, true);
    if (immutable)
    {
        // Preserve an existing hash and slice offset across the union transition. Snapshots remain independent.
        if (luaB_backingstorehashvalid(store))
        {
            copy->h.hash = store->h.hash;
            copy->flagsandtag |= LUA_BSTORE_HASH_VALID;
        }
        copy->h.aux = store->h.aux;
        store->cstrcopy = copy;
        store->flagsandtag = (store->flagsandtag & ~LUA_BSTORE_HASH_VALID) | LUA_BSTORE_CSTR_COPY;
        luaC_objbarrier(L, store, copy);
    }
    return copy;
}

void luaB_initbackingstorestate(lua_State* L)
{
    memset(&L->global->backingstores, 0, sizeof(L->global->backingstores));
}

/*
** C API
*/

// NULL for values other than backing stores.
static BackingStore* tostore(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    return o && ttisbackingstore(o) ? backingstorevalue(o) : NULL;
}

static BackingStore* storeat(lua_State* L, int idx)
{
    const TValue* o = luaA_toobject(L, idx);
    api_check(L, o != NULL && ttisbackingstore(o));
    return backingstorevalue(o);
}

static void preparestack(lua_State* L)
{
    // Reserve before ownership transfer; no throwing/collecting work follows allocation.
    lua_rawcheckstack(L, 1);
    luaC_checkGC(L);
    luaC_threadbarrier(L);
}

static void* pushstore(lua_State* L, BackingStore* store, size_t* len = NULL)
{
    setbackingstorevalue(L, L->top, store);
    ++L->top;
    return luaB_backingstoredata(store, len);
}

uint32_t lua_registerbackingstoretype(lua_State* L, lua_BackingStoreFree freecb, size_t userdatasize)
{
    return luaB_registerbackingstoretype(L, freecb, userdatasize);
}

void* lua_newbackingstore(lua_State* L, size_t len, int mutable_, int terminated)
{
    preparestack(L);
    return pushstore(L, luaB_newbackingstore(L, len, mutable_ != 0, terminated != 0));
}

void* lua_newexternalbackingstore(lua_State* L, void* data, size_t len, uint32_t tag, int mutable_, int terminated, const void* userdata)
{
    // Without a valid tag there is no callback to take ownership; every later failure frees data.
    uint32_t index = externalindex(L, tag);
    const BackingStoreState* state = &L->global->backingstores;
    void* failureuserdata = state->externaluserdatasizes[index] ? const_cast<void*>(userdata) : NULL;

    struct NewContext
    {
        void* data;
        size_t len;
        uint32_t tag;
        bool mutable_;
        bool terminated;
        const void* userdata;
        BackingStore* store;

        static void run(lua_State* L, void* ud)
        {
            NewContext* ctx = static_cast<NewContext*>(ud);
            preparestack(L);
            ctx->store = luaB_newexternalbackingstore(L, ctx->data, ctx->len, ctx->tag, ctx->mutable_, ctx->terminated, ctx->userdata);
        }
    } ctx = {data, len, tag, mutable_ != 0, terminated != 0, userdata, NULL};

    int status = luaD_rawrunprotected(L, &NewContext::run, &ctx);
    if (status != 0)
    {
        state->externalcallbacks[index](L, data, len, failureuserdata);
        luaD_throw(L, status);
    }
    // The stack slot was reserved inside the protected region.
    return pushstore(L, ctx.store);
}

void* lua_slicebackingstore(lua_State* L, int idx, size_t offset, size_t len)
{
    BackingStore* source = storeat(L, idx);
    preparestack(L);
    return pushstore(L, luaB_slicebackingstore(L, source, offset, len));
}

void* lua_copybackingstore(lua_State* L, int idx, int mutable_, int terminated)
{
    BackingStore* source = storeat(L, idx);
    preparestack(L);
    return pushstore(L, luaB_copybackingstore(L, source, mutable_ != 0, terminated != 0));
}

const char* lua_backingstorecstr(lua_State* L, int idx, size_t* len)
{
    BackingStore* source = storeat(L, idx);
    preparestack(L);
    return static_cast<const char*>(pushstore(L, luaB_backingstorecstr(L, source), len));
}

void* lua_tobackingstore(lua_State* L, int idx, size_t* len)
{
    const BackingStore* store = tostore(L, idx);
    return store ? luaB_backingstoredata(store, len) : NULL;
}

void* lua_getbackingstoreuserdata(lua_State* L, int idx)
{
    const BackingStore* store = tostore(L, idx);
    return store ? luaB_backingstoreuserdata(L, store) : NULL;
}

uint32_t lua_backingstoretag(lua_State* L, int idx)
{
    const BackingStore* store = tostore(L, idx);
    return store ? luaB_backingstoretag(store) : 0;
}

int lua_isbackingstoreexternal(lua_State* L, int idx)
{
    const BackingStore* store = tostore(L, idx);
    return store && luaB_backingstoreisexternal(store);
}

int lua_isbackingstoremutable(lua_State* L, int idx)
{
    const BackingStore* store = tostore(L, idx);
    return store && luaB_backingstoreismutable(store);
}

int lua_isbackingstoreterminated(lua_State* L, int idx)
{
    const BackingStore* store = tostore(L, idx);
    return store && luaB_backingstoreisterminated(store);
}

int lua_isbackingstoreslice(lua_State* L, int idx)
{
    const BackingStore* store = tostore(L, idx);
    return store && luaB_backingstoreisslice(store);
}

void lua_getbackingstoreroot(lua_State* L, int idx)
{
    BackingStore* store = storeat(L, idx);
    lua_rawcheckstack(L, 1);
    luaC_threadbarrier(L);
    pushstore(L, luaB_backingstoreroot(store));
}

size_t lua_backingstoreoffset(lua_State* L, int idx)
{
    const BackingStore* store = tostore(L, idx);
    return store ? luaB_backingstoreoffset(store) : 0;
}
