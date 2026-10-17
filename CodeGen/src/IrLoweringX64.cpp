// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "IrLoweringX64.h"

#include "Luau/CodeGenOptions.h"
#include "Luau/DenseHash.h"
#include "Luau/IrCallWrapperX64.h"
#include "Luau/Bytecode.h"
#include "Luau/IrData.h"
#include "Luau/IrUtils.h"
#include "Luau/LoweringStats.h"

#include "EmitBuiltinsX64.h"
#include "EmitCommonX64.h"
#include "EmitInstructionX64.h"
#include "NativeState.h"

#include "generated/LoweringRulesX64.h"

#include "lstate.h"
#include "lgc.h"

LUAU_FASTFLAG(LuauCodegenFixBufferLenCheck)
LUAU_FASTFLAG(LuauYieldIter2)
LUAU_FASTFLAG(LuauCIProto)
LUAU_FASTFLAG(LuauCodegenSharedLog)

namespace Luau
{
namespace CodeGen
{
namespace X64
{

IrLoweringX64::IrLoweringX64(LogBuilder* logger, AssemblyBuilderX64& build, ModuleHelpers& helpers, IrFunction& function, LoweringStats* stats)
    : logger(logger)
    , build(build)
    , helpers(helpers)
    , function(function)
    , stats(stats)
    , regs(logger, build, function, stats)
    , valueTracker(logger, function)
    , exitHandlerMap(~0u)
{
    valueTracker.setRestoreCallback(
        &regs,
        [](void* context, IrInst& inst)
        {
            ((IrRegAllocX64*)context)->restore(inst, false);
        }
    );

    build.align(kFunctionAlignment, X64::AlignmentDataX64::Ud2);
}

// Luwu Classes (rfcs/classes): authorize private/const access to the member at `slotReg` on
// class `classReg` (an object's lclass, or a class object directly) without bailing to the
// interpreter. A member with no access bits is unrestricted. A private/const member takes the fast
// path only when `classReg == currentClosure->l.p->ownerclass`, which is exactly
// luaR_closureownsprivateaccess (the class's methods and closures nested in them). A write passes the
// object being written as `writtenObject` (noreg for a read); a const member then additionally
// requires the closure to be the class's __init (luaR_closureisinit) and the object to be the `self`
// that __init is constructing, which is register 0 of its frame (luaR_checkconstassign).
// Everything else jumps to `mismatch`, where the interpreter fallback raises the correct error.
// `slotReg` holds the raw member offset (before it's scaled into a byte address) and must stay live
// across this call.
void emitClassMemberAuthX64(
    AssemblyBuilderX64& build,
    IrRegAllocX64& regs,
    RegisterX64 classReg,
    RegisterX64 slotReg,
    RegisterX64 writtenObject,
    Label& mismatch
)
{
    bool isWrite = writtenObject != noreg;

    // A read also stops on a blocked `__init` (LBC_CLASSMEMBER_INITBLOCKED), which no closure is
    // authorized for; writes never target `__init`, since it is a static member.
    uint8_t restrictBits = isWrite ? (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST) : (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_INITBLOCKED);

    Label authorized;

    ScopedRegX64 flag{regs, SizeX64::qword};
    ScopedRegX64 owner{regs, SizeX64::qword};

    // flag = classReg->memberflags[slot]
    build.mov(owner.reg, qword[classReg + offsetof(LuauClass, memberflags)]);
    build.movzx(dwordReg(flag.reg), byte[owner.reg + slotReg]);

    build.test(byteReg(flag.reg), restrictBits);
    build.jcc(ConditionX64::Zero, authorized); // unrestricted member: no check needed

    // owner = currentClosure->l.p->ownerclass (NULL outside any class -> never matches)
    build.mov(owner.reg, sClosure);
    build.mov(owner.reg, qword[owner.reg + offsetof(Closure, l.p)]);
    build.mov(owner.reg, qword[owner.reg + offsetof(Proto, ownerclass)]);
    build.cmp(owner.reg, classReg);
    build.jcc(ConditionX64::NotEqual, mismatch); // access from outside the owning class

    if (!isWrite)
    {
        build.test(byteReg(flag.reg), int8_t(LBC_CLASSMEMBER_INITBLOCKED));
        build.jcc(ConditionX64::NotZero, mismatch); // blocked `__init`, even from inside the class
    }

    if (isWrite)
    {
        // in-class: private is authorized; a const member additionally requires __init
        build.test(byteReg(flag.reg), int8_t(LBC_CLASSMEMBER_CONST));
        build.jcc(ConditionX64::Zero, authorized);

        build.cmp(byte[owner.reg + offsetof(LuauClass, hascustominit)], 0);
        build.jcc(ConditionX64::Equal, mismatch); // no custom __init -> const is never writable

        // require currentClosure == owner->staticmembers[initoffset - numberofinstancemembers]
        build.mov(dwordReg(flag.reg), dword[owner.reg + offsetof(LuauClass, initoffset)]);
        build.sub(dwordReg(flag.reg), dword[owner.reg + offsetof(LuauClass, numberofinstancemembers)]);
        build.shl(flag.reg, kTValueSizeLog2);
        build.mov(owner.reg, qword[owner.reg + offsetof(LuauClass, staticmembers)]);
        build.add(owner.reg, flag.reg);
        build.mov(owner.reg, qword[owner.reg + offsetof(TValue, value.gc)]);
        build.cmp(owner.reg, sClosure);
        build.jcc(ConditionX64::NotEqual, mismatch); // const write from a non-__init method

        // require R0 (the `self` __init is constructing) to be the object being written
        build.cmp(dword[rBase + offsetof(TValue, tt)], LUA_TOBJECT);
        build.jcc(ConditionX64::NotEqual, mismatch);
        build.cmp(qword[rBase + offsetof(TValue, value.gc)], writtenObject);
        build.jcc(ConditionX64::NotEqual, mismatch); // const write to an object other than __init's own `self`
    }

    build.setLabel(authorized);
}

void IrLoweringX64::lowerInst(IrInst& inst, uint32_t index, const IrBlock& next)
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
    case IrCmd::ADD_INT64:
        lower_ADD_INT64(*this, inst, index, next);
        break;
    case IrCmd::SUB_INT:
        lower_SUB_INT(*this, inst, index, next);
        break;
    case IrCmd::SUB_INT64:
        lower_SUB_INT64(*this, inst, index, next);
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
    case IrCmd::MUL_INT64:
        lower_MUL_INT64(*this, inst, index, next);
        break;
    case IrCmd::DIV_NUM:
        lower_DIV_NUM(*this, inst, index, next);
        break;
    case IrCmd::DIV_INT64:
        lower_DIV_INT64(*this, inst, index, next);
        break;
    case IrCmd::IDIV_NUM:
        lower_IDIV_NUM(*this, inst, index, next);
        break;
    case IrCmd::IDIV_INT64:
        lower_IDIV_INT64(*this, inst, index, next);
        break;
    case IrCmd::UDIV_INT64:
        lower_UDIV_INT64(*this, inst, index, next);
        break;
    case IrCmd::REM_INT64:
        lower_REM_INT64(*this, inst, index, next);
        break;
    case IrCmd::UREM_INT64:
        lower_UREM_INT64(*this, inst, index, next);
        break;
    case IrCmd::MULADD_NUM:
        lower_MULADD_NUM(*this, inst, index, next);
        break;
    case IrCmd::MOD_NUM:
        lower_MOD_NUM(*this, inst, index, next);
        break;
    case IrCmd::MOD_INT64:
        lower_MOD_INT64(*this, inst, index, next);
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
    case IrCmd::MULADD_VEC:
        lower_MULADD_VEC(*this, inst, index, next);
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
    case IrCmd::TABLE_SETNUM:
        lower_TABLE_SETNUM(*this, inst, index, next);
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

        // Fallbacks to non-IR instruction implementations
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

    case IrCmd::CHECK_DIV_INT64:
        lower_CHECK_DIV_INT64(*this, inst, index, next);
        break;
    case IrCmd::CHECK_CMP_INT64:
        lower_CHECK_CMP_INT64(*this, inst, index, next);
        break;
    case IrCmd::CMP_INT64:
        lower_CMP_INT64(*this, inst, index, next);
        break;
    case IrCmd::INT64_TO_NUM:
        lower_INT64_TO_NUM(*this, inst, index, next);
        break;
    case IrCmd::NUM_TO_INT64:
        lower_NUM_TO_INT64(*this, inst, index, next);
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
    case IrCmd::JUMP_CMP_PROTOID:
        lower_JUMP_CMP_PROTOID(*this, inst, index, next);
        break;

    // Pseudo instructions
    case IrCmd::NOP:
    case IrCmd::SUBSTITUTE:
    case IrCmd::MARK_USED:
    case IrCmd::MARK_DEAD:
        CODEGEN_ASSERT(!"Pseudo instructions should not be lowered");
        break;
    }

    valueTracker.afterInstLowering(inst, index);

    regs.currInstIdx = kInvalidInstIdx;

    regs.freeLastUseRegs(inst, index);
}

void IrLoweringX64::startBlock(const IrBlock& curr)
{
    if (curr.startpc != kBlockNoStartPc)
        allocAndIncrementCounterAt(
            curr.kind == IrBlockKind::Fallback ? CodeGenCounter::FallbackBlockExecuted : CodeGenCounter::RegularBlockExecuted, curr.startpc
        );

    if (curr.kind == IrBlockKind::ExitSync)
        regs.setupExitSyncEntry(function.getBlockIndex(curr));
}

void IrLoweringX64::finishBlock(const IrBlock& curr, const IrBlock& next)
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

void IrLoweringX64::finishFunction()
{
    if (FFlag::LuauCodegenSharedLog && logger && logger->options.includeAssembly)
        logger->formatAppend("; interrupt handlers\n");
    else if (!FFlag::LuauCodegenSharedLog && build.logText)
        build.logAppend("; interrupt handlers\n");

    for (InterruptHandler& handler : interruptHandlers)
    {
        build.setLabel(handler.self);
        build.mov(eax, handler.pcpos + 1);
        build.lea(rbx, handler.next);
        build.jmp(helpers.interrupt);
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

            build.jmp(helpers.exitContinueVmClearNativeFlag);
        }
        else
        {
            build.setLabel(handler.self);

            allocAndIncrementCounterAt(CodeGenCounter::VmExitTaken, handler.pcpos);

            build.mov(edx, handler.pcpos * sizeof(Instruction));
            build.jmp(helpers.updatePcAndContinueInVm);
        }
    }

    // An undefined instruction is placed after the function to be used as an aborting jump offset
    function.endLocation = build.getLabelOffset(build.setLabel());
    build.ud2();

    if (stats)
    {
        if (FFlag::LuauCodegenNoEcbData)
        {
            if (regs.maxUsedSlot > kSpillSlots)
                stats->regAllocErrors++;
        }
        else
        {
            if (regs.maxUsedSlot > kSpillSlots_DEPRECATED + kExtraSpillSlots_DEPRECATED)
                stats->regAllocErrors++;
        }

        if (regs.maxUsedSlot > stats->maxSpillSlotsUsed)
            stats->maxSpillSlotsUsed = regs.maxUsedSlot;
    }
}

bool IrLoweringX64::hasError() const
{
    // If register allocator had to use more stack slots than we have available, this function can't run natively
    if (FFlag::LuauCodegenNoEcbData)
    {
        if (regs.maxUsedSlot > kSpillSlots)
            return true;
    }
    else
    {
        if (regs.maxUsedSlot > kSpillSlots_DEPRECATED + kExtraSpillSlots_DEPRECATED)
            return true;
    }

    return false;
}

bool IrLoweringX64::isFallthroughBlock(const IrBlock& target, const IrBlock& next)
{
    return target.start == next.start;
}

Label& IrLoweringX64::getTargetLabel(IrOp op, uint32_t index, Label& fresh)
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

void IrLoweringX64::finalizeTargetLabel(IrOp op, uint32_t index, Label& fresh)
{
    if (op.kind == IrOpKind::Block && function.blockOp(op).kind == IrBlockKind::ExitSync)
    {
        // If branches were emitted via jumpOrAbortOnUndefNoFinalize, verify no allocations happened since
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

void IrLoweringX64::jumpOrFallthrough(IrBlock& target, const IrBlock& next)
{
    if (!isFallthroughBlock(target, next))
        build.jmp(target.label);
}

void IrLoweringX64::jumpOrAbortOnUndefNoFinalize(ConditionX64 cond, IrOp target, uint32_t index, const IrBlock& next, Label& fresh)
{
    // Validate that each branch to an exit sync block inside a single instruction will never see different register allocation state
    if (target.kind == IrOpKind::Block && function.blockOp(target).kind == IrBlockKind::ExitSync)
    {
        uint32_t token = regs.getAllocToken();

        if (exitSyncInstIdx != index)
        {
            exitSyncInstIdx = index;
            exitSyncAllocToken = token;
        }
        else
        {
            CODEGEN_ASSERT(exitSyncAllocToken == token);
        }
    }

    Label& label = getTargetLabel(target, index, fresh);

    if (target.kind == IrOpKind::Undef)
    {
        if (cond == ConditionX64::Count)
        {
            build.ud2(); // Unconditional jump to abort is just an abort
        }
        else
        {
            build.jcc(getNegatedCondition(cond), label);
            build.ud2();
            build.setLabel(label);
        }
    }
    else if (cond == ConditionX64::Count)
    {
        // Unconditional jump can be skipped if it's a fallthrough
        if (target.kind == IrOpKind::VmExit || !isFallthroughBlock(blockOp(target), next))
            build.jmp(label);
    }
    else
    {
        build.jcc(cond, label);
    }
}

void IrLoweringX64::jumpOrAbortOnUndef(ConditionX64 cond, IrOp target, uint32_t index, const IrBlock& next)
{
    Label fresh;
    jumpOrAbortOnUndefNoFinalize(cond, target, index, next, fresh);
    finalizeTargetLabel(target, index, fresh);
}

void IrLoweringX64::jumpOrAbortOnUndef(IrOp target, uint32_t index, const IrBlock& next)
{
    jumpOrAbortOnUndef(ConditionX64::Count, target, index, next);
}

void IrLoweringX64::storeFloat(OperandX64 dst, IrOp src)
{
    if (src.kind == IrOpKind::Constant)
    {
        ScopedRegX64 tmp{regs, SizeX64::xmmword};
        build.vmovss(tmp.reg, build.f32(float(doubleOp(src))));
        build.vmovss(dst, tmp.reg);
    }
    else if (src.kind == IrOpKind::Inst)
    {
        CODEGEN_ASSERT(getCmdValueKind(function.instOp(src).cmd) == IrValueKind::Float);
        build.vmovss(dst, regOp(src));
    }
    else
    {
        CODEGEN_ASSERT(!"Unsupported instruction form");
    }
}

void IrLoweringX64::storeDoubleAsFloat(OperandX64 dst, IrOp src)
{
    ScopedRegX64 tmp{regs, SizeX64::xmmword};

    if (src.kind == IrOpKind::Constant)
    {
        build.vmovss(tmp.reg, build.f32(float(doubleOp(src))));
    }
    else if (src.kind == IrOpKind::Inst)
    {
        build.vcvtsd2ss(tmp.reg, regOp(src), regOp(src));
    }
    else
    {
        CODEGEN_ASSERT(!"Unsupported instruction form");
    }
    build.vmovss(dst, tmp.reg);
}

void IrLoweringX64::checkSafeEnv(IrOp target, uint32_t index, const IrBlock& next)
{
    ScopedRegX64 tmp{regs, SizeX64::qword};

    build.mov(tmp.reg, sClosure);
    build.mov(tmp.reg, qword[tmp.reg + offsetof(Closure, env)]);
    build.cmp(byte[tmp.reg + offsetof(LuaTable, safeenv)], 0);

    jumpOrAbortOnUndef(ConditionX64::Equal, target, index, next);
}

void IrLoweringX64::allocAndIncrementCounterAt(CodeGenCounter kind, uint32_t pcpos)
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

void IrLoweringX64::incrementCounterAt(size_t offset)
{
    ScopedRegX64 tmp{regs, SizeX64::qword};

    // Get counter slot
    build.mov(tmp.reg, sClosure);
    build.mov(tmp.reg, qword[tmp.reg + offsetof(Closure, l.p)]);
    build.mov(tmp.reg, qword[tmp.reg + offsetof(Proto, execdata)]);

    // Increment
    build.inc(qword[tmp.reg + uint32_t(function.proto->sizecode + uint32_t(offset)) * 4]);
}

OperandX64 IrLoweringX64::memRegDoubleOp(IrOp op)
{
    switch (op.kind)
    {
    case IrOpKind::Inst:
        return regOp(op);
    case IrOpKind::Constant:
        return build.f64(doubleOp(op));
    case IrOpKind::VmReg:
        return luauRegValue(vmRegOp(op));
    case IrOpKind::VmConst:
        return luauConstantValue(vmConstOp(op));
    default:
        CODEGEN_ASSERT(!"Unsupported operand kind");
    }

    return noreg;
}

OperandX64 IrLoweringX64::memRegFloatOp(IrOp op)
{
    switch (op.kind)
    {
    case IrOpKind::Inst:
        CODEGEN_ASSERT(getCmdValueKind(function.instructions[op.index].cmd) == IrValueKind::Float);
        return regOp(op);
    case IrOpKind::Constant:
        return build.f32(float(doubleOp(op)));
    default:
        CODEGEN_ASSERT(!"Unsupported operand kind");
    }

    return noreg;
}

OperandX64 IrLoweringX64::memRegUintOp(IrOp op)
{
    switch (op.kind)
    {
    case IrOpKind::Inst:
        return regOp(op);
    case IrOpKind::Constant:
        return OperandX64(unsigned(intOp(op)));
    case IrOpKind::VmReg:
        return luauRegValueInt(vmRegOp(op));
    default:
        CODEGEN_ASSERT(!"Unsupported operand kind");
    }

    return noreg;
}

OperandX64 IrLoweringX64::memRegIntOp(IrOp op)
{
    switch (op.kind)
    {
    case IrOpKind::Inst:
        return regOp(op);
    case IrOpKind::Constant:
        return OperandX64(intOp(op));
    case IrOpKind::VmReg:
        return luauRegValueInt(vmRegOp(op));
    default:
        CODEGEN_ASSERT(!"Unsupported operand kind");
    }

    return noreg;
}

OperandX64 IrLoweringX64::memRegInt64Op(IrOp op)
{
    switch (op.kind)
    {
    case IrOpKind::Inst:
        return regOp(op);
    case IrOpKind::Constant:
        return build.i64(int64Op(op));
    case IrOpKind::VmReg:
        return luauRegValueInt64(vmRegOp(op));
    case IrOpKind::VmConst:
        return luauConstantValue(vmConstOp(op));
    default:
        CODEGEN_ASSERT(!"Unsupported operand kind");
    }

    return noreg;
}

OperandX64 IrLoweringX64::memRegTagOp(IrOp op)
{
    switch (op.kind)
    {
    case IrOpKind::Inst:
        return regOp(op);
    case IrOpKind::VmReg:
        return luauRegTag(vmRegOp(op));
    case IrOpKind::VmConst:
        return luauConstantTag(vmConstOp(op));
    default:
        CODEGEN_ASSERT(!"Unsupported operand kind");
    }

    return noreg;
}

RegisterX64 IrLoweringX64::regOp(IrOp op)
{
    IrInst& inst = function.instOp(op);

    if (inst.spilled || inst.needsReload)
        regs.restore(inst, false);

    CODEGEN_ASSERT(inst.regX64 != noreg);
    return inst.regX64;
}

OperandX64 IrLoweringX64::bufferAddrOp(IrOp bufferOp, IrOp indexOp, uint8_t tag)
{
    CODEGEN_ASSERT(tag == LUA_TUSERDATA || tag == LUA_TBUFFER || tag == LUA_TVECTOR);
    int dataOffset = tag == LUA_TBUFFER ? 0 : tag == LUA_TVECTOR ? offsetof(LuauVector, v) : offsetof(Udata, data);
    if (indexOp.kind == IrOpKind::Inst)
    {
        CODEGEN_ASSERT(!producesDirtyHighRegisterBits(function.instOp(indexOp).cmd)); // Ensure that high register bits are cleared
        return regOp(bufferOp) + qwordReg(regOp(indexOp)) + dataOffset;
    }
    else if (indexOp.kind == IrOpKind::Constant)
    {
        return regOp(bufferOp) + intOp(indexOp) + dataOffset;
    }

    CODEGEN_ASSERT(!"Unsupported instruction form");
    return noreg;
}

RegisterX64 IrLoweringX64::vecOp(IrOp op, ScopedRegX64& tmp)
{
    IrInst source = function.instOp(op);
    CODEGEN_ASSERT(source.cmd != IrCmd::SUBSTITUTE); // we don't process substitutions

    // source that comes from memory or from tag instruction has .w = TVECTOR, which is denormal
    // to avoid performance degradation on some CPUs we mask this component to produce zero
    // otherwise we conservatively assume the vector is a result of a well formed math op so .w is a normal number or zero
    if (source.cmd != IrCmd::LOAD_TVALUE && source.cmd != IrCmd::GET_UPVALUE && source.cmd != IrCmd::TAG_VECTOR)
        return regOp(op);

    tmp.alloc(SizeX64::xmmword);
    build.vandps(tmp.reg, regOp(op), vectorAndMaskOp());
    return tmp.reg;
}

IrConst IrLoweringX64::constOp(IrOp op) const
{
    return function.constOp(op);
}

uint8_t IrLoweringX64::tagOp(IrOp op) const
{
    return function.tagOp(op);
}

int IrLoweringX64::intOp(IrOp op) const
{
    return function.intOp(op);
}

int64_t IrLoweringX64::int64Op(IrOp op) const
{
    return function.int64Op(op);
}

unsigned IrLoweringX64::uintOp(IrOp op) const
{
    return function.uintOp(op);
}

unsigned IrLoweringX64::importOp(IrOp op) const
{
    return function.importOp(op);
}

double IrLoweringX64::doubleOp(IrOp op) const
{
    return function.doubleOp(op);
}

IrBlock& IrLoweringX64::blockOp(IrOp op) const
{
    return function.blockOp(op);
}

Label& IrLoweringX64::labelOp(IrOp op) const
{
    return blockOp(op).label;
}

OperandX64 IrLoweringX64::vectorAndMaskOp()
{
    if (vectorAndMask.base == noreg)
        vectorAndMask = build.u32x4(~0u, ~0u, ~0u, 0);

    return vectorAndMask;
}

} // namespace X64
} // namespace CodeGen
} // namespace Luau
