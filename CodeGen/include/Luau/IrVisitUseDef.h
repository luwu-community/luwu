// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Common.h"
#include "Luau/IrData.h"
#include "Luau/generated/IrCmdEffects.h"


namespace Luau
{
namespace CodeGen
{

// A register count from a fact's (vm-slots r count): a literal (-1 is up to the stack top) or a constant operand
inline int getVmEffectCount(IrFunction& function, IrInst& inst, IrEffectCount count)
{
    switch (count.kind)
    {
    case IrEffectCountKind::None:
        break;
    case IrEffectCountKind::Literal:
        return count.value;
    case IrEffectCountKind::IntOperand:
        return function.intOp(getOp(inst, count.value));
    case IrEffectCountKind::UintOperand:
        return function.uintOp(getOp(inst, count.value));
    case IrEffectCountKind::LowByteOfIntOperand:
        return uint8_t(function.intOp(getOp(inst, count.value)));
    }

    CODEGEN_ASSERT(!"Missing register count");
    return 0;
}

template<typename T>
static void visitVmEffect(T& visitor, IrFunction& function, IrInst& inst, const IrVmEffect& effect)
{
    IrOp op = getOp(inst, effect.op);

    switch (effect.kind)
    {
    case IrVmEffectKind::Use:
        visitor.use(op, effect.offset);
        break;
    case IrVmEffectKind::MaybeUse:
        visitor.maybeUse(op);
        break;
    case IrVmEffectKind::UseRange:
        visitor.useRange(vmRegOp(op) + effect.offset, getVmEffectCount(function, inst, effect.count));
        break;
    case IrVmEffectKind::UseVarargs:
        visitor.useVarargs(vmRegOp(op) + effect.offset);
        break;
    case IrVmEffectKind::Capture:
        visitor.capture(vmRegOp(op) + effect.offset);
        break;
    case IrVmEffectKind::Def:
        visitor.def(op, effect.offset);
        break;
    case IrVmEffectKind::MaybeDef:
        visitor.maybeDef(op);
        break;
    case IrVmEffectKind::DefRange:
        visitor.defRange(vmRegOp(op) + effect.offset, getVmEffectCount(function, inst, effect.count));
        break;
    }
}

// VM register uses, captures and defs, from the facts in Luna/defs/ir: all uses are visited before the defs
template<typename T>
static void visitVmRegDefsUses(T& visitor, IrFunction& function, IrInst& inst)
{
    const IrCmdEffects& effects = getInstEffects(function, inst);

    if (!effects.declaresVmRegs)
    {
        // All instructions which reference registers declare facts (or no-vm-effects)
        for (auto& op : inst.ops)
            CODEGEN_ASSERT(op.kind != IrOpKind::VmReg);
        return;
    }

    for (int i = 0; i < effects.vmEffectCount; ++i)
        visitVmEffect(visitor, function, inst, effects.vmEffects[i]);
}

template<typename T>
static void visitVmRegDefsUses(T& visitor, IrFunction& function, const IrBlock& block)
{
    for (uint32_t instIdx = block.start; instIdx <= block.finish; instIdx++)
    {
        IrInst& inst = function.instructions[instIdx];

        visitVmRegDefsUses(visitor, function, inst);
    }
}

} // namespace CodeGen
} // namespace Luau
