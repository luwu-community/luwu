// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Common.h"
#include "Luau/IrData.h"
#include "Luau/generated/IrCmdEffects.h"


namespace Luau
{
namespace CodeGen
{

// A register count from (vm-effects ...): a literal (-1 is up to the stack top) or a constant operand
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

// (vm-effects (hook ...)): runs after the declared uses and before the declared defs
template<typename T>
static void visitVmEffectsHook(T& visitor, IrFunction& function, IrInst& inst, IrVmEffectsHook hook)
{
    switch (hook)
    {
    case IrVmEffectsHook::None:
        break;
    case IrVmEffectsHook::InvokeFastcallUses:
        if (int count = function.intOp(OP_F(inst)); count != -1)
        {
            // Only LOP_FASTCALL3 lowering is allowed to have third optional argument
            if (count >= 3 && OP_E(inst).kind == IrOpKind::Undef)
            {
                CODEGEN_ASSERT(OP_D(inst).kind == IrOpKind::VmReg && vmRegOp(OP_D(inst)) == vmRegOp(OP_C(inst)) + 1);

                visitor.useRange(vmRegOp(OP_C(inst)), count);
            }
            else
            {
                if (count >= 1)
                    visitor.use(OP_C(inst));

                if (count >= 2)
                    visitor.maybeUse(OP_D(inst)); // Argument can also be a VmConst

                if (count >= 3)
                    visitor.maybeUse(OP_E(inst)); // Argument can also be a VmConst
            }
        }
        else
        {
            visitor.useVarargs(vmRegOp(OP_C(inst)));
        }
        // While ADJUST_STACK_TO_REG would semantically define the result range, the declared def-range defines it immediately
        break;
    case IrVmEffectsHook::FallbackNewObject:
    {
        // See IrData.h for the operand shapes. The class is read in place (the declared use), and the registers above
        // the instance hold either the constructor arguments (LBC_NEWOBJECT_DEFAULT and _INIT) or one
        // value per field (LBC_NEWOBJECT_FIELDS); the INIT form additionally fills in __init and self
        // for the CALL the compiler emits behind this instruction.
        int ra = vmRegOp(OP_B(inst));
        int form = function.intOp(OP_D(inst));
        int count = function.intOp(OP_E(inst));

        if (form == LBC_NEWOBJECT_INIT)
        {
            if (count > 0)
                visitor.useRange(ra + 3, count);

            visitor.defRange(ra, 3);
        }
        else
        {
            if (count > 0)
                visitor.useRange(ra + 1, count);

            visitor.def(OP_B(inst));
        }
        break;
    }
    case IrVmEffectsHook::Capture:
        if (function.uintOp(OP_B(inst)) == 1)
            visitor.capture(vmRegOp(OP_A(inst)));
        break;
    }
}

// VM register uses and defs, from (vm-effects ...) in Luna/defs/ir. All uses are visited before the defs.
template<typename T>
static void visitVmRegDefsUses(T& visitor, IrFunction& function, IrInst& inst)
{
    const IrCmdEffects& effects = getCmdEffects(inst.cmd);

    if (!effects.declaresVmRegs)
    {
        // All instructions which reference registers declare vm-effects
        for (auto& op : inst.ops)
            CODEGEN_ASSERT(op.kind != IrOpKind::VmReg);
        return;
    }

    int i = 0;
    for (; i < effects.vmEffectCount && effects.vmEffects[i].kind <= IrVmEffectKind::UseVarargs; ++i)
        visitVmEffect(visitor, function, inst, effects.vmEffects[i]);

    visitVmEffectsHook(visitor, function, inst, effects.vmHook);

    for (; i < effects.vmEffectCount; ++i)
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
