// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "RuleCompiler.h"

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
    size_t arity;
    bool allowMemArgs;
    std::function<void(CodeWriter&, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>&)> emit;
};

static void emitGuardBranchA64(CodeWriter& out, EvalContext& ctx, const std::string& condStr, const std::string& labelOp)
{
    ctx.use("build");
    ctx.use("index");
    ctx.addDecl("Label fresh;");
    out.line("build.b(" + condStr + ", lowering.getTargetLabel(" + labelOp + ", index, fresh));");
    out.line("lowering.finalizeTargetLabel(" + labelOp + ", index, fresh);");
}

static void emitGuardCbzA64(CodeWriter& out, EvalContext& ctx, bool notZero, const std::string& valReg, const std::string& labelOp)
{
    ctx.use("build");
    ctx.use("index");
    ctx.addDecl("Label fresh;");
    std::string instr = notZero ? "build.cbnz" : "build.cbz";
    out.line(instr + "(" + valReg + ", lowering.getTargetLabel(" + labelOp + ", index, fresh));");
    out.line("lowering.finalizeTargetLabel(" + labelOp + ", index, fresh);");
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
    {"a64:ldrb!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldrb!");
        out.line("build.ldrb(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldrh!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldrh!");
        out.line("build.ldrh(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-32!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldr-32!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-ptr!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldr-ptr!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-64!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldr-64!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldrsb!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldrsb!");
        out.line("build.ldrsb(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldrsh!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldrsh!");
        out.line("build.ldrsh(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldrsw!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldrsw!");
        out.line("build.ldrsw(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-f32!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldr-f32!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-f64!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldr-f64!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:ldr-vec!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[1], "a64:ldr-vec!");
        out.line("build.ldr(" + formatRegArgA64(raw[0].text) + ", " + formatMemA64(raw[1]) + ");");
    }}},
    {"a64:strb!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:strb!");
        out.line("build.strb(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:strh!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:strh!");
        out.line("build.strh(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-32!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:str-32!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-ptr!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:str-ptr!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-64!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:str-64!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-f32!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:str-f32!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-f64!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:str-f64!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-vec!", {2, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:str-vec!");
        out.line("build.str(" + formatRegArgA64(raw[1].text) + ", " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-wzr!", {1, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:str-wzr!");
        out.line("build.str(wzr, " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:str-xzr!", {1, true, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>&) {
        requireMemArgA64(ctx, raw[0], "a64:str-xzr!");
        out.line("build.str(xzr, " + formatMemA64(raw[0]) + ");");
    }}},
    {"a64:add-offset!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("emitAddOffset(build, " + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ");");
    }}},
    {"a64:code-addr!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("emitAddOffset(build, " + formatRegArgA64(args[0]) + ", rCode, (" + formatRegArgA64(args[1]) + ") * sizeof(Instruction));");
    }}},
    {"a64:mov-reg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:mov-imm-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", lowering.intOp(" + args[1] + "));");
    }}},
    {"a64:mov-imm-u32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", lowering.uintOp(" + args[1] + "));");
    }}},
    {"a64:mov-imm-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("emitLoadInt64(build, " + args[0] + ", lowering.int64Op(" + args[1] + "));");
    }}},
    {"a64:mov-imm-raw!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mov(" + args[0] + ", " + args[1] + ");");
    }}},
    {"a64:add-reg-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:add-reg-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:add-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:add-imm-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(lowering.int64Op(" + args[2] + ")));");
    }}},
    {"a64:add-imm-u16!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(" + args[2] + "));");
    }}},
    {"a64:add-shifted!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.add(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", " + args[3] + ");");
    }}},
    {"a64:sub-reg-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:sub-reg-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:sub-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:sub-imm-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(lowering.int64Op(" + args[2] + ")));");
    }}},
    {"a64:sub-imm-u16!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint16_t(" + args[2] + "));");
    }}},
    {"a64:neg-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.neg(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:neg-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.neg(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:mul-reg-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mul(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:mul-reg-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mul(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:msub!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.msub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", " + formatRegArgA64(args[3]) + ");");
    }}},
    {"a64:sdiv!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sdiv(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:udiv!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.udiv(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:rem!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.rem(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:sbfx!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sbfx(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:ubfx!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ubfx(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:ubfx-64!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ubfx(castReg(KindA64::x, " + args[0] + "), castReg(KindA64::x, " + formatRegArgA64(args[1]) + "), " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:sbfx-w-to-x!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sbfx(" + args[0] + ", castReg(KindA64::x, " + formatRegArgA64(args[1]) + "), " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:ubfx-w-to-x!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ubfx(" + args[0] + ", castReg(KindA64::x, " + formatRegArgA64(args[1]) + "), " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:sbfiz!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.sbfiz(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:ubfiz!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ubfiz(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ", " + args[3] + ");");
    }}},
    {"a64:rev-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.rev(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:rev-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.rev(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:clz!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.clz(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:rbit!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.rbit(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:mvn-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mvn_(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:mvn-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.mvn_(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:and-reg-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.and_(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:and-reg-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.and_(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:and-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.and_(" + args[0] + ", " + formatRegArgA64(args[1]) + ", unsigned(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:orr-reg-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.orr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:orr-reg-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.orr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:orr-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.orr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", unsigned(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:eor-reg-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.eor(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:eor-reg-64!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.eor(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:eor-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.eor(" + args[0] + ", " + formatRegArgA64(args[1]) + ", unsigned(lowering.intOp(" + args[2] + ")));");
    }}},
    {"a64:bic-reg!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.bic(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:lsl-reg!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lsl(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:lsl-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lsl(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ");");
    }}},
    {"a64:lsl-imm-u8!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lsl(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint8_t(" + args[2] + "));");
    }}},
    {"a64:lsr-reg!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lsr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:lsr-imm-32!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lsr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + args[2] + ");");
    }}},
    {"a64:lsr-imm-u8!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.lsr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint8_t(" + args[2] + "));");
    }}},
    {"a64:asr-reg!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.asr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:asr-imm-u8!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.asr(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint8_t(" + args[2] + "));");
    }}},
    {"a64:ror-reg!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ror(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:ror-imm-u8!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ror(" + args[0] + ", " + formatRegArgA64(args[1]) + ", uint8_t(" + args[2] + "));");
    }}},

    {"a64:cmp-reg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:cmp-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(" + formatRegArgA64(args[1]) + "));");
    }}},
    {"a64:cmp-imm-32!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(lowering.intOp(" + args[1] + ")));");
    }}},
    {"a64:cmp-imm-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(lowering.int64Op(" + args[1] + ")));");
    }}},
    {"a64:tst-reg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.tst(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:tst-imm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.tst(" + formatRegArgA64(args[0]) + ", " + args[1] + ");");
    }}},
    {"a64:cset-eq!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cset(" + args[0] + ", ConditionA64::Equal);");
    }}},
    {"a64:cset-ne!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cset(" + args[0] + ", ConditionA64::NotEqual);");
    }}},
    {"a64:cset-cond-int!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cset(" + args[0] + ", getConditionInt(" + args[1] + "));");
    }}},
    {"a64:cset-inv-cond-int!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cset(" + args[0] + ", getInverseCondition(getConditionInt(" + args[1] + ")));");
    }}},
    {"a64:cset-cond-int64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cset(" + args[0] + ", getConditionInt64(" + args[1] + "));");
    }}},
    {"a64:cset-inv-cond-int64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cset(" + args[0] + ", getInverseCondition(getConditionInt64(" + args[1] + ")));");
    }}},
    {"a64:csel!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.csel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", " + args[3] + ");");
    }}},
    {"a64:csel-cond-int64!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.csel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", getConditionInt64(" + args[3] + "));");
    }}},

    // Guard Branches (abort-on-undef / VM-exit aware via getTargetLabel + finalizeTargetLabel)
    {"a64:guard-b!", {1, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        ctx.use("index");
        ctx.addDecl("Label fresh;");
        out.line("build.b(lowering.getTargetLabel(" + args[0] + ", index, fresh));");
        out.line("lowering.finalizeTargetLabel(" + args[0] + ", index, fresh);");
    }}},
    {"a64:guard-neg-cond-int!", {2, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchA64(out, ctx, "getConditionInt(getNegatedCondition(" + args[0] + "))", args[1]);
    }}},
    {"a64:guard-neg-cond-int64!", {2, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardBranchA64(out, ctx, "getConditionInt64(getNegatedCondition(" + args[0] + "))", args[1]);
    }}},
    {"a64:guard-z!", {2, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardCbzA64(out, ctx, false, formatRegArgA64(args[0]), args[1]);
    }}},
    {"a64:guard-nz!", {2, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        emitGuardCbzA64(out, ctx, true, formatRegArgA64(args[0]), args[1]);
    }}},
    {"a64:guard-eq!", {3, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string valReg = formatRegArgA64(args[0]);
        std::string expStr = (raw[1].kind == OperandKind::Reg && raw[1].type == TypeKind::Ptr)
            ? formatRegArgA64(args[1])
            : ("uint16_t(" + formatRegArgA64(args[1]) + ")");
        out.line("build.cmp(" + valReg + ", " + expStr + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::Equal", args[2]);
    }}},
    {"a64:guard-ne!", {3, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string valReg = formatRegArgA64(args[0]);
        std::string expStr = (raw[1].kind == OperandKind::Reg && raw[1].type == TypeKind::Ptr)
            ? formatRegArgA64(args[1])
            : ("uint16_t(" + formatRegArgA64(args[1]) + ")");
        out.line("build.cmp(" + valReg + ", " + expStr + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::NotEqual", args[2]);
    }}},
    {"a64:branch-if-not-equal!", {3, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>& raw, const std::vector<std::string>& args) {
        std::string valReg = formatRegArgA64(args[0]);
        std::string expStr = (raw[1].kind == OperandKind::Reg && raw[1].type == TypeKind::Ptr)
            ? formatRegArgA64(args[1])
            : ("uint16_t(" + formatRegArgA64(args[1]) + ")");
        out.line("build.cmp(" + valReg + ", " + expStr + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::NotEqual", args[2]);
    }}},
    {"a64:guard-ule-reg!", {3, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::UnsignedLessEqual", args[2]);
    }}},
    {"a64:guard-ule-imm!", {3, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(" + args[1] + "));");
        emitGuardBranchA64(out, ctx, "ConditionA64::UnsignedLessEqual", args[2]);
    }}},
    {"a64:guard-ult-reg!", {3, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        emitGuardBranchA64(out, ctx, "ConditionA64::UnsignedLess", args[2]);
    }}},
    {"a64:guard-ult-imm!", {3, false, [](CodeWriter& out, EvalContext& ctx, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(" + args[1] + "));");
        emitGuardBranchA64(out, ctx, "ConditionA64::UnsignedLess", args[2]);
    }}},

    // Block Jumps (jump directly to lowering.labelOp(target))
    {"a64:jump-z!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cbz(" + formatRegArgA64(args[0]) + ", lowering.labelOp(" + args[1] + "));");
    }}},
    {"a64:jump-nz!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cbnz(" + formatRegArgA64(args[0]) + ", lowering.labelOp(" + args[1] + "));");
    }}},
    {"a64:jump-eq!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        out.line("build.b(ConditionA64::Equal, lowering.labelOp(" + args[2] + "));");
    }}},
    {"a64:jump-ne!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        out.line("build.b(ConditionA64::NotEqual, lowering.labelOp(" + args[2] + "));");
    }}},
    {"a64:jump-neg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(0));");
        out.line("build.b(ConditionA64::Less, lowering.labelOp(" + args[1] + "));");
    }}},
    {"a64:jump-cond-int-imm!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", uint16_t(lowering.intOp(" + args[1] + ")));");
        out.line("build.b(getConditionInt(" + args[2] + "), lowering.labelOp(" + args[3] + "));");
    }}},
    {"a64:jump-cond-int-reg!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.cmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
        out.line("build.b(getConditionInt(" + args[2] + "), lowering.labelOp(" + args[3] + "));");
    }}},
    {"a64:b!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.b(lowering.labelOp(" + args[0] + "));");
    }}},

    {"a64:fmov!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fmov(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fadd!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fadd(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:fsub!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fsub(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:fmul!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fmul(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:fdiv!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fdiv(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:fneg!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fneg(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fabs!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fabs(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fsqrt!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fsqrt(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fmla!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fmla(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ");");
    }}},
    {"a64:frintm!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.frintm(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:frintp!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.frintp(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:frinta!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.frinta(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcvt!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fcvt(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcvtzs!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fcvtzs(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcvtzs-64!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fcvtzs(castReg(KindA64::x, " + args[0] + "), " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:scvtf!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.scvtf(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:ucvtf!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.ucvtf(" + args[0] + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcmp!", {2, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fcmp(" + formatRegArgA64(args[0]) + ", " + formatRegArgA64(args[1]) + ");");
    }}},
    {"a64:fcmpz!", {1, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fcmpz(" + formatRegArgA64(args[0]) + ");");
    }}},
    {"a64:fcsel!", {4, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fcsel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", " + args[3] + ");");
    }}},
    {"a64:fcsel-lt!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fcsel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", getConditionFP(IrCondition::Less));");
    }}},
    {"a64:fcsel-gt!", {3, false, [](CodeWriter& out, EvalContext&, const std::vector<Operand>&, const std::vector<std::string>& args) {
        out.line("build.fcsel(" + args[0] + ", " + formatRegArgA64(args[1]) + ", " + formatRegArgA64(args[2]) + ", getConditionFP(IrCondition::Greater));");
    }}},
};

class TargetBuilderA64 : public TargetBuilder
{
public:
    std::string archName() const override { return "A64"; }
    std::string loweringClass() const override { return "IrLoweringA64"; }
    std::string destRegVar() const override { return "inst.regA64"; }

    void emitPreamble(CodeWriter& out, const std::vector<std::string>& fflags, const std::vector<std::string>& dfflags) override
    {
        out.raw("// Auto-generated by luwu-dsl-compiler from rules.luna. DO NOT EDIT!\n"
                "#pragma once\n\n"
                "#include \"IrLoweringA64.h\"\n"
                "#include \"EmitCommonA64.h\"\n"
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

        ctx.use("build");
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
        if (name == "a64:fits-imm-32?")
        {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "a64:fits-imm-32? requires 1 argument");
            return PredicateResult{"unsigned(lowering.intOp(" + getRawOpText(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate", Precedence::Relational};
        }
        if (name == "a64:fits-imm-64?")
        {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "a64:fits-imm-64? requires 1 argument");
            return PredicateResult{"uint64_t(lowering.int64Op(" + getRawOpText(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate", Precedence::Relational};
        }
        if (name == "a64:fits-mask-32?" || name == "a64:fits-mask?")
        {
            if (expr.elements.size() != 2)
                throw CompileError(expr, name + " requires 1 argument");
            return PredicateResult{"AssemblyBuilderA64::isMaskSupported(unsigned(lowering.intOp(" + getRawOpText(ctx, expr.elements[1]) + ")))", Precedence::Primary};
        }
        return std::nullopt;
    }

    std::optional<Operand> evalArchExpr(CodeWriter& out, EvalContext& ctx, const SExpr& expr, const std::string& destReg) override
    {
        if (!expr.isList() || expr.elements.empty())
            return std::nullopt;

        const std::string& name = expr.elements[0].text;
        if (name.rfind("a64:temp-", 0) != 0)
            return std::nullopt;

        if (expr.elements.size() != 2)
            throw CompileError(expr, name + " requires 1 argument");

        std::string helper;
        TypeKind tk = TypeKind::I32;
        if (name == "a64:temp-double") { helper = "tempDouble"; tk = TypeKind::F64; }
        else if (name == "a64:temp-float") { helper = "tempFloat"; tk = TypeKind::F32; }
        else if (name == "a64:temp-int") { helper = "tempInt"; tk = TypeKind::I32; }
        else if (name == "a64:temp-int64") { helper = "tempInt64"; tk = TypeKind::I64; }
        else if (name == "a64:temp-uint") { helper = "tempUint"; tk = TypeKind::U32; }
        else return std::nullopt;

        std::string raw = getRawOpText(ctx, expr.elements[1]);
        std::string callStr = "lowering." + helper + "(" + raw + ")";
        if (!destReg.empty())
        {
            out.line("RegisterA64 " + destReg + " = " + callStr + ";");
            return Operand::makeReg(tk, destReg);
        }
        return Operand::makeReg(tk, callStr);
    }

    bool isEagerBindingExpr(const std::string& opName) const override
    {
        return opName.rfind("a64:temp-", 0) == 0;
    }
};

void emitA64(std::ostream& os, const std::vector<Rule>& rules)
{
    TargetBuilderA64 target;
    compileRules(os, target, rules);
}

} // namespace Luwu::Dsl
