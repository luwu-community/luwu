// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "IrValueLocationTracking.h"

#include "Luau/IrDump.h"
#include "Luau/IrUtils.h"
#include "Luau/IrVisitUseDef.h"
#include "Luau/generated/IrCmdEffects.h"

LUAU_FASTFLAG(LuauCodegenDseRestoreHints)

namespace Luau
{
namespace CodeGen
{

IrValueLocationTracking::IrValueLocationTracking(LogBuilder* logger, IrFunction& function)
    : logger(logger)
    , function(function)
{
    vmRegValue.fill(kInvalidInstIdx);
    vmRegDependent.fill(kInvalidInstIdx);
}

void IrValueLocationTracking::setRestoreCallback(void* context, void (*callback)(void* context, IrInst& inst))
{
    restoreCallbackCtx = context;
    restoreCallback = callback;
}

bool IrValueLocationTracking::canBeRematerialized(IrCmd cmd)
{
    return cmd == IrCmd::UINT_TO_NUM || cmd == IrCmd::INT_TO_NUM;
}

bool IrValueLocationTracking::canRematerializeArguments(IrInst& inst)
{
    if (canBeRematerialized(inst.cmd) && OP_A(inst).kind == IrOpKind::Inst)
    {
        IrInst& depInst = function.instOp(OP_A(inst));

        // If this argument is last used in the current instructions, there's no point in preserving it
        if (depInst.lastUse != function.getInstIndex(inst))
            return true;
    }

    return false;
}

void IrValueLocationTracking::processStoreLocationHint(const StoreLocationHint* hint)
{
    CODEGEN_ASSERT(hint);
    CODEGEN_ASSERT(hint->op.kind == IrOpKind::VmReg);

    if (hint->instIdx != kInvalidInstIdx)
    {
        if (function.instructions[hint->instIdx].useCount == 0)
            return;

        int reg = vmRegOp(hint->op);

        // If the value already has a restore location, this hint is redundant
        ValueRestoreLocation existingLoc = function.findRestoreLocation(hint->instIdx, /*limitToCurrentBlock*/ false);

        if (existingLoc.op.kind != IrOpKind::None)
            return;

        if (reg > maxReg)
            maxReg = reg;

        bool captured = function.cfg.captured.regs.test(reg);

        invalidateRestoreOp(hint->op, /*skipValueInvalidation*/ false);

        if (!captured)
        {
            function.recordRestoreLocation(hint->instIdx, {hint->op, hint->kind, IrCmd::NOP, /*lazy*/ true});

            if (logger && logger->options.includeRegSpills)
                logger->formatAppendWithPrefix("  ; %%%u has a lazy restore location R%d\n", hint->instIdx, reg);
        }

        vmRegValue[reg] = hint->instIdx;
    }
}

void IrValueLocationTracking::applyLocationEffect(IrInst& inst, uint32_t instIdx, const IrLocationEffect& effect)
{
    IrOp op = getOp(inst, effect.op);

    switch (effect.kind)
    {
    case IrLocationEffectKind::None:
        break;
    case IrLocationEffectKind::Invalidate:
        invalidateRestoreOp(op, /*skipValueInvalidation*/ false);
        break;
    case IrLocationEffectKind::InvalidateKeepValues:
        // Tag update is a bit tricky, restore operations of values are not affected
        invalidateRestoreOp(op, /*skipValueInvalidation*/ true);
        break;
    case IrLocationEffectKind::InvalidateRange:
        invalidateRestoreVmRegs(vmRegOp(op) + effect.offset, getVmEffectCount(function, inst, effect.count));
        break;
    case IrLocationEffectKind::RestoreFromLoad:
        if (op.kind == IrOpKind::VmReg)
            invalidateRestoreOp(op, /*skipValueInvalidation*/ false);

        recordRestoreOp(instIdx, op);
        break;
    case IrLocationEffectKind::RestoreFromStore:
        // If this is not the last use of the stored value, we can restore it from this new location
        // Additionally, even if it's a last use, it might allow its argument to be restored
        if (IrOp value = getOp(inst, effect.valueOp); value.kind == IrOpKind::Inst)
        {
            IrInst& source = function.instOp(value);

            if (source.lastUse != instIdx || canRematerializeArguments(source))
                recordRestoreOp(value.index, op);
        }
        break;
    case IrLocationEffectKind::Rematerialize:
        recordRematerialization(instIdx, op, effect.conversion);
        break;
    }
}

void IrValueLocationTracking::beforeInstLowering(IrInst& inst)
{
    const IrCmdEffects& effects = getInstEffects(function, inst);

    if (!effects.declaresLocations)
    {
        // All instructions which reference registers declare facts (or (location-tracking :none))
        for (auto& op : inst.ops)
            CODEGEN_ASSERT(op.kind != IrOpKind::VmReg);
        return;
    }

    applyLocationEffect(inst, kInvalidInstIdx, effects.locationBefore);
}

void IrValueLocationTracking::afterInstLowering(IrInst& inst, uint32_t instIdx)
{
    applyLocationEffect(inst, instIdx, getInstEffects(function, inst).locationAfter);
}

// A result recomputed from operand `source` (NUM_TO_INT/NUM_TO_UINT of a double loaded from a VM register): it can be
// restored by applying `conversion` to that register's value
void IrValueLocationTracking::recordRematerialization(uint32_t instIdx, IrOp source, IrCmd conversion)
{
    if (source.kind == IrOpKind::Inst)
    {
        ValueRestoreLocation ownerLoc = function.findRestoreLocation(source.index, /* limitToCurrentBlock */ true);

        if (ownerLoc.op.kind == IrOpKind::VmReg && ownerLoc.kind == IrValueKind::Double && ownerLoc.conversionCmd == IrCmd::NOP && !ownerLoc.lazy)
        {
            int reg = vmRegOp(ownerLoc.op);

            if (!function.cfg.captured.regs.test(reg) && vmRegDependent[reg] == kInvalidInstIdx)
            {
                function.recordRestoreLocation(instIdx, {ownerLoc.op, IrValueKind::Double, conversion});

                vmRegDependent[reg] = instIdx;

                if (logger && logger->options.includeRegSpills)
                {
                    const char* conv = getConversionCmdSuffix(conversion);

                    logger->formatAppendWithPrefix("  ; %%%u can be restored from R%d%s\n", instIdx, reg, conv);
                }
            }
        }
    }
}

void IrValueLocationTracking::recordRestoreOp(uint32_t instIdx, IrOp location)
{

    IrInst& inst = function.instructions[instIdx];

    if (location.kind == IrOpKind::VmReg)
    {
        int reg = vmRegOp(location);

        if (reg > maxReg)
            maxReg = reg;

        // Record location in register memory only if register is not captured
        bool captured = function.cfg.captured.regs.test(reg);

        if (!captured)
        {
            function.recordRestoreLocation(instIdx, {location, getCmdValueKind(inst.cmd), IrCmd::NOP});

            if (logger && logger->options.includeRegSpills)
                logger->formatAppendWithPrefix("  ; %%%u can be restored from R%d\n", instIdx, reg);
        }

        vmRegValue[reg] = instIdx;

        // Any dependent value has to be cleared in beforeInstLowering before recording new restore operations
        CODEGEN_ASSERT(vmRegDependent[reg] == kInvalidInstIdx);

        if (canBeRematerialized(inst.cmd) && OP_A(inst).kind == IrOpKind::Inst)
        {
            uint32_t depInstIdx = OP_A(inst).index;

            if (!captured)
            {
                function.recordRestoreLocation(depInstIdx, {location, getCmdValueKind(inst.cmd), inst.cmd});

                if (logger && logger->options.includeRegSpills)
                    logger->formatAppendWithPrefix("  ; %%%u can be restored from R%d%s\n", depInstIdx, reg, getConversionCmdSuffix(inst.cmd));
            }

            vmRegDependent[reg] = depInstIdx;
        }
    }
    else if (location.kind == IrOpKind::VmConst)
    {
        function.recordRestoreLocation(instIdx, {location, getCmdValueKind(inst.cmd)});

        if (logger && logger->options.includeRegSpills)
            logger->formatAppendWithPrefix("  ; %%%u can be restored from K%d\n", instIdx, vmConstOp(location));
    }
}

void IrValueLocationTracking::invalidateRestoreOp(IrOp location, bool skipValueInvalidation)
{

    if (location.kind == IrOpKind::VmReg)
    {
        int reg = vmRegOp(location);
        uint32_t& instIdx = vmRegValue[reg];

        if (instIdx != kInvalidInstIdx)
        {
            IrInst& inst = function.instructions[instIdx];

            // If we are only modifying the tag, we can avoid invalidating tracked location of values
            if (skipValueInvalidation)
            {
                switch (getCmdValueKind(inst.cmd))
                {
                case IrValueKind::Double:
                case IrValueKind::Pointer:
                case IrValueKind::Int:
                case IrValueKind::Int64:
                    return;
                default:
                    break;
                }
            }

            // If instruction value is spilled and memory location is about to be lost, it has to be restored immediately
            if (inst.needsReload)
            {
                // Recorded restore location should be materialized by this point
                CODEGEN_ASSERT(!function.findRestoreLocation(instIdx, false).lazy);

                restoreCallback(restoreCallbackCtx, inst);
            }

            // Get the current restore location of the instruction
            ValueRestoreLocation currRestoreLocation = function.findRestoreLocation(instIdx, /* limitToCurrentBlock */ false);

            // If the current location is the one that is being invalidated, we can no longer restore from it
            if (location == currRestoreLocation.op)
            {
                function.recordRestoreLocation(instIdx, {});

                if (logger && logger->options.includeRegSpills && !inst.needsReload)
                    logger->formatAppendWithPrefix("  ; %%%u can no longer be restored from R%d\n", instIdx, reg);
            }

            // Register loses link with instruction
            instIdx = kInvalidInstIdx;

            // Invalidate chained instruction location as well
            uint32_t& depInstIdx = vmRegDependent[reg];

            if (depInstIdx != kInvalidInstIdx)
            {
                IrInst& depInst = function.instructions[depInstIdx];

                if (depInst.needsReload)
                {
                    // Recorded restore location should be materialized by this point
                    CODEGEN_ASSERT(!function.findRestoreLocation(depInstIdx, false).lazy);

                    restoreCallback(restoreCallbackCtx, depInst);
                }

                ValueRestoreLocation depRestoreLocation = function.findRestoreLocation(depInstIdx, /* limitToCurrentBlock */ false);

                if (location == depRestoreLocation.op)
                {
                    function.recordRestoreLocation(depInstIdx, {});

                    if (logger && logger->options.includeRegSpills && !depInst.needsReload)
                    {
                        const char* conv = getConversionCmdSuffix(depRestoreLocation.conversionCmd);

                        logger->formatAppendWithPrefix("  ; %%%u can no longer be restored from R%d%s\n", depInstIdx, reg, conv);
                    }
                }

                depInstIdx = kInvalidInstIdx;
            }
        }
    }
    else if (location.kind == IrOpKind::VmConst)
    {
        CODEGEN_ASSERT(!"VM constants are immutable");
    }
}

void IrValueLocationTracking::invalidateRestoreVmRegs(int start, int count)
{

    int end = count == -1 ? 255 : start + count;

    if (end > maxReg)
        end = maxReg;

    for (int reg = start; reg <= end; reg++)
        invalidateRestoreOp(IrOp{IrOpKind::VmReg, uint8_t(reg)}, /*skipValueInvalidation*/ false);
}

} // namespace CodeGen
} // namespace Luau
