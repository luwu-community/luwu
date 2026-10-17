// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "RuleCompiler.h"
#include "OpTemplates.h"

#include <functional>
#include <string_view>
#include <unordered_map>

namespace Luwu::Dsl
{

static std::string formatRegArg(const std::string& arg)
{
    if (arg.rfind("OP_", 0) == 0)
        return "lowering.regOp(" + arg + ")";
    return arg;
}

static std::string sizeForTypeX64(TypeKind type)
{
    switch (type)
    {
    case TypeKind::U8:
        return "SizeX64::byte";
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::U32:
    case TypeKind::Tag:
        return "SizeX64::dword";
    case TypeKind::F32:
    case TypeKind::F64:
    case TypeKind::Vec:
        return "SizeX64::xmmword";
    default:
        return "SizeX64::qword";
    }
}

struct ArchOpSpecX64
{
    size_t arity = 0;
    bool allowMemArgs = false;
    const char* syntax = nullptr;
    const char* desc = nullptr;
    std::function<void(CodeWriter&, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>&)> emit;
};

static void emitGuardBranchX64(CodeWriter& out, EvalContext& ctx, const std::string& condStr, const std::string& labelOp)
{
    ctx.use("index");
    ctx.use("next");
    out.line("lowering.jumpOrAbortOnUndef(" + condStr + ", " + labelOp + ", index, next);");
}

static void requireMemArgX64(EvalContext& ctx, const Operand& op, std::string_view opName)
{
    if (op.kind != OperandKind::Mem)
        throw CompileError(ctx.loc, std::string(opName) + " requires memory operand for addr");
}

static std::string formatMemX64(EvalContext& ctx, const Operand& val, const std::string& explicitSize = "")
{
    if (val.kind != OperandKind::Mem)
        return formatRegArg(val.text);

    std::string sizeStr = explicitSize;
    if (sizeStr.empty())
    {
        if (val.type == TypeKind::Void)
            throw CompileError(ctx.loc, "Memory operand requires an explicit type (e.g. :u8, :u32, :ptr)");
        sizeStr = (val.type == TypeKind::U8) ? "byte"
            : (val.type == TypeKind::I16) ? "word"
            : ((val.type == TypeKind::U32 || val.type == TypeKind::I32) ? "dword"
            : (val.type == TypeKind::Vec ? "xmmword" : "qword"));
    }
    return sizeStr + "[" + formatRegArg(val.memBase) + " + " + val.memOffset + "]";
}

static const std::unordered_map<std::string_view, ArchOpSpecX64> kArchOpsX64 = {
    // ========================================================================
    // A. Memory Loads, Stores & Address Helpers
    // ========================================================================
    {"x64:movzx-mem-u8!", {
        2, true,
        "(x64:movzx-mem-u8! <dest> <src/mem>)",
        "Load unsigned 8-bit byte from memory with zero extension",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:movzx-mem-u8!");
        out.line("build.movzx(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "byte") + ");");
    }}},
    {"x64:movzx-mem-u16!", {
        2, true,
        "(x64:movzx-mem-u16! <dest> <src/mem>)",
        "Load unsigned 16-bit word from memory with zero extension",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:movzx-mem-u16!");
        out.line("build.movzx(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "word") + ");");
    }}},
    {"x64:movsx-mem-i8!", {
        2, true,
        "(x64:movsx-mem-i8! <dest> <src/mem>)",
        "Load signed 8-bit byte from memory with sign extension",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:movsx-mem-i8!");
        out.line("build.movsx(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "byte") + ");");
    }}},
    {"x64:movsx-mem-i16!", {
        2, true,
        "(x64:movsx-mem-i16! <dest> <src/mem>)",
        "Load signed 16-bit word from memory with sign extension",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:movsx-mem-i16!");
        out.line("build.movsx(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "word") + ");");
    }}},
    {"x64:mov-mem-u32!", {
        2, true,
        "(x64:mov-mem-u32! <dest> <src/mem>)",
        "Load 32-bit unsigned dword from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:mov-mem-u32!");
        out.line("build.mov(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "dword") + ");");
    }}},
    {"x64:mov-mem-ptr!", {
        2, true,
        "(x64:mov-mem-ptr! <dest> <src/mem>)",
        "Load 64-bit pointer from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:mov-mem-ptr!");
        std::string dest = formatRegArg(raw[0].text);
        if (raw[1].memBase == "rClosure")
        {
            out.line("build.mov(" + dest + ", sClosure);");
            out.line("build.mov(" + dest + ", qword[" + dest + " + " + raw[1].memOffset + "]);");
        }
        else
        {
            out.line("build.mov(" + dest + ", " + formatMemX64(ctx, raw[1], "qword") + ");");
        }
    }}},
    {"x64:mov-mem-64!", {
        2, true,
        "(x64:mov-mem-64! <dest> <src/mem>)",
        "Load 64-bit qword from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:mov-mem-64!");
        out.line("build.mov(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "qword") + ");");
    }}},
    {"x64:vmovss-mem!", {
        2, true,
        "(x64:vmovss-mem! <dest> <src/mem>)",
        "Load 32-bit float from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:vmovss-mem!");
        out.line("build.vmovss(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "dword") + ");");
    }}},
    {"x64:vmovsd-mem!", {
        2, true,
        "(x64:vmovsd-mem! <dest> <src/mem>)",
        "Load 64-bit double from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:vmovsd-mem!");
        out.line("build.vmovsd(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "qword") + ");");
    }}},
    {"x64:vmovups-mem!", {
        2, true,
        "(x64:vmovups-mem! <dest> <src/mem>)",
        "Load 128-bit unaligned SIMD vector from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[1], "x64:vmovups-mem!");
        out.line("build.vmovups(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "xmmword") + ");");
    }}},
    {"x64:mov-to-mem-u8!", {
        2, true,
        "(x64:mov-to-mem-u8! <addr> <val>)",
        "Store 8-bit register byte into memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-u8!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "byte") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:mov-to-mem-u16!", {
        2, true,
        "(x64:mov-to-mem-u16! <addr> <val>)",
        "Store 16-bit register word into memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-u16!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "word") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:mov-to-mem-u32!", {
        2, true,
        "(x64:mov-to-mem-u32! <addr> <val>)",
        "Store 32-bit register dword into memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-u32!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "dword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:mov-to-mem-ptr!", {
        2, true,
        "(x64:mov-to-mem-ptr! <addr> <val>)",
        "Store pointer register into memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-ptr!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "qword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:mov-to-mem-64!", {
        2, true,
        "(x64:mov-to-mem-64! <addr> <val>)",
        "Store 64-bit register qword into memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-64!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "qword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:vmovss-to-mem!", {
        2, true,
        "(x64:vmovss-to-mem! <addr> <val>)",
        "Store 32-bit float register into memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[0], "x64:vmovss-to-mem!");
        out.line("build.vmovss(" + formatMemX64(ctx, raw[0], "dword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:vmovsd-to-mem!", {
        2, true,
        "(x64:vmovsd-to-mem! <addr> <val>)",
        "Store 64-bit double register into memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[0], "x64:vmovsd-to-mem!");
        out.line("build.vmovsd(" + formatMemX64(ctx, raw[0], "qword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:vmovups-to-mem!", {
        2, true,
        "(x64:vmovups-to-mem! <addr> <val>)",
        "Store 128-bit SIMD vector register into memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgX64(ctx, raw[0], "x64:vmovups-to-mem!");
        out.line("build.vmovups(" + formatMemX64(ctx, raw[0], "xmmword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:lea-reg-32!", {
        3, false,
        "(x64:lea-reg-32! <dest> <base> <offset>)",
        "Load effective address [base + offset-reg] (32-bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + " + formatRegArg(args[2]) + "]);");
    }}},
    {"x64:lea-reg-64!", {
        3, false,
        "(x64:lea-reg-64! <dest> <base> <offset>)",
        "Load effective address [base + offset-reg] (64-bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + " + formatRegArg(args[2]) + "]);");
    }}},
    {"x64:lea-imm-32!", {
        3, false,
        "(x64:lea-imm-32! <dest> <base> <offset>)",
        "Load effective address [base + offset-imm] (32-bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + lowering.intOp(" + args[2] + ")]);");
    }}},
    {"x64:lea-imm-64!", {
        3, false,
        "(x64:lea-imm-64! <dest> <base> <offset>)",
        "Load effective address [base + offset-imm] (64-bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + int32_t(lowering.int64Op(" + args[2] + "))]);");
    }}},
    {"x64:lea-sub-32!", {
        3, false,
        "(x64:lea-sub-32! <dest> <base> <offset>)",
        "Load effective address [base - offset-imm] (32-bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " - lowering.intOp(" + args[2] + ")]);");
    }}},
    {"x64:lea-sub-64!", {
        3, false,
        "(x64:lea-sub-64! <dest> <base> <offset>)",
        "Load effective address [base - offset-imm] (64-bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " - int32_t(lowering.int64Op(" + args[2] + "))]);");
    }}},
    {"x64:lea-offset!", {
        3, false,
        "(x64:lea-offset! <dest> <base> <offset>)",
        "Add immediate byte offset to base pointer into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + int32_t(" + args[2] + ")]);");
    }}},
    {"x64:code-addr!", {
        2, false,
        "(x64:code-addr! <dest> <src>)",
        "Compute absolute host memory address of bytecode instruction",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + formatRegArg(args[0]) + ", sCode);");
        out.line("build.add(" + formatRegArg(args[0]) + ", (" + formatRegArg(args[1]) + ") * sizeof(Instruction));");
    }}},

    // ========================================================================
    // B. Moves, Immediates & Lowering Accessors
    // ========================================================================
    {"x64:mov-reg!", {
        2, false,
        "(x64:mov-reg! <dest> <src>)",
        "Move register to register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:mov-closure!", {
        1, false,
        "(x64:mov-closure! <dest>)",
        "Move closure base register into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", sClosure);");
    }}},
    {"x64:mov-code!", {
        1, false,
        "(x64:mov-code! <dest>)",
        "Move code base register into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", sCode);");
    }}},
    {"x64:mov-imm-32!", {
        2, false,
        "(x64:mov-imm-32! <dest> <src>)",
        "Load 32-bit immediate into register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"x64:mov-imm-64!", {
        2, false,
        "(x64:mov-imm-64! <dest> <src>)",
        "Load 64-bit immediate into register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov64(" + args[0] + ", lowering.int64Op(" + args[1] + "));");
    }}},
    {"x64:mov-imm-raw!", {
        2, false,
        "(x64:mov-imm-raw! <dest> <src>)",
        "Load raw immediate into destination register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:mov-mem-reg-32!", {
        2, false,
        "(x64:mov-mem-reg-32! <dest> <src>)",
        "Load from 32-bit memory or register operand",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", lowering.memRegUintOp(" + args[1] + "));");
    }}},
    {"x64:mov-mem-reg-i32!", {
        2, false,
        "(x64:mov-mem-reg-i32! <dest> <src>)",
        "Load 32-bit signed integer from memory or register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:mov-mem-reg-64!", {
        2, false,
        "(x64:mov-mem-reg-64! <dest> <src>)",
        "Load from 64-bit memory or register operand",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:mov-mem-reg-tag!", {
        2, false,
        "(x64:mov-mem-reg-tag! <dest> <src>)",
        "Load tag byte from memory or register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", lowering.memRegTagOp(" + args[1] + "));");
    }}},
    {"x64:xor-zero!", {
        1, false,
        "(x64:xor-zero! <dest>)",
        "Zero out register using XOR (reg, reg)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.xor_(" + args[0] + ", " + args[0] + ");");
    }}},

    // ========================================================================
    // C. Integer Arithmetic & Sign/Zero Extensions
    // ========================================================================
    {"x64:add-reg-32!", {
        2, false,
        "(x64:add-reg-32! <dest> <src>)",
        "Add 32-bit register into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:add-reg-64!", {
        2, false,
        "(x64:add-reg-64! <dest> <src>)",
        "Add 64-bit register into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:add-imm-32!", {
        2, false,
        "(x64:add-imm-32! <dest> <src>)",
        "Add 32-bit immediate into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"x64:add-imm-64!", {
        2, false,
        "(x64:add-imm-64! <dest> <src>)",
        "Add 64-bit immediate into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", int32_t(lowering.int64Op(" + args[1] + ")));");
    }}},
    {"x64:add-offset!", {
        2, false,
        "(x64:add-offset! <dest> <src>)",
        "Add byte offset to base pointer",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", int32_t(" + args[1] + "));");
    }}},
    {"x64:add-mem-reg-32!", {
        2, false,
        "(x64:add-mem-reg-32! <dest> <src>)",
        "Add 32-bit memory or register operand into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:add-mem-reg-64!", {
        2, false,
        "(x64:add-mem-reg-64! <dest> <src>)",
        "Add 64-bit memory or register operand into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:sub-reg-32!", {
        2, false,
        "(x64:sub-reg-32! <dest> <src>)",
        "Subtract 32-bit register from destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:sub-reg-64!", {
        2, false,
        "(x64:sub-reg-64! <dest> <src>)",
        "Subtract 64-bit register from destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:sub-imm-32!", {
        2, false,
        "(x64:sub-imm-32! <dest> <src>)",
        "Subtract 32-bit immediate from destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"x64:sub-imm-64!", {
        2, false,
        "(x64:sub-imm-64! <dest> <src>)",
        "Subtract 64-bit immediate from destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", int32_t(lowering.int64Op(" + args[1] + ")));");
    }}},
    {"x64:sub-mem-reg-32!", {
        2, false,
        "(x64:sub-mem-reg-32! <dest> <src>)",
        "Subtract 32-bit memory or register operand from destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:sub-mem-reg-64!", {
        2, false,
        "(x64:sub-mem-reg-64! <dest> <src>)",
        "Subtract 64-bit memory or register operand from destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:neg-32!", {
        1, false,
        "(x64:neg-32! <dest>)",
        "Negate 32-bit integer register (two's complement)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.neg(" + args[0] + ");");
    }}},
    {"x64:neg-64!", {
        1, false,
        "(x64:neg-64! <dest>)",
        "Negate 64-bit integer register (two's complement)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.neg(" + args[0] + ");");
    }}},
    {"x64:inc!", {
        1, false,
        "(x64:inc! <dest>)",
        "Increment destination register by 1",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.inc(" + args[0] + ");");
    }}},
    {"x64:dec!", {
        1, false,
        "(x64:dec! <dest>)",
        "Decrement destination register by 1",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.dec(" + args[0] + ");");
    }}},
    {"x64:imul-32!", {
        2, false,
        "(x64:imul-32! <dest> <src>)",
        "Signed 32-bit integer multiplication",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.imul(" + args[0] + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:imul-64!", {
        2, false,
        "(x64:imul-64! <dest> <src>)",
        "Signed 64-bit integer multiplication into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.imul(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:imul-imm-32!", {
        3, false,
        "(x64:imul-imm-32! <dest> <src1> <src2>)",
        "Multiply 32-bit register and immediate into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.imul(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.intOp(" + args[2] + "));");
    }}},
    {"x64:movsx-byte!", {
        2, false,
        "(x64:movsx-byte! <dest> <src>)",
        "Sign-extend 8-bit byte to 32-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.movsx(" + args[0] + ", byteReg(" + formatRegArg(args[1]) + "));");
    }}},
    {"x64:movsx-word!", {
        2, false,
        "(x64:movsx-word! <dest> <src>)",
        "Sign-extend 16-bit word to 32-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.movsx(" + args[0] + ", wordReg(" + formatRegArg(args[1]) + "));");
    }}},
    {"x64:movsxd!", {
        2, false,
        "(x64:movsxd! <dest> <src>)",
        "Sign-extend 32-bit register to 64-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.movsxd(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:mov-dword-reg!", {
        2, false,
        "(x64:mov-dword-reg! <dest> <src>)",
        "Move 32-bit dword register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(dwordReg(" + args[0] + "), " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:mov-from-dword-reg!", {
        2, false,
        "(x64:mov-from-dword-reg! <dest> <src>)",
        "Move from 32-bit dword register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", dwordReg(" + formatRegArg(args[1]) + "));");
    }}},

    // ========================================================================
    // D. Bitwise, Shifts & Rotates
    // ========================================================================
    {"x64:bswap-32!", {
        1, false,
        "(x64:bswap-32! <dest>)",
        "Byte swap 32-bit destination register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.bswap(" + args[0] + ");");
    }}},
    {"x64:bswap-64!", {
        1, false,
        "(x64:bswap-64! <dest>)",
        "Byte swap 64-bit destination register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.bswap(" + args[0] + ");");
    }}},
    {"x64:not-32!", {
        1, false,
        "(x64:not-32! <dest>)",
        "Bitwise NOT of 32-bit destination register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.not_(" + args[0] + ");");
    }}},
    {"x64:not-64!", {
        1, false,
        "(x64:not-64! <dest>)",
        "Bitwise NOT of 64-bit destination register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.not_(" + args[0] + ");");
    }}},
    {"x64:and-32!", {
        2, false,
        "(x64:and-32! <dest> <src>)",
        "Bitwise AND 32-bit operand into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.and_(" + args[0] + ", lowering.memRegUintOp(" + args[1] + "));");
    }}},
    {"x64:and-64!", {
        2, false,
        "(x64:and-64! <dest> <src>)",
        "Bitwise AND 64-bit operand into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.and_(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:and-imm!", {
        2, false,
        "(x64:and-imm! <dest> <src>)",
        "Bitwise AND register with immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.and_(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:or-32!", {
        2, false,
        "(x64:or-32! <dest> <src>)",
        "Bitwise OR 32-bit operand into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.or_(" + args[0] + ", lowering.memRegUintOp(" + args[1] + "));");
    }}},
    {"x64:or-64!", {
        2, false,
        "(x64:or-64! <dest> <src>)",
        "Bitwise OR 64-bit operand into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.or_(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:or-imm!", {
        2, false,
        "(x64:or-imm! <dest> <src>)",
        "Bitwise OR register with immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.or_(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:xor-32!", {
        2, false,
        "(x64:xor-32! <dest> <src>)",
        "Bitwise XOR 32-bit operand into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.xor_(" + args[0] + ", lowering.memRegUintOp(" + args[1] + "));");
    }}},
    {"x64:xor-64!", {
        2, false,
        "(x64:xor-64! <dest> <src>)",
        "Bitwise XOR 64-bit operand into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.xor_(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:xor-imm!", {
        2, false,
        "(x64:xor-imm! <dest> <src>)",
        "Bitwise XOR register with immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.xor_(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:shl-imm!", {
        2, false,
        "(x64:shl-imm! <dest> <src>)",
        "Logical shift left register by immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.shl(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:shl-cl!", {
        1, false,
        "(x64:shl-cl! <dest>)",
        "Logical shift left register by cl register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.shl(" + args[0] + ", cl);");
    }}},
    {"x64:shr-imm-32!", {
        2, false,
        "(x64:shr-imm-32! <dest> <src>)",
        "Logical shift right 32-bit register by immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.shr(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:shr-imm!", {
        2, false,
        "(x64:shr-imm! <dest> <src>)",
        "Logical shift right register by immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.shr(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:shr-cl!", {
        1, false,
        "(x64:shr-cl! <dest>)",
        "Logical shift right register by cl register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.shr(" + args[0] + ", cl);");
    }}},
    {"x64:sar-imm!", {
        2, false,
        "(x64:sar-imm! <dest> <src>)",
        "Arithmetic shift right register by immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sar(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:sar-cl!", {
        1, false,
        "(x64:sar-cl! <dest>)",
        "Arithmetic shift right register by cl register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sar(" + args[0] + ", cl);");
    }}},
    {"x64:rol-imm!", {
        2, false,
        "(x64:rol-imm! <dest> <src>)",
        "Rotate left register by immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.rol(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:rol-cl!", {
        1, false,
        "(x64:rol-cl! <dest>)",
        "Rotate left register by cl register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.rol(" + args[0] + ", cl);");
    }}},
    {"x64:ror-imm!", {
        2, false,
        "(x64:ror-imm! <dest> <src>)",
        "Rotate right register by immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ror(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:ror-cl!", {
        1, false,
        "(x64:ror-cl! <dest>)",
        "Rotate right register by cl register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ror(" + args[0] + ", cl);");
    }}},
    {"x64:bsr!", {
        2, false,
        "(x64:bsr! <dest> <src>)",
        "Bit scan reverse (find most significant set bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.bsr(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:bsf!", {
        2, false,
        "(x64:bsf! <dest> <src>)",
        "Bit scan forward (find least significant set bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.bsf(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},

    // ========================================================================
    // E. Comparisons, Conditionals, Guard Branches & Block Jumps
    // ========================================================================
    {"x64:cmp!", {
        2, true,
        "(x64:cmp! <dest> <src/mem>)",
        "Compare two operands and set x86 FLAGS",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:test-reg!", {
        1, false,
        "(x64:test-reg! <dest>)",
        "Test bitwise AND of two registers and set FLAGS",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string r = formatRegArg(args[0]);
        out.line("build.test(" + r + ", " + r + ");");
    }}},
    {"x64:cmp-imm-32!", {
        2, false,
        "(x64:cmp-imm-32! <lhs> <rhs>)",
        "Compare 32-bit register with immediate and set FLAGS",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArg(args[0]) + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"x64:cmp-mem-reg-32!", {
        2, false,
        "(x64:cmp-mem-reg-32! <lhs> <rhs>)",
        "Compare 32-bit memory/register operand and set FLAGS",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArg(args[0]) + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:cmp-mem-reg-64!", {
        2, false,
        "(x64:cmp-mem-reg-64! <lhs> <rhs>)",
        "Compare 64-bit memory/register operand and set FLAGS",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArg(args[0]) + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:setcc-eq!", {
        1, false,
        "(x64:setcc-eq! <dest>)",
        "Set byte register if equal (ZF=1)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.setcc(ConditionX64::Equal, byteReg(" + args[0] + "));");
    }}},
    {"x64:setcc-ne!", {
        1, false,
        "(x64:setcc-ne! <dest>)",
        "Set byte register if not equal (ZF=0)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.setcc(ConditionX64::NotEqual, byteReg(" + args[0] + "));");
    }}},
    {"x64:setcc-cond-int!", {
        2, false,
        "(x64:setcc-cond-int! <dest> <src>)",
        "Set byte register based on 32-bit IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.setcc(getConditionInt(" + args[1] + "), byteReg(" + args[0] + "));");
    }}},
    {"x64:setcc-inv-cond-int!", {
        2, false,
        "(x64:setcc-inv-cond-int! <dest> <src>)",
        "Set byte register based on inverted 32-bit IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.setcc(getInverseCondition(getConditionInt(" + args[1] + ")), byteReg(" + args[0] + "));");
    }}},
    {"x64:cmov!", {
        3, false,
        "(x64:cmov! <dest> <src1> <src2>)",
        "Conditional move register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmov(" + args[0] + ", " + args[1] + ", " + formatRegArg(args[2]) + ");");
    }}},
    {"x64:cmov-cond-int-64!", {
        3, false,
        "(x64:cmov-cond-int-64! <dest> <src1> <src2>)",
        "Conditional move 64-bit register based on IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmov(getConditionInt(" + args[1] + "), " + args[0] + ", lowering.memRegInt64Op(" + args[2] + "));");
    }}},

    // Guard Branches (abort-on-undef / VM-exit aware via jumpOrAbortOnUndef)
    {"x64:guard-je!", {
        1, false,
        "(x64:guard-je! <label>)",
        "Guard branch if equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchX64(out, ctx, "ConditionX64::Equal", args[0]);
    }}},
    {"x64:guard-jne!", {
        1, false,
        "(x64:guard-jne! <label>)",
        "Guard branch if not equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[0]);
    }}},
    {"x64:guard-z!", {
        2, true,
        "(x64:guard-z! <dest> <src/mem>)",
        "Guard branch if zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", 0);");
        emitGuardBranchX64(out, ctx, "ConditionX64::Equal", args[1]);
    }}},
    {"x64:guard-nz!", {
        2, true,
        "(x64:guard-nz! <dest> <src/mem>)",
        "Guard branch if not zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", 0);");
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[1]);
    }}},
    {"x64:guard-eq!", {
        3, true,
        "(x64:guard-eq! <dest> <src/mem>)",
        "Guard branch if registers are equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::Equal", args[2]);
    }}},
    {"x64:guard-ne!", {
        3, true,
        "(x64:guard-ne! <dest> <src/mem>)",
        "Guard branch if not equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[2]);
    }}},
    {"x64:branch-if-not-equal!", {
        3, true,
        "(x64:branch-if-not-equal! <dest> <src/mem>)",
        "Compare and branch if not equal on X64",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[2]);
    }}},
    {"x64:branch-if-tag-not-equal!", {
        3, false,
        "(x64:branch-if-tag-not-equal! <val> <expected> <label>)",
        "Compare tag and branch to label if not equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(lowering.memRegTagOp(" + args[0] + "), " + args[1] + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[2]);
    }}},
    {"x64:guard-ule!", {
        3, true,
        "(x64:guard-ule! <dest> <src/mem>)",
        "Guard branch if unsigned less or equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::BelowEqual", args[2]);
    }}},
    {"x64:guard-ult!", {
        3, true,
        "(x64:guard-ult! <dest> <src/mem>)",
        "Guard branch if unsigned less than",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::Below", args[2]);
    }}},
    {"x64:guard-neg-cond-int!", {
        2, false,
        "(x64:guard-neg-cond-int! <val> <label>)",
        "Guard branch on negated 32-bit condition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchX64(out, ctx, "getConditionInt(getNegatedCondition(" + args[0] + "))", args[1]);
    }}},
    {"x64:guard-neg-cond-int64!", {
        2, false,
        "(x64:guard-neg-cond-int64! <val> <label>)",
        "Guard branch on negated 64-bit condition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchX64(out, ctx, "getConditionInt(getNegatedCondition(" + args[0] + "))", args[1]);
    }}},

    // Block Jumps (jump directly to lowering.labelOp(target))
    {"x64:jump-or-abort!", {
        1, false,
        "(x64:jump-or-abort! <dest>)",
        "Branch to target or abort on undefined block",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("index");
        ctx.use("next");
        out.line("lowering.jumpOrAbortOnUndef(" + args[0] + ", index, next);");
    }}},
    {"x64:jcc-eq!", {
        1, false,
        "(x64:jcc-eq! <dest>)",
        "Jump to block label if equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.jcc(ConditionX64::Equal, lowering.labelOp(" + args[0] + "));");
    }}},
    {"x64:jcc-ne!", {
        1, false,
        "(x64:jcc-ne! <dest>)",
        "Jump to block label if not equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.jcc(ConditionX64::NotEqual, lowering.labelOp(" + args[0] + "));");
    }}},
    {"x64:jcc-cond-int!", {
        2, false,
        "(x64:jcc-cond-int! <dest> <src>)",
        "Jump to block label based on IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.jcc(getConditionInt(" + args[0] + "), lowering.labelOp(" + args[1] + "));");
    }}},
    {"x64:jump-z!", {
        2, false,
        "(x64:jump-z! <val> <label>)",
        "Jump to block label if zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string r = formatRegArg(args[0]);
        out.line("build.test(" + r + ", " + r + ");");
        out.line("build.jcc(ConditionX64::Equal, lowering.labelOp(" + args[1] + "));");
    }}},
    {"x64:jump-nz!", {
        2, false,
        "(x64:jump-nz! <val> <label>)",
        "Jump to block label if not zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string r = formatRegArg(args[0]);
        out.line("build.test(" + r + ", " + r + ");");
        out.line("build.jcc(ConditionX64::NotEqual, lowering.labelOp(" + args[1] + "));");
    }}},
    {"x64:jump-eq!", {
        3, false,
        "(x64:jump-eq! <val> <expected> <label>)",
        "Jump to block label if registers are equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArg(args[0]) + ", " + formatRegArg(args[1]) + ");");
        out.line("build.jcc(ConditionX64::Equal, lowering.labelOp(" + args[2] + "));");
    }}},
    {"x64:jump-ne!", {
        3, false,
        "(x64:jump-ne! <val> <expected> <label>)",
        "Jump to block label if not equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArg(args[0]) + ", " + formatRegArg(args[1]) + ");");
        out.line("build.jcc(ConditionX64::NotEqual, lowering.labelOp(" + args[2] + "));");
    }}},
    {"x64:jump-neg!", {
        2, false,
        "(x64:jump-neg! <val> <label>)",
        "Jump to block label if register is negative",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string r = formatRegArg(args[0]);
        out.line("build.test(" + r + ", " + r + ");");
        out.line("build.jcc(ConditionX64::Less, lowering.labelOp(" + args[1] + "));");
    }}},
    {"x64:jump-cond-int!", {
        4, false,
        "(x64:jump-cond-int! <dest> <src1> <src2> <src3>)",
        "Jump to block label based on 32-bit IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArg(args[0]) + ", " + formatRegArg(args[1]) + ");");
        out.line("build.jcc(getConditionInt(" + args[2] + "), lowering.labelOp(" + args[3] + "));");
    }}},
    {"x64:jmp!", {
        1, false,
        "(x64:jmp! <label>)",
        "Unconditional jump to label",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.jmp(lowering.labelOp(" + args[0] + "));");
    }}},

    // ========================================================================
    // F. Floating-Point & Vector Primitives
    // ========================================================================
    {"x64:vmovsd!", {
        2, false,
        "(x64:vmovsd! <dest> <src>)",
        "Move scalar double-precision float",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vmovsd(" + args[0] + ", lowering.memRegDoubleOp(" + args[1] + "));");
    }}},
    {"x64:vmovss!", {
        2, false,
        "(x64:vmovss! <dest> <src>)",
        "Move scalar single-precision float",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vmovss(" + args[0] + ", lowering.memRegFloatOp(" + args[1] + "));");
    }}},
    {"x64:vmovups!", {
        2, false,
        "(x64:vmovups! <dest> <src>)",
        "Move unaligned SIMD vector",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vmovups(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:vaddsd!", {
        3, false,
        "(x64:vaddsd! <dest> <src1> <src2>)",
        "Add scalar double-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vaddsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vsubsd!", {
        3, false,
        "(x64:vsubsd! <dest> <src1> <src2>)",
        "Subtract scalar double-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vsubsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vmulsd!", {
        3, false,
        "(x64:vmulsd! <dest> <src1> <src2>)",
        "Multiply scalar double-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vmulsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vdivsd!", {
        3, false,
        "(x64:vdivsd! <dest> <src1> <src2>)",
        "Divide scalar double-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vdivsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vminsd!", {
        3, false,
        "(x64:vminsd! <dest> <src1> <src2>)",
        "Minimum of scalar double-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vminsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vmaxsd!", {
        3, false,
        "(x64:vmaxsd! <dest> <src1> <src2>)",
        "Maximum of scalar double-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vmaxsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vsqrtsd!", {
        2, false,
        "(x64:vsqrtsd! <dest> <src>)",
        "Compute square root of scalar double-precision float",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vsqrtsd(" + args[0] + ", " + args[0] + ", " + src + ");");
    }}},
    {"x64:vroundsd-floor!", {
        2, false,
        "(x64:vroundsd-floor! <dest> <src>)",
        "Round scalar double toward minus infinity (floor)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vroundsd(" + args[0] + ", " + args[0] + ", " + src + ", RoundingModeX64::RoundToNegativeInfinity);");
    }}},
    {"x64:vroundsd-ceil!", {
        2, false,
        "(x64:vroundsd-ceil! <dest> <src>)",
        "Round scalar double toward plus infinity (ceil)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vroundsd(" + args[0] + ", " + args[0] + ", " + src + ", RoundingModeX64::RoundToPositiveInfinity);");
    }}},
    {"x64:vaddss!", {
        3, false,
        "(x64:vaddss! <dest> <src1> <src2>)",
        "Add scalar single-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vaddss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vsubss!", {
        3, false,
        "(x64:vsubss! <dest> <src1> <src2>)",
        "Subtract scalar single-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vsubss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vmulss!", {
        3, false,
        "(x64:vmulss! <dest> <src1> <src2>)",
        "Multiply scalar single-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vmulss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vdivss!", {
        3, false,
        "(x64:vdivss! <dest> <src1> <src2>)",
        "Divide scalar single-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vdivss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vminss!", {
        3, false,
        "(x64:vminss! <dest> <src1> <src2>)",
        "Minimum of scalar single-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vminss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vmaxss!", {
        3, false,
        "(x64:vmaxss! <dest> <src1> <src2>)",
        "Maximum of scalar single-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vmaxss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vsqrtss!", {
        2, false,
        "(x64:vsqrtss! <dest> <src>)",
        "Compute square root of scalar single-precision float",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegFloatOp(" + args[1] + ")");
        out.line("build.vsqrtss(" + args[0] + ", " + args[0] + ", " + src + ");");
    }}},
    {"x64:vroundss-floor!", {
        2, false,
        "(x64:vroundss-floor! <dest> <src>)",
        "Round scalar float toward minus infinity (floor)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegFloatOp(" + args[1] + ")");
        out.line("build.vroundss(" + args[0] + ", " + args[0] + ", " + src + ", RoundingModeX64::RoundToNegativeInfinity);");
    }}},
    {"x64:vroundss-ceil!", {
        2, false,
        "(x64:vroundss-ceil! <dest> <src>)",
        "Round scalar float toward plus infinity (ceil)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegFloatOp(" + args[1] + ")");
        out.line("build.vroundss(" + args[0] + ", " + args[0] + ", " + src + ", RoundingModeX64::RoundToPositiveInfinity);");
    }}},
    {"x64:vcvtsi2sd!", {
        2, false,
        "(x64:vcvtsi2sd! <dest> <src>)",
        "Convert signed integer to scalar double-precision float",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vcvtsi2sd(" + args[0] + ", " + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:vcvtsi2sd-qword!", {
        2, false,
        "(x64:vcvtsi2sd-qword! <dest> <src>)",
        "Convert the full 64-bit register of a signed integer source to scalar double",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vcvtsi2sd(" + args[0] + ", " + args[0] + ", qwordReg(" + formatRegArg(args[1]) + "));");
    }}},
    {"x64:vcvtsi2ss!", {
        2, false,
        "(x64:vcvtsi2ss! <dest> <src>)",
        "Convert signed integer to scalar single-precision float",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vcvtsi2ss(" + args[0] + ", " + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:vcvtsi2ss-qword!", {
        2, false,
        "(x64:vcvtsi2ss-qword! <dest> <src>)",
        "Convert the full 64-bit register of a signed integer source to scalar float",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vcvtsi2ss(" + args[0] + ", " + args[0] + ", qwordReg(" + formatRegArg(args[1]) + "));");
    }}},
    {"x64:vcvttsd2si!", {
        2, false,
        "(x64:vcvttsd2si! <dest> <src>)",
        "Convert scalar double-precision float to signed integer (truncate)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vcvttsd2si(" + args[0] + ", lowering.memRegDoubleOp(" + args[1] + "));");
    }}},
    {"x64:vcvttsd2si-64!", {
        2, false,
        "(x64:vcvttsd2si-64! <dest> <src>)",
        "Convert scalar double to 64-bit signed integer (truncate)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vcvttsd2si(qwordReg(" + args[0] + "), lowering.memRegDoubleOp(" + args[1] + "));");
    }}},
    {"x64:vcvtsd2ss!", {
        2, false,
        "(x64:vcvtsd2ss! <dest> <src>)",
        "Convert scalar double-precision float to single-precision",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vcvtsd2ss(" + args[0] + ", " + args[0] + ", " + src + ");");
    }}},
    {"x64:vcvtss2sd!", {
        2, false,
        "(x64:vcvtss2sd! <dest> <src>)",
        "Convert scalar single-precision float to double-precision",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vcvtss2sd(" + args[0] + ", " + args[0] + ", " + src + ");");
    }}},
    {"x64:vucomisd!", {
        2, false,
        "(x64:vucomisd! <lhs> <rhs>)",
        "Unordered compare scalar double-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vucomisd(" + formatRegArg(args[0]) + ", lowering.memRegDoubleOp(" + args[1] + "));");
    }}},
    {"x64:vucomiss!", {
        2, false,
        "(x64:vucomiss! <lhs> <rhs>)",
        "Unordered compare scalar single-precision floats",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.vucomiss(" + formatRegArg(args[0]) + ", lowering.memRegFloatOp(" + args[1] + "));");
    }}},
};

// Operations defined as C++ templates (see TemplateOpSpec for the placeholder syntax)
static const TemplateOpTable kTemplateOpsX64 = {
    // VM stack / constant slots (index is a VM register or constant index, e.g. (vm-reg a))
    {"x64:luau-reg", {"luauReg({0:raw})", "xmmword operand for the TValue in VM register <0>", true}},
    {"x64:luau-reg-tag", {"luauRegTag({0:raw})", "dword operand for the tag of VM register <0>", true}},
    {"x64:luau-reg-value", {"luauRegValue({0:raw})", "qword operand for the value of VM register <0>", true}},
    {"x64:luau-reg-value-int", {"luauRegValueInt({0:raw})", "dword operand for the int value of VM register <0>", true}},
    {"x64:luau-reg-value-int64", {"luauRegValueInt64({0:raw})", "qword operand for the int64 value of VM register <0>", true}},
    {"x64:luau-reg-extra", {"luauRegExtra({0:raw})", "dword operand for the extra field of VM register <0>", true}},
    {"x64:luau-reg-value-vector", {"luauRegValueVector({0:raw}, {1:raw})", "dword operand for component <1> of the vector in VM register <0>", true}},
    {"x64:luau-reg-address", {"luauRegAddress({0:raw})", "Address operand of the TValue in VM register <0>", true}},
    {"x64:luau-node-key-tag", {"luauNodeKeyTag({0})", "dword operand for the key tag of LuaNode <0>", true}},
    {"x64:luau-node-key-value", {"luauNodeKeyValue({0})", "qword operand for the key value of LuaNode <0>", true}},
    {"x64:luau-constant", {"luauConstant({0:raw})", "xmmword operand for the TValue in constant <0>", true}},
    {"x64:luau-constant-tag", {"luauConstantTag({0:raw})", "dword operand for the tag of constant <0>", true}},
    {"x64:luau-constant-value", {"luauConstantValue({0:raw})", "qword operand for the value of constant <0>", true}},

    // IR operands as register-or-memory operands (constants become data section references)
    {"x64:mem-reg-double", {"lowering.memRegDoubleOp({0:raw})", "Register or memory operand holding double operand <0>", true}},
    {"x64:mem-reg-int64", {"lowering.memRegInt64Op({0:raw})", "Register, memory or immediate operand holding int64 operand <0>", true}},
    {"x64:mem-reg-uint", {"lowering.memRegUintOp({0:raw})", "Register, memory or immediate operand holding uint operand <0>", true}},
    {"x64:mem-reg-tag", {"lowering.memRegTagOp({0:raw})", "Register or memory operand holding the tag operand <0>", true}},
    {"x64:cond-int", {"getConditionInt({0})", "X64 condition for an IrCondition operand on int comparisons", true}},
    {"x64:mem-reg-float", {"lowering.memRegFloatOp({0:raw})", "Register or memory operand holding float operand <0>", true}},

    // Vector operands: vecOp returns the operand's register, or loads it into the given unallocated temp
    {"x64:unallocated-temp", {"ScopedRegX64{regs}", "Temporary register holder that helpers like x64:vec-op allocate on demand", true}},
    {"x64:vec-op", {"lowering.vecOp({0:raw}, {1:raw})", "Register holding vector operand <0>, using holder <1> if it must be loaded (may allocate)", true}},

    // Constants placed in the code's data section
    {"x64:f64", {"build.f64({0:double})", "Memory operand for a double constant (from a constant operand or literal)", true}},
    {"x64:f32", {"build.f32({0:raw})", "Memory operand for a float literal", true}},
    {"x64:f64x2", {"build.f64x2({0:raw}, {1:raw})", "Memory operand for two double literals", true}},
    {"x64:f32x4", {"build.f32x4({0:raw}, {1:raw}, {2:raw}, {3:raw})", "Memory operand for four float literals", true}},
    {"x64:u32x4", {"build.u32x4({0:raw}, {1:raw}, {2:raw}, {3:raw})", "Memory operand for four 32-bit integer literals", true}},
    {"x64:i32", {"build.i32({0:raw})", "Memory operand for a 32-bit integer literal", true}},
    {"x64:i64", {"build.i64({0:raw})", "Memory operand for a 64-bit integer literal", true}},

    // Fixed registers: claim a physical register for the rest of the block (holder from x64:unallocated-temp)
    {"x64:reg-of", {"{0:raw}.reg", "Register held by a ScopedRegX64 holder (from x64:unallocated-temp)", true}},
    {"x64:alloc!", {"{0:raw}.alloc(SizeX64::{1:raw});", "Allocate a register of size <1> (e.g. :qword) in holder <0>"}},
    {"x64:free!", {"{0:raw}.free();", "Release the register in holder <0> before the end of the block"}},
    {"x64:release", {"{0:raw}.release()", "Register in holder <0>, handed over (the holder no longer frees it)", true}},
    {"x64:take!", {"{0:raw}.take({1});", "Reserve physical register <1> in holder <0> (spilling its current value if needed)"}},

    // Sized memory operands from address expressions
    {"x64:ptr", {"{0:raw}[{1:raw}]", "Memory operand of size <0> (e.g. :byte, :dword) at address expression <1>", true}},
    {"x64:ptr2", {"{0:raw}[{1} + {2}]", "Memory operand of size <0> at <1> + <2>", true}},
    {"x64:ptr-scaled0", {"{0:raw}[{1} + {2} * {3:raw}]", "Memory operand of size <0> at <1> + <2> * <3>", true}},
    {"x64:ptr-scaled", {"{0:raw}[{1} + {2} * {3:raw} + {4:raw}]", "Memory operand of size <0> at <1> + <2> * <3> + <4>", true}},
    {"x64:buffer-addr", {"lowering.bufferAddrOp({0:raw}, {1:raw}, lowering.tagOp({2:raw}))", "Address of element <1> in buffer <0> (tag <2>)", true}},

    // Register views and immediates
    {"x64:byte-reg", {"byteReg({0})", "Low byte of register <0>", true}},
    {"x64:word-reg", {"wordReg({0})", "Low 16 bits of register <0>", true}},
    {"x64:dword-reg", {"dwordReg({0})", "Low 32 bits of register <0>", true}},
    {"x64:qword-reg", {"qwordReg({0})", "Full 64 bits of register <0>", true}},
    {"x64:imm-tag", {"OperandX64(lowering.tagOp({0:raw}))", "Immediate from tag constant <0>", true}},
    {"x64:cond", {"ConditionX64::{0:raw}", "X64 condition code (e.g. :NotParity)", true}},
    {"x64:imm", {"OperandX64({0:raw})", "Immediate operand <0>", true}},
    {"x64:imm8", {"OperandX64(int8_t({0:int}))", "8-bit immediate from int constant <0>", true}},
    {"x64:imm16", {"OperandX64(int16_t({0:int}))", "16-bit immediate from int constant <0>", true}},
    {"x64:imm32", {"OperandX64({0:int})", "32-bit immediate from int constant <0>", true}},

    // Runtime calls: spill guard and call wrapper are RAII helpers, so keep them in a (block ...)
    {"x64:scoped-spills", {"ScopedSpills(regs)", "Spill guard: live registers are spilled before the call and restored at the end of the block", true}},
    {"x64:call-wrapper", {"IrCallWrapperX64(regs, build)", "Call wrapper that moves arguments into ABI registers (may allocate)", true}},
    {"x64:call-wrapper-at", {"IrCallWrapperX64(regs, build, index)", "Call wrapper for the current instruction (frees operands at their last use)", true}},
    {"x64:call-arg!", {"{0:raw}.addArgument(SizeX64::{1:raw}, {2});", "Add argument <2> of size <1> (e.g. :qword) to call wrapper <0>"}},
    {"x64:call-arg-op!", {"{0:raw}.addArgument(SizeX64::{1:raw}, {2}, {3:raw});", "Add argument <2> of size <1> for IR operand <3> (freed at its last use) to call wrapper <0>"}},
    {"x64:call-arg-scoped!", {"{0:raw}.addArgument(SizeX64::{1:raw}, {2:raw});", "Hand the register in holder <2> to call wrapper <0> as an argument of size <1>"}},
    {"x64:call-result!", {"{0:raw}.setResultRegister({1}, index);", "Have call wrapper <0> return its result in <1>"}},
    {"x64:call!", {"{0:raw}.call({1});", "Call the function at <1> (e.g. a NativeContext slot) through wrapper <0>"}},
    {"x64:call-next-arg-reg", {"{0:raw}.suggestNextArgumentRegister(SizeX64::{1:raw})", "Register call wrapper <0> will use for its next argument of size <1>", true}},
    {"x64:addr", {"addr[{0} + {1:raw}]", "Address operand <0> + <1> (for lea and call arguments)", true}},
    {"x64:fallback!", {"emitFallback(regs, build, {0:raw}, {1:uint});", "Call the VM implementation at NativeContext offset <0> for the instruction at pc <1>"}},
    {"x64:luau-constant-address", {"luauConstantAddress({0:raw})", "Address operand of the TValue in constant <0>", true}},
    {"x64:number-tvalue-bytes", {"[&]() { TValue n = {}; setnvalue(&n, {0:uint}); return build.bytes(&n, sizeof(n)); }()",
        "Data-section copy of a number TValue holding uint constant <0>", true}},
    {"x64:call-arith-helper!", {"callArithHelper(regs, build, {0:raw}, {1}, {2}, TMS({3:int}));", "VM arithmetic: R<0> = <1> op <2> for metamethod <3>"}},
    {"x64:call-length-helper!", {"callLengthHelper(regs, build, {0:raw}, {1:raw});", "VM length: R<0> = #R<1>"}},
    {"x64:call-get-table!", {"callGetTable(regs, build, {0:raw}, {1}, {2:raw});", "VM table read: R<2> = R<0>[<1>]"}},
    {"x64:call-set-table!", {"callSetTable(regs, build, {0:raw}, {1}, {2:raw});", "VM table write: R<0>[<1>] = R<2>"}},
    {"x64:call-step-gc!", {"callStepGc(regs, build);", "Run a GC step if the GC threshold is reached"}},
    {"x64:preserve-and-free-inst-values!", {"regs.preserveAndFreeInstValues();", "Spill all instruction values so handler code can ignore the register state"}},
    {"x64:assert-all-free!", {"regs.assertAllFree();", "Check that no registers are allocated"}},
    {"x64:assert-no-spills!", {"regs.assertNoSpills();", "Check that no values are spilled"}},
    {"x64:inst-setlist!", {"emitInstSetList(regs, build, {0:raw}, {1:raw}, {2:int}, {3:uint}, {4:raw});", "SETLIST: R<0>[<3>...] = R<1>.. (<2> values; <4> = known count or -1)"}},
    {"x64:add-interrupt-handler!", {"{ Label resume = build.setLabel(); lowering.interruptHandlers.push_back({{0:raw}, {1:raw}, resume}); }",
        "Register an interrupt handler: label <0> jumps out for pc <1>, and the handler resumes here"}},
    {"x64:table-node-at-cached-slot!", {"getTableNodeAtCachedSlot(build, {0}, {1}, {2}, {3:uint});",
        "<1> = node of table <2> at the slot cached in instruction <3> (scratch <0>)"}},
    {"x64:convert-number-to-index-or-jump!", {"convertNumberToIndexOrJump(build, {0}, {1}, {2}, {3:raw});",
        "<2> = int index of number <1>, or jump to <3> if it isn't an exact integer (scratch <0>)"}},
    {"x64:check-object-barrier-conditions!", {"checkObjectBarrierConditions(build, {0}, {1}, {2}, {3:raw}, {4:raw}, {5:raw});",
        "Jump to <5> unless storing value <3> (register <2>, tag <4> or -1) into object <1> needs a GC barrier (scratch <0>)"}},
    {"x64:call-barrier-table-fast!", {"callBarrierTableFast(regs, build, {0}, {1:raw});", "Table back barrier for table <0> (IR operand <1>)"}},
    {"x64:call-barrier-object!", {"callBarrierObject(regs, build, {0:raw}, {}, {1}, {2:raw}, {3:raw});",
        "Object barrier for storing value <1> (IR operand <2>, tag <3> or -1) into object register <0>"}},
    {"x64:free-last-use-reg!", {"regs.freeLastUseReg(lowering.function.instOp({0:raw}), index);", "Release operand <0>'s register now if this is its last use"}},
    {"x64:ud2!", {"build.ud2();", "ud2 (trap)"}},
    {"x64:emit-builtin!", {"emitBuiltin(regs, build, {0:uint}, {1:raw}, {2:raw}, {3:int});", "Inline builtin <0> with R<1> = f(R<2>...) and <3> results"}},
    {"x64:inst-forgloop!", {"emitInstForGLoop(regs, build, {0:raw}, {1:int}, {2:raw});", "Generic for loop over R<0> with <1> variables, looping back to <2>"}},
    {"x64:inst-return!", {"emitInstReturn(build, lowering.helpers, {0:raw}, {1:int}, lowering.function.variadic);", "Return <1> values starting at R<0>"}},
    {"x64:inst-call!", {"emitInstCall(regs, build, lowering.helpers, {0:raw}, {1:int}, {2:int});", "Call R<0> with <1> arguments and <2> results"}},
    {"x64:class-member-auth!", {"emitClassMemberAuthX64(build, regs, {0}, {1}, {2}, {3:raw});",
        "Authorize private/const access to member slot <1> of class <0> (written object <2> or noreg), else jump to <3>"}},
    {"x64:update-base!", {"emitUpdateBase(build);", "Reload rBase from L->base after a call that may reallocate the stack"}},

    // Control flow
    {"x64:jcc!", {"build.jcc(ConditionX64::{0:raw}, {1:raw});", "Jump to local label <1> if condition <0> (e.g. :Equal) holds"}},
    {"x64:target-label", {"lowering.getTargetLabel({0:raw}, index, {1:raw})", "Label for branching to block/exit <0>, with fallback label <1>", true, true}},
    {"x64:jump-or-abort-no-finalize!", {"lowering.jumpOrAbortOnUndefNoFinalize(ConditionX64::{0:raw}, {1:raw}, index, next, {2:raw});",
        "Jump to block/exit <1> if condition <0> holds, sharing fallback label <2> (finalize once with x64:finalize-target-label!)"}},
    {"x64:finalize-target-label!", {"lowering.finalizeTargetLabel({0:raw}, index, {1:raw});", "Emit the exit/abort code for fallback label <1> of target <0>"}},
    {"x64:jump-or-abort-on-undef!", {"lowering.jumpOrAbortOnUndef(ConditionX64::{0:raw}, {1:raw}, index, next);",
        "Jump to block/exit <1> if condition <0> holds (aborts if <1> is undef)"}},

    // Instructions
    {"x64:mov!", {"build.mov({0}, {1});", "mov <0>, <1>"}},
    {"x64:add!", {"build.add({0}, {1});", "add <0>, <1>"}},
    {"x64:sub!", {"build.sub({0}, {1});", "sub <0>, <1>"}},
    {"x64:xor!", {"build.xor_({0}, {1});", "xor <0>, <1>"}},
    {"x64:test!", {"build.test({0}, {1});", "test <0>, <1>"}},
    {"x64:cmp-op!", {"build.cmp({0}, {1});", "cmp <0>, <1> (operands as given)"}},
    // Shifts and rotates by mnemonic (e.g. :shl, :shr, :sar, :rol, :ror)
    {"x64:shift-imm!", {"build.{0:raw}({1}, int8_t(unsigned({2:int})));", "<0> <1>, imm8 (byte-sized amount of int constant <2>)"}},
    {"x64:shift-imm64!", {"build.{0:raw}({1}, int8_t(unsigned({2:int64})));", "<0> <1>, imm8 (byte-sized amount of int64 constant <2>)"}},
    {"x64:shift-int8!", {"build.{0:raw}({1}, int8_t({2:raw}));", "<0> <1>, int8_t(<2>) for a compile-time amount <2>"}},
    {"x64:neg-amount-u8", {"uint8_t(-{0:int64})", "Negated int64 constant <0> truncated to 8 bits (shift amount)", true}},
    {"x64:shift-cl!", {"build.{0:raw}({1}, byteReg({2}));", "<0> <1>, cl (amount in the byte register of <2>, which must be rcx)"}},
    {"x64:bsr!", {"build.bsr({0}, {1});", "bsr <0>, <1>"}},
    {"x64:bsf!", {"build.bsf({0}, {1});", "bsf <0>, <1>"}},
    {"x64:jmp-label!", {"build.jmp({0:raw});", "jmp to local label <0>"}},
    {"x64:neg!", {"build.neg({0});", "neg <0>"}},
    {"x64:lea-addr!", {"build.lea({0}, addr[{1} + {2:raw}]);", "lea <0>, [<1> + <2>]"}},
    {"x64:jump-on-number-cmp!", {"jumpOnNumberCmp(build, {0}, {1}, {2}, {3}, {4:raw}, {5:raw});",
        "Compare numbers <1> and <2> (scratch <0>) and jump to <4> if condition <3> holds; <5> selects float precision"}},
    {"x64:cmov-cond!", {"build.cmov(ConditionX64::{0:raw}, {1}, {2});", "cmov<0> <1>, <2>"}},
    {"x64:and!", {"build.and_({0}, {1});", "and <0>, <1>"}},
    {"x64:or!", {"build.or_({0}, {1});", "or <0>, <1>"}},
    {"x64:setcc!", {"build.setcc({0:raw}, {1});", "setcc <cond 0>, <1>"}},
    {"x64:jump-if-truthy!", {"jumpIfTruthy(build, {0:raw}, {1:raw}, {2:raw});", "Jump to <1> if VM register <0> is truthy, to <2> if falsy"}},
    {"x64:jump-if-falsy!", {"jumpIfFalsy(build, {0:raw}, {1:raw}, {2:raw});", "Jump to <1> if VM register <0> is falsy, to <2> if truthy"}},
    {"x64:vpextrd!", {"build.vpextrd({0}, {1}, {2:raw});", "vpextrd <0>, <1>, <lane>"}},
    {"x64:movsx!", {"build.movsx({0}, {1});", "movsx <0>, <1>"}},
    {"x64:movzx!", {"build.movzx({0}, {1});", "movzx <0>, <1>"}},
    {"x64:vcvtsi2sd-op!", {"build.vcvtsi2sd({0}, {1}, {2});", "vcvtsi2sd <0>, <1>, <2>"}},
    {"x64:vucomisd-op!", {"build.vucomisd({0}, {1});", "vucomisd <0>, <1>"}},
    {"x64:shift-raw!", {"build.{0:raw}({1}, {2:raw});", "<0> <1>, <2> (amount as given)"}},
    {"x64:lea-addr3!", {"build.lea({0}, addr[{1} + {2} + {3:raw}]);", "lea <0>, [<1> + <2> + <3>]"}},
    {"x64:dec!", {"build.dec({0});", "dec <0>"}},
    {"x64:cqo!", {"build.cqo();", "cqo (sign-extend rax into rdx:rax)"}},
    {"x64:idiv!", {"build.idiv({0});", "idiv <0> (signed rdx:rax / <0>)"}},
    {"x64:div!", {"build.div({0});", "div <0> (unsigned rdx:rax / <0>)"}},
    {"x64:mov-imm32!", {"build.mov({0}, int32_t({1:int64}));", "mov <0>, imm32 (sign-extended) from an int64 constant that fits in 32 bits"}},
    {"x64:mov64!", {"build.mov64({0}, {1:int64});", "mov <0>, imm64 from an int64 constant"}},
    // AVX arithmetic and data movement with operands as given (the "-op" variants avoid older ops of the same name)
    {"x64:vmovsd3!", {"build.vmovsd({0}, {1}, {2});", "vmovsd <0>, <1>, <2> (merge low double)"}},
    {"x64:vmovss3!", {"build.vmovss({0}, {1}, {2});", "vmovss <0>, <1>, <2> (merge low float)"}},
    {"x64:vmovupd!", {"build.vmovupd({0}, {1});", "vmovupd <0>, <1>"}},
    {"x64:vaddsd-op!", {"build.vaddsd({0}, {1}, {2});", "vaddsd <0>, <1>, <2>"}},
    {"x64:vsubsd-op!", {"build.vsubsd({0}, {1}, {2});", "vsubsd <0>, <1>, <2>"}},
    {"x64:vmulsd-op!", {"build.vmulsd({0}, {1}, {2});", "vmulsd <0>, <1>, <2>"}},
    {"x64:vdivsd-op!", {"build.vdivsd({0}, {1}, {2});", "vdivsd <0>, <1>, <2>"}},
    {"x64:vandpd!", {"build.vandpd({0}, {1}, {2});", "vandpd <0>, <1>, <2>"}},
    {"x64:vandps!", {"build.vandps({0}, {1}, {2});", "vandps <0>, <1>, <2>"}},
    {"x64:vorpd!", {"build.vorpd({0}, {1}, {2});", "vorpd <0>, <1>, <2>"}},
    {"x64:vxorpd!", {"build.vxorpd({0}, {1}, {2});", "vxorpd <0>, <1>, <2>"}},
    {"x64:vxorps!", {"build.vxorps({0}, {1}, {2});", "vxorps <0>, <1>, <2>"}},
    {"x64:vcmpltsd!", {"build.vcmpltsd({0}, {1}, {2});", "vcmpltsd <0>, <1>, <2>"}},
    {"x64:vcmpltss!", {"build.vcmpltss({0}, {1}, {2});", "vcmpltss <0>, <1>, <2>"}},
    {"x64:vcmpeqsd!", {"build.vcmpeqsd({0}, {1}, {2});", "vcmpeqsd <0>, <1>, <2>"}},
    {"x64:vblendvpd!", {"build.vblendvpd({0}, {1}, {2}, {3});", "vblendvpd <0>, <1>, <2>, <3> (mask)"}},
    {"x64:vblendvps!", {"build.vblendvps({0}, {1}, {2}, {3});", "vblendvps <0>, <1>, <2>, <3> (mask)"}},
    {"x64:vroundsd!", {"build.vroundsd({0}, {1}, {2}, {3:raw});", "vroundsd <0>, <1>, <2>, <rounding mode>"}},
    {"x64:vfmadd213pd!", {"build.vfmadd213pd({0}, {1}, {2});", "vfmadd213pd <0>, <1>, <2> (<0> = <1> * <0> + <2>)"}},
    {"x64:vaddps!", {"build.vaddps({0}, {1}, {2});", "vaddps <0>, <1>, <2>"}},
    {"x64:vsubps!", {"build.vsubps({0}, {1}, {2});", "vsubps <0>, <1>, <2>"}},
    {"x64:vmulps!", {"build.vmulps({0}, {1}, {2});", "vmulps <0>, <1>, <2>"}},
    {"x64:vdivps!", {"build.vdivps({0}, {1}, {2});", "vdivps <0>, <1>, <2>"}},
    {"x64:vminps!", {"build.vminps({0}, {1}, {2});", "vminps <0>, <1>, <2>"}},
    {"x64:vmaxps!", {"build.vmaxps({0}, {1}, {2});", "vmaxps <0>, <1>, <2>"}},
    {"x64:vcmpeqps!", {"build.vcmpeqps({0}, {1}, {2});", "vcmpeqps <0>, <1>, <2>"}},
    {"x64:vroundps!", {"build.vroundps({0}, {1}, {2:raw});", "vroundps <0>, <1>, <rounding mode>"}},
    {"x64:vdpps!", {"build.vdpps({0}, {1}, {2}, {3:raw});", "vdpps <0>, <1>, <2>, <mask>"}},
    {"x64:vpshufps!", {"build.vpshufps({0}, {1}, {2}, {3:int});", "vpshufps <0>, <1>, <2>, <shuffle>"}},
    {"x64:vpinsrd!", {"build.vpinsrd({0}, {1}, {2}, {3:raw});", "vpinsrd <0>, <1>, <2>, <lane>"}},
    {"x64:vmovaps!", {"build.vmovaps({0}, {1});", "vmovaps <0>, <1>"}},
    {"x64:vfmadd213ps!", {"build.vfmadd213ps({0}, {1}, {2});", "vfmadd213ps <0>, <1>, <2> (<0> = <1> * <0> + <2>)"}},
    {"x64:store-float!", {"lowering.storeFloat({0}, {1:raw});", "Store float operand <1> (constant or register) to memory <0>"}},
    {"x64:vmovss-op!", {"build.vmovss({0}, {1});", "vmovss <0>, <1> (operands as given)"}},
    {"x64:vmovsd-op!", {"build.vmovsd({0}, {1});", "vmovsd <0>, <1> (operands as given)"}},
    {"x64:vmovups-op!", {"build.vmovups({0}, {1});", "vmovups <0>, <1> (operands as given)"}},
};

class TargetBuilderX64 : public TargetBuilder
{
public:
    std::string archName() const override { return "X64"; }
    std::string loweringClass() const override { return "IrLoweringX64"; }
    std::string destRegVar() const override { return "inst.regX64"; }

    void emitPreamble(CodeWriter& out, const std::vector<std::string>& fflags, const std::vector<std::string>& dfflags) override
    {
        out.raw("// Auto-generated by luwu-dsl-compiler from rules.luna. DO NOT EDIT!\n"
                "#pragma once\n\n"
                "#include \"IrLoweringX64.h\"\n"
                "#include \"EmitCommonX64.h\"\n"
                "#include \"EmitInstructionX64.h\"\n"
                "#include \"lstate.h\"\n"
                "#include \"lgc.h\"\n\n");
        for (const auto& flag : fflags)
            out.line("LUAU_FASTFLAG(" + flag + ")");
        for (const auto& flag : dfflags)
            out.line("LUAU_DYNAMIC_FASTFLAG(" + flag + ")");
        if (!fflags.empty() || !dfflags.empty())
            out.line();
        out.raw("namespace Luau\n"
                "{\n"
                "namespace CodeGen\n"
                "{\n"
                "namespace X64\n"
                "{\n\n");
    }

    void emitPostamble(CodeWriter& out) override
    {
        out.raw("} // namespace X64\n"
                "} // namespace CodeGen\n"
                "} // namespace Luau\n");
    }

    void emitAllocProduce(CodeWriter& out, EvalContext& ctx, TypeKind type, bool isReuse, const std::vector<std::string>& rawOps) override
    {
        ctx.use("regs");
        ctx.use("index");
        ctx.use("inst");
        std::string sizeStr = sizeForTypeX64(type);
        if (isReuse)
        {
            std::string ops;
            for (size_t i = 0; i < rawOps.size(); ++i)
                ops += (i > 0 ? ", " : "") + rawOps[i];
            out.line("inst.regX64 = regs.allocRegOrReuse(" + sizeStr + ", index, {" + ops + "});");
        }
        else
            out.line("inst.regX64 = regs.allocReg(" + sizeStr + ", index);");
    }

    Operand emitAllocTemp(CodeWriter& out, EvalContext& ctx, TypeKind type, const std::string& varName) override
    {
        ctx.use("regs");
        std::string sizeStr = sizeForTypeX64(type);
        out.line("ScopedRegX64 " + varName + "{regs, " + sizeStr + "};");
        return Operand::makeReg(type, varName + ".reg");
    }

    std::string resolveMemBase(CodeWriter& out, EvalContext& ctx, const SExpr& baseExpr, const Operand& baseOp, const std::string& destReg = "") override
    {
        (void)out;
        (void)destReg;
        if (baseExpr.isSymbol() && baseExpr.text == "rClosure")
            return "rClosure";
        if (baseOp.text == "rClosure")
            return "rClosure";

        if (baseOp.kind == OperandKind::Mem)
            throw CompileError(ctx.loc, "Cannot use memory operand as memory base in X64; load it into a register first");

        return baseOp.text;
    }

    void emitTakeResult(CodeWriter& out, EvalContext& ctx, const std::string& physReg) override
    {
        ctx.use("regs");
        out.line("inst.regX64 = regs.takeReg(" + physReg + ", index);");
    }

    std::string regVarType() const override { return "RegisterX64"; }

    std::string conditionalTempExpr(CodeWriter& out, EvalContext& ctx, TypeKind type, const std::string& cond, const std::string& varName) override
    {
        // ScopedRegX64 owns the register until the end of the enclosing block, so declare it unallocated and allocate on demand
        ctx.use("regs");
        std::string holder = varName + "_tmp";
        out.line("ScopedRegX64 " + holder + "{regs};");
        out.line("if (" + cond + ")");
        out.line("    " + holder + ".alloc(" + sizeForTypeX64(type) + ");");
        return holder + ".reg";
    }

    std::string formatMemOperand(EvalContext& ctx, const Operand& op, const std::string& size) const override
    {
        return formatMemX64(ctx, op, size);
    }

    const TemplateOpSpec* findTemplateOp(const std::string& name) const override
    {
        auto it = kTemplateOpsX64.find(name);
        return it == kTemplateOpsX64.end() ? nullptr : &it->second;
    }

    void getTemplateOpDocs(std::vector<FormDoc>& ops, std::vector<FormDoc>& values) const override
    {
        getTemplateDocs(kTemplateOpsX64, ops, values);
    }

    std::string formatRegOperand(const Operand& op) const override
    {
        return formatRegArg(op.text);
    }

    std::set<std::string> globalSymbols() const override
    {
        // rClosure is not a register on X64; loads through it are lowered via sClosure
        std::set<std::string> globals = {"rState", "rBase", "rConstants", "rNativeContext", "rClosure", "sClosure", "sCode", "sTemporarySlot",
            "sSpillArea", "noreg", "rip"};
        // Physical registers, for fixed-register constraints (shifts, division) and ABI glue
        for (const char* name : {"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil", "eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi",
                 "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi"})
            globals.insert(name);
        for (int i = 8; i < 16; ++i)
        {
            for (const char* suffix : {"", "b", "d"})
                globals.insert("r" + std::to_string(i) + suffix);
        }
        for (int i = 0; i < 16; ++i)
            globals.insert("xmm" + std::to_string(i));
        return globals;
    }

    bool emitArchOp(CodeWriter& out, EvalContext& ctx, const std::string& opName, const std::vector<Operand>& rawArgs) override
    {
        auto it = kArchOpsX64.find(opName);
        if (it == kArchOpsX64.end())
            return false;

        const ArchOpSpecX64& spec = it->second;
        if (rawArgs.size() != spec.arity)
        {
            throw CompileError(
                ctx.loc,
                opName + " requires " + std::to_string(spec.arity) + " argument(s), got " + std::to_string(rawArgs.size())
            );
        }

        std::vector<std::string> args;
        args.reserve(rawArgs.size());
        for (const auto& a : rawArgs)
        {
            if (!spec.allowMemArgs && a.kind == OperandKind::Mem)
                throw CompileError(ctx.loc, "Cannot implicitly convert memory operand to register in X64; use an explicit load subrule");
            args.push_back(a.text);
        }

        spec.emit(out, ctx, rawArgs, args);
        return true;
    }

    void getArchOpDocs(std::vector<FormDoc>& out) const override
    {
        for (const auto& [name, spec] : kArchOpsX64)
        {
            out.push_back({std::string(name), spec.syntax ? spec.syntax : "", spec.desc ? spec.desc : ""});
        }
    }
};

void emitX64(std::ostream& os, const std::vector<Rule>& rules)
{
    TargetBuilderX64 target;
    compileRules(os, target, rules);
}

void getX64Docs(std::vector<FormDoc>& ops, std::vector<FormDoc>& preds)
{
    TargetBuilderX64 target;
    target.getArchOpDocs(ops);
    target.getArchPredicateDocs(preds);
    target.getTemplateOpDocs(ops, preds);
}

} // namespace Luwu::Dsl
