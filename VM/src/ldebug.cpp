// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
// This code is based on Lua 5.x implementation licensed under MIT License; see lua_LICENSE.txt for details
#include "ldebug.h"

#include "lapi.h"
#include "lfunc.h"
#include "lmem.h"
#include "lgc.h"
#include "ldo.h"
#include "lbytecode.h"
#include "lvm.h"
#include "lclass.h"
#include "lstring.h"
#include "ltable.h"
#include "lualib.h"

#include <limits.h>
#include <string.h>
#include <stdio.h>

LUAU_FASTFLAG(LuauCIProto)

static const char* getfuncname(Closure* cl);

static int currentpc(lua_State* L, CallInfo* ci)
{
    if (FFlag::LuauCIProto)
        return pcRel(ci->savedpc, ci->p);
    else
        return pcRel(ci->savedpc, ci_func(ci)->l.p);
}

static int currentline(lua_State* L, CallInfo* ci)
{
    if (FFlag::LuauCIProto)
        return luaG_getline(ci->p, currentpc(L, ci));
    else
        return luaG_getline(ci_func(ci)->l.p, currentpc(L, ci));
}

static Proto* getluaproto(CallInfo* ci)
{
    if (FFlag::LuauCIProto)
        return cast_to(Proto*, ci->p);
    else
        return (isLua(ci) ? cast_to(Proto*, ci_func(ci)->l.p) : NULL);
}

int lua_getargument(lua_State* L, int level, int n)
{
    if (unsigned(level) >= unsigned(L->ci - L->base_ci))
        return 0;

    CallInfo* ci = L->ci - level;
    // changing tables in native functions externally may invalidate safety contracts wrt table state (metatable/size/readonly)
    if (ci->flags & LUA_CALLINFO_NATIVE)
        return 0;

    Proto* fp = getluaproto(ci);
    int res = 0;

    if (fp && n > 0)
    {
        if (n <= fp->numparams)
        {
            luaC_threadbarrier(L);
            luaA_pushvalue(L, ci->base + (n - 1));
            res = 1;
        }
        else if (fp->is_vararg && n < ci->base - ci->func)
        {
            luaC_threadbarrier(L);
            luaA_pushvalue(L, ci->func + n);
            res = 1;
        }
    }

    return res;
}

const char* lua_getlocal(lua_State* L, int level, int n)
{
    if (unsigned(level) >= unsigned(L->ci - L->base_ci))
        return NULL;

    CallInfo* ci = L->ci - level;
    // changing tables in native functions externally may invalidate safety contracts wrt table state (metatable/size/readonly)
    if (ci->flags & LUA_CALLINFO_NATIVE)
        return NULL;

    Proto* fp = getluaproto(ci);
    const LocVar* var = fp ? luaF_getlocal(fp, n, currentpc(L, ci)) : NULL;
    if (var)
    {
        luaC_threadbarrier(L);
        luaA_pushvalue(L, ci->base + var->reg);
    }
    const char* name = var ? getstr(var->varname) : NULL;
    return name;
}

const char* lua_setlocal(lua_State* L, int level, int n)
{
    api_check(L, L->top - L->base >= 1);

    if (unsigned(level) >= unsigned(L->ci - L->base_ci))
        return NULL;

    CallInfo* ci = L->ci - level;
    // changing registers in native functions externally may invalidate safety contracts wrt register type tags
    if (ci->flags & LUA_CALLINFO_NATIVE)
        return NULL;

    Proto* fp = getluaproto(ci);
    const LocVar* var = fp ? luaF_getlocal(fp, n, currentpc(L, ci)) : NULL;
    if (var)
        setobj2s(L, ci->base + var->reg, L->top - 1);
    L->top--; // pop value
    const char* name = var ? getstr(var->varname) : NULL;
    return name;
}

static Closure* auxgetinfo(lua_State* L, const char* what, lua_Debug* ar, Closure* f, CallInfo* ci)
{
    Closure* cl = NULL;
    for (; *what; what++)
    {
        switch (*what)
        {
        case 's':
        {
            if (f->isC)
            {
                ar->source = "=[C]";
                ar->what = "C";
                ar->linedefined = -1;
                ar->short_src = "[C]";
            }
            else
            {
                TString* source = (FFlag::LuauCIProto && ci != nullptr ? ci->p : f->l.p)->source;
                ar->source = getstr(source);
                ar->what = "Lua";
                ar->linedefined = (FFlag::LuauCIProto && ci != nullptr ? ci->p : f->l.p)->linedefined;
                ar->short_src = luaO_chunkid(ar->ssbuf, sizeof(ar->ssbuf), getstr(source), source->len);
            }
            break;
        }
        case 'l':
        {
            if (ci)
            {
                ar->currentline = isLua(ci) ? currentline(L, ci) : -1;
            }
            else
            {
                ar->currentline = f->isC ? -1 : f->l.p->linedefined;
            }

            break;
        }
        case 'u':
        {
            ar->nupvals = f->nupvalues;
            break;
        }
        case 'a':
        {
            if (f->isC)
            {
                ar->isvararg = 1;
                ar->nparams = 0;
            }
            else
            {
                ar->isvararg = (FFlag::LuauCIProto && ci != nullptr ? ci->p : f->l.p)->is_vararg;
                ar->nparams = (FFlag::LuauCIProto && ci != nullptr ? ci->p : f->l.p)->numparams;
            }
            break;
        }
        case 'n':
        {
            ar->name = ci ? getfuncname(ci_func(ci)) : getfuncname(f);
            break;
        }
        case 'f':
        {
            cl = f;
            break;
        }
        default:;
        }
    }
    return cl;
}

void lua_callhook(lua_State* L, lua_Hook hook, void* userdata)
{
    api_check(L, hook != nullptr);
    api_check(L, L->ci != L->base_ci);

    return luau_callhook(L, hook, userdata);
}

int lua_stackdepth(lua_State* L)
{
    return int(L->ci - L->base_ci);
}

int lua_getinfo(lua_State* L, int level, const char* what, lua_Debug* ar)
{
    Closure* f = NULL;
    CallInfo* ci = NULL;
    if (level < 0)
    {
        // element has to be within stack
        if (-level > L->top - L->base)
            return 0;

        StkId func = L->top + level;

        // and it has to be a function
        if (!ttisfunction(func))
            return 0;

        f = clvalue(func);
    }
    else if (unsigned(level) < unsigned(L->ci - L->base_ci))
    {
        ci = L->ci - level;
        LUAU_ASSERT(ttisfunction(ci->func));
        f = clvalue(ci->func);
    }
    if (f)
    {
        // auxgetinfo fills ar and optionally requests to put closure on stack
        if (Closure* fcl = auxgetinfo(L, what, ar, f, ci))
        {
            luaC_threadbarrier(L);

            // Luwu Classes (rfcs/classes): a function only the VM calls is never handed out; its frame still counts as
            // a level, and the option gives nil in its place
            if (luaR_isvminternalfunction(L, fcl))
                setnilvalue(L->top);
            else
                setclvalue(L, L->top, fcl);

            incr_top(L);
        }
    }
    return f ? 1 : 0;
}

static const char* getfuncname(Closure* cl)
{
    if (cl->isC)
    {
        if (cl->c.debugname)
        {
            return cl->c.debugname;
        }
    }
    else
    {
        Proto* p = cl->l.p;

        if (p->debugname)
        {
            return getstr(p->debugname);
        }
    }
    return nullptr;
}

/*
** Luwu structured runtime errors: the VM and the libraries record an error (global_State::errorrecord) and throw a
** sentinel in its place. Nothing is formatted at the raise. The first catch turns the record into what its catcher
** gets: a message for `pcall` and the other protected calls (luaG_pusherrorrecord), or a variant object for `try`
** (luaR_newerrorvariant).
*/

void luaG_clearerrorrecord(global_State* g)
{
    LuwuErrorRecord& record = g->errorrecord;
    record.active = false;
    record.variant = 0;
    record.subkind = 0;
    record.count = 0;
    for (int i = 0; i < LUWU_ERROR_MAXFIELDS; i++)
    {
        setnilvalue(&record.fields[i]);
        record.opaquetypes[i] = NULL;
    }
    record.source = NULL;
    record.line = 0;
    record.prefix = NULL;
}

bool luaG_iserrorrecord(lua_State* L, const TValue* value)
{
    LuwuErrorRecord* record = &L->global->errorrecord;
    return record->active && ttislightuserdata(value) && pvalue(value) == record;
}

// Starts recording an error of `variant` raised by the code running in `ci`. Only a Lua frame gives it a position.
static LuwuErrorRecord& beginerrorrecord(lua_State* L, LuwuBuiltinClass variant, CallInfo* ci)
{
    global_State* g = L->global;
    luaG_clearerrorrecord(g);

    LuwuErrorRecord& record = g->errorrecord;
    record.variant = uint8_t(variant);
    if (ci && isLua(ci))
    {
        record.source = getluaproto(ci)->source;
        record.line = currentline(L, ci);
    }
    return record;
}

// Luwu opaque values: the shared frozen table an error record holds in place of an opaque value, made the first time
// one is recorded
static LuaTable* opaqueplaceholder(lua_State* L)
{
    global_State* g = L->global;
    if (!g->opaqueplaceholder)
    {
        LuaTable* placeholder = luaH_new(L, 0, 0);
        placeholder->readonly = true;
        g->opaqueplaceholder = placeholder;
    }
    return g->opaqueplaceholder;
}

// Adds the next position of the error being recorded. Luwu opaque values: an opaque value is recorded as the placeholder,
// with its type name kept for the message. A class stays itself: it's what names an opaque object's missing members.
static void adderrorfield(lua_State* L, LuwuErrorRecord& record, const TValue* value)
{
    LUAU_ASSERT(record.count < LUWU_ERROR_MAXFIELDS);
    TValue* field = &record.fields[record.count];
    if (!ttisclass(value) && luaR_isopaque(L, value))
    {
        record.opaquetypes[record.count] = luaS_new(L, luaT_objtypename(L, value));
        sethvalue(L, field, opaqueplaceholder(L));
    }
    else
    {
        setobj(L, field, value);
    }
    record.count++;
}

// Sets `o` to `s`, or to nil when `s` is NULL
static void setoptionalstring(lua_State* L, TValue* o, TString* s)
{
    if (s)
    {
        setsvalue(L, o, s);
    }
    else
    {
        setnilvalue(o);
    }
}

// Sets `o` to `value`, or to nil when `value` is NULL
static void setoptionalobj(lua_State* L, TValue* o, const TValue* value)
{
    if (value)
    {
        setobj(L, o, value);
    }
    else
    {
        setnilvalue(o);
    }
}

// Pushes `s`, or nil when it's NULL
static void pushoptionalstring(lua_State* L, TString* s)
{
    setoptionalstring(L, L->top, s);
    L->top++;
}

// Throws the error being recorded
static l_noret throwerrorrecord(lua_State* L)
{
    LuwuErrorRecord& record = L->global->errorrecord;
    lua_rawcheckstack(L, 1);
    record.active = true;
    setpvalue(L->top, &record, 0);
    L->top++;
    luaD_throw(L, LUA_ERRRUN);
}

// The nearest frame of Luwu code, which a C function raises on behalf of
static CallInfo* nearestluaframe(lua_State* L)
{
    CallInfo* ci = L->ci;
    while (ci > L->base_ci && !isLua(ci))
        ci--;
    return ci;
}

// Raises an `Error.Runtime` of `kind` with `message` at the code running in `ci`
static l_noret raiseruntime(lua_State* L, CallInfo* ci, LuwuRuntimeKind kind, TString* message)
{
    LuwuErrorRecord& record = beginerrorrecord(L, LUWU_BUILTIN_ERR_RUNTIME, ci);
    record.subkind = uint8_t(kind);

    // the `kind` position is written from `subkind` at the catch
    adderrorfield(L, record, luaO_nilobject);
    TValue text;
    setsvalue(L, &text, message);
    adderrorfield(L, record, &text);
    throwerrorrecord(L);
}

static LUA_PRINTF_ATTR(3, 4) l_noret runtimeerror(lua_State* L, LuwuRuntimeKind kind, const char* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    char result[LUA_BUFFERSIZE];
    vsnprintf(result, sizeof(result), fmt, argp);
    va_end(argp);

    raiseruntime(L, L->ci, kind, luaS_new(L, result));
}

// The frame whose position a raise reports (LuwuErrorAt), or NULL for none
static CallInfo* errorframe(lua_State* L, LuwuErrorAt at)
{
    switch (at)
    {
    case LUWU_ERRORAT_RUNNING:
        return L->ci;
    case LUWU_ERRORAT_NEARESTLUA:
        return nearestluaframe(L);
    case LUWU_ERRORAT_CALLER:
    {
        // luaL_where's rule: the caller's position, when it's Luwu code that has one
        CallInfo* caller = L->ci > L->base_ci ? L->ci - 1 : NULL;
        bool positioned = caller && caller > L->base_ci && isLua(caller) && currentline(L, caller) > 0;
        return positioned ? caller : NULL;
    }
    }

    LUAU_ASSERT(!"unknown error position");
    return NULL;
}

l_noret luaG_runerrorcallerv(lua_State* L, LuwuRuntimeKind kind, const char* fmt, va_list args)
{
    lua_rawcheckstack(L, 1);
    luaO_pushvfstring(L, fmt, args);
    TString* message = tsvalue(L->top - 1);
    L->top--;

    raiseruntime(L, errorframe(L, LUWU_ERRORAT_CALLER), kind, message);
}

l_noret luaG_raiseerror(lua_State* L, LuwuBuiltinClass variant, uint8_t subkind, LuwuErrorAt at, const TValue* fields, int count)
{
    LUAU_ASSERT(variant >= LUWU_BUILTIN_ERR_FIRST && variant <= LUWU_BUILTIN_ERR_LAST);
    LuwuErrorRecord& record = beginerrorrecord(L, variant, errorframe(L, at));
    record.subkind = subkind;
    for (int i = 0; i < count; i++)
        adderrorfield(L, record, &fields[i]);
    throwerrorrecord(L);
}

/*
** Luwu structured runtime errors: names for the values NotIndexable and NotCallable errors are about, read from the
** bytecode that loaded them, like Lua 5.4's getobjname
*/

// The opcode of the instruction at `pc`, looking through a breakpoint
static LuauOpcode opcodeat(const Proto* p, int pc)
{
    uint8_t op = uint8_t(LUAU_INSN_OP(p->code[pc]));
    if (op == LOP_BREAK && p->debuginsn)
        op = p->debuginsn[pc];
    return LuauOpcode(op);
}

// Whether the instruction at `pc` can write register `reg`. Anything this doesn't know writes nothing is taken to write
// every register from its A up, so an unknown instruction only loses a name.
static bool writesregister(const Proto* p, int pc, int reg)
{
    uint32_t insn = p->code[pc];
    int a = LUAU_INSN_A(insn);

    switch (opcodeat(p, pc))
    {
    case LOP_NOP:
    case LOP_BREAK:
    case LOP_SETGLOBAL:
    case LOP_SETUPVAL:
    case LOP_CLOSEUPVALS:
    case LOP_SETTABLE:
    case LOP_SETTABLEKS:
    case LOP_SETTABLEN:
    case LOP_RETURN:
    case LOP_JUMP:
    case LOP_JUMPBACK:
    case LOP_JUMPIF:
    case LOP_JUMPIFNOT:
    case LOP_JUMPIFEQ:
    case LOP_JUMPIFLE:
    case LOP_JUMPIFLT:
    case LOP_JUMPIFNOTEQ:
    case LOP_JUMPIFNOTLE:
    case LOP_JUMPIFNOTLT:
    case LOP_JUMPX:
    case LOP_JUMPXEQKNIL:
    case LOP_JUMPXEQKB:
    case LOP_JUMPXEQKN:
    case LOP_JUMPXEQKS:
    case LOP_SETLIST:
    case LOP_FASTCALL:
    case LOP_FASTCALL1:
    case LOP_FASTCALL2:
    case LOP_FASTCALL2K:
    case LOP_FASTCALL3:
    case LOP_PREPVARARGS:
    case LOP_COVERAGE:
    case LOP_CAPTURE:
    case LOP_SETUDATAKS:
        return false;

    case LOP_LOADNIL:
    case LOP_LOADB:
    case LOP_LOADN:
    case LOP_LOADK:
    case LOP_LOADKX:
    case LOP_MOVE:
    case LOP_GETGLOBAL:
    case LOP_GETUPVAL:
    case LOP_GETIMPORT:
    case LOP_GETTABLE:
    case LOP_GETTABLEKS:
    case LOP_GETTABLEN:
    case LOP_NEWCLOSURE:
    case LOP_DUPCLOSURE:
    case LOP_ADD:
    case LOP_SUB:
    case LOP_MUL:
    case LOP_DIV:
    case LOP_IDIV:
    case LOP_MOD:
    case LOP_POW:
    case LOP_ADDK:
    case LOP_SUBK:
    case LOP_MULK:
    case LOP_DIVK:
    case LOP_IDIVK:
    case LOP_MODK:
    case LOP_POWK:
    case LOP_SUBRK:
    case LOP_DIVRK:
    case LOP_AND:
    case LOP_OR:
    case LOP_ANDK:
    case LOP_ORK:
    case LOP_CONCAT:
    case LOP_NOT:
    case LOP_MINUS:
    case LOP_LENGTH:
    case LOP_NEWTABLE:
    case LOP_DUPTABLE:
    case LOP_GETUDATAKS:
        return a == reg;

    case LOP_NAMECALL:
        return reg == a || reg == a + 1;

    default:
        return reg >= a;
    }
}

// The last instruction before `lastpc` that wrote register `reg`, or -1 when there's none or a jump could skip it
static int findsetreg(const Proto* p, int lastpc, int reg)
{
    int setreg = -1;
    int jumptarget = 0;
    for (int pc = 0; pc < lastpc;)
    {
        LuauOpcode op = opcodeat(p, pc);
        uint32_t insn = (p->code[pc] & ~0xffu) | uint32_t(op);

        int target = Luau::getJumpTarget(insn, uint32_t(pc));
        if (target > pc && target <= lastpc && target > jumptarget)
            jumptarget = target;

        if (writesregister(p, pc, reg))
            setreg = pc < jumptarget ? -1 : pc;

        pc += Luau::getOpLength(op);
    }
    return setreg;
}

// The string constant `index` of `p`, or NULL
static TString* stringconstant(const Proto* p, uint32_t index)
{
    if (index >= uint32_t(p->sizek) || !ttisstring(&p->k[index]))
        return NULL;
    return tsvalue(&p->k[index]);
}

// The first word of the instruction that the word at `pc` belongs to. A frame's saved pc can point at an instruction's
// auxiliary word.
static int instructionstart(const Proto* p, int pc)
{
    int start = 0;
    while (start < pc)
    {
        int next = start + Luau::getOpLength(opcodeat(p, start));
        if (next > pc)
            break;
        start = next;
    }
    return start;
}

// The name of the value in register `reg` at `pc`, and in `kind` how it was reached; NULL when it can't be named
static TString* getobjname(const Proto* p, int pc, int reg, LuwuNameKind* kind)
{
    if (const LocVar* local = luaF_findlocal(p, reg, pc))
    {
        *kind = LUWU_NAME_LOCAL;
        return local->varname;
    }

    int setpc = findsetreg(p, pc, reg);
    if (setpc < 0)
        return NULL;

    uint32_t insn = p->code[setpc];
    uint32_t aux = setpc + 1 < p->sizecode ? p->code[setpc + 1] : 0;
    switch (opcodeat(p, setpc))
    {
    case LOP_MOVE:
    {
        int source = LUAU_INSN_B(insn);
        return source < reg ? getobjname(p, setpc, source, kind) : NULL;
    }
    case LOP_GETGLOBAL:
        *kind = LUWU_NAME_GLOBAL;
        return stringconstant(p, aux);
    case LOP_GETIMPORT:
    {
        // an import id holds its path's length and up to three constant indices, 10 bits each
        int length = int(aux >> 30);
        uint32_t last = length == 1 ? (aux >> 20) & 1023 : length == 2 ? (aux >> 10) & 1023 : aux & 1023;
        *kind = length == 1 ? LUWU_NAME_GLOBAL : LUWU_NAME_FIELD;
        return stringconstant(p, last);
    }
    case LOP_GETTABLEKS:
        *kind = LUWU_NAME_FIELD;
        return stringconstant(p, aux);
    case LOP_NAMECALL:
        if (reg != LUAU_INSN_A(insn))
            return NULL;
        *kind = LUWU_NAME_METHOD;
        return stringconstant(p, aux);
    case LOP_GETUPVAL:
    {
        int upvalue = LUAU_INSN_B(insn);
        if (!p->upvalues || upvalue >= p->sizeupvalues)
            return NULL;
        *kind = LUWU_NAME_UPVALUE;
        return p->upvalues[upvalue];
    }
    default:
        return NULL;
    }
}

// Raises a NotIndexable or NotCallable error about `value`, named when it's a register of the running Luwu code
static l_noret raisenamederror(lua_State* L, LuwuBuiltinClass variant, const TValue* value, const TValue* index)
{
    TString* name = NULL;
    LuwuNameKind kind = LUWU_NAME_NONE;
    CallInfo* ci = L->ci;
    if (isLua(ci) && value >= ci->base && value < ci->top)
    {
        const Proto* p = getluaproto(ci);
        name = getobjname(p, instructionstart(p, currentpc(L, ci)), int(value - ci->base), &kind);
    }

    TValue fields[4];
    setoptionalstring(L, &fields[0], name);
    // the kind is written from the subkind at the catch
    setnilvalue(&fields[1]);
    setobj(L, &fields[2], value);
    int count = 3;
    if (index)
    {
        setobj(L, &fields[3], index);
        count = 4;
    }

    luaG_raiseerror(L, variant, uint8_t(name ? kind : LUWU_NAME_NONE), LUWU_ERRORAT_RUNNING, fields, count);
}

void luaG_prefixerrorrecord(lua_State* L)
{
    LuwuErrorRecord& record = L->global->errorrecord;
    LUAU_ASSERT(record.active);

    // a coroutine wrapped in another adds its position in front of the inner one's, as for an error that's a string
    luaL_where(L, 1);
    if (record.prefix)
    {
        setsvalue(L, L->top, record.prefix);
        L->top++;
        lua_concat(L, 2);
    }
    record.prefix = tsvalue(L->top - 1);
    L->top--;
}

// Formats the recorded error's message (luaG_replaceerrorrecord) and pushes it. Formatting can run Luwu code through
// `__tostring`, which can raise, so the record's values move onto the stack first and the record is cleared.
static void pusherrorrecord(lua_State* L, void* ud)
{
    global_State* g = L->global;
    luaR_fillerrorsubkind(L, g->errorrecord);
    LuwuErrorRecord record = g->errorrecord;

    // the record's values, then the room the C API calls formatting makes have
    lua_rawcheckstack(L, 2 * LUWU_ERROR_MAXFIELDS + 2 + LUA_MINSTACK);
    for (int i = 0; i < LUWU_ERROR_MAXFIELDS; i++)
    {
        setobj2s(L, L->top, &record.fields[i]);
        L->top++;
        pushoptionalstring(L, record.opaquetypes[i]);
    }
    pushoptionalstring(L, record.prefix);
    pushoptionalstring(L, record.source);
    luaG_clearerrorrecord(g);

    luaL_Strbuf b;
    luaL_buffinit(L, &b);
    if (record.prefix)
        luaL_addlstring(&b, getstr(record.prefix), record.prefix->len);
    if (record.source)
    {
        char chunkbuf[LUA_IDSIZE];
        const char* chunkid = luaO_chunkid(chunkbuf, sizeof(chunkbuf), getstr(record.source), record.source->len);
        char line[32];
        snprintf(line, sizeof(line), ":%d: ", record.line);
        luaL_addstring(&b, chunkid);
        luaL_addstring(&b, line);
    }
    luaR_adderrorrecord(&b, record);
    luaL_pushresult(&b);
}

int luaG_replaceerrorrecord(lua_State* L, StkId slot)
{
    LUAU_ASSERT(luaG_iserrorrecord(L, slot));
    ptrdiff_t saved = savestack(L, slot);
    int status = luaD_rawrunprotectedrecorded(L, pusherrorrecord, NULL);
    luaG_clearerrorrecord(L->global);

    slot = restorestack(L, saved);
    if (status != LUA_OK)
    {
        // an error raised while formatting is an error in error handling, as in an `xpcall` handler
        status = status == LUA_ERRMEM ? LUA_ERRMEM : LUA_ERRERR;
        luaD_seterrorobj(L, status, slot);
        return status;
    }

    setobj2s(L, slot, L->top - 1);
    L->top = slot + 1;
    return LUA_ERRRUN;
}

// Raises an InvalidOperation for operator `op` applied to `left` and, for a binary operator, `right`
static l_noret operationerror(lua_State* L, TMS op, const TValue* left, const TValue* right)
{
    TValue fields[3];
    // the operator is written from the subkind at the catch
    setnilvalue(&fields[0]);
    setobj(L, &fields[1], left);
    int count = 2;
    if (right)
    {
        setobj(L, &fields[2], right);
        count = 3;
    }
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_INVALIDOPERATION, uint8_t(op), LUWU_ERRORAT_RUNNING, fields, count);
}

l_noret luaG_typeerrorL(lua_State* L, const TValue* o, const char* op)
{
    if (strcmp(op, "call") == 0)
        raisenamederror(L, LUWU_BUILTIN_ERR_NOTCALLABLE, o, NULL);
    if (strcmp(op, "get length of") == 0)
        operationerror(L, TM_LEN, o, NULL);

    const char* t = luaT_objtypename(L, o);

    runtimeerror(L, LUWU_RUNTIME_TYPE, "attempt to %s a %s value", op, t);
}

l_noret luaG_forerrorL(lua_State* L, const TValue* o, const char* what)
{
    const char* t = luaT_objtypename(L, o);

    runtimeerror(L, LUWU_RUNTIME_TYPE, "invalid 'for' %s (number expected, got %s)", what, t);
}

l_noret luaG_concaterror(lua_State* L, StkId p1, StkId p2)
{
    operationerror(L, TM_CONCAT, p1, p2);
}

l_noret luaG_aritherror(lua_State* L, const TValue* p1, const TValue* p2, TMS op)
{
    operationerror(L, op, p1, p2);
}

l_noret luaG_ordererror(lua_State* L, const TValue* p1, const TValue* p2, TMS op)
{
    operationerror(L, op, p1, p2);
}

l_noret luaG_indexerror(lua_State* L, const TValue* p1, const TValue* p2)
{
    raisenamederror(L, LUWU_BUILTIN_ERR_NOTINDEXABLE, p1, p2);
}

l_noret luaG_invalidkeyerror(lua_State* L, const TValue* indexed, const TValue* key)
{
    TValue fields[2];
    setobj(L, &fields[0], indexed);
    setobj(L, &fields[1], key);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_INVALIDKEY, 0, LUWU_ERRORAT_RUNNING, fields, 2);
}

// Luwu Classes (rfcs/classes): a name the value does not have. The RFC's glossary splits the two
// halves of this: objects carry *fields*, while a class's namespace holds *members* -- its static
// functions plus the field names its objects are laid out with. Neither is a table, so neither has
// "keys". A class also gets pointed at the object case, because reaching for a field through the
// class (`Cat.age`) is by far the most common way to land here.
l_noret luaG_missingmembererror(lua_State* L, const TValue* p1, const TValue* p2)
{
    if (!ttisstring(p2))
        runtimeerror(L, LUWU_RUNTIME_MEMBER, "cannot index %s with a %s", luaT_objtypename(L, p1), luaT_objtypename(L, p2));

    // Luwu opaque values: an opaque class's objects stay out of the error, which names their class instead. A private
    // member of one raises the same error (luaR_checkprivateaccess).
    if (ttisobject(p1) && objectvalue(p1)->lclass->isopaque)
        luaG_classmembererror(L, objectvalue(p1)->lclass, true, tsvalue(p2));

    TValue fields[2];
    setobj(L, &fields[0], p1);
    setobj(L, &fields[1], p2);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_MISSINGMEMBER, 0, LUWU_ERRORAT_RUNNING, fields, 2);
}

l_noret luaG_classmembererror(lua_State* L, const LuauClass* classdef, bool onobject, const TString* key)
{
    // the class stands for one of its objects with subkind 1 (luaR_adderrorrecord's MissingMember message)
    TValue fields[2];
    setclassvalue(L, &fields[0], const_cast<LuauClass*>(classdef));
    setsvalue(L, &fields[1], const_cast<TString*>(key));
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_MISSINGMEMBER, uint8_t(onobject ? 1 : 0), LUWU_ERRORAT_RUNNING, fields, 2);
}

// Luwu Classes (rfcs/classes): `Cat.age` where `age` is one of Cat's *fields*. The class knows the
// name perfectly well -- it lays its objects out with it -- so this deserves better than being told
// the class has never heard of it.
l_noret luaG_instancefieldonclasserror(lua_State* L, const TValue* p1, const TValue* p2)
{
    const char* className = getstr(classvalue(p1)->name);

    // Luwu Traits (rfcs/classes/traits.md): a trait's fields only exist in the objects of implementing classes
    if (classvalue(p1)->istrait)
        runtimeerror(
            L,
            LUWU_RUNTIME_MEMBER,
            "cannot read field '%s' of trait '%s': fields exist only on objects",
            getstr(tsvalue(p2)),
            className
        );

    runtimeerror(
        L,
        LUWU_RUNTIME_MEMBER,
        "'%s' is a field of objects of class '%s', not a member of the class itself; did you mean to access it on an object of '%s' instead?",
        getstr(tsvalue(p2)),
        className,
        className
    );
}

l_noret luaG_methoderror(lua_State* L, const TValue* p1, const TString* p2)
{
    const char* t1 = luaT_objtypename(L, p1);

    runtimeerror(L, LUWU_RUNTIME_MEMBER, "attempt to call missing method '%s' of %s", getstr(p2), t1);
}

// Luwu Destructuring (rfcs/destructuring.md): a positional pattern `.(a, b)` on an object without that position
l_noret luaG_positionalerror(lua_State* L, const LuauClass* classdef, int position)
{
    const char* className = getstr(classdef->name);

    if (classdef->numberofpositional < 0)
        luaG_runerrorL(L, "can't destructure an object of class '%s' positionally: '%s' has no primary constructor", className, className);

    luaG_runerrorL(
        L,
        "can't destructure an object of class '%s' at position %d: its primary constructor has %d parameter%s",
        className,
        position + 1,
        classdef->numberofpositional,
        classdef->numberofpositional == 1 ? "" : "s"
    );
}

l_noret luaG_readonlyerror(lua_State* L, const TValue* target)
{
    TValue fields[1];
    setoptionalobj(L, &fields[0], target);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_READONLY, 0, LUWU_ERRORAT_RUNNING, fields, 1);
}

l_noret luaG_privateaccesserror(lua_State* L, const TValue* p2, const TString* className)
{
    const char* t1 = getstr(className);
    runtimeerror(L, LUWU_RUNTIME_MEMBER, "'%s' is a private member of '%s', it cannot be accessed outside %s's class scope", getstr(tsvalue(p2)), t1, t1);
}

// Raises a ReadOnly error for a write to const field `key` of `object`. Subkind 1 is a write from outside the class's
// `__init`, 2 one from `__init` to another object (luaR_adderrorrecord's ReadOnly message).
static l_noret constassignerror(lua_State* L, const TValue* object, const TValue* key, uint8_t subkind)
{
    TValue fields[2];
    setobj(L, &fields[0], object);
    setobj(L, &fields[1], key);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_READONLY, subkind, LUWU_ERRORAT_RUNNING, fields, 2);
}

l_noret luaG_constassignerror(lua_State* L, const TValue* object, const TValue* key)
{
    constassignerror(L, object, key, 1);
}

l_noret luaG_constassignnotselferror(lua_State* L, const TValue* object, const TValue* key)
{
    constassignerror(L, object, key, 2);
}

l_noret luaG_stackoverflowerror(lua_State* L, bool cstack)
{
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_STACKOVERFLOW, uint8_t(cstack ? 1 : 0), LUWU_ERRORAT_RUNNING, NULL, 0);
}

l_noret luaG_notyieldableerror(lua_State* L)
{
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_NOTYIELDABLE, 0, LUWU_ERRORAT_RUNNING, NULL, 0);
}

l_noret luaG_argumenterror(lua_State* L, int narg, const char* fname, const char* extramsg)
{
    TValue fields[4];
    const TValue* value = luaA_toobject(L, narg);
    setoptionalobj(L, &fields[0], value);
    setsvalue(L, &fields[1], luaS_new(L, extramsg));
    setnvalue(&fields[2], narg);
    setoptionalstring(L, &fields[3], fname ? luaS_new(L, fname) : NULL);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_INVALIDARGUMENT, 0, LUWU_ERRORAT_CALLER, fields, 4);
}

l_noret luaG_unexpectedtypeerror(lua_State* L, int narg, const char* fname, const char* tname)
{
    TValue fields[4];
    setsvalue(L, &fields[0], luaS_new(L, tname));
    // a missing argument is `none`, which tells it from a nil one
    const TValue* value = luaA_toobject(L, narg);
    if (value)
    {
        setobj(L, &fields[1], value);
    }
    else
    {
        setsymnonevalue(&fields[1]);
    }
    setnvalue(&fields[2], narg);
    setoptionalstring(L, &fields[3], fname ? luaS_new(L, fname) : NULL);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_UNEXPECTEDTYPE, 0, LUWU_ERRORAT_CALLER, fields, 4);
}

l_noret luaG_assertionerror(lua_State* L, const TValue* message)
{
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_ASSERTIONFAILED, 0, LUWU_ERRORAT_CALLER, message, 1);
}

l_noret luaG_blockedinitaccesserror(lua_State* L, const TString* className)
{
    const char* t1 = getstr(className);
    runtimeerror(L, LUWU_RUNTIME_MEMBER, "'__init' of '%s' cannot be accessed or called explicitly; construct a new object with %s(...) instead", t1, t1);
}

l_noret luaG_runerroratlua(lua_State* L, LuwuRuntimeKind kind, const char* msg)
{
    raiseruntime(L, nearestluaframe(L), kind, luaS_new(L, msg));
}

// Luwu builtin classes: luaG_indexerror for an `__index` written in C (`none`'s), at the Lua code that indexed
l_noret luaG_indexerroratlua(lua_State* L, const TValue* p1, const TValue* p2)
{
    TValue fields[4];
    setnilvalue(&fields[0]);
    setnilvalue(&fields[1]);
    setobj(L, &fields[2], p1);
    setobj(L, &fields[3], p2);
    luaG_raiseerror(L, LUWU_BUILTIN_ERR_NOTINDEXABLE, LUWU_NAME_NONE, LUWU_ERRORAT_NEARESTLUA, fields, 4);
}
// Luwu Classes (rfcs/classes): raised by construction, which may be running in the class's C
// constructor (`pcall(C)`), so the location is the Lua code constructing rather than the running frame.
l_noret luaG_privateconstructorerror(lua_State* L, const TString* className)
{
    const char* t1 = getstr(className);
    char result[LUA_BUFFERSIZE];
    snprintf(result, sizeof(result), "the constructor of '%s' is private; '%s' can only be constructed inside %s's class scope", t1, t1, t1);
    luaG_runerroratlua(L, LUWU_RUNTIME_MEMBER, result);
}

// Luwu Classes (rfcs/classes): raised by CHECKSELFCLASS when `self` isn't an object of the method's class.
// `selfCall` is true when the check was emitted at an O2 inline site for a `:` call, and only changes the message.
//
// An inline site fails when the receiver's annotation names the wrong class, e.g. a VecDeque passed to
// `function push(list: List, v) list:push(v) end`: O0/O1 dispatch to `VecDeque:push`, but O2 inlined
// `List:push`. Erroring rather than falling back to a dynamic call keeps inline sites small, and tells the
// user their call site or annotation is wrong and isn't getting the O2 optimization they asked for.
l_noret luaG_classmethoderror(lua_State* L, const TValue* self, const LuauClass* expected, const TString* methodName)
{
    const char* expectedName = getstr(expected->name);
    const char* method = getstr(methodName);

    if (ttisclass(self))
        luaG_runerrorL(
            L, "attempt to call class method '%s.%s' with 'Self' of class '%s'", expectedName, method, getstr(classvalue(self)->name)
        );

    luaG_runerrorL(
        L,
        "attempt to call class method '%s.%s' with 'Self' of type '%s'; call it on the class: '%s:%s()'",
        expectedName,
        method,
        luaT_objtypename(L, self),
        expectedName,
        method
    );
}

l_noret luaG_selfclasserror(lua_State* L, const TValue* self, const LuauClass* expected, const TString* methodName, bool selfCall)
{
    const char* expectedName = getstr(expected->name);
    const char* method = getstr(methodName);
    char result[LUA_BUFFERSIZE];

    if (!ttisobject(self))
    {
        int specificIndexingSyntax = selfCall ? ':' : '.';
        snprintf(
            result,
            sizeof(result),
            "attempt to call method '%s%c%s' with 'self' of type '%s'",
            expectedName,
            specificIndexingSyntax,
            method,
            luaT_objtypename(L, self)
        );
    }
    else if (selfCall)
    {
        const char* actualName = getstr(objectvalue(self)->lclass->name);
        snprintf(
            result,
            sizeof(result),
            "attempt to call inlined method '%s:%s' on an object of class '%s'; this occurred because inlining optimizations are enabled "
            "and the passed 'self' did not match the expected type annotation '%s'",
            expectedName,
            method,
            actualName,
            expectedName
        );
    }
    else
    {
        const char* actualName = getstr(objectvalue(self)->lclass->name);
        snprintf(result, sizeof(result), "attempt to call method '%s.%s' with 'self' of class '%s'", expectedName, method, actualName);
    }

    // A builtin class's C method checks `self` itself (luaR_checknativeself), so the location is the Lua code calling it
    luaG_runerroratlua(L, LUWU_RUNTIME_MEMBER, result);
}

static void pusherrorat(lua_State* L, CallInfo* ci, const char* msg)
{
    if (isLua(ci))
    {
        TString* source = getluaproto(ci)->source;
        char chunkbuf[LUA_IDSIZE]; // add file:line information
        const char* chunkid = luaO_chunkid(chunkbuf, sizeof(chunkbuf), getstr(source), source->len);
        int line = currentline(L, ci);
        luaO_pushfstring(L, "%s:%d: %s", chunkid, line, msg);
    }
    else
    {
        lua_pushstring(L, msg);
    }
}

static void pusherror(lua_State* L, const char* msg)
{
    pusherrorat(L, L->ci, msg);
}

l_noret luaG_runerrorL(lua_State* L, const char* fmt, ...)
{
    va_list argp;
    va_start(argp, fmt);
    char result[LUA_BUFFERSIZE];
    vsnprintf(result, sizeof(result), fmt, argp);
    va_end(argp);

    raiseruntime(L, L->ci, LUWU_RUNTIME_OTHER, luaS_new(L, result));
}

void luaG_pusherror(lua_State* L, const char* error)
{
    lua_rawcheckstack(L, 1);

    pusherror(L, error);
}

void luaG_breakpoint(lua_State* L, Proto* p, int line, bool enable)
{
    void (*ondisable)(lua_State*, Proto*) = L->global->ecb.disable;

    // since native code doesn't support breakpoints, we would need to update all call frames with LUAU_CALLINFO_NATIVE that refer to p
    if (p->lineinfo && (ondisable || !p->execdata))
    {
        for (int i = 0; i < p->sizecode; ++i)
        {
            // note: we keep prologue as is, instead opting to break at the first meaningful instruction
            if (LUAU_INSN_OP(p->code[i]) == LOP_PREPVARARGS)
                continue;

            if (luaG_getline(p, i) != line)
                continue;

            // lazy copy of the original opcode array; done when the first breakpoint is set
            if (!p->debuginsn)
            {
                p->debuginsn = luaM_newarray(L, p->sizecode, uint8_t, p->memcat);
                for (int j = 0; j < p->sizecode; ++j)
                    p->debuginsn[j] = LUAU_INSN_OP(p->code[j]);
            }

            uint8_t op = enable ? LOP_BREAK : LUAU_INSN_OP(p->debuginsn[i]);

            // patch just the opcode byte, leave arguments alone
            p->code[i] &= ~0xff;
            p->code[i] |= op;
            LUAU_ASSERT(LUAU_INSN_OP(p->code[i]) == op);

            // currently we don't restore native code when breakpoint is disabled.
            // this will be addressed in the future.
            if (enable && p->execdata && ondisable)
                ondisable(L, p);

            // note: this is important!
            // we only patch the *first* instruction in each proto that's attributed to a given line
            // this can be changed, but if requires making patching a bit more nuanced so that we don't patch AUX words
            break;
        }
    }

    for (int i = 0; i < p->sizep; ++i)
    {
        luaG_breakpoint(L, p->p[i], line, enable);
    }
}

bool luaG_onbreak(lua_State* L)
{
    if (L->ci == L->base_ci)
        return false;

    if (!isLua(L->ci))
        return false;

    return LUAU_INSN_OP(*L->ci->savedpc) == LOP_BREAK;
}

int luaG_getline(Proto* p, int pc)
{
    LUAU_ASSERT(pc >= 0 && pc < p->sizecode);

    if (!p->lineinfo)
        return 0;

    return p->abslineinfo[pc >> p->linegaplog2] + p->lineinfo[pc];
}

int luaG_isnative(lua_State* L, int level)
{
    if (unsigned(level) >= unsigned(L->ci - L->base_ci))
        return 0;

    CallInfo* ci = L->ci - level;
    return (ci->flags & LUA_CALLINFO_NATIVE) != 0 ? 1 : 0;
}

int luaG_hasnative(lua_State* L, int level)
{
    if (unsigned(level) >= unsigned(L->ci - L->base_ci))
        return 0;

    CallInfo* ci = L->ci - level;

    Proto* proto = getluaproto(ci);
    if (proto == nullptr)
        return 0;

    return (proto->execdata != nullptr);
}

void lua_singlestep(lua_State* L, int enabled)
{
    L->singlestep = bool(enabled);
}

static int getmaxline(Proto* p)
{
    int result = -1;

    for (int i = 0; i < p->sizecode; ++i)
    {
        int line = luaG_getline(p, i);
        result = result < line ? line : result;
    }

    for (int i = 0; i < p->sizep; ++i)
    {
        int psize = getmaxline(p->p[i]);
        result = result < psize ? psize : result;
    }

    return result;
}

// Find the line number with instructions. If the provided line doesn't have any instruction, it should return the next valid line number.
static int getnextline(Proto* p, int line)
{
    int closest = -1;

    if (p->lineinfo)
    {
        for (int i = 0; i < p->sizecode; ++i)
        {
            // note: we keep prologue as is, instead opting to break at the first meaningful instruction
            if (LUAU_INSN_OP(p->code[i]) == LOP_PREPVARARGS)
                continue;

            int candidate = luaG_getline(p, i);

            if (candidate == line)
                return line;

            if (candidate > line && (closest == -1 || candidate < closest))
                closest = candidate;
        }
    }

    for (int i = 0; i < p->sizep; ++i)
    {
        int candidate = getnextline(p->p[i], line);

        if (candidate == line)
            return line;

        if (candidate > line && (closest == -1 || candidate < closest))
            closest = candidate;
    }

    return closest;
}

int lua_breakpoint(lua_State* L, int funcindex, int line, int enabled)
{
    const TValue* func = luaA_toobject(L, funcindex);
    api_check(L, ttisfunction(func) && !clvalue(func)->isC);

    Proto* p = clvalue(func)->l.p;

    // set the breakpoint to the next closest line with valid instructions
    int target = getnextline(p, line);

    if (target != -1)
        luaG_breakpoint(L, p, target, bool(enabled));

    return target;
}

int lua_atbreakpoint(lua_State* L)
{
    return luaG_onbreak(L) ? 1 : 0;
}

static void getcoverage(Proto* p, int depth, int* buffer, size_t size, void* context, lua_Coverage callback)
{
    memset(buffer, -1, size * sizeof(int));

    for (int i = 0; i < p->sizecode; ++i)
    {
        Instruction insn = p->code[i];
        if (LUAU_INSN_OP(insn) != LOP_COVERAGE)
            continue;

        int line = luaG_getline(p, i);
        int hits = LUAU_INSN_E(insn);

        LUAU_ASSERT(size_t(line) < size);
        buffer[line] = buffer[line] < hits ? hits : buffer[line];
    }

    const char* debugname = p->debugname ? getstr(p->debugname) : NULL;
    int linedefined = p->linedefined;

    callback(context, debugname, linedefined, depth, buffer, size);

    for (int i = 0; i < p->sizep; ++i)
        getcoverage(p->p[i], depth + 1, buffer, size, context, callback);
}

void lua_getcoverage(lua_State* L, int funcindex, void* context, lua_Coverage callback)
{
    const TValue* func = luaA_toobject(L, funcindex);
    api_check(L, ttisfunction(func) && !clvalue(func)->isC);

    Proto* p = clvalue(func)->l.p;

    size_t size = getmaxline(p) + 1;
    if (size == 0)
        return;

    int* buffer = luaM_newarray(L, size, int, 0);

    getcoverage(p, 0, buffer, size, context, callback);

    luaM_freearray(L, buffer, size, int, 0);
}

static void getcounters(lua_State* L, Proto* p, void* context, lua_CounterFunction functionvisit, lua_CounterValue countervisit)
{
    if (p->execdata != nullptr && L->global->ecb.getcounterdata != nullptr)
    {
        size_t count = 0;
        char* data = L->global->ecb.getcounterdata(L, p, &count);

        if (data != nullptr && count != 0)
        {
            const char* debugname = p->debugname ? getstr(p->debugname) : nullptr;
            int linedefined = p->linedefined;

            functionvisit(context, debugname, linedefined);

            for (size_t i = 0; i < count; i++)
            {
                uint32_t kind = 0;
                memcpy(&kind, data + 0, sizeof(kind));
                data += sizeof(kind);

                uint32_t pcpos = 0;
                memcpy(&pcpos, data + 0, sizeof(pcpos));
                data += sizeof(pcpos);

                uint64_t hits = 0;
                memcpy(&hits, data + 0, sizeof(hits));
                data += sizeof(hits);

                int line = pcpos == ~0u ? p->linedefined : luaG_getline(p, pcpos);

                countervisit(context, kind, line, hits);
            }
        }
    }

    for (int i = 0; i < p->sizep; ++i)
        getcounters(L, p->p[i], context, functionvisit, countervisit);
}

void lua_getcounters(lua_State* L, int funcindex, void* context, lua_CounterFunction functionvisit, lua_CounterValue countervisit)
{
    const TValue* func = luaA_toobject(L, funcindex);
    api_check(L, ttisfunction(func) && !clvalue(func)->isC);

    if (L->global->ecb.getcounterdata == nullptr)
        return;

    Proto* p = clvalue(func)->l.p;

    getcounters(L, p, context, functionvisit, countervisit);
}

static size_t append(char* buf, size_t bufsize, size_t offset, const char* data)
{
    size_t size = strlen(data);
    size_t copy = offset + size >= bufsize ? bufsize - offset - 1 : size;
    memcpy(buf + offset, data, copy);
    return offset + copy;
}

// Luwu try expressions: lua_debugtrace's lines for the top `levels` frames, so a `try` expression's traceback stops at
// the frame that caught the error
static size_t appendtrace(lua_State* L, char* buf, size_t bufsize, int levels)
{
    const int limit1 = 10;
    const int limit2 = 10;

    int depth = int(L->ci - L->base_ci);
    if (depth > levels)
        depth = levels;
    size_t offset = 0;

    lua_Debug ar;
    for (int level = 0; level < levels && lua_getinfo(L, level, "sln", &ar); ++level)
    {
        if (ar.source)
            offset = append(buf, bufsize, offset, ar.short_src);

        if (ar.currentline > 0)
        {
            char line[32];
            snprintf(line, sizeof(line), ":%d", ar.currentline);

            offset = append(buf, bufsize, offset, line);
        }

        if (ar.name)
        {
            offset = append(buf, bufsize, offset, " function ");
            offset = append(buf, bufsize, offset, ar.name);
        }

        offset = append(buf, bufsize, offset, "\n");

        if (depth > limit1 + limit2 && level == limit1 - 1)
        {
            char skip[32];
            snprintf(skip, sizeof(skip), "... (+%d frames)\n", int(depth - limit1 - limit2));

            offset = append(buf, bufsize, offset, skip);

            level = depth - limit2 - 1;
        }
    }

    return offset;
}

const char* lua_debugtrace(lua_State* L)
{
    static char buf[4096];

    size_t offset = appendtrace(L, buf, sizeof(buf), INT_MAX);

    LUAU_ASSERT(offset < sizeof(buf));
    buf[offset] = '\0';

    return buf;
}

TString* luaG_traceback(lua_State* L, int levels)
{
    char buf[4096];
    size_t offset = appendtrace(L, buf, sizeof(buf), levels);

    // without the last line's newline
    if (offset > 0 && buf[offset - 1] == '\n')
        offset--;

    return luaS_newlstr(L, buf, offset);
}
