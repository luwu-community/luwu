// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "RuleCompiler.h"
#include "OpTemplates.h"

#include <functional>
#include <string_view>
#include <unordered_map>

namespace Luwu::Dsl
{

static std::string formatRegArgA64(const std::string& arg)
{
    if (arg.rfind("OP_", 0) == 0)
        return "lowering.regOp(" + arg + ")";
    return arg;
}

static std::string kindForTypeA64(TypeKind type)
{
    switch (type)
    {
    case TypeKind::U8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::U32:
    case TypeKind::Tag:
        return "KindA64::w";
    case TypeKind::F32:
        return "KindA64::s";
    case TypeKind::F64:
        return "KindA64::d";
    case TypeKind::Vec:
        return "KindA64::q";
    default:
        return "KindA64::x";
    }
}

struct ArchOpSpecA64
{
    size_t arity = 0;
    bool allowMemArgs = false;
    const char* syntax = nullptr;
    const char* desc = nullptr;
    std::function<void(CodeWriter&, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>&)> emit;
};

struct ArchPredicateSpecA64
{
    const char* syntax;
    const char* desc;
    PredicateResult (*eval)(EvalContext& ctx, const SExpr& expr);
};

static const std::unordered_map<std::string_view, ArchPredicateSpecA64> kArchPredicatesA64 = {
    {"a64:fits-imm-32?", {
        "(a64:fits-imm-32? <op>)",
        "Test if 32-bit immediate fits A64 12-bit add/sub range",
        [](EvalContext& ctx, const SExpr& expr) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "a64:fits-imm-32? requires 1 argument");
            return PredicateResult{"unsigned(lowering.intOp(" + getRawOpText(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate", Precedence::Relational};
        }
    }},
    {"a64:fits-imm-64?", {
        "(a64:fits-imm-64? <op>)",
        "Test if 64-bit immediate fits A64 12-bit add/sub range",
        [](EvalContext& ctx, const SExpr& expr) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "a64:fits-imm-64? requires 1 argument");
            return PredicateResult{"uint64_t(lowering.int64Op(" + getRawOpText(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate", Precedence::Relational};
        }
    }},
    {"a64:fits-mask-32?", {
        "(a64:fits-mask-32? <op>)",
        "Test if 32-bit immediate is a valid A64 logical bitmask",
        [](EvalContext& ctx, const SExpr& expr) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "a64:fits-mask-32? requires 1 argument");
            return PredicateResult{"AssemblyBuilderA64::isMaskSupported(unsigned(lowering.intOp(" + getRawOpText(ctx, expr.elements[1]) + ")))", Precedence::Primary};
        }
    }},
    {"a64:fmov-f64?", {
        "(a64:fmov-f64? <op>)",
        "Test if a double constant operand can be materialized with a single fmov",
        [](EvalContext& ctx, const SExpr& expr) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "a64:fmov-f64? requires 1 argument");
            return PredicateResult{"AssemblyBuilderA64::isFmovSupportedFp64(lowering.doubleOp(" + getRawOpText(ctx, expr.elements[1]) + "))", Precedence::Primary};
        }
    }},
    {"a64:fmov-f32?", {
        "(a64:fmov-f32? <op>)",
        "Test if a float constant operand can be materialized with a single fmov",
        [](EvalContext& ctx, const SExpr& expr) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "a64:fmov-f32? requires 1 argument");
            return PredicateResult{"AssemblyBuilderA64::isFmovSupportedFp32(float(lowering.doubleOp(" + getRawOpText(ctx, expr.elements[1]) + ")))", Precedence::Primary};
        }
    }}
};

static void emitGuardBranchA64(CodeWriter& out, EvalContext& ctx, const std::string& condStr, const std::string& labelOp)
{
    ctx.use("build");
    ctx.use("index");
    std::string fresh = ctx.freshLabelFor(labelOp);
    out.line("build.b(" + condStr + ", lowering.getTargetLabel(" + labelOp + ", index, " + fresh + "));");
    out.line("lowering.finalizeTargetLabel(" + labelOp + ", index, " + fresh + ");");
}

static void emitGuardCbzA64(CodeWriter& out, EvalContext& ctx, bool notZero, const std::string& valReg, const std::string& labelOp)
{
    ctx.use("build");
    ctx.use("index");
    std::string fresh = ctx.freshLabelFor(labelOp);
    std::string instr = notZero ? "build.cbnz" : "build.cbz";
    out.line(instr + "(" + valReg + ", lowering.getTargetLabel(" + labelOp + ", index, " + fresh + "));");
    out.line("lowering.finalizeTargetLabel(" + labelOp + ", index, " + fresh + ");");
}

static void requireMemArgA64(EvalContext& ctx, const Operand& op, std::string_view opName)
{
    if (op.kind != OperandKind::Mem)
        throw CompileError(ctx.loc, std::string(opName) + " requires memory operand for addr");
}

static std::string formatMemA64(const Operand& op)
{
    return "mem(" + formatRegArgA64(op.memBase) + ", " + op.memOffset + ")";
}

static const std::unordered_map<std::string_view, ArchOpSpecA64> kArchOpsA64 = {
    {"a64:ldrb!", {
        2, true,
        "(a64:ldrb! <dest> <addr>)",
        "Load unsigned 8-bit byte from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldrb!");
        out.line("build.ldrb(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldrh!", {
        2, true,
        "(a64:ldrh! <dest> <addr>)",
        "Load unsigned 16-bit halfword from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldrh!");
        out.line("build.ldrh(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-32!", {
        2, true,
        "(a64:ldr-32! <dest> <addr>)",
        "Load 32-bit word from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldr-32!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-ptr!", {
        2, true,
        "(a64:ldr-ptr! <dest> <addr>)",
        "Load 64-bit pointer from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldr-ptr!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-64!", {
        2, true,
        "(a64:ldr-64! <dest> <addr>)",
        "Load 64-bit doubleword from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldr-64!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldrsb!", {
        2, true,
        "(a64:ldrsb! <dest> <addr>)",
        "Load signed 8-bit byte from memory with sign extension",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldrsb!");
        out.line("build.ldrsb(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldrsh!", {
        2, true,
        "(a64:ldrsh! <dest> <addr>)",
        "Load signed 16-bit halfword from memory with sign extension",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldrsh!");
        out.line("build.ldrsh(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldrsw!", {
        2, true,
        "(a64:ldrsw! <dest> <addr>)",
        "Load signed 32-bit word from memory with sign extension to 64 bits",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldrsw!");
        out.line("build.ldrsw(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-f32!", {
        2, true,
        "(a64:ldr-f32! <dest> <addr>)",
        "Load 32-bit single-precision float from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldr-f32!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-f64!", {
        2, true,
        "(a64:ldr-f64! <dest> <addr>)",
        "Load 64-bit double-precision float from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldr-f64!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-vec!", {
        2, true,
        "(a64:ldr-vec! <dest> <addr>)",
        "Load 128-bit SIMD vector from memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[1], "a64:ldr-vec!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:strb!", {
        2, true,
        "(a64:strb! <addr> <val>)",
        "Store 8-bit byte to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:strb!");
        out.line("build.strb(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:strh!", {
        2, true,
        "(a64:strh! <addr> <val>)",
        "Store 16-bit halfword to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:strh!");
        out.line("build.strh(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-32!", {
        2, true,
        "(a64:str-32! <addr> <val>)",
        "Store 32-bit word to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:str-32!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-ptr!", {
        2, true,
        "(a64:str-ptr! <addr> <val>)",
        "Store 64-bit pointer to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:str-ptr!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-64!", {
        2, true,
        "(a64:str-64! <addr> <val>)",
        "Store 64-bit doubleword to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:str-64!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-f32!", {
        2, true,
        "(a64:str-f32! <addr> <val>)",
        "Store 32-bit single-precision float to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:str-f32!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-f64!", {
        2, true,
        "(a64:str-f64! <addr> <val>)",
        "Store 64-bit double-precision float to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:str-f64!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-vec!", {
        2, true,
        "(a64:str-vec! <addr> <val>)",
        "Store 128-bit SIMD vector to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:str-vec!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-wzr!", {
        1, true,
        "(a64:str-wzr! <addr>)",
        "Store 32-bit zero register to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:str-wzr!");
        out.line("build.str(wzr, " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-xzr!", {
        1, true,
        "(a64:str-xzr! <addr>)",
        "Store 64-bit zero register to memory",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        ctx.use("build");
        requireMemArgA64(ctx, raw[0], "a64:str-xzr!");
        out.line("build.str(xzr, " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:add-offset!", {
        3, false,
        "(a64:add-offset! <dest> <src1> <src2>)",
        "Add immediate or byte offset to base pointer",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("emitAddOffset(build, " + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ");");
    }}},
    {"a64:code-addr!", {
        2, false,
        "(a64:code-addr! <dest> <offset>)",
        "Compute absolute host memory address of bytecode instruction",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("emitAddOffset(build, " + formatRegArgA64(args[0]) + ", rCode, (" + formatRegArgA64(args[1]) + ") * sizeof(Instruction));");
    }}},
    {"a64:mov-reg!", {
        2, false,
        "(a64:mov-reg! <dest> <src>)",
        "Move register to register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:mov-imm-32!", {
        2, false,
        "(a64:mov-imm-32! <dest> <src>)",
        "Load 32-bit signed immediate into register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"a64:mov-imm-u32!", {
        2, false,
        "(a64:mov-imm-u32! <dest> <src>)",
        "Load 32-bit unsigned immediate into register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", lowering.uintOp(" + args[1] + "));");
    }}},
    {"a64:mov-imm-64!", {
        2, false,
        "(a64:mov-imm-64! <dest> <src>)",
        "Load 64-bit integer immediate into register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("emitLoadInt64(build, " + args[0] + ", lowering.int64Op(" + args[1] + "));");
    }}},
    {"a64:fmov-imm-f64!", {
        2, false,
        "(a64:fmov-imm-f64! <dest> <src>)",
        "Load a double constant operand with fmov (check a64:fmov-f64? first)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fmov(" + args[0] + ", lowering.doubleOp(" + args[1] + "));");
    }}},
    {"a64:fmov-imm-f32!", {
        2, false,
        "(a64:fmov-imm-f32! <dest> <src>)",
        "Load a float constant operand with fmov (check a64:fmov-f32? first)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fmov(" + args[0] + ", float(lowering.doubleOp(" + args[1] + ")));");
    }}},
    {"a64:load-f64-bits!", {
        3, false,
        "(a64:load-f64-bits! <dest> <src> <scratch>)",
        "Load a double constant operand that fmov cannot encode, via a 64-bit scratch register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("emitLoadDoubleBits(build, " + args[0] + ", " + args[2] + ", lowering.doubleOp(" + args[1] + "));");
    }}},
    {"a64:load-f32-bits!", {
        3, false,
        "(a64:load-f32-bits! <dest> <src> <scratch>)",
        "Load a float constant operand that fmov cannot encode, via a 64-bit scratch register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("emitLoadFloatBits(build, " + args[0] + ", " + args[2] + ", float(lowering.doubleOp(" + args[1] + ")));");
    }}},
    {"a64:mov-imm-raw!", {
        2, false,
        "(a64:mov-imm-raw! <dest> <src>)",
        "Load raw immediate into register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mov(" + args[0] + ", " + args[1] + ");");
    }}},
    {"a64:add-reg-32!", {
        3, false,
        "(a64:add-reg-32! <dest> <src1> <src2>)",
        "Add two 32-bit registers into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:add-reg-64!", {
        3, false,
        "(a64:add-reg-64! <dest> <src1> <src2>)",
        "Add two 64-bit registers into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:add-imm-32!", {
        3, false,
        "(a64:add-imm-32! <dest> <src1> <src2>)",
        "Add 32-bit register and immediate into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:add-imm-64!", {
        3, false,
        "(a64:add-imm-64! <dest> <src1> <src2>)",
        "Add 64-bit register and immediate into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(lowering.int64Op(" + args[2] + ")));");
    }}},
    {"a64:add-imm-u16!", {
        3, false,
        "(a64:add-imm-u16! <dest> <src1> <src2>)",
        "Add 16-bit unsigned immediate into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(" + args[2] + "));");
    }}},
    {"a64:add-shifted!", {
        4, false,
        "(a64:add-shifted! <dest> <src1> <src2> <shift>)",
        "Add shifted register into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", " + args[3] + ");");
    }}},
    {"a64:sub-reg-32!", {
        3, false,
        "(a64:sub-reg-32! <dest> <src1> <src2>)",
        "Subtract two 32-bit registers into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:sub-reg-64!", {
        3, false,
        "(a64:sub-reg-64! <dest> <src1> <src2>)",
        "Subtract two 64-bit registers into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:sub-imm-32!", {
        3, false,
        "(a64:sub-imm-32! <dest> <src1> <src2>)",
        "Subtract immediate from 32-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:sub-imm-64!", {
        3, false,
        "(a64:sub-imm-64! <dest> <src1> <src2>)",
        "Subtract immediate from 64-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(lowering.int64Op(" + args[2] + ")));");
    }}},
    {"a64:sub-imm-u16!", {
        3, false,
        "(a64:sub-imm-u16! <dest> <src1> <src2>)",
        "Subtract 16-bit unsigned immediate into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(" + args[2] + "));");
    }}},
    {"a64:neg-32!", {
        2, false,
        "(a64:neg-32! <dest> <src>)",
        "Negate 32-bit integer register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.neg(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:neg-64!", {
        2, false,
        "(a64:neg-64! <dest> <src>)",
        "Negate 64-bit integer register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.neg(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:mul-reg-32!", {
        3, false,
        "(a64:mul-reg-32! <dest> <src1> <src2>)",
        "Multiply two 32-bit registers into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mul(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:mul-reg-64!", {
        3, false,
        "(a64:mul-reg-64! <dest> <src1> <src2>)",
        "Multiply two 64-bit registers into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mul(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:msub!", {
        4, false,
        "(a64:msub! <dest> <src1> <src2> <subtrahend>)",
        "Multiply-subtract (dest = d - a * b)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.msub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", " + formatRegArgA64(args[3]) + ");");
    }}},
    {"a64:sdiv!", {
        3, false,
        "(a64:sdiv! <dest> <src1> <src2>)",
        "Signed integer division into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sdiv(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:udiv!", {
        3, false,
        "(a64:udiv! <dest> <src1> <src2>)",
        "Unsigned integer division into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.udiv(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:rem!", {
        3, false,
        "(a64:rem! <dest> <src1> <src2>)",
        "Signed integer remainder into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.rem(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:sbfx!", {
        4, false,
        "(a64:sbfx! <dest> <src> <lsb> <width>)",
        "Signed bitfield extract",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sbfx(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:ubfx!", {
        4, false,
        "(a64:ubfx! <dest> <src> <lsb> <width>)",
        "Unsigned bitfield extract",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ubfx(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:ubfx-64!", {
        4, false,
        "(a64:ubfx-64! <dest> <src1> <src2> <src3>)",
        "Unsigned bitfield extract (64-bit)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ubfx(castReg(KindA64::x, " + args[0] + "), castReg(KindA64::x, " + formatRegArgA64(args[1]) + "), " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:sbfx-w-to-x!", {
        4, false,
        "(a64:sbfx-w-to-x! <dest> <src> <lsb> <width>)",
        "Signed bitfield extract from 32-bit to 64-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sbfx(" + args[0] + ", castReg(KindA64::x, " + formatRegArgA64(args[1]) + "), " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:ubfx-w-to-x!", {
        4, false,
        "(a64:ubfx-w-to-x! <dest> <src> <lsb> <width>)",
        "Unsigned bitfield extract from 32-bit to 64-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ubfx(" + args[0] + ", castReg(KindA64::x, " + formatRegArgA64(args[1]) + "), " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:sbfiz!", {
        4, false,
        "(a64:sbfiz! <dest> <src> <lsb> <width>)",
        "Signed bitfield insert in zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.sbfiz(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:ubfiz!", {
        4, false,
        "(a64:ubfiz! <dest> <src> <lsb> <width>)",
        "Unsigned bitfield insert in zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ubfiz(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:rev-32!", {
        2, false,
        "(a64:rev-32! <dest> <src>)",
        "Reverse byte order (endianness) of 32-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.rev(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:rev-64!", {
        2, false,
        "(a64:rev-64! <dest> <src>)",
        "Reverse byte order (endianness) of 64-bit register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.rev(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:clz!", {
        2, false,
        "(a64:clz! <dest> <src>)",
        "Count leading zeros of register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.clz(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:rbit!", {
        2, false,
        "(a64:rbit! <dest> <src>)",
        "Reverse bit order of register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.rbit(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:mvn-32!", {
        2, false,
        "(a64:mvn-32! <dest> <src>)",
        "Bitwise NOT of 32-bit register into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mvn_(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:mvn-64!", {
        2, false,
        "(a64:mvn-64! <dest> <src>)",
        "Bitwise NOT of 64-bit register into destination",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.mvn_(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:and-reg-32!", {
        3, false,
        "(a64:and-reg-32! <dest> <src1> <src2>)",
        "Bitwise AND two 32-bit registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.and_(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:and-reg-64!", {
        3, false,
        "(a64:and-reg-64! <dest> <src1> <src2>)",
        "Bitwise AND two 64-bit registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.and_(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:and-imm-32!", {
        3, false,
        "(a64:and-imm-32! <dest> <src1> <src2>)",
        "Bitwise AND 32-bit register and logical immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.and_(" + args[0] + ", " + formatRegArgA64(args[1]) + ", unsigned(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:orr-reg-32!", {
        3, false,
        "(a64:orr-reg-32! <dest> <src1> <src2>)",
        "Bitwise OR two 32-bit registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.orr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:orr-reg-64!", {
        3, false,
        "(a64:orr-reg-64! <dest> <src1> <src2>)",
        "Bitwise OR two 64-bit registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.orr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:orr-imm-32!", {
        3, false,
        "(a64:orr-imm-32! <dest> <src1> <src2>)",
        "Bitwise OR 32-bit register and logical immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.orr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", unsigned(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:eor-reg-32!", {
        3, false,
        "(a64:eor-reg-32! <dest> <src1> <src2>)",
        "Bitwise XOR two 32-bit registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.eor(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:eor-reg-64!", {
        3, false,
        "(a64:eor-reg-64! <dest> <src1> <src2>)",
        "Bitwise XOR two 64-bit registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.eor(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:eor-imm-32!", {
        3, false,
        "(a64:eor-imm-32! <dest> <src1> <src2>)",
        "Bitwise XOR 32-bit register and logical immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.eor(" + args[0] + ", " + formatRegArgA64(args[1]) + ", unsigned(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:bic-reg!", {
        3, false,
        "(a64:bic-reg! <dest> <src1> <src2>)",
        "Bitwise AND with complement (dest = a & ~b)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.bic(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:lsl-reg!", {
        3, false,
        "(a64:lsl-reg! <dest> <src1> <src2>)",
        "Logical shift left by register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lsl(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:lsl-imm-32!", {
        3, false,
        "(a64:lsl-imm-32! <dest> <src1> <src2>)",
        "Logical shift left 32-bit register by immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lsl(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ");");
    }}},
    {"a64:lsl-imm-u8!", {
        3, false,
        "(a64:lsl-imm-u8! <dest> <src1> <src2>)",
        "Logical shift left by immediate amount",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lsl(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint8_t(" + args[2] + "));");
    }}},
    {"a64:lsr-reg!", {
        3, false,
        "(a64:lsr-reg! <dest> <src1> <src2>)",
        "Logical shift right by register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lsr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:lsr-imm-32!", {
        3, false,
        "(a64:lsr-imm-32! <dest> <src1> <src2>)",
        "Logical shift right 32-bit register by immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lsr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ");");
    }}},
    {"a64:lsr-imm-u8!", {
        3, false,
        "(a64:lsr-imm-u8! <dest> <src1> <src2>)",
        "Logical shift right by immediate amount",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.lsr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint8_t(" + args[2] + "));");
    }}},
    {"a64:asr-reg!", {
        3, false,
        "(a64:asr-reg! <dest> <src1> <src2>)",
        "Arithmetic shift right by register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.asr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:asr-imm-u8!", {
        3, false,
        "(a64:asr-imm-u8! <dest> <src1> <src2>)",
        "Arithmetic shift right by immediate amount",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.asr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint8_t(" + args[2] + "));");
    }}},
    {"a64:ror-reg!", {
        3, false,
        "(a64:ror-reg! <dest> <src1> <src2>)",
        "Rotate right by register",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ror(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:ror-imm-u8!", {
        3, false,
        "(a64:ror-imm-u8! <dest> <src1> <src2>)",
        "Rotate right by immediate amount",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ror(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint8_t(" + args[2] + "));");
    }}},

    {"a64:cmp-reg!", {
        2, false,
        "(a64:cmp-reg! <lhs> <rhs>)",
        "Compare two registers and set condition flags",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:cmp-imm!", {
        2, false,
        "(a64:cmp-imm! <lhs> <rhs>)",
        "Compare register with immediate and set condition flags",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(" + formatRegArgA64(args[1]) + "));");
    }}},
    {"a64:cmp-imm-32!", {
        2, false,
        "(a64:cmp-imm-32! <lhs> <rhs>)",
        "Compare 32-bit register with immediate and set condition flags",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(lowering.intOp(" + args[1] + ")));");
    }}},
    {"a64:cmp-imm-64!", {
        2, false,
        "(a64:cmp-imm-64! <lhs> <rhs>)",
        "Compare 64-bit register with immediate and set condition flags",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(lowering.int64Op(" + args[1] + ")));");
    }}},
    {"a64:tst-reg!", {
        2, false,
        "(a64:tst-reg! <lhs> <rhs>)",
        "Test bitwise AND of two registers and set condition flags",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.tst(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:tst-imm!", {
        2, false,
        "(a64:tst-imm! <lhs> <rhs>)",
        "Test bitwise AND of register and immediate and set flags",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.tst(" + formatRegArgA64(args[0]) + ", " + args[1] + ");");
    }}},
    {"a64:cset-eq!", {
        1, false,
        "(a64:cset-eq! <dest>)",
        "Conditionally set register to 1 if equal, 0 otherwise",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cset(" + args[0] + ", ConditionA64::Equal);");
    }}},
    {"a64:cset-ne!", {
        1, false,
        "(a64:cset-ne! <dest>)",
        "Conditionally set register to 1 if not equal, 0 otherwise",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cset(" + args[0] + ", ConditionA64::NotEqual);");
    }}},
    {"a64:cset-cond-int!", {
        2, false,
        "(a64:cset-cond-int! <dest> <cond>)",
        "Conditionally set register based on 32-bit IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cset(" + args[0] + ", getConditionInt(" + args[1] + "));");
    }}},
    {"a64:cset-inv-cond-int!", {
        2, false,
        "(a64:cset-inv-cond-int! <dest> <cond>)",
        "Conditionally set register based on inverted 32-bit IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cset(" + args[0] + ", getInverseCondition(getConditionInt(" + args[1] + ")));");
    }}},
    {"a64:cset-cond-int64!", {
        2, false,
        "(a64:cset-cond-int64! <dest> <cond>)",
        "Conditionally set register based on 64-bit IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cset(" + args[0] + ", getConditionInt64(" + args[1] + "));");
    }}},
    {"a64:cset-inv-cond-int64!", {
        2, false,
        "(a64:cset-inv-cond-int64! <dest> <cond>)",
        "Conditionally set register based on inverted 64-bit IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cset(" + args[0] + ", getInverseCondition(getConditionInt64(" + args[1] + ")));");
    }}},
    {"a64:csel!", {
        4, false,
        "(a64:csel! <dest> <cond> <src1> <src2>)",
        "Conditional select between two registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.csel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", " + args[3] + ");");
    }}},
    {"a64:csel-cond-int64!", {
        4, false,
        "(a64:csel-cond-int64! <dest> <cond> <src1> <src2>)",
        "Conditional select based on 64-bit IrCondition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.csel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", getConditionInt64(" + args[3] + "));");
    }}},

    // Guard Branches (abort-on-undef / VM-exit aware via getTargetLabel + finalizeTargetLabel)
    {"a64:guard-b!", {
        1, false,
        "(a64:guard-b! <label>)",
        "Guard branch unconditionally to label",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        ctx.use("index");
        std::string fresh = ctx.freshLabelFor(args[0]);
        out.line("build.b(lowering.getTargetLabel(" + args[0] + ", index, " + fresh + "));");
        out.line("lowering.finalizeTargetLabel(" + args[0] + ", index, " + fresh + ");");
    }}},
    {"a64:guard-neg-cond-int!", {
        2, false,
        "(a64:guard-neg-cond-int! <val> <label>)",
        "Guard branch on negated 32-bit condition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        emitGuardBranchA64(out, ctx, "getConditionInt(getNegatedCondition(" + args[0] + "))", args[1]);
    }}},
    {"a64:guard-neg-cond-int64!", {
        2, false,
        "(a64:guard-neg-cond-int64! <val> <label>)",
        "Guard branch on negated 64-bit condition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        emitGuardBranchA64(out, ctx, "getConditionInt64(getNegatedCondition(" + args[0] + "))", args[1]);
    }}},
    {"a64:guard-z!", {
        2, false,
        "(a64:guard-z! <val> <label>)",
        "Guard branch if register is zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        emitGuardCbzA64(out, ctx, false, formatRegArgA64(args[0]), args[1]);
    }}},
    {"a64:guard-nz!", {
        2, false,
        "(a64:guard-nz! <val> <label>)",
        "Guard branch if register is not zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        emitGuardCbzA64(out, ctx, true, formatRegArgA64(args[0]), args[1]);
    }}},
    {"a64:guard-eq!", {
        3, false,
        "(a64:guard-eq! <val> <expected> <label>)",
        "Guard branch if registers are equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string valReg = formatRegArgA64(args[0]);
        std::string expStr = (raw[1].kind == OperandKind::Reg && raw[1].type == TypeKind::Ptr)
            ? formatRegArgA64(args[1])
            : ("uint16_t(" + formatRegArgA64(args[1]) + ")");
        out.line("build.cmp(" + valReg + ", " + expStr + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::Equal", args[2]);
    }}},
    {"a64:guard-ne!", {
        3, false,
        "(a64:guard-ne! <val> <expected> <label>)",
        "Guard branch if registers are not equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string valReg = formatRegArgA64(args[0]);
        std::string expStr = (raw[1].kind == OperandKind::Reg && raw[1].type == TypeKind::Ptr)
            ? formatRegArgA64(args[1])
            : ("uint16_t(" + formatRegArgA64(args[1]) + ")");
        out.line("build.cmp(" + valReg + ", " + expStr + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::NotEqual", args[2]);
    }}},
    {"a64:branch-if-not-equal!", {
        3, false,
        "(a64:branch-if-not-equal! <val> <expected> <label>)",
        "Compare and branch if not equal on A64",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        ctx.use("build");
        std::string valReg = formatRegArgA64(args[0]);
        std::string expStr = (raw[1].kind == OperandKind::Reg && raw[1].type == TypeKind::Ptr)
            ? formatRegArgA64(args[1])
            : ("uint16_t(" + formatRegArgA64(args[1]) + ")");
        out.line("build.cmp(" + valReg + ", " + expStr + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::NotEqual", args[2]);
    }}},
    {"a64:guard-ule-reg!", {
        3, false,
        "(a64:guard-ule-reg! <val> <expected> <label>)",
        "Guard branch if unsigned less-or-equal (register)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::UnsignedLessEqual", args[2]);
    }}},
    {"a64:guard-ule-imm!", {
        3, false,
        "(a64:guard-ule-imm! <val> <expected> <label>)",
        "Guard branch if unsigned less-or-equal (immediate)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(" + args[1] + "));");
        emitGuardBranchA64(out, ctx, "ConditionA64::UnsignedLessEqual", args[2]);
    }}},
    {"a64:guard-ult-reg!", {
        3, false,
        "(a64:guard-ult-reg! <val> <expected> <label>)",
        "Guard branch if unsigned less-than (register)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::UnsignedLess", args[2]);
    }}},
    {"a64:guard-ult-imm!", {
        3, false,
        "(a64:guard-ult-imm! <val> <expected> <label>)",
        "Guard branch if unsigned less-than (immediate)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(" + args[1] + "));");
        emitGuardBranchA64(out, ctx, "ConditionA64::UnsignedLess", args[2]);
    }}},

    // Block Jumps (jump directly to lowering.labelOp(target))
    {"a64:jump-z!", {
        2, false,
        "(a64:jump-z! <val> <label>)",
        "Jump to block label if register is zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cbz(" + formatRegArgA64(args[0]) + ", lowering.labelOp(" + args[1] + "));");
    }}},
    {"a64:jump-nz!", {
        2, false,
        "(a64:jump-nz! <val> <label>)",
        "Jump to block label if register is not zero",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cbnz(" + formatRegArgA64(args[0]) + ", lowering.labelOp(" + args[1] + "));");
    }}},
    {"a64:jump-eq!", {
        3, false,
        "(a64:jump-eq! <val> <expected> <label>)",
        "Jump to block label if registers are equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        out.line("build.b(ConditionA64::Equal, lowering.labelOp(" + args[2] + "));");
    }}},
    {"a64:jump-ne!", {
        3, false,
        "(a64:jump-ne! <val> <expected> <label>)",
        "Jump to block label if registers are not equal",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        out.line("build.b(ConditionA64::NotEqual, lowering.labelOp(" + args[2] + "));");
    }}},
    {"a64:jump-neg!", {
        2, false,
        "(a64:jump-neg! <val> <label>)",
        "Jump to block label if register is negative",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(0));");
        out.line("build.b(ConditionA64::Less, lowering.labelOp(" + args[1] + "));");
    }}},
    {"a64:jump-cond-int-imm!", {
        4, false,
        "(a64:jump-cond-int-imm! <cond> <lhs> <rhs> <label>)",
        "Jump on 32-bit condition comparing register and immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(lowering.intOp(" + args[1] + ")));");
        out.line("build.b(getConditionInt(" + args[2] + "), lowering.labelOp(" + args[3] + "));");
    }}},
    {"a64:jump-cond-int-reg!", {
        4, false,
        "(a64:jump-cond-int-reg! <cond> <lhs> <rhs> <label>)",
        "Jump on 32-bit condition comparing two registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        out.line("build.b(getConditionInt(" + args[2] + "), lowering.labelOp(" + args[3] + "));");
    }}},
    {"a64:b!", {
        1, false,
        "(a64:b! <label>)",
        "Unconditional branch to label",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.b(lowering.labelOp(" + args[0] + "));");
    }}},

    {"a64:fmov!", {
        2, false,
        "(a64:fmov! <dest> <src>)",
        "Move floating-point register or immediate",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fmov(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fadd!", {
        3, false,
        "(a64:fadd! <dest> <src1> <src2>)",
        "Floating-point addition",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fadd(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:fsub!", {
        3, false,
        "(a64:fsub! <dest> <src1> <src2>)",
        "Floating-point subtraction",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fsub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:fmul!", {
        3, false,
        "(a64:fmul! <dest> <src1> <src2>)",
        "Floating-point multiplication",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fmul(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:fdiv!", {
        3, false,
        "(a64:fdiv! <dest> <src1> <src2>)",
        "Floating-point division",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fdiv(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:fneg!", {
        2, false,
        "(a64:fneg! <dest> <src>)",
        "Floating-point negation",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fneg(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fabs!", {
        2, false,
        "(a64:fabs! <dest> <src>)",
        "Floating-point absolute value",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fabs(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fsqrt!", {
        2, false,
        "(a64:fsqrt! <dest> <src>)",
        "Floating-point square root",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fsqrt(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fmla!", {
        3, false,
        "(a64:fmla! <dest> <src1> <src2>)",
        "Floating-point fused multiply-add",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fmla(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:frintm!", {
        2, false,
        "(a64:frintm! <dest> <src>)",
        "Round floating-point toward minus infinity (floor)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.frintm(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:frintp!", {
        2, false,
        "(a64:frintp! <dest> <src>)",
        "Round floating-point toward plus infinity (ceil)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.frintp(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:frinta!", {
        2, false,
        "(a64:frinta! <dest> <src>)",
        "Round floating-point to nearest integer",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.frinta(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcvt!", {
        2, false,
        "(a64:fcvt! <dest> <src>)",
        "Floating-point precision convert",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fcvt(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcvtzs!", {
        2, false,
        "(a64:fcvtzs! <dest> <src>)",
        "Convert floating-point to signed 32-bit integer",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fcvtzs(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcvtzs-64!", {
        2, false,
        "(a64:fcvtzs-64! <dest> <src>)",
        "Convert floating-point to signed 64-bit integer",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fcvtzs(castReg(KindA64::x, " + args[0] + "), " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:scvtf!", {
        2, false,
        "(a64:scvtf! <dest> <src>)",
        "Convert signed integer to floating-point",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.scvtf(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:ucvtf!", {
        2, false,
        "(a64:ucvtf! <dest> <src>)",
        "Convert unsigned integer to floating-point",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.ucvtf(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcmp!", {
        2, false,
        "(a64:fcmp! <lhs> <rhs>)",
        "Compare floating-point registers",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fcmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcmpz!", {
        1, false,
        "(a64:fcmpz! <val>)",
        "Compare floating-point register against zero (+0.0)",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fcmpz(" + formatRegArgA64(args[0]) + ");");
    }}},
    {"a64:fcsel!", {
        4, false,
        "(a64:fcsel! <dest> <cond> <src1> <src2>)",
        "Floating-point conditional select",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fcsel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", " + args[3] + ");");
    }}},
    {"a64:fcsel-lt!", {
        3, false,
        "(a64:fcsel-lt! <dest> <src1> <src2>)",
        "Floating-point conditional select if less than",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fcsel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", getConditionFP(IrCondition::Less));");
    }}},
    {"a64:fcsel-gt!", {
        3, false,
        "(a64:fcsel-gt! <dest> <src1> <src2>)",
        "Floating-point conditional select if greater than",
        [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("build");
        out.line("build.fcsel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", getConditionFP(IrCondition::Greater));");
    }}},
};

// Operations defined as C++ templates (see TemplateOpSpec for the placeholder syntax)
static const TemplateOpTable kTemplateOpsA64 = {
    // Addresses (may allocate a temporary register for the address computation)
    {"a64:temp-addr", {"lowering.tempAddr({0:raw}, {1:int})", "Address of a VmReg/VmConst/pointer operand plus offset (may allocate)", true}},

    {"a64:temp-addr-buffer", {"lowering.tempAddrBuffer({0:raw}, {1:raw}, lowering.tagOp({2:raw}))", "Address of element <1> in buffer <0> (tag <2>; may allocate)", true}},
    {"a64:max-imm", {"int(AssemblyBuilderA64::kMaxImmediate)", "Largest add/sub/cmp immediate", true}},
    {"a64:cast-d", {"castReg(KindA64::d, {0})", "Register <0> viewed as a 64-bit float register", true}},
    {"a64:cast-x", {"castReg(KindA64::x, {0})", "Register <0> viewed as a 64-bit register", true}},
    {"a64:cast-w", {"castReg(KindA64::w, {0})", "Register <0> viewed as a 32-bit register", true}},
    // Register allocator state around calls
    {"a64:free-reg!", {"regs.freeReg({0});", "Release register <0> so it isn't recorded in the spill sequence"}},
    {"a64:take-reg!", {"{0} = regs.takeReg({0}, index);", "Re-reserve register <0> for the current instruction's value"}},
    {"a64:spill", {"regs.spill(index)", "Spill all live registers before a call; returns the spill sequence start for a64:restore!", true}},
    {"a64:restore!", {"regs.restore({0:raw});", "Restore registers spilled by a64:spill"}},
    {"a64:spill-keeping!", {"regs.spill(index, {{0}});", "Spill live registers before a call, keeping <0> (already loaded) in place"}},
    {"a64:spill-keeping2!", {"regs.spill(index, {{0}, {1}});", "Spill live registers before a call, keeping <0> and <1> in place"}},
    {"a64:spill-keeping3!", {"regs.spill(index, {{0}, {1}, {2}});", "Spill live registers before a call, keeping <0>, <1> and <2> in place"}},
    {"a64:spill-keeping", {"regs.spill(index, {{0}})", "Spill live registers keeping <0> in place; returns the spill sequence start for a64:restore!", true}},
    {"a64:add-interrupt-handler!", {"{ Label resume = build.setLabel(); lowering.interruptHandlers.push_back({{0:raw}, {1:uint}, resume}); }",
        "Register an interrupt handler: label <0> jumps out for pc <1>, and the handler resumes here"}},
    {"a64:spill-keeping2", {"regs.spill(index, {{0}, {1}})", "Spill live registers keeping <0> and <1> in place; returns the spill sequence start", true}},
    {"a64:check-object-barrier-conditions!", {"lowering.checkObjectBarrierConditions({0}, {1}, {2}, {3:raw}, {4:raw}, {5:raw});",
        "Branch to <5> unless storing value <3> (register <2>, tag <4> or -1) into object <0> needs a GC barrier (scratch <1>)"}},
    {"a64:spill-all!", {"regs.spill(index);", "Spill all live registers before a call that doesn't return values in registers"}},
    {"a64:fallback!", {"emitFallback(build, {0:raw}, {1:uint});", "Call the VM implementation at NativeContext offset <0> for the instruction at pc <1>"}},
    {"a64:emit-add-offset!", {"emitAddOffset(build, {0}, {1}, {2:raw});", "<0> = <1> + <2> for any offset"}},
    {"a64:adr-number-tvalue!", {"{ TValue n = {}; setnvalue(&n, {1:uint}); build.adr({0}, &n, sizeof(n)); }",
        "Load the address of a data-section number TValue holding uint constant <1> into <0>"}},
    {"a64:emit-abort!", {"emitAbort(build, {0:raw});", "Place local label <0> on a trap, skipped by normal control flow"}},
    {"a64:emit-builtin!", {"lowering.error |= !emitBuiltin(build, lowering.function, regs, {0:uint}, {1:raw}, {2:raw}, {3:int});",
        "Inline builtin <0> with R<1> = f(R<2>...) and <3> results (records a lowering error if unsupported)"}},
    {"a64:class-member-auth!", {"emitClassMemberAuthA64(build, regs, {0}, {1}, {2}, {3:raw});",
        "Authorize private/const access to member slot <1> of class <0> (written object <2> or noreg), else branch to <3>"}},
    {"a64:update-base!", {"emitUpdateBase(build);", "Reload rBase from L->base after a call that may reallocate the stack"}},
    {"a64:mark-exit-sync!", {"lowering.exitSyncInstIdx = index;\nlowering.exitSyncAllocToken = regs.getAllocToken();",
        "Record the register state so every branch to an exit sync block can be checked to see the same allocation"}},
    {"a64:mem-post", {"mem({0}, {1:raw}, AddressKindA64::post)", "Address <0>, post-incremented by <1>", true}},
    {"a64:addr-plus", {"AddressA64({0:raw}.base, {0:raw}.data + {1:raw})", "Immediate address <0> displaced by <1> bytes", true}},
    {"a64:addr-fits-vector?", {"({0:raw}.kind == AddressKindA64::imm && {0:raw}.data % 4 == 0 && unsigned({0:raw}.data + 8) / 4 <= AddressA64::kMaxOffset)",
        "Test if immediate address <0> can address three consecutive floats", true}},
    {"a64:cast-s", {"castReg(KindA64::s, {0})", "Register <0> viewed as a 32-bit float register", true}},
    {"a64:cast-q", {"castReg(KindA64::q, {0})", "Register <0> viewed as a 128-bit vector register", true}},
    {"a64:cond", {"ConditionA64::{0:raw}", "A64 condition code (e.g. :NotEqual)", true}},
    {"a64:cond-int-of", {"getConditionInt({0})", "A64 condition for an IrCondition operand on int comparisons", true}},
    {"a64:cond-inverse", {"getInverseCondition({0:raw})", "Inverse of an A64 condition", true}},
    {"a64:cond-fp-of", {"getConditionFP({0})", "A64 condition for an IrCondition operand on floating-point comparisons", true}},
    {"a64:cond-int64-of", {"getConditionInt64({0})", "A64 condition for an IrCondition operand on int64 comparisons", true}},
    {"a64:cond-fp-negated-of", {"getConditionFP(getNegatedCondition({0}))", "A64 floating-point condition for the negation of IrCondition operand <0>", true}},
    {"a64:cond-int64", {"getConditionInt64(IrCondition::{0:raw})", "A64 condition for an IrCondition on int64 comparisons (e.g. :Equal)", true}},

    // Branch targets: a block/exit label, using the given local label when the target needs a fresh one (VM exit or undef)
    {"a64:target-label", {"lowering.getTargetLabel({0:raw}, index, {1:raw})", "Label for branching to block/exit <0>, with fallback label <1>", true, true}},
    {"a64:finalize-target-label!", {"lowering.finalizeTargetLabel({0:raw}, index, {1:raw});", "Emit the exit/abort code for fallback label <1> of target <0>"}},
    {"a64:cond-fp", {"getConditionFP(IrCondition::{0:raw})", "A64 condition for an IrCondition on floating-point comparisons (e.g. :Equal)", true}},
    {"a64:double-bits", {"getDoubleBits({0:double})", "Bit pattern of a double constant operand", true}},

    // Instructions
    {"a64:ldr!", {"build.ldr({0}, {1});", "ldr <0>, <1> (load sized by the destination register)"}},
    {"a64:str!", {"build.str({0}, {1});", "str <0>, <1> (store sized by the source register)"}},
    {"a64:mov-imm-unsigned!", {"build.mov({0}, unsigned({1:int}));", "mov <0>, int constant <1> reinterpreted as unsigned"}},
    // Shifts and rotates by mnemonic (e.g. :lsl, :lsr, :asr, :ror)
    {"a64:shift-raw!", {"build.{0:raw}({1}, {2}, {3:raw});", "<0> <1>, <2>, #<3> (amount as given)"}},
    {"a64:shift-reg!", {"build.{0:raw}({1}, {2}, {3});", "<0> <1>, <2>, <3> (shift amount in a register)"}},
    {"a64:shift-imm32!", {"build.{0:raw}({1}, {2}, uint8_t(unsigned({3:int}) & 31));", "<0> <1>, <2>, #(<3> & 31)"}},
    {"a64:shift-imm64!", {"build.{0:raw}({1}, {2}, uint8_t(unsigned({3:int}) & 63));", "<0> <1>, <2>, #(<3> & 63)"}},
    {"a64:rol-imm32!", {"build.ror({0}, {1}, uint8_t((32 - unsigned({2:int})) & 31));", "Rotate <1> left by int constant <2> (as ror by 32 - <2>)"}},
    {"a64:b-label!", {"build.b({0:raw});", "b to local label <0>"}},
    {"a64:neg!", {"build.neg({0}, {1});", "neg <0>, <1>"}},
    {"a64:clz-op!", {"build.clz({0}, {1});", "clz <0>, <1>"}},
    {"a64:rbit-op!", {"build.rbit({0}, {1});", "rbit <0>, <1>"}},
    {"a64:ldrb-op!", {"build.ldrb({0}, {1});", "ldrb <0>, <address>"}},
    {"a64:ldrsb-op!", {"build.ldrsb({0}, {1});", "ldrsb <0>, <address>"}},
    {"a64:ldrh-op!", {"build.ldrh({0}, {1});", "ldrh <0>, <address>"}},
    {"a64:ldrsh-op!", {"build.ldrsh({0}, {1});", "ldrsh <0>, <address>"}},
    {"a64:strb-op!", {"build.strb({0}, {1});", "strb <0>, <address>"}},
    {"a64:strh-op!", {"build.strh({0}, {1});", "strh <0>, <address>"}},
    {"a64:fjcvtzs!", {"build.fjcvtzs({0}, {1});", "fjcvtzs <0>, <1> (sets Z iff the conversion is exact)"}},
    {"a64:add-imm!", {"build.add({0}, {1}, uint16_t({2:raw}));", "add <0>, <1>, #<2>"}},
    {"a64:sub-imm!", {"build.sub({0}, {1}, uint16_t({2:raw}));", "sub <0>, <1>, #<2>"}},
    {"a64:sub-reg!", {"build.sub({0}, {1}, {2});", "sub <0>, <1>, <2>"}},
    {"a64:cmp-op!", {"build.cmp({0}, {1});", "cmp <0>, <1> (register or uint16_t immediate as given)"}},
    {"a64:cset!", {"build.cset({0}, {1:raw});", "cset <0>, <cond 1>"}},
    {"a64:eor-imm!", {"build.eor({0}, {1}, {2:raw});", "eor <0>, <1>, #<2>"}},
    {"a64:orr-op!", {"build.orr({0}, {1}, {2});", "orr <0>, <1>, <2>"}},
    {"a64:umov-4s!", {"build.umov_4s({0}, {1}, {2:raw});", "umov <0>, <1>.s[<lane>]"}},
    {"a64:ldp!", {"build.ldp({0}, {1}, {2});", "ldp <0>, <1>, <address>"}},
    {"a64:sub-shifted!", {"build.sub({0}, {1}, {2}, {3:raw});", "sub <0>, <1>, <2> shifted left by <3>"}},
    {"a64:stp!", {"build.stp({0}, {1}, {2});", "stp <0>, <1>, <address>"}},
    {"a64:br!", {"build.br({0});", "br <0> (indirect jump)"}},
    {"a64:blr!", {"build.blr({0});", "blr <0> (call)"}},
    {"a64:and-shifted!", {"build.and_({0}, {1}, {2}, {3:raw});", "and <0>, <1>, <2> shifted by <3> (negative: right shift)"}},
    {"a64:bic-op!", {"build.bic({0}, {1}, {2});", "bic <0>, <1>, <2>"}},
    {"a64:fcvtzs-op!", {"build.fcvtzs({0}, {1});", "fcvtzs <0>, <1>"}},
    {"a64:and-op!", {"build.and_({0}, {1}, {2});", "and <0>, <1>, <2>"}},
    {"a64:tbz!", {"build.tbz({0}, {1:raw}, {2:raw});", "tbz <0>, #<bit 1>, <label>"}},
    {"a64:cbz!", {"build.cbz({0}, {1:raw});", "cbz <0>, <label>"}},
    {"a64:cbnz!", {"build.cbnz({0}, {1:raw});", "cbnz <0>, <label>"}},
    {"a64:b-cond!", {"build.b({0:raw}, {1:raw});", "b.<cond> <label> (condition from a64:cond / a64:cond-int64)"}},
    {"a64:ccmn!", {"build.ccmn({0}, {1:raw}, {2:raw}, {3:raw});", "ccmn <0>, #<1>, #<nzcv 3>, <cond 2>"}},
    {"a64:fcmgt-4s!", {"build.fcmgt_4s({0}, {1}, {2});", "fcmgt <0>.4s, <1>.4s, <2>.4s"}},
    {"a64:fcmeq-4s!", {"build.fcmeq_4s({0}, {1}, {2});", "fcmeq <0>.4s, <1>.4s, <2>.4s"}},
    {"a64:bif!", {"build.bif({0}, {1}, {2});", "bif <0>, <1>, <2> (insert <1> where mask <2> is clear)"}},
    {"a64:bit!", {"build.bit({0}, {1}, {2});", "bit <0>, <1>, <2> (insert <1> where mask <2> is set)"}},
    {"a64:faddp!", {"build.faddp({0}, {1});", "faddp <0>, <1> (pairwise add)"}},
    {"a64:dup-4s!", {"build.dup_4s({0}, {1}, {2:int});", "dup <0>, <1>.s[<2>]"}},
    {"a64:ins-4s!", {"build.ins_4s({0}, {1}, {2:raw});", "ins <0>.s[<2>], <1>"}},
    {"a64:adr-u32x3!", {"{ uint32_t vec[4] = {{1:raw}, {1:raw}, {1:raw}, 0}; build.adr({0}, vec, sizeof(vec)); }",
        "Load the address of a data-section vector of three copies of 32-bit pattern <1> (fourth lane 0) into <0>"}},
    {"a64:mov!", {"build.mov({0}, {1});", "mov <0>, <1> (register or immediate)"}},
};

class TargetBuilderA64 : public TargetBuilder
{
public:
    std::string archName() const override { return "A64"; }
    std::string loweringClass() const override { return "IrLoweringA64"; }
    std::string destRegVar() const override { return "inst.regA64"; }

    void emitPreamble(CodeWriter& out, const std::vector<std::string>& fflags, const std::vector<std::string>& dfflags) override
    {
        out.raw("// Auto-generated by luwu-dsl-compiler from Luna/defs/luwu.luna. DO NOT EDIT!\n"
                "#pragma once\n\n"
                "#include \"IrLoweringA64.h\"\n"
                "#include \"EmitCommonA64.h\"\n"
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
                "namespace A64\n"
                "{\n\n");
    }

    void emitPostamble(CodeWriter& out) override
    {
        out.raw("} // namespace A64\n"
                "} // namespace CodeGen\n"
                "} // namespace Luau\n");
    }

    void emitAllocProduce(CodeWriter& out, EvalContext& ctx, TypeKind type, bool isReuse, const std::vector<std::string>& rawOps) override
    {
        ctx.use("regs");
        ctx.use("index");
        ctx.use("inst");
        std::string kind = kindForTypeA64(type);
        if (isReuse)
        {
            std::string line = "inst.regA64 = regs.allocReuse(" + kind + ", index, {";
            for (size_t i = 0; i < rawOps.size(); ++i)
            {
                if (i > 0)
                    line += ", ";
                line += rawOps[i];
            }
            line += "});";
            out.line(line);
        }
        else
            out.line("inst.regA64 = regs.allocReg(" + kind + ", index);");
    }

    Operand emitAllocTemp(CodeWriter& out, EvalContext& ctx, TypeKind type, const std::string& varName) override
    {
        ctx.use("regs");
        std::string kindStr = kindForTypeA64(type);
        out.line("RegisterA64 " + varName + " = regs.allocTemp(" + kindStr + ");");
        return Operand::makeReg(type, varName);
    }

    std::string resolveMemBase(CodeWriter& out, EvalContext& ctx, const SExpr& baseExpr, const Operand& baseOp, const std::string& destReg = "") override
    {
        (void)out;
        (void)baseExpr;
        (void)destReg;
        if (baseOp.kind == OperandKind::Mem)
            throw CompileError(ctx.loc, "Cannot use memory operand as memory base in A64; load it into a register first");
        return formatRegArgA64(baseOp.text);
    }

    bool emitArchOp(CodeWriter& out, EvalContext& ctx, const std::string& opName, const std::vector<Operand>& rawArgs) override
    {
        auto it = kArchOpsA64.find(opName);
        if (it == kArchOpsA64.end())
            return false;

        const ArchOpSpecA64& spec = it->second;
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
                throw CompileError(ctx.loc, "Cannot implicitly convert memory operand to register in A64; use an explicit load subrule");
            args.push_back(a.text);
        }

        spec.emit(out, ctx, rawArgs, args);
        return true;
    }

    std::optional<PredicateResult> evalArchPredicate(EvalContext& ctx, const SExpr& expr, const std::string&) override
    {
        if (!expr.isList() || expr.elements.empty())
            return std::nullopt;

        const std::string& name = expr.elements[0].text;
        auto it = kArchPredicatesA64.find(name);
        if (it != kArchPredicatesA64.end())
            return it->second.eval(ctx, expr);
        return std::nullopt;
    }

    void emitTakeResult(CodeWriter& out, EvalContext& ctx, const std::string& physReg) override
    {
        ctx.use("regs");
        out.line("inst.regA64 = regs.takeReg(" + physReg + ", index);");
    }

    std::string regVarType() const override { return "RegisterA64"; }

    std::string formatMemOperand(EvalContext&, const Operand& op, const std::string&) const override
    {
        return formatMemA64(op);
    }

    const TemplateOpSpec* findTemplateOp(const std::string& name) const override
    {
        auto it = kTemplateOpsA64.find(name);
        return it == kTemplateOpsA64.end() ? nullptr : &it->second;
    }

    void getTemplateOpDocs(std::vector<FormDoc>& ops, std::vector<FormDoc>& values) const override
    {
        getTemplateDocs(kTemplateOpsA64, ops, values);
    }

    std::string formatRegOperand(const Operand& op) const override
    {
        return formatRegArgA64(op.text);
    }

    std::string conditionalTempExpr(CodeWriter&, EvalContext& ctx, TypeKind type, const std::string&, const std::string&) override
    {
        ctx.use("regs");
        return "regs.allocTemp(" + kindForTypeA64(type) + ")";
    }

    std::set<std::string> globalSymbols() const override
    {
        std::set<std::string> globals = {"rState", "rBase", "rConstants", "rClosure", "rCode", "rGlobalState", "rNativeContext", "noreg", "xzr", "wzr", "sp"};
        // Physical registers, for code that runs with all allocatable registers free or spilled (calls, ABI glue)
        for (int i = 0; i < 32; ++i)
        {
            for (const char* prefix : {"x", "w", "d", "s", "q"})
                globals.insert(prefix + std::to_string(i));
        }
        return globals;
    }

    void getArchPredicateDocs(std::vector<FormDoc>& out) const override
    {
        for (const auto& [name, spec] : kArchPredicatesA64)
        {
            out.push_back({std::string(name), spec.syntax, spec.desc});
        }
    }

    void getArchOpDocs(std::vector<FormDoc>& out) const override;
};

static std::string formatSyntaxA64(std::string_view name, const ArchOpSpecA64& spec)
{
    if (spec.syntax && *spec.syntax)
        return spec.syntax;

    std::string syn = "(" + std::string(name);
    if (spec.allowMemArgs)
    {
        if (name.find("str") != std::string_view::npos)
            syn += (spec.arity == 1 ? " <addr>" : " <addr> <val>");
        else
            syn += " <dest> <addr>";
    }
    else
    {
        if (spec.arity == 1)
            syn += (name.find("guard-b") != std::string_view::npos || name.find("b!") != std::string_view::npos) ? " <label>" : " <dest>";
        else if (spec.arity == 2)
            syn += (name.find("jump") != std::string_view::npos || name.find("guard") != std::string_view::npos) ? " <val> <label>" : " <dest> <src>";
        else if (spec.arity == 3)
            syn += (name.find("jump") != std::string_view::npos || name.find("guard") != std::string_view::npos || name.find("branch") != std::string_view::npos) ? " <val> <expected> <label>" : " <dest> <src1> <src2>";
        else if (spec.arity == 4)
            syn += " <dest> <src1> <src2> <src3>";
    }
    syn += ")";
    return syn;
}

static std::string describeArchOpA64(std::string_view name, const ArchOpSpecA64& spec)
{
    if (spec.desc && *spec.desc)
        return spec.desc;

    std::string s(name);
    if (s.rfind("a64:", 0) == 0)
        s = s.substr(4);
    if (!s.empty() && s.back() == '!')
        s.pop_back();

    if (s == "ldrb") return "Load unsigned 8-bit byte from memory";
    if (s == "ldrh") return "Load unsigned 16-bit halfword from memory";
    if (s == "ldr-32") return "Load 32-bit word from memory";
    if (s == "ldr-ptr") return "Load 64-bit pointer from memory";
    if (s == "ldr-64") return "Load 64-bit doubleword from memory";
    if (s == "ldrsb") return "Load signed 8-bit byte from memory with sign extension";
    if (s == "ldrsh") return "Load signed 16-bit halfword from memory with sign extension";
    if (s == "ldrsw") return "Load signed 32-bit word from memory with sign extension to 64 bits";
    if (s == "ldr-f32") return "Load 32-bit single-precision float from memory";
    if (s == "ldr-f64") return "Load 64-bit double-precision float from memory";
    if (s == "ldr-vec") return "Load 128-bit SIMD vector from memory";
    if (s == "strb") return "Store 8-bit byte to memory";
    if (s == "strh") return "Store 16-bit halfword to memory";
    if (s == "str-32") return "Store 32-bit word to memory";
    if (s == "str-ptr") return "Store 64-bit pointer to memory";
    if (s == "str-64") return "Store 64-bit doubleword to memory";
    if (s == "str-f32") return "Store 32-bit single-precision float to memory";
    if (s == "str-f64") return "Store 64-bit double-precision float to memory";
    if (s == "str-vec") return "Store 128-bit SIMD vector to memory";
    if (s == "str-wzr") return "Store 32-bit zero register to memory";
    if (s == "str-xzr") return "Store 64-bit zero register to memory";
    if (s == "add-offset") return "Add immediate or byte offset to base pointer";
    if (s == "code-addr") return "Compute absolute host memory address of bytecode instruction";
    if (s == "mov-reg") return "Move register to register";
    if (s == "mov-imm-32") return "Load 32-bit signed immediate into register";
    if (s == "mov-imm-u32") return "Load 32-bit unsigned immediate into register";
    if (s == "mov-imm-64") return "Load 64-bit integer immediate into register";
    if (s == "mov-imm-raw") return "Load raw immediate into register";
    if (s == "add-reg-32") return "Add two 32-bit registers into destination";
    if (s == "add-reg-64") return "Add two 64-bit registers into destination";
    if (s == "add-imm-32") return "Add 32-bit register and immediate into destination";
    if (s == "add-imm-64") return "Add 64-bit register and immediate into destination";
    if (s == "add-imm-u16") return "Add 16-bit unsigned immediate into destination";
    if (s == "add-shifted") return "Add shifted register into destination";
    if (s == "sub-reg-32") return "Subtract two 32-bit registers into destination";
    if (s == "sub-reg-64") return "Subtract two 64-bit registers into destination";
    if (s == "sub-imm-32") return "Subtract immediate from 32-bit register";
    if (s == "sub-imm-64") return "Subtract immediate from 64-bit register";
    if (s == "sub-imm-u16") return "Subtract 16-bit unsigned immediate into destination";
    if (s == "neg-32") return "Negate 32-bit integer register";
    if (s == "neg-64") return "Negate 64-bit integer register";
    if (s == "mul-reg-32") return "Multiply two 32-bit registers into destination";
    if (s == "mul-reg-64") return "Multiply two 64-bit registers into destination";
    if (s == "msub") return "Multiply-subtract (dest = d - a * b)";
    if (s == "sdiv") return "Signed integer division into destination";
    if (s == "udiv") return "Unsigned integer division into destination";
    if (s == "rem") return "Signed integer remainder into destination";
    if (s == "sbfx") return "Signed bitfield extract";
    if (s == "ubfx") return "Unsigned bitfield extract";
    if (s == "ubfx-64") return "Unsigned bitfield extract (64-bit)";
    if (s == "sbfx-w-to-x") return "Signed bitfield extract from 32-bit to 64-bit register";
    if (s == "and-reg-32") return "Bitwise AND two 32-bit registers";
    if (s == "and-reg-64") return "Bitwise AND two 64-bit registers";
    if (s == "and-imm-32") return "Bitwise AND 32-bit register and logical immediate";
    if (s == "orr-reg-32") return "Bitwise OR two 32-bit registers";
    if (s == "orr-reg-64") return "Bitwise OR two 64-bit registers";
    if (s == "orr-imm-32") return "Bitwise OR 32-bit register and logical immediate";
    if (s == "eor-reg-32") return "Bitwise XOR two 32-bit registers";
    if (s == "eor-reg-64") return "Bitwise XOR two 64-bit registers";
    if (s == "eor-imm-32") return "Bitwise XOR 32-bit register and logical immediate";
    if (s == "mvn-32") return "Bitwise NOT of 32-bit register into destination";
    if (s == "mvn-64") return "Bitwise NOT of 64-bit register into destination";
    if (s == "rev-32") return "Reverse byte order (endianness) of 32-bit register";
    if (s == "rev-64") return "Reverse byte order (endianness) of 64-bit register";
    if (s == "lsl-reg") return "Logical shift left by register";
    if (s == "lsl-imm-u8") return "Logical shift left by immediate amount";
    if (s == "lsr-reg") return "Logical shift right by register";
    if (s == "lsr-imm-u8") return "Logical shift right by immediate amount";
    if (s == "asr-reg") return "Arithmetic shift right by register";
    if (s == "asr-imm-u8") return "Arithmetic shift right by immediate amount";
    if (s == "ror-reg") return "Rotate right by register";
    if (s == "ror-imm-u8") return "Rotate right by immediate amount";
    if (s == "cmp-reg") return "Compare two registers and set condition flags";
    if (s == "cmp-imm" || s == "cmp-imm-32") return "Compare 32-bit register with immediate";
    if (s == "cmp-imm-64") return "Compare 64-bit register with immediate";
    if (s == "tst-reg") return "Test bitwise AND of two registers and set condition flags";
    if (s == "tst-imm") return "Test bitwise AND of register and immediate";
    if (s == "cset-eq") return "Conditionally set register to 1 if equal, 0 otherwise";
    if (s == "cset-ne") return "Conditionally set register to 1 if not equal, 0 otherwise";
    if (s == "cset-cond-int") return "Conditionally set register based on IrCondition";
    if (s == "cset-inv-cond-int") return "Conditionally set register based on inverted IrCondition";
    if (s == "csel") return "Conditional select between two registers";
    if (s == "csel-cond-int64") return "Conditional select based on 64-bit IrCondition";
    if (s == "guard-b") return "Guard branch directly to label";
    if (s == "guard-z") return "Guard branch if register is zero";
    if (s == "guard-nz") return "Guard branch if register is not zero";
    if (s == "guard-eq") return "Guard branch if registers are equal";
    if (s == "guard-ne") return "Guard branch if registers are not equal";
    if (s == "branch-if-not-equal") return "Compare and branch if not equal on A64";
    if (s == "jump-z") return "Jump to block label if register is zero";
    if (s == "jump-nz") return "Jump to block label if register is not zero";
    if (s == "jump-eq") return "Jump to block label if registers are equal";
    if (s == "jump-ne") return "Jump to block label if registers are not equal";
    if (s == "jump-neg") return "Jump to block label if register is negative";
    if (s == "b") return "Unconditional branch to label";
    if (s == "fmov") return "Move floating-point register or immediate";
    if (s == "fadd") return "Floating-point addition";
    if (s == "fsub") return "Floating-point subtraction";
    if (s == "fmul") return "Floating-point multiplication";
    if (s == "fdiv") return "Floating-point division";
    if (s == "fneg") return "Floating-point negation";
    if (s == "fabs") return "Floating-point absolute value";
    if (s == "fsqrt") return "Floating-point square root";
    if (s == "fmla") return "Floating-point fused multiply-add";
    if (s == "fcmp") return "Floating-point compare";
    if (s == "fcsel") return "Floating-point conditional select";
    if (s == "fcvt") return "Floating-point precision convert";
    if (s == "fcvtzs") return "Floating-point convert to signed integer (round toward zero)";
    if (s == "scvtf") return "Convert signed integer to floating-point";

    return "AArch64 instruction: " + s;
}

void TargetBuilderA64::getArchOpDocs(std::vector<FormDoc>& out) const
{
    for (const auto& [name, spec] : kArchOpsA64)
    {
        std::string syn = formatSyntaxA64(name, spec);
        std::string desc = spec.desc ? spec.desc : describeArchOpA64(name, spec);
        out.push_back({std::string(name), std::move(syn), std::move(desc)});
    }
}

void emitA64(std::ostream& os, const std::vector<Rule>& rules)
{
    TargetBuilderA64 target;
    compileRules(os, target, rules);
}

void getA64Docs(std::vector<FormDoc>& ops, std::vector<FormDoc>& preds)
{
    TargetBuilderA64 target;
    target.getArchOpDocs(ops);
    target.getArchPredicateDocs(preds);
    target.getTemplateOpDocs(ops, preds);
}

} // namespace Luwu::Dsl
