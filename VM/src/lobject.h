// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#pragma once

#include "lua.h"
#include "lcommon.h"

/*
** Union of all collectible objects
*/
typedef union GCObject GCObject;

/*
** Common Header for all collectible objects (in macro form, to be included in other objects)
*/
// clang-format off
#define CommonHeader \
     uint8_t tt; uint8_t marked; uint8_t memcat
// clang-format on

/*
** Common header in struct form
*/
typedef struct GCheader
{
    CommonHeader;
} GCheader;

/*
** Union of all Lua values
*/
typedef union
{
    GCObject* gc;
    void* p;
    double n;
    int b;
    int64_t l;
    float v[2]; // v[0], v[1] live here; v[2] lives in TValue::extra
} Value;

/*
** Tagged Values
*/

typedef struct lua_TValue
{
    Value value;
    int extra[LUA_EXTRA_SIZE];
    int tt;
} TValue;

#if LUA_VECTOR_SIZE == 4
#define condvector4(vec4expr, vec3expr) vec4expr
#else
#define condvector4(vec4expr, vec3expr) vec3expr
#endif

#if LUA_VECTOR_DOUBLE == 1
#define condvectordouble(vecdoubleexpr, vecfloatexpr) vecdoubleexpr
#else
#define condvectordouble(vecdoubleexpr, vecfloatexpr) vecfloatexpr
#endif

// Macros to test type
#define ttisnil(o) (ttype(o) == LUA_TNIL)
#define ttissymnone(o) (ttype(o) == LUA_TSYMNONE)
#define ttisnumber(o) (ttype(o) == LUA_TNUMBER)
#define ttisinteger(o) (ttype(o) == LUA_TINTEGER)
#define ttisstring(o) (ttype(o) == LUA_TSTRING)
#define ttistable(o) (ttype(o) == LUA_TTABLE)
#define ttisfunction(o) (ttype(o) == LUA_TFUNCTION)
#define ttisboolean(o) (ttype(o) == LUA_TBOOLEAN)
#define ttisuserdata(o) (ttype(o) == LUA_TUSERDATA)
#define ttisthread(o) (ttype(o) == LUA_TTHREAD)
#define ttisbuffer(o) (ttype(o) == LUA_TBUFFER)
#define ttislightuserdata(o) (ttype(o) == LUA_TLIGHTUSERDATA)
#define ttisvector(o) (ttype(o) == LUA_TVECTOR)
#define ttisupval(o) (ttype(o) == LUA_TUPVAL)
#define ttisclass(o) (ttype(o) == LUA_TCLASS)
#define ttisobject(o) (ttype(o) == LUA_TOBJECT)

// Macros to access values
#define ttype(o) ((o)->tt)
#define gcvalue(o) check_exp(iscollectable(o), (o)->value.gc)
#define pvalue(o) check_exp(ttislightuserdata(o), (o)->value.p)
#define nvalue(o) check_exp(ttisnumber(o), (o)->value.n)
#define lvalue(o) check_exp(ttisinteger(o), (o)->value.l)
#define vvalue(o) check_exp(ttisvector(o), condvectordouble((o)->value.gc->vec.v, (o)->value.v))
#define tsvalue(o) check_exp(ttisstring(o), &(o)->value.gc->ts)
#define uvalue(o) check_exp(ttisuserdata(o), &(o)->value.gc->u)
#define clvalue(o) check_exp(ttisfunction(o), &(o)->value.gc->cl)
#define hvalue(o) check_exp(ttistable(o), &(o)->value.gc->h)
#define bvalue(o) check_exp(ttisboolean(o), (o)->value.b)
#define thvalue(o) check_exp(ttisthread(o), &(o)->value.gc->th)
#define bufvalue(o) check_exp(ttisbuffer(o), &(o)->value.gc->buf)
#define upvalue(o) check_exp(ttisupval(o), &(o)->value.gc->uv)
#define classvalue(o) check_exp(ttisclass(o), &(o)->value.gc->lclass)
#define objectvalue(o) check_exp(ttisobject(o), &(o)->value.gc->lobject)

#define l_isfalse(o) (ttisnil(o) || (ttisboolean(o) && bvalue(o) == 0) || ttissymnone(o))

#define lightuserdatatag(o) check_exp(ttislightuserdata(o), (o)->extra[0])

// Internal tags used by the VM
#define LU_TAG_ITERATOR LUA_UTAG_LIMIT

/*
** for internal debug only
*/
#define checkconsistency(obj) LUAU_ASSERT(!iscollectable(obj) || (ttype(obj) == (obj)->value.gc->gch.tt))

#define checkliveness(g, obj) LUAU_ASSERT(!iscollectable(obj) || ((ttype(obj) == (obj)->value.gc->gch.tt) && !isdead(g, (obj)->value.gc)))

// Macros to set values
#define setnilvalue(obj) ((obj)->tt = LUA_TNIL)
#define setsymnonevalue(obj) ((obj)->tt = LUA_TSYMNONE)

#define setnvalue(obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.n = (x); \
        i_o->tt = LUA_TNUMBER; \
    }

#define setlvalue(obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.l = (x); \
        i_o->tt = LUA_TINTEGER; \
    }

#if LUA_VECTOR_DOUBLE == 1
#define setvvalue(L, obj, x, y, z, w) \
    { \
        TValue* i_o = (obj); \
        LuauVector* i_vec = luaVec_newvector((L), double(x), double(y), double(z), condvector4(double(w), 0)); \
        i_o->value.gc = cast_to(GCObject*, i_vec); \
        i_o->tt = LUA_TVECTOR; \
        checkliveness((L)->global, i_o); \
    }
#else
#define setvvalue(L, obj, x, y, z, w) \
    { \
        TValue* i_o = (obj); \
        float* i_v = i_o->value.v; \
        i_v[0] = float(x); \
        i_v[1] = float(y); \
        i_v[2] = float(z); \
        condvector4(i_v[3] = float(w), (void)(w)); \
        i_o->tt = LUA_TVECTOR; \
    }
#endif

#define setpvalue(obj, x, tag) \
    { \
        TValue* i_o = (obj); \
        i_o->value.p = (x); \
        i_o->extra[0] = (tag); \
        i_o->tt = LUA_TLIGHTUSERDATA; \
    }

#define setbvalue(obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.b = (x); \
        i_o->tt = LUA_TBOOLEAN; \
    }

#define setsvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TSTRING; \
        checkliveness(L->global, i_o); \
    }

#define setuvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TUSERDATA; \
        checkliveness(L->global, i_o); \
    }

#define setthvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TTHREAD; \
        checkliveness(L->global, i_o); \
    }

#define setbufvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TBUFFER; \
        checkliveness(L->global, i_o); \
    }

#define setclvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TFUNCTION; \
        checkliveness(L->global, i_o); \
    }

#define sethvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TTABLE; \
        checkliveness(L->global, i_o); \
    }

#define setptvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TPROTO; \
        checkliveness(L->global, i_o); \
    }

#define setupvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TUPVAL; \
        checkliveness(L->global, i_o); \
    }

#define setobj(L, obj1, obj2) \
    { \
        const TValue* o2 = (obj2); \
        TValue* o1 = (obj1); \
        *o1 = *o2; \
        checkliveness(L->global, o1); \
    }

#define setclassvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TCLASS; \
        checkliveness(L->global, i_o); \
    }


#define setobjectvalue(L, obj, x) \
    { \
        TValue* i_o = (obj); \
        i_o->value.gc = cast_to(GCObject*, (x)); \
        i_o->tt = LUA_TOBJECT; \
        checkliveness(L->global, i_o); \
    }

/*
** different types of sets, according to destination
*/

// to stack
#define setobj2s setobj
// from table to same table (no barrier)
#define setobjt2t setobj
// to table (needs barrier)
#define setobj2t setobj
// to new object (no barrier)
#define setobj2n setobj
// to object or static member (needs barrier)
#define setobj2class setobj

#define setttype(obj, tt) (ttype(obj) = (tt))

#define iscollectable(o) (ttype(o) >= LUA_TSTRING)

// wrapper for returning userdata properties directly
struct DirectFieldResult
{
    lua_State* L;
    TValue* slot;
};

typedef TValue* StkId; // index to stack elements

/*
** String headers for string table
*/
typedef struct TString
{
    CommonHeader;
    // 1 byte padding

    int16_t atom;
    uint8_t is_external;

    // 1 byte padding

    TString* next; // next string in the hash table bucket

    unsigned int hash;
    unsigned int len;

    union
    {
        char data[1]; // string data is allocated right after the header
        struct
        {
            char* dataptr; // points to external memory
            lua_StringFree free_cb;
            void* userdata;
        } ext;
    };
} TString;

#define getstr(ts) ((ts)->is_external ? (ts)->ext.dataptr : (ts)->data)
#define tsisinline(ts) (!((ts)->is_external))
#define getexternalmeta(ts) (&(ts)->ext)

#define svalue(o) getstr(tsvalue(o))

typedef struct Udata
{
    CommonHeader;

    uint8_t tag;

    int len;

    struct LuaTable* metatable;

    // userdata is allocated right after the header
    // while the alignment is only 8 here, for sizes starting at 16 bytes, 16 byte alignment is provided
    alignas(8) char data[1];
} Udata;

typedef struct LuauBuffer
{
    CommonHeader;
    // mode 0: standard internal buffer, memory in inline_data
    // mode 1: external buffer, immutable (cannot be written from Lua)
    // mode 2: external buffer, mutable (can be written from Lua)
    uint8_t mode;

    unsigned int len;
    char* data;
    void* userdata;
    lua_BufferFree free_cb;

    alignas(8) char inline_data[1];
} Buffer;

typedef struct LuauVector
{
    CommonHeader;
    // 1 byte padding

    LUA_VECTOR_TYPE v[LUA_VECTOR_SIZE];
} LuauVector;

enum FeedbackVectorSlotKind
{
    CALL_TARGET
};

struct FeedbackVectorSlot
{
    FeedbackVectorSlotKind kind;

    union
    {
        struct
        {
            uint32_t pc;
            uint32_t proto;
            uint32_t hits;
        } call_target;
    };
};

// Luwu try expressions: an error raised while the function runs [startpc, endpc) resumes at handlerpc, with the error
// in register target and where it was raised in the three registers after it (see LOP_WRAPRESULT)
struct LuauTryRange
{
    uint32_t startpc;
    uint32_t endpc;
    uint32_t handlerpc;
    uint8_t target;
};

/*
** Function Prototypes
*/
// clang-format off
// Luwu table presizing: what one NEWTABLE instruction learned about the tables it makes
typedef struct TableSite
{
    uint32_t pc;           // the NEWTABLE instruction's index in its proto's code
    int arrayhint;         // the array size the next table here starts with
    struct LuaTable* last; // the last table made here, weak: the GC clears it when that table dies (cleartablesites)
} TableSite;

typedef struct Proto
{
    CommonHeader;

    uint8_t nups; // number of upvalues
    uint8_t numparams;
    uint8_t is_vararg;
    uint8_t maxstacksize;
    uint8_t flags;

    TValue* k;              // constants used by the function
    Instruction* code;      // function bytecode
    struct Proto** p;       // functions defined inside the function
    const Instruction* codeentry;

    void* execdata;
    uintptr_t exectarget;

    uint8_t* lineinfo;      // for each instruction, line number as a delta from baseline
    int* abslineinfo;       // baseline line info, one entry for each 1<<linegaplog2 instructions; allocated after lineinfo
    struct LocVar* locvars; // information about local variables
    TString** upvalues;     // upvalue names
    TString* source;

    TString* debugname;
    uint8_t* debuginsn; // a copy of code[] array with just opcodes

    uint8_t* typeinfo;

    void* userdata;

    // Luwu Classes (rfcs/classes): the class this proto belongs to, or NULL. A proto belongs to a
    // class if it is one of the class's own methods (including `__init` and `__defaults`), or if it is
    // lexically nested anywhere inside one.
    //
    // It is set on the whole tree of nested protos when the method closure is registered
    // (luaR_addclassmember, luaR_stampownerclass). It is GC-marked in traverseproto.
    //
    // The interpreter (luaR_closureownsprivateaccess) and native codegen both use it to authorize
    // private and const member access without scanning the class's static members. Method protos and
    // the protos nested in them are unique per class, so `object->lclass ==
    // currentClosure->l.p->ownerclass` is exactly the "closure owns private access" test. A const write
    // additionally requires the closure to be the class's `__init`.
    struct LuauClass* ownerclass;

    GCObject* gclist;

    int sizecode;
    int sizep;
    int sizelocvars;
    int sizeupvalues;
    int sizek;
    int sizelineinfo;
    int linegaplog2;
    int linedefined;
    int bytecodeid;
    int sizetypeinfo;

    // Luwu table presizing: one TableSite per NEWTABLE instruction, in code order. Made when the proto is loaded.
    int sizetablesites;
    uint8_t tablesiteslisted; // whether this proto is on global_State::sitedprotos this GC cycle
    TableSite* tablesites;
    struct Proto* nextsited; // next proto on global_State::sitedprotos

    FeedbackVectorSlot* feedbackvec;
    uint32_t feedbackvecsize;
    uint32_t funid;
    Proto* optimized;
    Proto* deoptimized;
    uint64_t cost;

    // Luwu try expressions: the protected ranges, innermost first where they nest; NULL when there are none
    LuauTryRange* tryranges;
    int sizetryranges;
} Proto;
// clang-format on

typedef struct LocVar
{
    TString* varname;
    int startpc; // first point where variable is active
    int endpc;   // first point where variable is dead
    uint8_t reg; // register slot, relative to base, where variable is stored
} LocVar;

/*
** Upvalues
*/

typedef struct UpVal
{
    CommonHeader;
    uint8_t markedopen; // set if reachable from an alive thread (only valid during atomic)

    // 4 byte padding (x64)

    TValue* v; // points to stack or to its own value
    union
    {
        TValue value; // the value (when closed)
        struct
        {
            // global double linked list (when open)
            struct UpVal* prev;
            struct UpVal* next;

            // thread linked list (when open)
            struct UpVal* threadnext;
        } open;
    } u;
} UpVal;

#define upisopen(up) ((up)->v != &(up)->u.value)

/*
** Closures
*/

typedef struct Closure
{
    CommonHeader;

    uint8_t isC;
    uint8_t nupvalues;
    uint8_t stacksize;
    uint8_t preload;

    GCObject* gclist;
    struct LuaTable* env;

    union
    {
        struct
        {
            lua_CFunction f;
            lua_Continuation cont;
            const char* debugname;
            TValue upvals[1];
        } c;

        struct
        {
            lua_CFunction f;
            lua_Continuation cont;
            const char* debugname;
            void (*dtor)(struct lua_State* L, void* data, size_t sz);
            size_t size;
            // Luwu: the function lua_pushdatacfunctionk was given, which `f` (luaF_calldatacfunction) calls; NULL for other
            // fat closures
            int (*df)(struct lua_State* L, void* data, int nargs);
            alignas(8) char data[1];
        } fatc;

        struct
        {
            struct Proto* p;
            TValue uprefs[1];
        } l;
    };
} Closure;

#define iscfunction(o) (ttype(o) == LUA_TFUNCTION && clvalue(o)->isC)
#define isLfunction(o) (ttype(o) == LUA_TFUNCTION && !clvalue(o)->isC)

/*
** Tables
*/

typedef struct TKey
{
    ::Value value;
    int extra[LUA_EXTRA_SIZE];
    unsigned tt : 4;
    int next : 28; // for chaining
} TKey;

typedef struct LuaNode
{
    TValue val;
    TKey key;
} LuaNode;

// copy a value into a key
#define setnodekey(L, node, obj) \
    { \
        LuaNode* n_ = (node); \
        const TValue* i_o = (obj); \
        n_->key.value = i_o->value; \
        memcpy(n_->key.extra, i_o->extra, sizeof(n_->key.extra)); \
        n_->key.tt = i_o->tt; \
        checkliveness(L->global, i_o); \
    }

// copy a value from a key
#define getnodekey(L, obj, node) \
    { \
        TValue* i_o = (obj); \
        const LuaNode* n_ = (node); \
        i_o->value = n_->key.value; \
        memcpy(i_o->extra, n_->key.extra, sizeof(i_o->extra)); \
        i_o->tt = n_->key.tt; \
        checkliveness(L->global, i_o); \
    }

// clang-format off
typedef struct LuaTable
{
    CommonHeader;

    uint8_t tmcache;    // 1<<p means tagmethod(p) is not present
    uint8_t readonly;   // sandboxing feature to prohibit writes to table
    uint8_t safeenv;    // environment doesn't share globals with other scripts
    uint8_t lsizenode;  // log2 of size of `node' array
    uint8_t nodemask8;  // (1<<lsizenode)-1, truncated to 8 bits

    int sizearray; // size of `array' array
    union
    {
        int lastfree;  // any free position is before this position
        int aboundary; // negated 'boundary' of `array' array; iff aboundary < 0
    };

    struct LuaTable* metatable;
    TValue* array;  // array part
    LuaNode* node;
    GCObject* gclist;
} LuaTable;
// clang-format on

// Luwu collections: a frameless version of a builtin class's method, which the interpreter runs at the NAMECALL
// instead of calling the C method. It reads `self` and the `nargs` arguments at `args`, writes its results from `res`
// and returns how many, or returns -1 to leave the call to the C method. It never allocates, raises or calls Lua, so
// every error still comes from the C method.
typedef int (*luaR_FastMethod)(lua_State* L, struct LuauObject* self, StkId res, StkId args, int nargs);

typedef struct LuauClass
{
    CommonHeader;

    GCObject* gclist;

    TString* name;

    // Mapping from offset to static members (only methods for now).
    TValue* staticmembers;

    // Mapping from member name to offset.
    LuaTable* memberstooffset;

    // Mapping from offset to member name. NULL at `initoffset`: `__init` is only ever resolved by name
    // (see luaR_sealclassshape).
    TString** offsettomember;

    // Metatable for this *class object*. At time of writing this only contains
    // __call, but we may add more metamethods to class objects in the future.
    LuaTable* metatable;

    // Metatable for instances of this class. NULL until the first metamethod
    // is added via luaR_addclassmember.
    LuaTable* instancemetatable;

    // Number of instance members that we expect instances of this class object
    // to have.
    uint32_t numberofinstancemembers;

    // Total number of members that we expect this class object to have between
    // instance and static members.
    //
    // We store this number as an optimization. It's pretty rare that we need
    // to reference the specific number of static members, but it's very common
    // to reference the total number of members (for validating hot paths in
    // the interpreter) and the number of instance members (branching on
    // instance or static members, creating objects).
    uint32_t numberofallmembers;

    // Set when this class defines a user `__init` method. When true, the
    // class's constructor (`ClassName(...)`) calls `__init(self, ...)`
    // instead of the default POD table-copy constructor.
    bool hascustominit;

    // The member offset of `__init` (index `staticmembers` with it minus `numberofinstancemembers`).
    // Every class has an `__init` member; its slot holds a closure only when `hascustominit` is set.
    // Used by construction, the private-constructor check and the `const`-write check.
    uint32_t initoffset;

    // Set when this class's `__init` is the one a primary constructor implies (`class Cat(name)`),
    // i.e. it does nothing but assign fields from its parameters. That is what makes it sound for a
    // construction site to initialize the instance positionally and skip the call entirely -- see
    // LOP_NEWOBJECT's FIELDS form, which checks this before honoring that shape on a class with a
    // custom `__init`. Only ever set together with `hascustominit`.
    bool hasprimaryinit;

    // Per-member attribute bits, indexed by the same offset as `offsettomember` (see
    // LBC_CLASSMEMBER_* in Luau/Bytecode.h). Owned by this class object; freed in luaR_freeclass.
    uint8_t* memberflags;

    // True if any entry in `memberflags` has LBC_CLASSMEMBER_PRIVATE set. Lets a member access whose
    // slot is already cached skip the private-access check entirely for classes without one.
    bool hasprivatemembers;

    // True if any entry in `memberflags` has LBC_CLASSMEMBER_CONST set. Same idea as
    // `hasprivatemembers`, but for skipping the const-write check on SETTABLEKS.
    bool hasconstmembers;

    // True if this class has a synthesized `__defaults` static member: a niladic function,
    // compiled alongside POD classes with at least one field default, that returns every field's
    // default value (nil where unset) in declaration order. Only ever set when some field has a
    // default and there's no custom `__init` (see hascustominit).
    bool haspoddefaultsfn;

    // The offset of `__defaults` in `staticmembers`, only meaningful when haspoddefaultsfn is set.
    uint32_t poddefaultsoffset;

    // Luwu Traits (rfcs/classes/traits.md): a class without an `__init` that implements traits has a synthesized
    // `__implements(self)`. It evaluates the `implements` arguments and initializes the trait fields (LOP_INITTRAITS),
    // and the constructor calls it before the class's own field defaults. A class with an `__init` does the same work
    // at the top of `__init` instead, so it has none.
    bool haspodimplementsfn;

    // The offset of `__implements` in `staticmembers`, only meaningful when haspodimplementsfn is set.
    uint32_t podimplementsoffset;

    // Compile-time-constant field defaults: one TValue per instance member, in the same offset order
    // as `offsettomember`, nil for a member with no default. Copied straight into a new instance by
    // the POD constructor, which is why such a class needs no `__defaults` closure at all. NULL when
    // the class has no defaults, or when any default is a non-constant expression (that one has to be
    // re-evaluated per construction, so the whole class falls back to `__defaults`). Owned by this
    // class object; freed in luaR_freeclass and marked in traverseclass.
    TValue* memberdefaults;

    // Luwu Traits (rfcs/classes/traits.md): this class value is a trait. A trait is never constructed, and none of its members
    // can be read through it (the VM sets LBC_CLASSMEMBER_INITBLOCKED on all of them): implementing classes get copies.
    bool istrait;

    // Luwu Traits (rfcs/classes/traits.md): the class has an `implements` list that luaR_implementtraits hasn't finished implementing.
    // Implementing grows the layout, so an object made before then would be too small for it: construction refuses.
    // Code can reach the class that early -- its binding is set when its statement starts, and the statement runs user
    // code before implementing (an `__index` in an `implements` expression, a trait's `needs`). A class whose
    // implementing failed stays pending, so it never makes an object; one that succeeded is never implemented again.
    bool traitspending;

    // Luwu Traits (rfcs/classes/traits.md): the number of instance members the class itself declares. The fields of the
    // traits it implements come after them (see luaR_implementtraits); the class's `__defaults` returns only its own.
    uint32_t numberofownmembers;

    // Luwu Traits (rfcs/classes/traits.md): every trait this class implements, listed or implied through `needs`, in the
    // order luaR_implementtraits attached them. NULL when there are none. Owned by this class; marked in traverseclass.
    struct LuauClass** traits;
    uint32_t numtraits;

    // Luwu Traits (rfcs/classes/traits.md): how construction initializes the fields the class gets from its traits
    // (LOP_INITTRAITS, luaR_newtraitinitrunner). NULL when no trait has a field that needs computing. Owned by this class;
    // marked in traverseclass.
    // - `traitinits` holds the class's copy of each trait's `__traitinit`, in dependency order: a trait's needed traits
    //   come before it, so an overriding trait's value is the one left in the field.
    // - `traitinitargs` says which `implements` arguments each copy takes: two entries per copy, the offset into the
    //   class's `implements` arguments (all entries' arguments, in list order) and the count. A copy of a trait the
    //   class didn't list, or listed without arguments, takes none.
    TValue* traitinits;
    uint32_t* traitinitargs;
    uint32_t numtraitinits;

    // Luwu Traits (rfcs/classes/traits.md): the offsets of the overridden fields whose overriding value is a constant
    // default. The overridden trait's initializer writes the field by name after allocation copied the constant in, so
    // INITTRAITS copies these constants in again once the initializers have run. NULL when there are none. Owned by
    // this class.
    uint32_t* overriddenconsts;
    uint32_t numoverriddenconsts;

    // Luwu Traits (rfcs/classes/traits.md): the class's copies of the trait defaults it overrides, keyed by the trait's
    // own closure, for `Trait.method(obj)`, which runs the trait's default (luaR_traitmethod). NULL when the class
    // overrides none. Marked in traverseclass.
    struct LuaTable* traitdefaults;

    // Luwu Traits (rfcs/classes/traits.md): the copies of trait functions the compiler made for this class
    // (LBC_NEWCLASSMEMBER_TRAITCOPY), keyed by the trait's own closure, for luaR_implementtraits to use instead of copying
    // those functions. Only set while the class statement runs: implementing drops it. Marked in traverseclass.
    struct LuaTable* compiledtraitcopies;

    // Luwu collections: the number field `#` reads for a builtin container (vec, vecdeque, orderedmap) instead of
    // calling its `__len`, which returns the same number. LUAR_NO_LENGTH_FIELD for every other class.
    uint32_t lengthfield;

    // Luwu bundles: an object of this class is a table key by the values of its fields, not by its identity, so equal
    // bundles index the same entry (luaR_keyhash, luaR_keyequal). Only the bundle classes set it: their objects can't
    // change.
    bool keybyvalue;

    // Luwu collections: a builtin class's fast methods by member offset, with NULL for the members that have none. NULL
    // for every other class. Owned by this class.
    luaR_FastMethod* fastmethods;

    // Luwu Traits (rfcs/classes/traits.md): the class's version of trait members read through a trait (luaR_getqualified),
    // keyed by the trait, each a table from a member's name to its function: the class's qualified implementations
    // (`function Json.name`) and its copies of the defaults of scoped members (`function Self.name`). Until the class
    // implements its traits, it holds only the qualified implementations, unchecked. NULL when there are none. Marked in
    // traverseclass.
    struct LuaTable* qualified;

    // Luwu class reflection (rfcs/classes): what `class.member` and `class.fields` give through the class, made the first
    // time: the ClassMember of each member at its offset + 1, and the field lists under "public" and "all". NULL until
    // then. Marked in traverseclass.
    struct LuaTable* reflection;

    // Luwu Destructuring (rfcs/destructuring.md): what a positional pattern `.(a, b)` reads from an object of this class.
    // `positionalmembers[i]` is the offset of the member at position i: a primary constructor's parameters, in order.
    // `numberofpositional` is -1 for a class whose objects can't be destructured positionally (no primary constructor),
    // and `positionalmembers` is NULL while it's 0 or less. Owned by this class object; freed in luaR_freeclass.
    uint32_t* positionalmembers;
    int numberofpositional;

} LuauClass;

#define LUAR_NO_LENGTH_FIELD UINT32_MAX

// Luwu Classes (rfcs/classes): the header is 24 bytes on 64-bit platforms, where upstream's is 40. Upstream also
// stores a `members` pointer, which always points just past the header. Luwu computes it instead (luaR_members) and
// keeps `numberofmembers` in the padding after CommonHeader, which saves 16 bytes per object.
typedef struct LuauObject
{
    CommonHeader;

    // Luwu collections: what the object's storage block holds (LuwuStorageKind), or LUAR_STORAGE_NONE without one. The
    // object keeps it because a sweep can free the class first. It sits in the padding after CommonHeader.
    uint8_t storagekind;

    // The number of members that this instance contains. We need this in order
    // to free ourselves if we got swept in the same GC cycle as our class
    // pointer.
    uint32_t numberofmembers;

    GCObject* gclist;

    // The class object that this value is an instance of.
    LuauClass* lclass;

} LuauObject;

// Luwu collections: the kinds of storage block a builtin container owns (LuauObject::storagekind)
enum LuwuStorageKind
{
    LUAR_STORAGE_NONE,
    // `Vec`: entries 1 to count in items[0] to items[count - 1]
    LUAR_STORAGE_VEC,
    // `VecDeque`: a ring. The entry i places from the front is in items[(head + i) & (capacity - 1)].
    LUAR_STORAGE_RING,
};

// Luwu collections: the entries of a `Vec` or `VecDeque`, in a block the object owns from its first value on. The
// object's first member points to it as light userdata, and is nil before the first value. It's allocated with luaM_
// in the object's memory category and freed with the object (luaR_freeobject). traverseobject marks the element type and the live entries; the slots
// after them are never read.
typedef struct LuauStorage
{
    uint32_t count;
    // The number of slots in `items`, always a power of two
    uint32_t capacity;
    // A ring's front slot; always 0 for a `Vec`, so the ring's formula finds a `Vec`'s entries too
    uint32_t head;
    // The type the first value fixed (see lcollectionslib.cpp): the tag every entry has, or LUAR_STORAGE_ENUM when the
    // entries are the values of a builtin enum. Entries tagged LUA_TOBJECT are objects of `elementclass`.
    uint8_t elementtag;
    // The builtin enum when `elementtag` is LUAR_STORAGE_ENUM
    uint8_t elementenum;
    uint16_t padding;
    struct LuauClass* elementclass;
    TValue items[1];
} LuauStorage;

// Luwu try expressions: LuauStorage::elementtag for a builtin enum, which no value's tag matches
#define LUAR_STORAGE_ENUM 0xff

// Luwu collections: the member of a builtin container that points to its LuauStorage
#define LUAR_STORAGE_MEMBER 0

// Luwu Classes (rfcs/classes): an object's members are allocated inline, immediately after its
// header (luaR_objectsize, luaR_newobject, luaR_newobjectuninit). Native code addresses them from the
// object pointer with this offset; anything that ever allocates members out of line must change it
// and luaR_members.
#define LUAR_OBJECT_MEMBERS_OFFSET sizeof(LuauObject)

// Luwu Classes (rfcs/classes): the fields of `object`, which follow its header
LUAU_FORCEINLINE TValue* luaR_members(LuauObject* object)
{
    return reinterpret_cast<TValue*>(object + 1);
}

LUAU_FORCEINLINE const TValue* luaR_members(const LuauObject* object)
{
    return reinterpret_cast<const TValue*>(object + 1);
}

/*
** `module' operation for hashing (size is always a power of 2)
*/
#define lmod(s, size) (check_exp((size & (size - 1)) == 0, (cast_to(int, (s) & ((size) - 1)))))

#define twoto(x) ((int)(1 << (x)))
#define sizenode(t) (twoto((t)->lsizenode))

#define luaO_nilobject (&luaO_nilobject_)

LUAI_DATA const TValue luaO_nilobject_;

#define ceillog2(x) (luaO_log2((x) - 1) + 1)

LUAI_FUNC int luaO_log2(unsigned int x);
LUAI_FUNC int luaO_rawequalObj(const TValue* t1, const TValue* t2);
LUAI_FUNC int luaO_rawequalKey(const TKey* t1, const TValue* t2);
LUAI_FUNC int luaO_str2d(const char* s, double* result);
LUAI_FUNC int luaO_str2l(const char* s, int64_t* result, int base = 10);
LUAI_FUNC const char* luaO_pushvfstring(lua_State* L, const char* fmt, va_list argp);
LUAI_FUNC const char* luaO_pushfstring(lua_State* L, const char* fmt, ...);
LUAI_FUNC const char* luaO_chunkid(char* buf, size_t buflen, const char* source, size_t srclen);
