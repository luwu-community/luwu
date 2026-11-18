// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "IrValidation.h"

#include "Luau/CodeGenCommon.h"
#include "Luau/IrData.h"
#include "Luau/IrDump.h"
#include "Luau/IrUtils.h"

#include <stdio.h>

LUAU_FASTFLAGVARIABLE(DebugCodegenVerifyIr)
LUAU_FASTFLAG(DebugLuauAbortingChecks)

namespace Luau
{
namespace CodeGen
{

static const char* getOpKindName(IrOpKind kind)
{
    switch (kind)
    {
    case IrOpKind::None:
        return "none";
    case IrOpKind::Undef:
        return "undef";
    case IrOpKind::Constant:
        return "constant";
    case IrOpKind::Condition:
        return "condition";
    case IrOpKind::Inst:
        return "inst";
    case IrOpKind::Block:
        return "block";
    case IrOpKind::VmReg:
        return "Rn";
    case IrOpKind::VmConst:
        return "Kn";
    case IrOpKind::VmUpvalue:
        return "UPn";
    case IrOpKind::VmExit:
        return "vmexit";
    }

    LUAU_UNREACHABLE();
}

static const char* getConstKindName(IrConstKind kind)
{
    switch (kind)
    {
    case IrConstKind::Int:
        return "int";
    case IrConstKind::Int64:
        return "int64";
    case IrConstKind::Uint:
        return "uint";
    case IrConstKind::Double:
        return "double";
    case IrConstKind::Tag:
        return "tag";
    case IrConstKind::Import:
        return "import";
    }

    LUAU_UNREACHABLE();
}

template<typename T>
static bool hasBit(unsigned mask, T kind)
{
    return (mask & (1u << unsigned(kind))) != 0;
}

// What an operand refers to once SUBSTITUTE instructions are looked through
struct ResolvedOp
{
    IrOpKind kind = IrOpKind::None;
    IrValueKind valueKind = IrValueKind::None; // for Inst
    IrConstKind constKind = IrConstKind::Int;  // for Constant
    const IrInst* inst = nullptr;
};

static ResolvedOp resolve(const IrFunction& function, IrOp op)
{
    for (int depth = 0; depth < 16; ++depth)
    {
        if (op.kind == IrOpKind::Constant)
            return {IrOpKind::Constant, IrValueKind::None, function.constants[op.index].kind, nullptr};

        if (op.kind != IrOpKind::Inst)
            return {op.kind};

        const IrInst& inst = function.instructions[op.index];
        if (inst.cmd != IrCmd::SUBSTITUTE)
            return {IrOpKind::Inst, getCmdValueKind(inst.cmd), IrConstKind::Int, &inst};

        op = inst.ops.empty() ? IrOp{} : inst.ops[0];
    }

    return {IrOpKind::Inst, IrValueKind::Unknown};
}

static std::string describe(const ResolvedOp& op)
{
    std::string text = getOpKindName(op.kind);
    if (op.kind == IrOpKind::Inst)
        text += std::string(" (") + (op.inst ? getCmdName(op.inst->cmd) : "SUBSTITUTE chain") + ", " + getValueKindName(op.valueKind) + ")";
    else if (op.kind == IrOpKind::Constant)
        text += std::string(" (") + getConstKindName(op.constKind) + ")";
    return text;
}

static bool matches(const IrOperandSpec& spec, const ResolvedOp& op)
{
    if (!hasBit(spec.opKinds, op.kind))
        return false;
    if (op.kind == IrOpKind::Inst)
        return hasBit(spec.valueKinds, op.valueKind);
    if (op.kind == IrOpKind::Constant)
        return hasBit(spec.constKinds, op.constKind);
    return true;
}

static void validateInst(
    const IrFunction& function,
    uint32_t blockIdx,
    uint32_t index,
    IrValidationTarget target,
    std::vector<std::string>& problems
)
{
    const IrInst& inst = function.instructions[index];
    const IrCmdOperands& spec = getCmdOperands(inst.cmd);
    std::string where = std::string(getCmdName(inst.cmd)) + " %" + std::to_string(index) + " (block " + std::to_string(blockIdx) + ")";

    for (uint32_t i = 0; i < inst.ops.size(); ++i)
    {
        IrOp raw = inst.ops[i];
        std::string operand = where + " operand " + char('A' + i);

        if (i >= spec.count)
        {
            if (raw.kind != IrOpKind::None)
                problems.push_back(operand + ": " + getCmdName(inst.cmd) + " has " + std::to_string(spec.count) + " operand(s), but this one is " +
                                   getOpKindName(raw.kind));
            continue;
        }

        if (raw.kind == IrOpKind::Inst)
        {
            const IrInst& target = function.instructions[raw.index];
            if (target.cmd == IrCmd::NOP)
                problems.push_back(operand + ": refers to %" + std::to_string(raw.index) + ", which was removed (NOP)");
            else if (target.cmd != IrCmd::SUBSTITUTE && !hasResult(target.cmd))
                problems.push_back(operand + ": refers to %" + std::to_string(raw.index) + " (" + getCmdName(target.cmd) + "), which has no result");
        }

        // Block and exit targets only make sense on instructions that branch
        if ((raw.kind == IrOpKind::Block || raw.kind == IrOpKind::VmExit) && !isBlockTerminator(inst.cmd) && !isNonTerminatingJump(inst.cmd) &&
            !isPseudo(inst.cmd))
            problems.push_back(operand + ": is a " + getOpKindName(raw.kind) + ", but " + getCmdName(inst.cmd) + " isn't a guard or terminator");

        ResolvedOp op = resolve(function, raw);
        if (matches(spec.ops[i], op))
            continue;
        // target-specific / flag-specific extra kinds
        if (target == IrValidationTarget::A64 && matches(spec.targetA64[i], op))
            continue;
        if (target == IrValidationTarget::X64 && matches(spec.targetX64[i], op))
            continue;
        if (FFlag::DebugLuauAbortingChecks && matches(spec.flagDebugLuauAbortingChecks[i], op))
            continue;

        problems.push_back(operand + ": " + describe(op) + " isn't one of the kinds declared for it in Luna/defs/ir");
    }
}

std::vector<std::string> validateIr(const IrFunction& function, IrValidationTarget target)
{
    std::vector<std::string> problems;

    for (uint32_t blockIdx = 0; blockIdx < function.blocks.size(); ++blockIdx)
    {
        const IrBlock& block = function.blocks[blockIdx];
        if (block.kind == IrBlockKind::Dead || block.start == ~0u)
            continue;

        // The last live instruction ends the block; nothing before it may
        uint32_t last = ~0u;
        for (uint32_t index = block.start; index <= block.finish; ++index)
        {
            const IrInst& inst = function.instructions[index];
            if (inst.cmd == IrCmd::NOP)
                continue;

            if (last != ~0u && isBlockTerminator(function.instructions[last].cmd))
                problems.push_back(std::string(getCmdName(inst.cmd)) + " %" + std::to_string(index) + " (block " + std::to_string(blockIdx) +
                                   "): follows terminator " + getCmdName(function.instructions[last].cmd) + " %" + std::to_string(last));

            validateInst(function, blockIdx, index, target, problems);
            last = index;
        }

        if (last != ~0u && !isBlockTerminator(function.instructions[last].cmd))
            problems.push_back("block " + std::to_string(blockIdx) + " ends with " + getCmdName(function.instructions[last].cmd) + " %" +
                               std::to_string(last) + ", which isn't a terminator");
    }

    return problems;
}

void verifyIr(const IrFunction& function, IrValidationTarget target, const char* after)
{
    if (!FFlag::DebugCodegenVerifyIr)
        return;

    std::vector<std::string> problems = validateIr(function, target);
    if (problems.empty())
        return;

    fprintf(stderr, "Invalid IR after %s:\n", after);
    for (const auto& problem : problems)
        fprintf(stderr, "  %s\n", problem.c_str());

    CODEGEN_ASSERT(!"Invalid IR (see stderr)");
}

} // namespace CodeGen
} // namespace Luau
