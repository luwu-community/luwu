// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "IrLoweringA64.h"

#include "Luau/Bytecode.h"
#include "Luau/DenseHash.h"
#include "Luau/IrData.h"
#include "Luau/IrUtils.h"
#include "Luau/LoweringStats.h"

#include "EmitCommonA64.h"
#include "NativeState.h"

#include "generated/LoweringRulesA64.h"

#include "lstate.h"
#include "lgc.h"

LUAU_FASTFLAGVARIABLE(LuauCodegenFixBufferLenCheck)
LUAU_FASTFLAGVARIABLE(LuauCodegenFixTwoResA64Builtin)
LUAU_FASTFLAG(LuauYieldIter2)
LUAU_FASTFLAG(LuauCIProto)
LUAU_FASTFLAG(LuauCodegenSharedLog)

namespace Luau
{
namespace CodeGen
{
namespace A64
{




void emitAbort(AssemblyBuilderA64& build, Label& abort)
{
    Label skip;
    build.b(skip);
    build.setLabel(abort);
    build.udf();
    build.setLabel(skip);
}

void emitFallback(AssemblyBuilderA64& build, int offset, int pcpos)
{
    // fallback(L, instruction, base, k)
    build.mov(x0, rState);
    emitAddOffset(build, x1, rCode, pcpos * sizeof(Instruction));
    build.mov(x2, rBase);
    build.mov(x3, rConstants);
    build.ldr(x4, mem(rNativeContext, offset));
    build.blr(x4);

    emitUpdateBase(build);
}

void emitInvokeLibm1P(AssemblyBuilderA64& build, size_t func, int arg)
{
    CODEGEN_ASSERT(kTempSlots >= 1);
    CODEGEN_ASSERT(unsigned(sTemporary.data) <= AssemblyBuilderA64::kMaxImmediate);
    build.ldr(d0, mem(rBase, arg * sizeof(TValue) + offsetof(TValue, value.n)));
    build.add(x0, sp, uint16_t(sTemporary.data)); // sp-relative offset
    build.ldr(x1, mem(rNativeContext, uint32_t(func)));
    build.blr(x1);
}

bool emitBuiltin(AssemblyBuilderA64& build, IrFunction& function, IrRegAllocA64& regs, int bfid, int res, int arg, int nresults)
{
    switch (bfid)
    {
    case LBF_MATH_FREXP:
    {
        CODEGEN_ASSERT(nresults == 1 || nresults == 2);
        emitInvokeLibm1P(build, offsetof(NativeContext, libm_frexp), arg);
        build.str(d0, mem(rBase, res * sizeof(TValue) + offsetof(TValue, value.n)));

        RegisterA64 temp = regs.allocTemp(KindA64::w);
        build.mov(temp, LUA_TNUMBER);
        build.str(temp, mem(rBase, res * sizeof(TValue) + offsetof(TValue, tt)));

        if (nresults == 2)
        {
            if (FFlag::LuauCodegenFixTwoResA64Builtin)
            {
                RegisterA64 temp2 = regs.allocTemp(KindA64::w);
                build.ldr(temp2, sTemporary);
                build.scvtf(d1, temp2);
            }
            else
            {
                build.ldr(w0, sTemporary);
                build.scvtf(d1, w0);
            }

            build.str(d1, mem(rBase, (res + 1) * sizeof(TValue) + offsetof(TValue, value.n)));
            build.str(temp, mem(rBase, (res + 1) * sizeof(TValue) + offsetof(TValue, tt)));
        }
        return true;
    }
    case LBF_MATH_MODF:
    {
        CODEGEN_ASSERT(nresults == 1 || nresults == 2);
        emitInvokeLibm1P(build, offsetof(NativeContext, libm_modf), arg);
        build.ldr(d1, sTemporary);
        build.str(d1, mem(rBase, res * sizeof(TValue) + offsetof(TValue, value.n)));

        RegisterA64 temp = regs.allocTemp(KindA64::w);
        build.mov(temp, LUA_TNUMBER);
        build.str(temp, mem(rBase, res * sizeof(TValue) + offsetof(TValue, tt)));

        if (nresults == 2)
        {
            build.str(d0, mem(rBase, (res + 1) * sizeof(TValue) + offsetof(TValue, value.n)));
            build.str(temp, mem(rBase, (res + 1) * sizeof(TValue) + offsetof(TValue, tt)));
        }
        return true;
    }

    default:
        CODEGEN_ASSERT(!"Missing A64 lowering");
        return false;
    }
}

// Luwu Classes (rfcs/classes): the a64 counterpart of emitClassMemberAuthX64. Authorizes
// private/const access to the member at `slotReg` on `classReg`, which is an object's lclass or a
// class object. When access is not authorized, it jumps to `mismatch`, the interpreter fallback that
// raises the error.
//
// - A member with no access bits is unrestricted.
// - A private or const member passes only when `classReg == currentClosure->l.p->ownerclass`. This is
//   exactly luaR_closureownsprivateaccess.
// - A const write also requires the closure to be the class's `__init` (luaR_closureisinit), and the
//   object being written to be the `self` in register 0 of its frame (luaR_checkconstassign).
//   `writtenObject` is the object being written, or noreg for a read.
//
// `slotReg` holds the raw member offset and must stay live across this call.
//
// Every temporary is reserved before the first branch. The branch to `authorized` rejoins the main
// line, so a register evicted after it would be stored only on the path that didn't take it.
void emitClassMemberAuthA64(
    AssemblyBuilderA64& build,
    IrRegAllocA64& regs,
    RegisterA64 classReg,
    RegisterA64 slotReg,
    RegisterA64 writtenObject,
    Label& mismatch
)
{
    bool isWrite = writtenObject != noreg;

    // See emitClassMemberAuthX64 for why a read also stops on LBC_CLASSMEMBER_INITBLOCKED.
    uint32_t restrictBits = isWrite ? (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST) : (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_INITBLOCKED);

    Label authorized;

    RegisterA64 owner = regs.allocTemp(KindA64::x);
    RegisterA64 flagw = regs.allocTemp(KindA64::w);

    // flag = classReg->memberflags[slot]
    build.ldr(owner, mem(classReg, offsetof(LuauClass, memberflags)));
    build.ldrb(flagw, mem(owner, slotReg));

    // PRIVATE | INITBLOCKED (bits 0 and 7) has no logical-immediate encoding, so test against a register
    RegisterA64 maskw = regs.allocTemp(KindA64::w);
    build.mov(maskw, int(restrictBits));
    build.tst(flagw, maskw);
    build.b(ConditionA64::Equal, authorized); // Z set: unrestricted member, no check needed

    // owner = currentClosure->l.p->ownerclass (NULL outside any class -> never matches)
    build.ldr(owner, mem(rClosure, offsetof(Closure, l.p)));
    build.ldr(owner, mem(owner, offsetof(Proto, ownerclass)));
    build.cmp(owner, classReg);
    build.b(ConditionA64::NotEqual, mismatch); // access from outside the owning class

    if (!isWrite)
    {
        build.tst(flagw, uint32_t(LBC_CLASSMEMBER_INITBLOCKED));
        build.b(ConditionA64::NotEqual, mismatch); // Z clear: blocked `__init`, even from inside the class
    }

    if (isWrite)
    {
        build.tst(flagw, uint32_t(LBC_CLASSMEMBER_CONST));
        build.b(ConditionA64::Equal, authorized); // private but not const: authorized

        // the flag and the mask are dead from here on, so they hold the index arithmetic
        RegisterA64 tempw = maskw;
        build.ldrb(tempw, mem(owner, offsetof(LuauClass, hascustominit)));
        build.cbz(tempw, mismatch); // no custom __init -> const is never writable

        // require currentClosure == owner->staticmembers[initoffset - numberofinstancemembers]
        RegisterA64 idxw = flagw;
        RegisterA64 idxx = castReg(KindA64::x, idxw);
        build.ldr(idxw, mem(owner, offsetof(LuauClass, initoffset)));
        build.ldr(tempw, mem(owner, offsetof(LuauClass, numberofinstancemembers)));
        build.sub(idxw, idxw, tempw);

        // owner is not read again, so it walks to the `__init` closure itself
        build.ldr(owner, mem(owner, offsetof(LuauClass, staticmembers)));
        build.add(owner, owner, idxx, kTValueSizeLog2);
        build.ldr(owner, mem(owner, offsetof(TValue, value.gc)));
        build.cmp(owner, rClosure);
        build.b(ConditionA64::NotEqual, mismatch); // const write from a non-__init method

        // require R0 (the `self` __init is constructing) to be the object being written
        build.ldr(tempw, mem(rBase, offsetof(TValue, tt)));
        build.cmp(tempw, uint16_t(LUA_TOBJECT));
        build.b(ConditionA64::NotEqual, mismatch);
        build.ldr(owner, mem(rBase, offsetof(TValue, value.gc)));
        build.cmp(owner, writtenObject);
        build.b(ConditionA64::NotEqual, mismatch); // const write to an object other than __init's own `self`
    }

    build.setLabel(authorized);
}

IrLoweringA64::IrLoweringA64(LogBuilder* logger, AssemblyBuilderA64& build, ModuleHelpers& helpers, IrFunction& function, LoweringStats* stats)
    : logger(logger)
    , build(build)
    , helpers(helpers)
    , function(function)
    , stats(stats)
    , regs(logger, build, function, stats, {{x0, x15}, {x16, x17}, {q0, q7}, {q16, q31}})
    , valueTracker(logger, function)
    , exitHandlerMap(~0u)
{
    valueTracker.setRestoreCallback(
        this,
        [](void* context, IrInst& inst)
        {
            IrLoweringA64* self = static_cast<IrLoweringA64*>(context);
            self->regs.restoreReg(inst);
        }
    );
}

void IrLoweringA64::lowerInst(IrInst& inst, uint32_t index, const IrBlock& next)
{
    regs.currInstIdx = index;

    valueTracker.beforeInstLowering(inst);

    switch (inst.cmd)
    {
    case IrCmd::LOAD_TAG:
        lower_LOAD_TAG(*this, inst, index, next);
        break;
    case IrCmd::LOAD_POINTER:
        lower_LOAD_POINTER(*this, inst, index, next);
        break;
    case IrCmd::LOAD_BUFFER_DATA:
        lower_LOAD_BUFFER_DATA(*this, inst, index, next);
        break;
    case IrCmd::LOAD_DOUBLE:
        lower_LOAD_DOUBLE(*this, inst, index, next);
        break;
    case IrCmd::LOAD_INT:
        lower_LOAD_INT(*this, inst, index, next);
        break;
    case IrCmd::LOAD_INT64:
        lower_LOAD_INT64(*this, inst, index, next);
        break;
    case IrCmd::LOAD_FLOAT:
        lower_LOAD_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::LOAD_TVALUE:
        lower_LOAD_TVALUE(*this, inst, index, next);
        break;
    case IrCmd::LOAD_ENV:
        lower_LOAD_ENV(*this, inst, index, next);
        break;
    case IrCmd::GET_ARR_ADDR:
        lower_GET_ARR_ADDR(*this, inst, index, next);
        break;
    case IrCmd::GET_SLOT_NODE_ADDR:
        lower_GET_SLOT_NODE_ADDR(*this, inst, index, next);
        break;
    case IrCmd::GET_HASH_NODE_ADDR:
        lower_GET_HASH_NODE_ADDR(*this, inst, index, next);
        break;
    case IrCmd::LOAD_OWNER_CLASS:
        lower_LOAD_OWNER_CLASS(*this, inst, index, next);
        break;
    case IrCmd::GET_CLOSURE_UPVAL_ADDR:
        lower_GET_CLOSURE_UPVAL_ADDR(*this, inst, index, next);
        break;
    case IrCmd::STORE_TAG:
        lower_STORE_TAG(*this, inst, index, next);
        break;
    case IrCmd::STORE_POINTER:
        lower_STORE_POINTER(*this, inst, index, next);
        break;
    case IrCmd::STORE_EXTRA:
        lower_STORE_EXTRA(*this, inst, index, next);
        break;
    case IrCmd::STORE_DOUBLE:
        lower_STORE_DOUBLE(*this, inst, index, next);
        break;
    case IrCmd::STORE_INT:
        lower_STORE_INT(*this, inst, index, next);
        break;
    case IrCmd::STORE_INT64:
        lower_STORE_INT64(*this, inst, index, next);
        break;
    case IrCmd::STORE_VECTOR:
        lower_STORE_VECTOR(*this, inst, index, next);
        break;
    case IrCmd::STORE_TVALUE:
        lower_STORE_TVALUE(*this, inst, index, next);
        break;
    case IrCmd::STORE_SPLIT_TVALUE:
        lower_STORE_SPLIT_TVALUE(*this, inst, index, next);
        break;
    case IrCmd::ADD_INT:
        lower_ADD_INT(*this, inst, index, next);
        break;
    case IrCmd::SUB_INT:
        lower_SUB_INT(*this, inst, index, next);
        break;
    case IrCmd::ADD_INT64:
        lower_ADD_INT64(*this, inst, index, next);
        break;
    case IrCmd::SUB_INT64:
        lower_SUB_INT64(*this, inst, index, next);
        break;
    case IrCmd::MUL_INT64:
        lower_MUL_INT64(*this, inst, index, next);
        break;
    case IrCmd::DIV_INT64:
        lower_DIV_INT64(*this, inst, index, next);
        break;
    case IrCmd::IDIV_INT64:
        lower_IDIV_INT64(*this, inst, index, next);
        break;
    case IrCmd::CHECK_DIV_INT64:
        lower_CHECK_DIV_INT64(*this, inst, index, next);
        break;
    case IrCmd::UDIV_INT64:
        lower_UDIV_INT64(*this, inst, index, next);
        break;
    case IrCmd::REM_INT64:
        lower_REM_INT64(*this, inst, index, next);
        break;
    case IrCmd::MOD_INT64:
        lower_MOD_INT64(*this, inst, index, next);
        break;
    case IrCmd::UREM_INT64:
        lower_UREM_INT64(*this, inst, index, next);
        break;
    case IrCmd::SEXTI8_INT:
        lower_SEXTI8_INT(*this, inst, index, next);
        break;
    case IrCmd::SEXTI16_INT:
        lower_SEXTI16_INT(*this, inst, index, next);
        break;
    case IrCmd::ADD_NUM:
        lower_ADD_NUM(*this, inst, index, next);
        break;
    case IrCmd::SUB_NUM:
        lower_SUB_NUM(*this, inst, index, next);
        break;
    case IrCmd::MUL_NUM:
        lower_MUL_NUM(*this, inst, index, next);
        break;
    case IrCmd::DIV_NUM:
        lower_DIV_NUM(*this, inst, index, next);
        break;
    case IrCmd::IDIV_NUM:
        lower_IDIV_NUM(*this, inst, index, next);
        break;
    case IrCmd::MOD_NUM:
        lower_MOD_NUM(*this, inst, index, next);
        break;
    case IrCmd::MULADD_NUM:
        lower_MULADD_NUM(*this, inst, index, next);
        break;
    case IrCmd::MIN_NUM:
        lower_MIN_NUM(*this, inst, index, next);
        break;
    case IrCmd::MAX_NUM:
        lower_MAX_NUM(*this, inst, index, next);
        break;
    case IrCmd::UNM_NUM:
        lower_UNM_NUM(*this, inst, index, next);
        break;
    case IrCmd::FLOOR_NUM:
        lower_FLOOR_NUM(*this, inst, index, next);
        break;
    case IrCmd::CEIL_NUM:
        lower_CEIL_NUM(*this, inst, index, next);
        break;
    case IrCmd::ROUND_NUM:
        lower_ROUND_NUM(*this, inst, index, next);
        break;
    case IrCmd::SQRT_NUM:
        lower_SQRT_NUM(*this, inst, index, next);
        break;
    case IrCmd::ABS_NUM:
        lower_ABS_NUM(*this, inst, index, next);
        break;
    case IrCmd::SIGN_NUM:
        lower_SIGN_NUM(*this, inst, index, next);
        break;
    case IrCmd::ADD_FLOAT:
        lower_ADD_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::SUB_FLOAT:
        lower_SUB_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::MUL_FLOAT:
        lower_MUL_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::DIV_FLOAT:
        lower_DIV_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::MIN_FLOAT:
        lower_MIN_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::MAX_FLOAT:
        lower_MAX_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::UNM_FLOAT:
        lower_UNM_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::FLOOR_FLOAT:
        lower_FLOOR_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::CEIL_FLOAT:
        lower_CEIL_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::SQRT_FLOAT:
        lower_SQRT_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::ABS_FLOAT:
        lower_ABS_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::SIGN_FLOAT:
        lower_SIGN_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::SELECT_NUM:
        lower_SELECT_NUM(*this, inst, index, next);
        break;
    case IrCmd::SELECT_INT64:
        lower_SELECT_INT64(*this, inst, index, next);
        break;
    case IrCmd::SELECT_VEC:
        lower_SELECT_VEC(*this, inst, index, next);
        break;
    case IrCmd::SELECT_IF_TRUTHY:
        lower_SELECT_IF_TRUTHY(*this, inst, index, next);
        break;
    case IrCmd::MULADD_VEC:
        lower_MULADD_VEC(*this, inst, index, next);
        break;
    case IrCmd::ADD_VEC:
        lower_ADD_VEC(*this, inst, index, next);
        break;
    case IrCmd::SUB_VEC:
        lower_SUB_VEC(*this, inst, index, next);
        break;
    case IrCmd::MUL_VEC:
        lower_MUL_VEC(*this, inst, index, next);
        break;
    case IrCmd::DIV_VEC:
        lower_DIV_VEC(*this, inst, index, next);
        break;
    case IrCmd::IDIV_VEC:
        lower_IDIV_VEC(*this, inst, index, next);
        break;
    case IrCmd::UNM_VEC:
        lower_UNM_VEC(*this, inst, index, next);
        break;
    case IrCmd::MIN_VEC:
        lower_MIN_VEC(*this, inst, index, next);
        break;
    case IrCmd::MAX_VEC:
        lower_MAX_VEC(*this, inst, index, next);
        break;
    case IrCmd::FLOOR_VEC:
        lower_FLOOR_VEC(*this, inst, index, next);
        break;
    case IrCmd::CEIL_VEC:
        lower_CEIL_VEC(*this, inst, index, next);
        break;
    case IrCmd::ABS_VEC:
        lower_ABS_VEC(*this, inst, index, next);
        break;
    case IrCmd::DOT_VEC:
        lower_DOT_VEC(*this, inst, index, next);
        break;
    case IrCmd::EXTRACT_VEC:
        lower_EXTRACT_VEC(*this, inst, index, next);
        break;
    case IrCmd::NOT_ANY:
        lower_NOT_ANY(*this, inst, index, next);
        break;
    case IrCmd::CMP_INT:
        lower_CMP_INT(*this, inst, index, next);
        break;
    case IrCmd::CMP_INT64:
        lower_CMP_INT64(*this, inst, index, next);
        break;
    case IrCmd::CMP_ANY:
        lower_CMP_ANY(*this, inst, index, next);
        break;
    case IrCmd::CMP_TAG:
        lower_CMP_TAG(*this, inst, index, next);
        break;
    case IrCmd::CMP_SPLIT_TVALUE:
        lower_CMP_SPLIT_TVALUE(*this, inst, index, next);
        break;
    case IrCmd::JUMP:
        lower_JUMP(*this, inst, index, next);
        break;
    case IrCmd::JUMP_IF_TRUTHY:
        lower_JUMP_IF_TRUTHY(*this, inst, index, next);
        break;
    case IrCmd::JUMP_IF_FALSY:
        lower_JUMP_IF_FALSY(*this, inst, index, next);
        break;
    case IrCmd::JUMP_EQ_TAG:
        lower_JUMP_EQ_TAG(*this, inst, index, next);
        break;
    case IrCmd::JUMP_CMP_INT:
        lower_JUMP_CMP_INT(*this, inst, index, next);
        break;
    case IrCmd::JUMP_EQ_POINTER:
        lower_JUMP_EQ_POINTER(*this, inst, index, next);
        break;
    case IrCmd::JUMP_CMP_NUM:
        lower_JUMP_CMP_NUM(*this, inst, index, next);
        break;
    case IrCmd::JUMP_CMP_FLOAT:
        lower_JUMP_CMP_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::JUMP_FORN_LOOP_COND:
        lower_JUMP_FORN_LOOP_COND(*this, inst, index, next);
        break;
    case IrCmd::TABLE_LEN:
        lower_TABLE_LEN(*this, inst, index, next);
        break;
    case IrCmd::STRING_LEN:
        lower_STRING_LEN(*this, inst, index, next);
        break;
    case IrCmd::BUFFER_ISFROZEN:
        lower_BUFFER_ISFROZEN(*this, inst, index, next);
        break;
    case IrCmd::CLASS_ISINSTANCE:
        lower_CLASS_ISINSTANCE(*this, inst, index, next);
        break;
    case IrCmd::TABLE_SETNUM:
        lower_TABLE_SETNUM(*this, inst, index, next);
        break;
    case IrCmd::NEW_TABLE:
        lower_NEW_TABLE(*this, inst, index, next);
        break;
    case IrCmd::DUP_TABLE:
        lower_DUP_TABLE(*this, inst, index, next);
        break;
    case IrCmd::TRY_NUM_TO_INDEX:
        lower_TRY_NUM_TO_INDEX(*this, inst, index, next);
        break;
    case IrCmd::TRY_CALL_FASTGETTM:
        lower_TRY_CALL_FASTGETTM(*this, inst, index, next);
        break;
    case IrCmd::NEW_USERDATA:
        lower_NEW_USERDATA(*this, inst, index, next);
        break;
    case IrCmd::NEW_VECTOR:
        lower_NEW_VECTOR(*this, inst, index, next);
        break;
    case IrCmd::INT64_TO_NUM:
        lower_INT64_TO_NUM(*this, inst, index, next);
        break;
    case IrCmd::INT_TO_NUM:
        lower_INT_TO_NUM(*this, inst, index, next);
        break;
    case IrCmd::UINT_TO_NUM:
        lower_UINT_TO_NUM(*this, inst, index, next);
        break;
    case IrCmd::UINT_TO_FLOAT:
        lower_UINT_TO_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::NUM_TO_INT:
        lower_NUM_TO_INT(*this, inst, index, next);
        break;
    case IrCmd::NUM_TO_INT64:
        lower_NUM_TO_INT64(*this, inst, index, next);
        break;
    case IrCmd::NUM_TO_UINT:
        lower_NUM_TO_UINT(*this, inst, index, next);
        break;
    case IrCmd::FLOAT_TO_NUM:
        lower_FLOAT_TO_NUM(*this, inst, index, next);
        break;
    case IrCmd::NUM_TO_FLOAT:
        lower_NUM_TO_FLOAT(*this, inst, index, next);
        break;
    case IrCmd::FLOAT_TO_VEC:
        lower_FLOAT_TO_VEC(*this, inst, index, next);
        break;
    case IrCmd::TAG_VECTOR:
        lower_TAG_VECTOR(*this, inst, index, next);
        break;
    case IrCmd::TRUNCATE_UINT:
        lower_TRUNCATE_UINT(*this, inst, index, next);
        break;
    case IrCmd::ADJUST_STACK_TO_REG:
        lower_ADJUST_STACK_TO_REG(*this, inst, index, next);
        break;
    case IrCmd::ADJUST_STACK_TO_TOP:
        lower_ADJUST_STACK_TO_TOP(*this, inst, index, next);
        break;
    case IrCmd::FASTCALL:
        lower_FASTCALL(*this, inst, index, next);
        break;
    case IrCmd::INVOKE_FASTCALL:
        lower_INVOKE_FASTCALL(*this, inst, index, next);
        break;
    case IrCmd::CHECK_FASTCALL_RES:
        lower_CHECK_FASTCALL_RES(*this, inst, index, next);
        break;
    case IrCmd::DO_ARITH:
        lower_DO_ARITH(*this, inst, index, next);
        break;
    case IrCmd::DO_LEN:
        lower_DO_LEN(*this, inst, index, next);
        break;
    case IrCmd::GET_TABLE:
        lower_GET_TABLE(*this, inst, index, next);
        break;
    case IrCmd::SET_TABLE:
        lower_SET_TABLE(*this, inst, index, next);
        break;
    case IrCmd::GET_CACHED_IMPORT:
        lower_GET_CACHED_IMPORT(*this, inst, index, next);
        break;
    case IrCmd::CONCAT:
        lower_CONCAT(*this, inst, index, next);
        break;
    case IrCmd::GET_UPVALUE:
        lower_GET_UPVALUE(*this, inst, index, next);
        break;
    case IrCmd::SET_UPVALUE:
        lower_SET_UPVALUE(*this, inst, index, next);
        break;
    case IrCmd::CHECK_TAG:
        lower_CHECK_TAG(*this, inst, index, next);
        break;
    case IrCmd::CHECK_TRUTHY:
        lower_CHECK_TRUTHY(*this, inst, index, next);
        break;
    case IrCmd::CHECK_READONLY:
        lower_CHECK_READONLY(*this, inst, index, next);
        break;
    case IrCmd::CHECK_NO_METATABLE:
        lower_CHECK_NO_METATABLE(*this, inst, index, next);
        break;
    case IrCmd::CHECK_SAFE_ENV:
        lower_CHECK_SAFE_ENV(*this, inst, index, next);
        break;
    case IrCmd::CHECK_ARRAY_SIZE:
        lower_CHECK_ARRAY_SIZE(*this, inst, index, next);
        break;
    case IrCmd::JUMP_SLOT_MATCH:
        lower_JUMP_SLOT_MATCH(*this, inst, index, next);
        break;
    case IrCmd::CHECK_SLOT_MATCH:
        lower_CHECK_SLOT_MATCH(*this, inst, index, next);
        break;
    case IrCmd::CHECK_NODE_NO_NEXT:
        lower_CHECK_NODE_NO_NEXT(*this, inst, index, next);
        break;
    case IrCmd::CHECK_NODE_VALUE:
        lower_CHECK_NODE_VALUE(*this, inst, index, next);
        break;
    case IrCmd::CHECK_BUFFER_MUTABLE:
        lower_CHECK_BUFFER_MUTABLE(*this, inst, index, next);
        break;
    case IrCmd::CHECK_BUFFER_LEN:
        lower_CHECK_BUFFER_LEN(*this, inst, index, next);
        break;
    case IrCmd::CHECK_USERDATA_TAG:
        lower_CHECK_USERDATA_TAG(*this, inst, index, next);
        break;
    case IrCmd::CHECK_OBJECT_CLASS:
        lower_CHECK_OBJECT_CLASS(*this, inst, index, next);
        break;
    case IrCmd::TRY_OBJECT_MEMBER_ADDR:
        lower_TRY_OBJECT_MEMBER_ADDR(*this, inst, index, next);
        break;
    case IrCmd::OBJECT_MEMBER_ADDR:
        lower_OBJECT_MEMBER_ADDR(*this, inst, index, next);
        break;
    case IrCmd::CHECK_CLASS_FIELDS_CONSTRUCTIBLE:
        lower_CHECK_CLASS_FIELDS_CONSTRUCTIBLE(*this, inst, index, next);
        break;
    case IrCmd::NEW_OBJECT:
        lower_NEW_OBJECT(*this, inst, index, next);
        break;
    case IrCmd::TRY_CLASS_MEMBER_ADDR:
        lower_TRY_CLASS_MEMBER_ADDR(*this, inst, index, next);
        break;
    case IrCmd::TRY_OBJECT_NAMECALL_ADDR:
        lower_TRY_OBJECT_NAMECALL_ADDR(*this, inst, index, next);
        break;
    case IrCmd::CHECK_CMP_NUM:
        lower_CHECK_CMP_NUM(*this, inst, index, next);
        break;
    case IrCmd::CHECK_CMP_INT:
        lower_CHECK_CMP_INT(*this, inst, index, next);
        break;
    case IrCmd::CHECK_CMP_INT64:
        lower_CHECK_CMP_INT64(*this, inst, index, next);
        break;
    case IrCmd::INTERRUPT:
        lower_INTERRUPT(*this, inst, index, next);
        break;
    case IrCmd::CHECK_GC:
        lower_CHECK_GC(*this, inst, index, next);
        break;
    case IrCmd::BARRIER_OBJ:
        lower_BARRIER_OBJ(*this, inst, index, next);
        break;
    case IrCmd::BARRIER_TABLE_BACK:
        lower_BARRIER_TABLE_BACK(*this, inst, index, next);
        break;
    case IrCmd::BARRIER_TABLE_FORWARD:
        lower_BARRIER_TABLE_FORWARD(*this, inst, index, next);
        break;
    case IrCmd::SET_SAVEDPC:
        lower_SET_SAVEDPC(*this, inst, index, next);
        break;
    case IrCmd::CLOSE_UPVALS:
        lower_CLOSE_UPVALS(*this, inst, index, next);
        break;
    case IrCmd::CAPTURE:
        lower_CAPTURE(*this, inst, index, next);
        break;
    case IrCmd::SETLIST:
        lower_SETLIST(*this, inst, index, next);
        break;
    case IrCmd::CALL:
        lower_CALL(*this, inst, index, next);
        break;
    case IrCmd::RETURN:
        lower_RETURN(*this, inst, index, next);
        break;
    case IrCmd::FORGLOOP:
        lower_FORGLOOP(*this, inst, index, next);
        break;
    case IrCmd::FORGLOOP_FALLBACK:
        lower_FORGLOOP_FALLBACK(*this, inst, index, next);
        break;
    case IrCmd::FORGPREP_XNEXT_FALLBACK:
        lower_FORGPREP_XNEXT_FALLBACK(*this, inst, index, next);
        break;
    case IrCmd::COVERAGE:
        lower_COVERAGE(*this, inst, index, next);
        break;

        // Full instruction fallbacks
    case IrCmd::FALLBACK_GETGLOBAL:
        lower_FALLBACK_GETGLOBAL(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_SETGLOBAL:
        lower_FALLBACK_SETGLOBAL(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_GETTABLEKS:
        lower_FALLBACK_GETTABLEKS(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_SETTABLEKS:
        lower_FALLBACK_SETTABLEKS(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_NAMECALL:
        lower_FALLBACK_NAMECALL(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_PREPVARARGS:
        lower_FALLBACK_PREPVARARGS(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_GETVARARGS:
        lower_FALLBACK_GETVARARGS(*this, inst, index, next);
        break;
    case IrCmd::NEWCLOSURE:
        lower_NEWCLOSURE(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_DUPCLOSURE:
        lower_FALLBACK_DUPCLOSURE(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_NEWOBJECT:
        lower_FALLBACK_NEWOBJECT(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_NEWCLASSMEMBER:
        lower_FALLBACK_NEWCLASSMEMBER(*this, inst, index, next);
        break;
    case IrCmd::FALLBACK_FORGPREP:
        lower_FALLBACK_FORGPREP(*this, inst, index, next);
        break;

    // Pseudo instructions
    case IrCmd::NOP:
    case IrCmd::SUBSTITUTE:
    case IrCmd::MARK_USED:
    case IrCmd::MARK_DEAD:
        CODEGEN_ASSERT(!"Pseudo instructions should not be lowered");
        break;

    case IrCmd::BITAND_INT64:
        lower_BITAND_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITXOR_INT64:
        lower_BITXOR_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITOR_INT64:
        lower_BITOR_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITNOT_INT64:
        lower_BITNOT_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITLSHIFT_INT64:
        lower_BITLSHIFT_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITRSHIFT_INT64:
        lower_BITRSHIFT_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITARSHIFT_INT64:
        lower_BITARSHIFT_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITLROTATE_INT64:
        lower_BITLROTATE_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITRROTATE_INT64:
        lower_BITRROTATE_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITCOUNTLZ_INT64:
        lower_BITCOUNTLZ_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITCOUNTRZ_INT64:
        lower_BITCOUNTRZ_INT64(*this, inst, index, next);
        break;
    case IrCmd::BYTESWAP_INT64:
        lower_BYTESWAP_INT64(*this, inst, index, next);
        break;
    case IrCmd::BITAND_UINT:
        lower_BITAND_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITXOR_UINT:
        lower_BITXOR_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITOR_UINT:
        lower_BITOR_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITNOT_UINT:
        lower_BITNOT_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITLSHIFT_UINT:
        lower_BITLSHIFT_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITRSHIFT_UINT:
        lower_BITRSHIFT_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITARSHIFT_UINT:
        lower_BITARSHIFT_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITLROTATE_UINT:
        lower_BITLROTATE_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITRROTATE_UINT:
        lower_BITRROTATE_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITCOUNTLZ_UINT:
        lower_BITCOUNTLZ_UINT(*this, inst, index, next);
        break;
    case IrCmd::BITCOUNTRZ_UINT:
        lower_BITCOUNTRZ_UINT(*this, inst, index, next);
        break;
    case IrCmd::BYTESWAP_UINT:
        lower_BYTESWAP_UINT(*this, inst, index, next);
        break;
    case IrCmd::INVOKE_LIBM:
        lower_INVOKE_LIBM(*this, inst, index, next);
        break;
    case IrCmd::GET_TYPE:
        lower_GET_TYPE(*this, inst, index, next);
        break;
    case IrCmd::GET_TYPEOF:
        lower_GET_TYPEOF(*this, inst, index, next);
        break;

    case IrCmd::FINDUPVAL:
        lower_FINDUPVAL(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_READI8:
        lower_BUFFER_READI8(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_READU8:
        lower_BUFFER_READU8(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_WRITEI8:
        lower_BUFFER_WRITEI8(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_READI16:
        lower_BUFFER_READI16(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_READU16:
        lower_BUFFER_READU16(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_WRITEI16:
        lower_BUFFER_WRITEI16(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_READI32:
        lower_BUFFER_READI32(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_WRITEI32:
        lower_BUFFER_WRITEI32(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_READF32:
        lower_BUFFER_READF32(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_WRITEF32:
        lower_BUFFER_WRITEF32(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_READF64:
        lower_BUFFER_READF64(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_WRITEF64:
        lower_BUFFER_WRITEF64(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_READI64:
        lower_BUFFER_READI64(*this, inst, index, next);
        break;

    case IrCmd::BUFFER_WRITEI64:
        lower_BUFFER_WRITEI64(*this, inst, index, next);
        break;

    case IrCmd::JUMP_CMP_PROTOID:
        lower_JUMP_CMP_PROTOID(*this, inst, index, next);
        break;

        // To handle unsupported instructions, add "case IrCmd::OP" and make sure to set error = true!
    }

    valueTracker.afterInstLowering(inst, index);

    regs.currInstIdx = kInvalidInstIdx;

    regs.freeLastUseRegs(inst, index);
    regs.freeTempRegs();
}

void IrLoweringA64::startBlock(const IrBlock& curr)
{
    if (curr.startpc != kBlockNoStartPc)
        allocAndIncrementCounterAt(
            curr.kind == IrBlockKind::Fallback ? CodeGenCounter::FallbackBlockExecuted : CodeGenCounter::RegularBlockExecuted, curr.startpc
        );

    if (curr.kind == IrBlockKind::ExitSync)
        regs.setupExitSyncEntry(function.getBlockIndex(curr));
}

void IrLoweringA64::finishBlock(const IrBlock& curr, const IrBlock& next)
{
    if (!regs.spills.empty())
    {
        // If we have spills remaining, we have to immediately lower the successor block
        for (uint32_t predIdx : predecessors(function.cfg, function.getBlockIndex(next)))
            CODEGEN_ASSERT(predIdx == function.getBlockIndex(curr) || function.blocks[predIdx].kind == IrBlockKind::Dead);

        // And the next block cannot be a join block in cfg
        CODEGEN_ASSERT(next.useCount == 1);
    }
}

void IrLoweringA64::finishFunction()
{
    if (FFlag::LuauCodegenSharedLog && logger && logger->options.includeAssembly)
        logger->formatAppend("; interrupt handlers\n");
    else if (!FFlag::LuauCodegenSharedLog && build.logText)
        build.logAppend("; interrupt handlers\n");

    for (InterruptHandler& handler : interruptHandlers)
    {
        build.setLabel(handler.self);
        build.mov(x0, (handler.pcpos + 1) * sizeof(Instruction));
        build.adr(x1, handler.next);
        build.b(helpers.interrupt);
    }

    if (FFlag::LuauCodegenSharedLog && logger && logger->options.includeAssembly)
        logger->formatAppend("; exit handlers\n");
    else if (!FFlag::LuauCodegenSharedLog && build.logText)
        build.logAppend("; exit handlers\n");

    for (ExitHandler& handler : exitHandlers)
    {
        if (handler.pcpos == kVmExitEntryGuardPc)
        {
            build.setLabel(handler.self);

            allocAndIncrementCounterAt(CodeGenCounter::VmExitTaken, ~0u);

            build.b(helpers.exitContinueVmClearNativeFlag);
        }
        else
        {
            build.setLabel(handler.self);

            allocAndIncrementCounterAt(CodeGenCounter::VmExitTaken, handler.pcpos);

            build.mov(x0, handler.pcpos * sizeof(Instruction));
            build.b(helpers.updatePcAndContinueInVm);
        }
    }

    // An undefined instruction is placed after the function to be used as an aborting jump offset
    function.endLocation = build.getLabelOffset(build.setLabel());
    build.udf();

    if (stats)
    {
        if (error)
            stats->loweringErrors++;

        if (regs.error)
            stats->regAllocErrors++;
    }
}

bool IrLoweringA64::hasError() const
{
    return error || regs.error;
}

bool IrLoweringA64::isFallthroughBlock(const IrBlock& target, const IrBlock& next)
{
    return target.start == next.start;
}

void IrLoweringA64::jumpOrFallthrough(IrBlock& target, const IrBlock& next)
{
    if (!isFallthroughBlock(target, next))
        build.b(target.label);
}

Label& IrLoweringA64::getTargetLabel(IrOp op, uint32_t index, Label& fresh)
{
    if (op.kind == IrOpKind::Undef)
        return fresh;

    if (op.kind == IrOpKind::VmExit)
    {
        if (uint32_t* index = exitHandlerMap.find(vmExitOp(op)))
            return exitHandlers[*index].self;

        return fresh;
    }

    return labelOp(op);
}

void IrLoweringA64::finalizeTargetLabel(IrOp op, uint32_t index, Label& fresh)
{
    if (op.kind == IrOpKind::Undef)
    {
        emitAbort(build, fresh);
    }
    else if (op.kind == IrOpKind::Block && blockOp(op).kind == IrBlockKind::ExitSync)
    {
        // Multi-branch instructions must capture exitSyncAllocToken before the first branch to verify all sync exit branches have same state
        if (exitSyncInstIdx == index)
            CODEGEN_ASSERT(exitSyncAllocToken == regs.getAllocToken());

        // Snapshot current register/spill locations of values the exit sync block needs, and release registers at last use
        VmExitSyncInfo* syncInfo = function.vmExitInfo.find(index);
        CODEGEN_ASSERT(syncInfo);

        for (auto argOp : syncInfo->argOps)
            regs.recordAndFreeLastUse(op.index, function.instOp(argOp), index);
    }
    else if (op.kind == IrOpKind::VmExit && fresh.id != 0)
    {
        exitHandlerMap[vmExitOp(op)] = uint32_t(exitHandlers.size());
        exitHandlers.push_back({fresh, vmExitOp(op)});
    }
}

void IrLoweringA64::checkSafeEnv(IrOp target, uint32_t index, const IrBlock& next)
{
    Label fresh; // used when guard aborts execution or jumps to a VM exit
    RegisterA64 temp = regs.allocTemp(KindA64::x);
    RegisterA64 tempw = castReg(KindA64::w, temp);
    build.ldr(temp, mem(rClosure, offsetof(Closure, env)));
    build.ldrb(tempw, mem(temp, offsetof(LuaTable, safeenv)));
    build.cbz(tempw, getTargetLabel(target, index, fresh));
    finalizeTargetLabel(target, index, fresh);
}

void IrLoweringA64::allocAndIncrementCounterAt(CodeGenCounter kind, uint32_t pcpos)
{
    if (!function.recordCounters)
        return;

    if (FFlag::LuauCodegenSharedLog && logger && logger->options.includeAssembly)
        logger->formatAppend("; counter kind %u at pcpos %d\n", unsigned(kind), pcpos);
    else if (!FFlag::LuauCodegenSharedLog && build.logText)
        build.logAppend("; counter kind %u at pcpos %d\n", unsigned(kind), pcpos);

    // {uint32_t, uint32_t, uint64_t}
    function.extraNativeData.push_back(unsigned(kind));
    function.extraNativeData.push_back(pcpos);
    incrementCounterAt(function.extraNativeData.size());
    function.extraNativeData.push_back(0);
    function.extraNativeData.push_back(0);
}

void IrLoweringA64::incrementCounterAt(size_t offset)
{
    RegisterA64 temp1 = regs.allocTemp(KindA64::x);
    RegisterA64 temp2 = regs.allocTemp(KindA64::x);

    // Get counter slot
    build.ldr(temp1, mem(rClosure, offsetof(Closure, l.p)));
    build.ldr(temp1, mem(temp1, offsetof(Proto, execdata)));
    emitAddOffset(build, temp2, temp1, (unsigned(function.proto->sizecode) + offset) * 4);

    // Increment
    build.ldr(temp1, temp2);
    build.add(temp1, temp1, uint16_t(1));
    build.str(temp1, temp2);

    regs.freeTemp(temp1);
    regs.freeTemp(temp2);
}

void IrLoweringA64::checkObjectBarrierConditions(RegisterA64 object, RegisterA64 temp, RegisterA64 ra, IrOp raOp, int ratag, Label& skip)
{
    RegisterA64 tempw = castReg(KindA64::w, temp);

    // iscollectable(ra)
    if (ratag == -1 || !isGCO(ratag))
    {
        if (raOp.kind == IrOpKind::Inst)
        {
            build.umov_4s(tempw, ra, 3);
        }
        else
        {
            AddressA64 addr = tempAddr(raOp, offsetof(TValue, tt), temp);
            build.ldr(tempw, addr);
        }

        build.cmp(tempw, uint16_t(LUA_TSTRING));
        build.b(ConditionA64::Less, skip);
    }

    // isblack(obj2gco(o))
    build.ldrb(tempw, mem(object, offsetof(GCheader, marked)));
    build.tbz(tempw, BLACKBIT, skip);

    // iswhite(gcvalue(ra))
    if (raOp.kind == IrOpKind::Inst)
    {
        build.fmov(temp, castReg(KindA64::d, ra));
    }
    else
    {
        AddressA64 addr = tempAddr(raOp, offsetof(TValue, value), temp);
        build.ldr(temp, addr);
    }

    build.ldrb(tempw, mem(temp, offsetof(GCheader, marked)));
    build.tst(tempw, bit2mask(WHITE0BIT, WHITE1BIT));
    build.b(ConditionA64::Equal, skip); // Equal = Zero after tst
}

RegisterA64 IrLoweringA64::tempDouble(IrOp op)
{
    if (op.kind == IrOpKind::Inst)
        return regOp(op);
    else if (op.kind == IrOpKind::Constant)
    {
        double val = doubleOp(op);

        if (AssemblyBuilderA64::isFmovSupportedFp64(val))
        {
            RegisterA64 temp = regs.allocTemp(KindA64::d);
            build.fmov(temp, val);
            return temp;
        }
        else
        {
            RegisterA64 temp1 = regs.allocTemp(KindA64::x);
            RegisterA64 temp2 = regs.allocTemp(KindA64::d);
            emitLoadDoubleBits(build, temp2, temp1, val);
            return temp2;
        }
    }
    else
    {
        CODEGEN_ASSERT(!"Unsupported instruction form");
        return noreg;
    }
}

RegisterA64 IrLoweringA64::tempFloat(IrOp op)
{
    if (op.kind == IrOpKind::Inst)
        return regOp(op);
    else if (op.kind == IrOpKind::Constant)
    {
        float val = float(doubleOp(op));

        if (AssemblyBuilderA64::isFmovSupportedFp32(val))
        {
            RegisterA64 temp = regs.allocTemp(KindA64::s);
            build.fmov(temp, val);
            return temp;
        }
        else
        {
            RegisterA64 temp = regs.allocTemp(KindA64::s);
            RegisterA64 temp2 = regs.allocTemp(KindA64::x);
            emitLoadFloatBits(build, temp, temp2, val);
            return temp;
        }
    }
    else
    {
        CODEGEN_ASSERT(!"Unsupported instruction form");
        return noreg;
    }
}

RegisterA64 IrLoweringA64::tempInt(IrOp op)
{
    if (op.kind == IrOpKind::Inst)
        return regOp(op);
    else if (op.kind == IrOpKind::Constant)
    {
        RegisterA64 temp = regs.allocTemp(KindA64::w);
        build.mov(temp, intOp(op));
        return temp;
    }
    else
    {
        CODEGEN_ASSERT(!"Unsupported instruction form");
        return noreg;
    }
}

RegisterA64 IrLoweringA64::tempInt64(IrOp op)
{
    if (op.kind == IrOpKind::Inst)
        return regOp(op);
    else if (op.kind == IrOpKind::Constant)
    {
        RegisterA64 temp = regs.allocTemp(KindA64::x);
        uint64_t u = uint64_t(int64Op(op));

        // Count non-zero halfwords (movz path) vs non-0xFFFF halfwords (movn path)
        int movzCount = 0;
        int movnCount = 0;
        for (int shift = 0; shift < 64; shift += 16)
        {
            uint16_t hw = uint16_t(u >> shift);
            if (hw != 0)
                movzCount++;
            if (hw != 0xFFFF)
                movnCount++;
        }

        if (movzCount <= movnCount)
        {
            // movz path: emit movz for first non-zero halfword, movk for rest
            bool first = true;
            for (int shift = 0; shift < 64; shift += 16)
            {
                uint16_t hw = uint16_t(u >> shift);
                if (hw != 0)
                {
                    if (first)
                    {
                        build.movz(temp, hw, shift);
                        first = false;
                    }
                    else
                    {
                        build.movk(temp, hw, shift);
                    }
                }
            }

            if (first)
                build.movz(temp, 0);
        }
        else
        {
            // movn path: use movn for first non-0xFFFF halfword, movk for rest
            bool first = true;
            for (int shift = 0; shift < 64; shift += 16)
            {
                uint16_t hw = uint16_t(u >> shift);
                if (hw != 0xFFFF)
                {
                    if (first)
                    {
                        build.movn(temp, uint16_t(~hw), shift);
                        first = false;
                    }
                    else
                    {
                        build.movk(temp, hw, shift);
                    }
                }
            }

            if (first)
                build.movn(temp, 0);
        }

        return temp;
    }
    else
    {
        CODEGEN_ASSERT(!"Unsupported instruction form");
        return noreg;
    }
}

RegisterA64 IrLoweringA64::tempUint(IrOp op)
{
    if (op.kind == IrOpKind::Inst)
        return regOp(op);
    else if (op.kind == IrOpKind::Constant)
    {
        RegisterA64 temp = regs.allocTemp(KindA64::w);
        build.mov(temp, unsigned(intOp(op)));
        return temp;
    }
    else
    {
        CODEGEN_ASSERT(!"Unsupported instruction form");
        return noreg;
    }
}

AddressA64 IrLoweringA64::tempAddr(IrOp op, int offset, RegisterA64 tempStorage)
{
    // This is needed to tighten the bounds checks in the VmConst case below
    CODEGEN_ASSERT(offset % 4 == 0);
    // Full encoded range is wider depending on the load size, but this assertion helps establish a smaller guaranteed working range [0..4096)
    CODEGEN_ASSERT(offset >= 0 && unsigned(offset / 4) <= AssemblyBuilderA64::kMaxImmediate);

    if (op.kind == IrOpKind::VmReg)
    {
        return mem(rBase, vmRegOp(op) * sizeof(TValue) + offset);
    }
    else if (op.kind == IrOpKind::VmConst)
    {
        size_t constantOffset = vmConstOp(op) * sizeof(TValue) + offset;

        // Note: cumulative offset is guaranteed to be divisible by 4; we can use that to expand the useful range that doesn't require temporaries
        if (constantOffset / 4 <= AddressA64::kMaxOffset)
            return mem(rConstants, int(constantOffset));

        RegisterA64 temp = tempStorage == noreg ? regs.allocTemp(KindA64::x) : tempStorage;
        CODEGEN_ASSERT(temp.kind == KindA64::x && "temp storage, when provided, must be an 'x' register");

        emitAddOffset(build, temp, rConstants, constantOffset);
        return temp;
    }
    // If we have a register, we assume it's a pointer to TValue
    else if (op.kind == IrOpKind::Inst)
    {
        CODEGEN_ASSERT(getCmdValueKind(function.instOp(op).cmd) == IrValueKind::Pointer);
        return mem(regOp(op), offset);
    }
    else
    {
        CODEGEN_ASSERT(!"Unsupported instruction form");
        return noreg;
    }
}

AddressA64 IrLoweringA64::tempAddrBuffer(IrOp bufferOp, IrOp indexOp, uint8_t tag)
{
    CODEGEN_ASSERT(tag == LUA_TUSERDATA || tag == LUA_TBUFFER || tag == LUA_TVECTOR);
    int dataOffset = tag == LUA_TBUFFER ? 0 : tag == LUA_TVECTOR ? offsetof(LuauVector, v) : offsetof(Udata, data);
    if (indexOp.kind == IrOpKind::Inst)
    {
        CODEGEN_ASSERT(!producesDirtyHighRegisterBits(function.instOp(indexOp).cmd));

        RegisterA64 temp = regs.allocTemp(KindA64::x);
        build.add(temp, regOp(bufferOp), regOp(indexOp)); // implicit uxtw
        return mem(temp, dataOffset);
    }
    else if (indexOp.kind == IrOpKind::Constant)
    {
        // Since the resulting address may be used to load any size, including 1 byte, from an unaligned offset, we are limited by unscaled
        // encoding
        if (unsigned(intOp(indexOp)) + dataOffset <= 255)
            return mem(regOp(bufferOp), int(intOp(indexOp) + dataOffset));

        // indexOp can only be negative in dead code (since offsets are checked); this avoids assertion in emitAddOffset
        if (intOp(indexOp) < 0)
            return mem(regOp(bufferOp), dataOffset);

        RegisterA64 temp = regs.allocTemp(KindA64::x);
        emitAddOffset(build, temp, regOp(bufferOp), size_t(intOp(indexOp)));
        return mem(temp, dataOffset);
    }

    CODEGEN_ASSERT(!"Unsupported instruction form");
    return noreg;
}

RegisterA64 IrLoweringA64::regOp(IrOp op)
{
    IrInst& inst = function.instOp(op);

    if (inst.spilled || inst.needsReload)
        regs.restoreReg(inst);

    CODEGEN_ASSERT(inst.regA64 != noreg);
    return inst.regA64;
}

IrConst IrLoweringA64::constOp(IrOp op) const
{
    return function.constOp(op);
}

uint8_t IrLoweringA64::tagOp(IrOp op) const
{
    return function.tagOp(op);
}

int IrLoweringA64::intOp(IrOp op) const
{
    return function.intOp(op);
}

int64_t IrLoweringA64::int64Op(IrOp op) const
{
    return function.int64Op(op);
}

unsigned IrLoweringA64::uintOp(IrOp op) const
{
    return function.uintOp(op);
}

unsigned IrLoweringA64::importOp(IrOp op) const
{
    return function.importOp(op);
}

double IrLoweringA64::doubleOp(IrOp op) const
{
    return function.doubleOp(op);
}

IrBlock& IrLoweringA64::blockOp(IrOp op) const
{
    return function.blockOp(op);
}

Label& IrLoweringA64::labelOp(IrOp op) const
{
    return blockOp(op).label;
}

} // namespace A64
} // namespace CodeGen
} // namespace Luau
