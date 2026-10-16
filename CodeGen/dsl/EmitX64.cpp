// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "RuleCompiler.h"

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
    size_t arity;
    bool allowMemArgs;
    std::function<void(CodeWriter&, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>&)> emit;
};

static void emitGuardBranchX64(CodeWriter& out, EvalContext& ctx, const std::string& condStr, const std::string& labelOp)
{
    ctx.use("build");
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
    {"x64:movzx-mem-u8!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:movzx-mem-u8!");
        out.line("build.movzx(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "byte") + ");");
    }}},
    {"x64:movzx-mem-u16!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:movzx-mem-u16!");
        out.line("build.movzx(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "word") + ");");
    }}},
    {"x64:movsx-mem-i8!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:movsx-mem-i8!");
        out.line("build.movsx(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "byte") + ");");
    }}},
    {"x64:movsx-mem-i16!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:movsx-mem-i16!");
        out.line("build.movsx(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "word") + ");");
    }}},
    {"x64:mov-mem-u32!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:mov-mem-u32!");
        out.line("build.mov(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "dword") + ");");
    }}},
    {"x64:mov-mem-ptr!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
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
    {"x64:mov-mem-64!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:mov-mem-64!");
        out.line("build.mov(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "qword") + ");");
    }}},
    {"x64:vmovss-mem!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:vmovss-mem!");
        out.line("build.vmovss(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "dword") + ");");
    }}},
    {"x64:vmovsd-mem!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:vmovsd-mem!");
        out.line("build.vmovsd(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "qword") + ");");
    }}},
    {"x64:vmovups-mem!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[1], "x64:vmovups-mem!");
        out.line("build.vmovups(" + formatRegArg(raw[0].text) + ", " + formatMemX64(ctx, raw[1], "xmmword") + ");");
    }}},
    {"x64:mov-to-mem-u8!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-u8!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "byte") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:mov-to-mem-u16!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-u16!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "word") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:mov-to-mem-u32!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-u32!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "dword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:mov-to-mem-ptr!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-ptr!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "qword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:mov-to-mem-64!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[0], "x64:mov-to-mem-64!");
        out.line("build.mov(" + formatMemX64(ctx, raw[0], "qword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:vmovss-to-mem!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[0], "x64:vmovss-to-mem!");
        out.line("build.vmovss(" + formatMemX64(ctx, raw[0], "dword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:vmovsd-to-mem!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[0], "x64:vmovsd-to-mem!");
        out.line("build.vmovsd(" + formatMemX64(ctx, raw[0], "qword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:vmovups-to-mem!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgX64(ctx, raw[0], "x64:vmovups-to-mem!");
        out.line("build.vmovups(" + formatMemX64(ctx, raw[0], "xmmword") + ", " + formatRegArg(raw[1].text) + ");");
    }}},
    {"x64:lea-reg-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + " + formatRegArg(args[2]) + "]);");
    }}},
    {"x64:lea-reg-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + " + formatRegArg(args[2]) + "]);");
    }}},
    {"x64:lea-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + lowering.intOp(" + args[2] + ")]);");
    }}},
    {"x64:lea-imm-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + int32_t(lowering.int64Op(" + args[2] + "))]);");
    }}},
    {"x64:lea-sub-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " - lowering.intOp(" + args[2] + ")]);");
    }}},
    {"x64:lea-sub-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " - int32_t(lowering.int64Op(" + args[2] + "))]);");
    }}},
    {"x64:lea-offset!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lea(" + args[0] + ", addr[" + formatRegArg(args[1]) + " + int32_t(" + args[2] + ")]);");
    }}},
    {"x64:code-addr!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + formatRegArg(args[0]) + ", sCode);");
        out.line("build.add(" + formatRegArg(args[0]) + ", (" + formatRegArg(args[1]) + ") * sizeof(Instruction));");
    }}},

    // ========================================================================
    // B. Moves, Immediates & Lowering Accessors
    // ========================================================================
    {"x64:mov-reg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:mov-closure!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", sClosure);");
    }}},
    {"x64:mov-code!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", sCode);");
    }}},
    {"x64:mov-imm-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"x64:mov-imm-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov64(" + args[0] + ", lowering.int64Op(" + args[1] + "));");
    }}},
    {"x64:mov-imm-raw!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:mov-mem-reg-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", lowering.memRegUintOp(" + args[1] + "));");
    }}},
    {"x64:mov-mem-reg-i32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:mov-mem-reg-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:mov-mem-reg-tag!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", lowering.memRegTagOp(" + args[1] + "));");
    }}},
    {"x64:xor-zero!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.xor_(" + args[0] + ", " + args[0] + ");");
    }}},

    // ========================================================================
    // C. Integer Arithmetic & Sign/Zero Extensions
    // ========================================================================
    {"x64:add-reg-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:add-reg-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:add-imm-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"x64:add-imm-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", int32_t(lowering.int64Op(" + args[1] + ")));");
    }}},
    {"x64:add-offset!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", int32_t(" + args[1] + "));");
    }}},
    {"x64:add-mem-reg-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:add-mem-reg-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:sub-reg-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:sub-reg-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:sub-imm-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"x64:sub-imm-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", int32_t(lowering.int64Op(" + args[1] + ")));");
    }}},
    {"x64:sub-mem-reg-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:sub-mem-reg-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:neg-32!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.neg(" + args[0] + ");");
    }}},
    {"x64:neg-64!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.neg(" + args[0] + ");");
    }}},
    {"x64:inc!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.inc(" + args[0] + ");");
    }}},
    {"x64:dec!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.dec(" + args[0] + ");");
    }}},
    {"x64:imul-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.imul(" + args[0] + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:imul-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.imul(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:imul-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.imul(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.intOp(" + args[2] + "));");
    }}},
    {"x64:movsx-byte!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.movsx(" + args[0] + ", byteReg(" + formatRegArg(args[1]) + "));");
    }}},
    {"x64:movsx-word!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.movsx(" + args[0] + ", wordReg(" + formatRegArg(args[1]) + "));");
    }}},
    {"x64:movsxd!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.movsxd(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:mov-dword-reg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(dwordReg(" + args[0] + "), " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:mov-from-dword-reg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", dwordReg(" + formatRegArg(args[1]) + "));");
    }}},

    // ========================================================================
    // D. Bitwise, Shifts & Rotates
    // ========================================================================
    {"x64:bswap-32!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.bswap(" + args[0] + ");");
    }}},
    {"x64:bswap-64!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.bswap(" + args[0] + ");");
    }}},
    {"x64:not-32!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.not_(" + args[0] + ");");
    }}},
    {"x64:not-64!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.not_(" + args[0] + ");");
    }}},
    {"x64:and-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.and_(" + args[0] + ", lowering.memRegUintOp(" + args[1] + "));");
    }}},
    {"x64:and-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.and_(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:and-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.and_(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:or-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.or_(" + args[0] + ", lowering.memRegUintOp(" + args[1] + "));");
    }}},
    {"x64:or-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.or_(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:or-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.or_(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:xor-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.xor_(" + args[0] + ", lowering.memRegUintOp(" + args[1] + "));");
    }}},
    {"x64:xor-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.xor_(" + args[0] + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:xor-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.xor_(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:shl-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.shl(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:shl-cl!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.shl(" + args[0] + ", cl);");
    }}},
    {"x64:shr-imm-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.shr(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:shr-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.shr(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:shr-cl!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.shr(" + args[0] + ", cl);");
    }}},
    {"x64:sar-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sar(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:sar-cl!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sar(" + args[0] + ", cl);");
    }}},
    {"x64:rol-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.rol(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:rol-cl!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.rol(" + args[0] + ", cl);");
    }}},
    {"x64:ror-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ror(" + args[0] + ", " + args[1] + ");");
    }}},
    {"x64:ror-cl!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ror(" + args[0] + ", cl);");
    }}},
    {"x64:bsr!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.bsr(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:bsf!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.bsf(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},

    // ========================================================================
    // E. Comparisons, Conditionals, Guard Branches & Block Jumps
    // ========================================================================
    {"x64:cmp!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:test-reg!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        std::string r = formatRegArg(args[0]);
        out.line("build.test(" + r + ", " + r + ");");
    }}},
    {"x64:cmp-imm-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArg(args[0]) + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"x64:cmp-mem-reg-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArg(args[0]) + ", lowering.memRegIntOp(" + args[1] + "));");
    }}},
    {"x64:cmp-mem-reg-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArg(args[0]) + ", lowering.memRegInt64Op(" + args[1] + "));");
    }}},
    {"x64:setcc-eq!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.setcc(ConditionX64::Equal, byteReg(" + args[0] + "));");
    }}},
    {"x64:setcc-ne!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.setcc(ConditionX64::NotEqual, byteReg(" + args[0] + "));");
    }}},
    {"x64:setcc-cond-int!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.setcc(getConditionInt(" + args[1] + "), byteReg(" + args[0] + "));");
    }}},
    {"x64:setcc-inv-cond-int!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.setcc(getInverseCondition(getConditionInt(" + args[1] + ")), byteReg(" + args[0] + "));");
    }}},
    {"x64:cmov!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmov(" + args[0] + ", " + args[1] + ", " + formatRegArg(args[2]) + ");");
    }}},
    {"x64:cmov-cond-int-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmov(getConditionInt(" + args[1] + "), " + args[0] + ", lowering.memRegInt64Op(" + args[2] + "));");
    }}},

    // Guard Branches (abort-on-undef / VM-exit aware via jumpOrAbortOnUndef)
    {"x64:guard-je!", {1, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchX64(out, ctx, "ConditionX64::Equal", args[0]);
    }}},
    {"x64:guard-jne!", {1, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[0]);
    }}},
    {"x64:guard-z!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", 0);");
        emitGuardBranchX64(out, ctx, "ConditionX64::Equal", args[1]);
    }}},
    {"x64:guard-nz!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", 0);");
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[1]);
    }}},
    {"x64:guard-eq!", {3, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::Equal", args[2]);
    }}},
    {"x64:guard-ne!", {3, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[2]);
    }}},
    {"x64:branch-if-not-equal!", {3, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[2]);
    }}},
    {"x64:branch-if-tag-not-equal!", {3, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(lowering.memRegTagOp(" + args[0] + "), " + args[1] + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::NotEqual", args[2]);
    }}},
    {"x64:guard-ule!", {3, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::BelowEqual", args[2]);
    }}},
    {"x64:guard-ult!", {3, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatMemX64(ctx, raw[0]) + ", " + formatRegArg(args[1]) + ");");
        emitGuardBranchX64(out, ctx, "ConditionX64::Below", args[2]);
    }}},
    {"x64:guard-neg-cond-int!", {2, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchX64(out, ctx, "getConditionInt(getNegatedCondition(" + args[0] + "))", args[1]);
    }}},
    {"x64:guard-neg-cond-int64!", {2, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchX64(out, ctx, "getConditionInt(getNegatedCondition(" + args[0] + "))", args[1]);
    }}},

    // Block Jumps (jump directly to lowering.labelOp(target))
    {"x64:jump-or-abort!", {1, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("index");
        ctx.use("next");
        out.line("lowering.jumpOrAbortOnUndef(" + args[0] + ", index, next);");
    }}},
    {"x64:jcc-eq!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.jcc(ConditionX64::Equal, lowering.labelOp(" + args[0] + "));");
    }}},
    {"x64:jcc-ne!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.jcc(ConditionX64::NotEqual, lowering.labelOp(" + args[0] + "));");
    }}},
    {"x64:jcc-cond-int!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.jcc(getConditionInt(" + args[0] + "), lowering.labelOp(" + args[1] + "));");
    }}},
    {"x64:jump-z!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        std::string r = formatRegArg(args[0]);
        out.line("build.test(" + r + ", " + r + ");");
        out.line("build.jcc(ConditionX64::Equal, lowering.labelOp(" + args[1] + "));");
    }}},
    {"x64:jump-nz!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        std::string r = formatRegArg(args[0]);
        out.line("build.test(" + r + ", " + r + ");");
        out.line("build.jcc(ConditionX64::NotEqual, lowering.labelOp(" + args[1] + "));");
    }}},
    {"x64:jump-eq!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArg(args[0]) + ", " + formatRegArg(args[1]) + ");");
        out.line("build.jcc(ConditionX64::Equal, lowering.labelOp(" + args[2] + "));");
    }}},
    {"x64:jump-ne!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArg(args[0]) + ", " + formatRegArg(args[1]) + ");");
        out.line("build.jcc(ConditionX64::NotEqual, lowering.labelOp(" + args[2] + "));");
    }}},
    {"x64:jump-neg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        std::string r = formatRegArg(args[0]);
        out.line("build.test(" + r + ", " + r + ");");
        out.line("build.jcc(ConditionX64::Less, lowering.labelOp(" + args[1] + "));");
    }}},
    {"x64:jump-cond-int!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArg(args[0]) + ", " + formatRegArg(args[1]) + ");");
        out.line("build.jcc(getConditionInt(" + args[2] + "), lowering.labelOp(" + args[3] + "));");
    }}},
    {"x64:jmp!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.jmp(lowering.labelOp(" + args[0] + "));");
    }}},

    // ========================================================================
    // F. Floating-Point & Vector Primitives
    // ========================================================================
    {"x64:vmovsd!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vmovsd(" + args[0] + ", lowering.memRegDoubleOp(" + args[1] + "));");
    }}},
    {"x64:vmovss!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vmovss(" + args[0] + ", lowering.memRegFloatOp(" + args[1] + "));");
    }}},
    {"x64:vmovups!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vmovups(" + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:vaddsd!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vaddsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vsubsd!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vsubsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vmulsd!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vmulsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vdivsd!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vdivsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vminsd!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vminsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vmaxsd!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vmaxsd(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegDoubleOp(" + args[2] + "));");
    }}},
    {"x64:vsqrtsd!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vsqrtsd(" + args[0] + ", " + args[0] + ", " + src + ");");
    }}},
    {"x64:vroundsd-floor!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vroundsd(" + args[0] + ", " + args[0] + ", " + src + ", RoundingModeX64::RoundToNegativeInfinity);");
    }}},
    {"x64:vroundsd-ceil!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vroundsd(" + args[0] + ", " + args[0] + ", " + src + ", RoundingModeX64::RoundToPositiveInfinity);");
    }}},
    {"x64:vaddss!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vaddss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vsubss!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vsubss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vmulss!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vmulss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vdivss!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vdivss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vminss!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vminss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vmaxss!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vmaxss(" + args[0] + ", " + formatRegArg(args[1]) + ", lowering.memRegFloatOp(" + args[2] + "));");
    }}},
    {"x64:vsqrtss!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegFloatOp(" + args[1] + ")");
        out.line("build.vsqrtss(" + args[0] + ", " + args[0] + ", " + src + ");");
    }}},
    {"x64:vroundss-floor!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegFloatOp(" + args[1] + ")");
        out.line("build.vroundss(" + args[0] + ", " + args[0] + ", " + src + ", RoundingModeX64::RoundToNegativeInfinity);");
    }}},
    {"x64:vroundss-ceil!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegFloatOp(" + args[1] + ")");
        out.line("build.vroundss(" + args[0] + ", " + args[0] + ", " + src + ", RoundingModeX64::RoundToPositiveInfinity);");
    }}},
    {"x64:vcvtsi2sd!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vcvtsi2sd(" + args[0] + ", " + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:vcvtsi2sd-u32!", {2, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("function");
        ctx.use("regs");
        ctx.use("build");
        out.line("if (IrCmd source = function.instOp(" + args[1] + ").cmd; source == IrCmd::NUM_TO_UINT)");
        out.line("{");
        out.line("    ScopedRegX64 tmp{regs, SizeX64::dword};");
        out.line("    build.mov(tmp.reg, lowering.regOp(" + args[1] + "));");
        out.line("    build.vcvtsi2sd(" + args[0] + ", " + args[0] + ", qwordReg(tmp.reg));");
        out.line("}");
        out.line("else");
        out.line("{");
        out.line("    CODEGEN_ASSERT(source != IrCmd::SUBSTITUTE);");
        out.line("    build.vcvtsi2sd(" + args[0] + ", " + args[0] + ", qwordReg(lowering.regOp(" + args[1] + ")));");
        out.line("}");
    }}},
    {"x64:vcvtsi2ss!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vcvtsi2ss(" + args[0] + ", " + args[0] + ", " + formatRegArg(args[1]) + ");");
    }}},
    {"x64:vcvtsi2ss-u32!", {2, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("function");
        ctx.use("regs");
        ctx.use("build");
        out.line("if (IrCmd source = function.instOp(" + args[1] + ").cmd; source == IrCmd::NUM_TO_UINT)");
        out.line("{");
        out.line("    ScopedRegX64 tmp{regs, SizeX64::dword};");
        out.line("    build.mov(tmp.reg, lowering.regOp(" + args[1] + "));");
        out.line("    build.vcvtsi2ss(" + args[0] + ", " + args[0] + ", qwordReg(tmp.reg));");
        out.line("}");
        out.line("else");
        out.line("{");
        out.line("    CODEGEN_ASSERT(source != IrCmd::SUBSTITUTE);");
        out.line("    build.vcvtsi2ss(" + args[0] + ", " + args[0] + ", qwordReg(lowering.regOp(" + args[1] + ")));");
        out.line("}");
    }}},
    {"x64:vcvttsd2si!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vcvttsd2si(" + args[0] + ", lowering.memRegDoubleOp(" + args[1] + "));");
    }}},
    {"x64:vcvttsd2si-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vcvttsd2si(qwordReg(" + args[0] + "), lowering.memRegDoubleOp(" + args[1] + "));");
    }}},
    {"x64:vcvtsd2ss!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vcvtsd2ss(" + args[0] + ", " + args[0] + ", " + src + ");");
    }}},
    {"x64:vcvtss2sd!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string src = raw[1].rawOp.empty() ? formatRegArg(args[1]) : ("lowering.memRegDoubleOp(" + args[1] + ")");
        out.line("build.vcvtss2sd(" + args[0] + ", " + args[0] + ", " + src + ");");
    }}},
    {"x64:vucomisd!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vucomisd(" + formatRegArg(args[0]) + ", lowering.memRegDoubleOp(" + args[1] + "));");
    }}},
    {"x64:vucomiss!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.vucomiss(" + formatRegArg(args[0]) + ", lowering.memRegFloatOp(" + args[1] + "));");
    }}},
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
                "#include \"lstate.h\"\n\n");
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
            out.line("inst.regX64 = regs.allocRegOrReuse(" + sizeStr + ", index, {" + rawOps[0] + "});");
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

        ctx.use("build");
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
};

void emitX64(std::ostream& os, const std::vector<Rule>& rules)
{
    TargetBuilderX64 target;
    compileRules(os, target, rules);
}

} // namespace Luwu::Dsl
