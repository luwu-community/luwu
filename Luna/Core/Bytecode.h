// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Core/Value.h"

namespace Luwu::Dsl
{

// Registers are frame-local Value slots, not machine registers. Calls use a
// contiguous function/argument window; temporaries are reused while compiling.
enum class BytecodeOp : uint8_t
{
    Constant,
    Quote,
    LoadLocal,
    LoadUpvalue,
    LoadName,
    LoadGlobal,
    StoreLocal,
    StoreUpvalue,
    SetName,
    SetGlobal,
    Define,
    Closure,
    JumpFalse,
    Jump,
    Call,
    TailCall,
    Return,
    Syntax,
    SyntaxCase,
    Invalid,
};

struct BytecodeInstruction
{
    BytecodeOp op;
    uint32_t a = 0, b = 0, c = 0;
    const SExpr* source = nullptr;
};

struct BytecodeCase
{
    const SExpr* clause = nullptr;
    const BytecodeFunction* fender = nullptr;
    const BytecodeFunction* body = nullptr;
};

struct BytecodeCasePlan
{
    const SExpr* form = nullptr;
    std::vector<SExpr> literals;
    std::vector<BytecodeCase> clauses;
};

struct BytecodeFunction : GcCell
{
    struct Capture { bool local; uint32_t index; std::string name; };
    const SExpr* form = nullptr;
    std::vector<BytecodeInstruction> instructions;
    std::vector<Value> constants;
    std::vector<const BytecodeFunction*> children;
    std::vector<BytecodeCasePlan> cases;
    std::vector<std::string> params;
    std::string restParam;
    std::vector<std::string> localNames;
    std::unordered_map<std::string, uint32_t> localIndices;
    std::vector<Capture> captures;
    std::unordered_map<std::string, uint32_t> captureIndices;
    uint32_t registerCount = 1;

    void markChildren(std::vector<const GcCell*>& worklist) const override;
};

// Input is expanded core syntax; the compiler never invokes the macro expander.
const BytecodeFunction* compileBytecode(const SExpr& expression, bool globalContext = false);
void dumpBytecode(std::ostream& out, const BytecodeFunction& function);

} // namespace Luwu::Dsl
