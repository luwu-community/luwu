// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Bytecode.h"
#include "Luau/DenseHash.h"
#include "Luau/IrAnalysis.h"
#include "Luau/Label.h"
#include "Luau/RegisterX64.h"
#include "Luau/RegisterA64.h"
#include "Luau/SmallVector.h"
#include "Luau/generated/IrCmd.h"

#include <optional>
#include <vector>

#include <stdint.h>
#include <string.h>

#define OP_A(inst) getOp(inst, 0)
#define OP_B(inst) getOp(inst, 1)
#define OP_C(inst) getOp(inst, 2)
#define OP_D(inst) getOp(inst, 3)
#define OP_E(inst) getOp(inst, 4)
#define OP_F(inst) getOp(inst, 5)
#define OP_G(inst) getOp(inst, 6)

struct Proto;

namespace Luau
{
namespace CodeGen
{

struct LoweringStats;

constexpr uint8_t kUnknownTag = 0xff;

// IR extensions to LuauBuiltinFunction enum (these only exist inside IR, and start from 256 to avoid collisions)
enum
{
    LBF_IR_MATH_LOG2 = 256,
};

// IR instruction command (IrCmd): declared with define-ircmd in Luna/defs/ir, generated into Luau/generated/IrCmd.h.
// In the command descriptions, the following abbreviations are used:
// * Rn - VM stack register slot, n in 0..254
// * Kn - VM proto constant slot, n in 0..2^23-1
// * UPn - VM function upvalue slot, n in 0..199
// * A, B, C, D, E, F, G are instruction arguments

enum class IrConstKind : uint8_t
{
    Int,
    Int64,
    Uint,
    Double,
    Tag,
    Import,
};

struct IrConst
{
    IrConstKind kind;

    union
    {
        int valueInt;
        int64_t valueInt64;
        unsigned valueUint;
        double valueDouble;
        uint8_t valueTag;
    };
};

enum class IrCondition : uint8_t
{
    Equal,
    NotEqual,
    Less,
    NotLess,
    LessEqual,
    NotLessEqual,
    Greater,
    NotGreater,
    GreaterEqual,
    NotGreaterEqual,

    UnsignedLess,
    UnsignedLessEqual,
    UnsignedGreater,
    UnsignedGreaterEqual,

    Count
};

enum class IrOpKind : uint32_t
{
    None,

    Undef,

    // To reference a constant value
    Constant,

    // To specify a condition code
    Condition,

    // To reference a result of a previous instruction
    Inst,

    // To reference a basic block in control flow
    Block,

    // To reference a VM register
    VmReg,

    // To reference a VM constant
    VmConst,

    // To reference a VM upvalue
    VmUpvalue,

    // To reference an exit to VM at specific PC pos
    VmExit,
};

// VmExit uses a special value to indicate that pcpos update should be skipped
// This is only used during type checking at function entry
inline constexpr uint32_t kVmExitEntryGuardPc = (1u << 28) - 1;

struct IrOp
{
    IrOpKind kind : 4;
    uint32_t index : 28;

    IrOp()
        : kind(IrOpKind::None)
        , index(0)
    {
    }

    IrOp(IrOpKind kind, uint32_t index)
        : kind(kind)
        , index(index)
    {
    }

    bool operator==(const IrOp& rhs) const
    {
        return kind == rhs.kind && index == rhs.index;
    }

    bool operator!=(const IrOp& rhs) const
    {
        return !(*this == rhs);
    }
};

static_assert(sizeof(IrOp) == 4);

enum class IrValueKind : uint8_t
{
    Unknown, // Used by SUBSTITUTE, argument has to be checked to get type
    None,
    Tag,
    Int,
    Int64,
    Pointer,
    Float,
    Double,
    Tvalue,

    Count
};

using IrOps = SmallVector<IrOp, 6>;

struct IrInst
{
    IrCmd cmd;

    // Operands
    // All frequently used instructions use only A-F slots.
    IrOps ops;

    uint32_t lastUse = 0;
    uint16_t useCount = 0;

    // Location of the result (optional)
    X64::RegisterX64 regX64 = X64::noreg;
    A64::RegisterA64 regA64 = A64::noreg;
    bool reusedReg = false;
    bool spilled = false;
    bool needsReload = false;
};

inline IrOp& getOp(IrInst& inst, uint32_t idx)
{
    if (LUAU_UNLIKELY(idx >= inst.ops.size()))
    {
        inst.ops.resize(idx + 1);
    }
    return inst.ops[idx];
}

inline IrOp& getOp(IrInst* inst, uint32_t idx)
{
    return getOp(*inst, idx);
}

inline bool hasOp(IrInst& inst, uint32_t idx)
{
    return idx < inst.ops.size();
}

// TODO: once we update kind checks to not use getOp, it will no longer cause a resize and second part can be removed
#define HAS_OP_A(inst) (0 < (inst).ops.size() && (inst).ops[0].kind != IrOpKind::None)
#define HAS_OP_B(inst) (1 < (inst).ops.size() && (inst).ops[1].kind != IrOpKind::None)
#define HAS_OP_C(inst) (2 < (inst).ops.size() && (inst).ops[2].kind != IrOpKind::None)
#define HAS_OP_D(inst) (3 < (inst).ops.size() && (inst).ops[3].kind != IrOpKind::None)
#define HAS_OP_E(inst) (4 < (inst).ops.size() && (inst).ops[4].kind != IrOpKind::None)
#define HAS_OP_F(inst) (5 < (inst).ops.size() && (inst).ops[5].kind != IrOpKind::None)
#define HAS_OP_G(inst) (6 < (inst).ops.size() && (inst).ops[6].kind != IrOpKind::None)

#define OPT_OP_A(inst) (0 < (inst).ops.size() && (inst).ops[0].kind != IrOpKind::None ? (inst).ops[0] : IrOp{})
#define OPT_OP_B(inst) (1 < (inst).ops.size() && (inst).ops[1].kind != IrOpKind::None ? (inst).ops[1] : IrOp{})
#define OPT_OP_C(inst) (2 < (inst).ops.size() && (inst).ops[2].kind != IrOpKind::None ? (inst).ops[2] : IrOp{})
#define OPT_OP_D(inst) (3 < (inst).ops.size() && (inst).ops[3].kind != IrOpKind::None ? (inst).ops[3] : IrOp{})
#define OPT_OP_E(inst) (4 < (inst).ops.size() && (inst).ops[4].kind != IrOpKind::None ? (inst).ops[4] : IrOp{})
#define OPT_OP_F(inst) (5 < (inst).ops.size() && (inst).ops[5].kind != IrOpKind::None ? (inst).ops[5] : IrOp{})
#define OPT_OP_G(inst) (6 < (inst).ops.size() && (inst).ops[6].kind != IrOpKind::None ? (inst).ops[6] : IrOp{})

// When IrInst operands are used, current instruction index is often required to track lifetime
inline constexpr uint32_t kInvalidInstIdx = ~0u;

struct IrInstHash
{
    static const uint32_t m = 0x5bd1e995;
    static const int r = 24;

    static uint32_t mix(uint32_t h, uint32_t k)
    {
        // MurmurHash2 step
        k *= m;
        k ^= k >> r;
        k *= m;

        h *= m;
        h ^= k;

        return h;
    }

    static uint32_t mix(uint32_t h, IrOp op)
    {
        static_assert(sizeof(op) == sizeof(uint32_t));
        uint32_t k;
        memcpy(&k, &op, sizeof(op));

        return mix(h, k);
    }

    size_t operator()(const IrInst& key) const
    {
        // MurmurHash2 unrolled
        uint32_t h = 25;

        h = mix(h, uint32_t(key.cmd));
        for (size_t i = 0; i < 7; i++)
            h = mix(h, i < uint32_t(key.ops.size()) ? key.ops[i] : IrOp{});

        // MurmurHash2 tail
        h ^= h >> 13;
        h *= m;
        h ^= h >> 15;

        return h;
    }
};

struct IrInstEq
{
    bool operator()(const IrInst& a, const IrInst& b) const
    {
        if (a.cmd != b.cmd)
            return false;
        if (a.ops.size() == b.ops.size())
        {
            for (size_t i = 0; i < a.ops.size(); i++)
                if (a.ops[i] != b.ops[i])
                    return false;
        }
        else if (a.ops.size() < b.ops.size())
        {
            size_t i = 0;
            for (; i < a.ops.size(); i++)
                if (a.ops[i] != b.ops[i])
                    return false;
            for (; i < b.ops.size(); i++)
                if (b.ops[i].kind != IrOpKind::None)
                    return false;
        }
        else
        {
            size_t i = 0;
            for (; i < b.ops.size(); i++)
                if (a.ops[i] != b.ops[i])
                    return false;
            for (; i < a.ops.size(); i++)
                if (a.ops[i].kind != IrOpKind::None)
                    return false;
        }
        return true;
    }
};

enum class IrBlockKind : uint8_t
{
    Bytecode,
    Fallback,
    Internal,
    Linearized,
    ExitSync,
    Dead,
};

inline constexpr uint32_t kBlockNoStartPc = ~0u;

inline constexpr uint8_t kBlockFlagSafeEnvCheck = 1 << 0;
inline constexpr uint8_t kBlockFlagSafeEnvClear = 1 << 1;
inline constexpr uint8_t kBlockFlagEntryArgCheck = 1 << 2;

struct IrBlock
{
    IrBlockKind kind;
    uint8_t flags = 0;
    uint16_t useCount = 0;

    // 'start' and 'finish' define an inclusive range of instructions which belong to this block inside the function
    // When block has been constructed, 'finish' always points to the first and only terminating instruction
    uint32_t start = ~0u;
    uint32_t finish = ~0u;

    uint32_t sortkey = ~0u;
    uint32_t chainkey = 0;
    uint32_t expectedNextBlock = ~0u;

    // Bytecode PC position at which the block was generated
    uint32_t startpc = kBlockNoStartPc;

    Label label;
};

struct BytecodeMapping
{
    uint32_t irLocation;
    uint32_t asmLocation;
};

struct BytecodeBlock
{
    // 'start' and 'finish' define an inclusive range of instructions which belong to the block
    int startpc = -1;
    int finishpc = -1;
};

struct BytecodeTypes
{
    uint8_t result = LBC_TYPE_ANY;
    uint8_t a = LBC_TYPE_ANY;
    uint8_t b = LBC_TYPE_ANY;
    uint8_t c = LBC_TYPE_ANY;
};

struct BytecodeRegTypeInfo
{
    uint8_t type = LBC_TYPE_ANY;
    uint8_t reg = 0; // Register slot where variable is stored
    int startpc = 0; // First point where variable is alive (could be before variable has been assigned a value)
    int endpc = 0;   // First point where variable is dead
};

struct BytecodeTypeInfo
{
    std::vector<uint8_t> argumentTypes;
    std::vector<BytecodeRegTypeInfo> regTypes;
    std::vector<uint8_t> upvalueTypes;

    // Offsets into regTypes for each individual register
    // One extra element at the end contains the vector size for easier arr[Rn], arr[Rn + 1] range access
    std::vector<uint32_t> regTypeOffsets;
};

struct ValueRestoreLocation
{
    IrOp op;             // Operand representing the location (Rn/Kn)
    IrValueKind kind;    // The kind of value at the restore location
    IrCmd conversionCmd; // Type conversion instruction that was used to store the value at the restore location
    bool lazy;           // This location comes from a DSE hint and is emitted on demand (see StoreLocationHint)
};

struct StoreLocationHint
{
    IrOp op;          // Operand representing available location (Rn)
    uint32_t instIdx; // Value that was supposed to be stored there
    IrValueKind kind; // Value kind
};

struct VmExitStoreRecord
{
    uint32_t instIdx = kInvalidInstIdx;
    IrInst backup;
};

struct VmExitStoreInfo
{
    uint8_t reg = 0;
    SmallVector<VmExitStoreRecord, 2> stores;
};

struct VmExitSyncInfo
{
    std::vector<VmExitStoreInfo> regStores;

    IrOp block;
    IrOp vmExit;
    SmallVector<IrOp, 2> argOps;
};

struct IrFunction
{
    std::vector<IrBlock> blocks;
    std::vector<IrInst> instructions;
    std::vector<IrConst> constants;

    std::vector<BytecodeBlock> bcBlocks;
    std::vector<BytecodeTypes> bcTypes;

    std::vector<BytecodeMapping> bcMapping;
    uint32_t entryBlock = 0;
    uint32_t entryLocation = 0;
    uint32_t endLocation = 0;

    std::vector<uint32_t> extraNativeData;

    // For each instruction, an operand that can be used to recompute the value
    std::vector<ValueRestoreLocation> valueRestoreOps;
    std::vector<uint32_t> validRestoreOpBlocks;
    DenseHashMap<uint32_t, StoreLocationHint> storeLocationHints{kInvalidInstIdx};

    DenseHashMap<uint32_t, VmExitSyncInfo> vmExitInfo{kInvalidInstIdx};
    DenseHashMap<uint32_t, uint32_t> blockToVmExitMap{~0u};

    BytecodeTypeInfo bcOriginalTypeInfo; // Bytecode type information as loaded
    BytecodeTypeInfo bcTypeInfo;         // Bytecode type information with additional inferences

    Proto* proto = nullptr;
    bool variadic = false;

    CfgInfo cfg;

    LoweringStats* stats = nullptr;

    bool recordCounters = false; // Taken from CompilationOptions for easy access

    uint64_t jitRngState = 0; // PCG32 state for NOP padding; seeded per-function in lowerFunction

    // Stores register tags that are known after constant propagating through a block, indexed by that block's index
    std::vector<std::vector<uint8_t>> blockExitTags; // blockIdx → tag array

    IrBlock& blockOp(IrOp op)
    {
        CODEGEN_ASSERT(op.kind == IrOpKind::Block);
        return blocks[op.index];
    }

    IrInst& instOp(IrOp op)
    {
        CODEGEN_ASSERT(op.kind == IrOpKind::Inst);
        return instructions[op.index];
    }

    IrInst* asInstOp(IrOp op)
    {
        if (op.kind == IrOpKind::Inst)
            return &instructions[op.index];

        return nullptr;
    }

    IrConst& constOp(IrOp op)
    {
        CODEGEN_ASSERT(op.kind == IrOpKind::Constant);
        return constants[op.index];
    }

    uint8_t tagOp(IrOp op)
    {
        IrConst& value = constOp(op);

        CODEGEN_ASSERT(value.kind == IrConstKind::Tag);
        return value.valueTag;
    }

    std::optional<uint8_t> asTagOp(IrOp op)
    {
        if (op.kind != IrOpKind::Constant)
            return std::nullopt;

        IrConst& value = constOp(op);

        if (value.kind != IrConstKind::Tag)
            return std::nullopt;

        return value.valueTag;
    }

    int intOp(IrOp op)
    {
        IrConst& value = constOp(op);

        CODEGEN_ASSERT(value.kind == IrConstKind::Int);
        return value.valueInt;
    }

    int64_t int64Op(IrOp op)
    {
        IrConst& value = constOp(op);

        CODEGEN_ASSERT(value.kind == IrConstKind::Int64);
        return value.valueInt64;
    }

    std::optional<int> asIntOp(IrOp op)
    {
        if (op.kind != IrOpKind::Constant)
            return std::nullopt;

        IrConst& value = constOp(op);

        if (value.kind != IrConstKind::Int)
            return std::nullopt;

        return value.valueInt;
    }

    std::optional<int64_t> asInt64Op(IrOp op)
    {
        if (op.kind != IrOpKind::Constant)
            return std::nullopt;

        IrConst& value = constOp(op);

        if (value.kind != IrConstKind::Int64)
            return std::nullopt;

        return value.valueInt64;
    }

    unsigned uintOp(IrOp op)
    {
        IrConst& value = constOp(op);

        CODEGEN_ASSERT(value.kind == IrConstKind::Uint);
        return value.valueUint;
    }

    unsigned importOp(IrOp op)
    {
        IrConst& value = constOp(op);

        CODEGEN_ASSERT(value.kind == IrConstKind::Import);
        return value.valueUint;
    }

    std::optional<unsigned> asUintOp(IrOp op)
    {
        if (op.kind != IrOpKind::Constant)
            return std::nullopt;

        IrConst& value = constOp(op);

        if (value.kind != IrConstKind::Uint)
            return std::nullopt;

        return value.valueUint;
    }

    double doubleOp(IrOp op)
    {
        IrConst& value = constOp(op);

        CODEGEN_ASSERT(value.kind == IrConstKind::Double);
        return value.valueDouble;
    }

    std::optional<double> asDoubleOp(IrOp op)
    {
        if (op.kind != IrOpKind::Constant)
            return std::nullopt;

        IrConst& value = constOp(op);

        if (value.kind != IrConstKind::Double)
            return std::nullopt;

        return value.valueDouble;
    }

    uint32_t getBlockIndex(const IrBlock& block) const
    {
        // Can only be called with blocks from our vector
        CODEGEN_ASSERT(&block >= blocks.data() && &block <= blocks.data() + blocks.size());
        return uint32_t(&block - blocks.data());
    }

    uint32_t getInstIndex(const IrInst& inst) const
    {
        // Can only be called with instructions from our vector
        CODEGEN_ASSERT(&inst >= instructions.data() && &inst <= instructions.data() + instructions.size());
        return uint32_t(&inst - instructions.data());
    }

    void recordRestoreLocation(uint32_t instIdx, ValueRestoreLocation location)
    {
        CODEGEN_ASSERT(location.op.kind == IrOpKind::None || location.op.kind == IrOpKind::VmReg || location.op.kind == IrOpKind::VmConst);

        if (instIdx >= valueRestoreOps.size())
            valueRestoreOps.resize(instIdx + 1);

        valueRestoreOps[instIdx] = location;
    }

    void materializeRestoreLocation(uint32_t instIdx)
    {
        CODEGEN_ASSERT(instIdx < valueRestoreOps.size());
        CODEGEN_ASSERT(valueRestoreOps[instIdx].lazy);

        valueRestoreOps[instIdx].lazy = false;
    }

    ValueRestoreLocation findRestoreLocation(uint32_t instIdx, bool limitToCurrentBlock) const
    {
        if (instIdx >= valueRestoreOps.size())
            return {};

        // When spilled, values can only reference restore operands in the current block chain
        if (limitToCurrentBlock)
        {
            for (uint32_t blockIdx : validRestoreOpBlocks)
            {
                const IrBlock& block = blocks[blockIdx];

                if (instIdx >= block.start && instIdx <= block.finish)
                    return valueRestoreOps[instIdx];
            }

            return {};
        }

        return valueRestoreOps[instIdx];
    }

    ValueRestoreLocation findRestoreLocation(const IrInst& inst, bool limitToCurrentBlock) const
    {
        return findRestoreLocation(getInstIndex(inst), limitToCurrentBlock);
    }

    bool hasRestoreLocation(const IrInst& inst, bool limitToCurrentBlock) const
    {
        return findRestoreLocation(getInstIndex(inst), limitToCurrentBlock).op.kind != IrOpKind::None;
    }

    void recordStoreLocationHint(uint32_t instIdx, StoreLocationHint hint)
    {
        storeLocationHints[instIdx] = hint;
    }

    const StoreLocationHint* findStoreLocationHint(uint32_t instIdx) const
    {
        return storeLocationHints.find(instIdx);
    }

    BytecodeTypes getBytecodeTypesAt(int pcpos) const
    {
        CODEGEN_ASSERT(pcpos >= 0);

        if (size_t(pcpos) < bcTypes.size())
            return bcTypes[pcpos];

        return BytecodeTypes();
    }
};

inline IrCondition conditionOp(IrOp op)
{
    CODEGEN_ASSERT(op.kind == IrOpKind::Condition);
    return IrCondition(op.index);
}

inline int vmRegOp(IrOp op)
{
    CODEGEN_ASSERT(op.kind == IrOpKind::VmReg);
    return op.index;
}

inline int vmConstOp(IrOp op)
{
    CODEGEN_ASSERT(op.kind == IrOpKind::VmConst);
    return op.index;
}

inline int vmUpvalueOp(IrOp op)
{
    CODEGEN_ASSERT(op.kind == IrOpKind::VmUpvalue);
    return op.index;
}

inline uint32_t vmExitOp(IrOp op)
{
    CODEGEN_ASSERT(op.kind == IrOpKind::VmExit);
    return op.index;
}

} // namespace CodeGen
} // namespace Luau
