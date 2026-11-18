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

    lowerInstGenerated(*this, inst, index, next);

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

OperandX64 IrLoweringX64::tvalueMemOp(IrOp op, int offset, SizeX64 size)
{
    switch (op.kind)
    {
    case IrOpKind::VmReg:
        return OperandX64(size, noreg, 1, rBase, int32_t(vmRegOp(op) * sizeof(TValue)) + offset);
    case IrOpKind::VmConst:
        return OperandX64(size, noreg, 1, rConstants, int32_t(vmConstOp(op) * sizeof(TValue)) + offset);
    case IrOpKind::Inst:
        return OperandX64(size, noreg, 1, regOp(op), offset);
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
