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
// The caller supplies three dead registers as scratch. This avoids allocating or spilling inside
// the helper, whose branches rejoin the main line at `authorized`.
void emitClassMemberAuthA64(
    AssemblyBuilderA64& build,
    RegisterA64 classReg,
    RegisterA64 slotReg,
    RegisterA64 writtenObject,
    Label& mismatch,
    RegisterA64 owner,
    RegisterA64 flagw,
    RegisterA64 maskw
)
{
    bool isWrite = writtenObject != noreg;

    // See emitClassMemberAuthX64 for why a read also stops on LBC_CLASSMEMBER_INITBLOCKED.
    uint32_t restrictBits = isWrite ? (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_CONST) : (LBC_CLASSMEMBER_PRIVATE | LBC_CLASSMEMBER_INITBLOCKED);

    Label authorized;

    // flag = classReg->memberflags[slot]
    build.ldr(owner, mem(classReg, offsetof(LuauClass, memberflags)));
    build.ldrb(flagw, mem(owner, slotReg));

    // PRIVATE | INITBLOCKED (bits 0 and 7) has no logical-immediate encoding, so test against a register
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

    lowerInstGenerated(*this, inst, index, next);

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
