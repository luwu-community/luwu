// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "OpTemplates.h"
#include "CompileError.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Luwu::Dsl
{

static const TemplateOpTable kCoreTemplateOps = {
    // IR operand payloads
    {"vm-reg", {"vmRegOp({0:raw})", "VM register index of a VmReg operand", true}},
    {"vm-const", {"vmConstOp({0:raw})", "Constant table index of a VmConst operand", true}},
    {"vm-exit", {"vmExitOp({0:raw})", "Bytecode pc of a VmExit operand", true}},
    {"upvalue-index", {"vmUpvalueOp({0:raw})", "Upvalue index of a VmUpvalue operand", true}},
    {"int-val", {"lowering.intOp({0:raw})", "Signed 32-bit value of a constant operand", true}},
    {"int64-val", {"lowering.int64Op({0:raw})", "Signed 64-bit value of a constant operand", true}},
    {"uint-val", {"lowering.uintOp({0:raw})", "Unsigned 32-bit value of a constant operand", true}},
    {"double-val", {"lowering.doubleOp({0:raw})", "Double value of a constant operand", true}},
    {"tag-val", {"lowering.tagOp({0:raw})", "Type tag value of a constant operand", true}},
    {"cond-val", {"conditionOp({0:raw})", "IrCondition value of a condition operand", true}},
    {"block-of", {"lowering.blockOp({0:raw})", "IrBlock referenced by a block operand", true}},
    {"label-of", {"lowering.labelOp({0:raw})", "Label of a block operand", true}},
    {"inst-of", {"lowering.function.instOp({0:raw})", "IrInst producing an instruction operand", true}},
    {"helper-label", {"lowering.helpers.{0:raw}", "Shared helper label (e.g. :exitContinueVm)", true}},

    {"to-unsigned", {"unsigned({0:raw})", "C++ unsigned(<0>)", true}},
    {"to-float", {"float({0:raw})", "C++ float(<0>)", true}},
    {"to-size", {"size_t({0:raw})", "C++ size_t(<0>)", true}},
    {"reg-op", {"lowering.regOp({0:raw})", "Register of instruction operand <0>, resolved now (restoring a spilled value here)", true}},
    {"ir-condition", {"IrCondition::{0:raw}", "IrCondition constant (e.g. :Greater)", true}},
    {"negated-cond", {"getNegatedCondition({0})", "Negation of IrCondition <0>", true}},
    {"check-safe-env!", {"lowering.checkSafeEnv({0:raw}, index, next);", "Guard that the environment is safe (exit to <0> otherwise)"}},
    {"to-int32", {"int32_t({0:raw})", "C++ int32_t(<0>)", true}},
    {"bit", {"(1 << {0:int})", "Bit mask with bit <0> set", true}},
    {"label-bound?", {"({0:raw}.id != 0)", "Test if a local label was used (a branch refers to it)", true}},
    {"import-val", {"lowering.importOp({0:raw})", "Import id of an import constant operand", true}},
    {"label-or-local", {"({0:raw}.kind == IrOpKind::Undef ? {1:raw} : lowering.labelOp({0:raw}))",
        "Label of block operand <0>, or local label <1> when <0> is undef (an abort)", true, true}},
    {"int-operand?", {"({0:raw}.kind == IrOpKind::Constant ? lowering.function.constOp({0:raw}).kind == IrConstKind::Int : getCmdValueKind(lowering.function.instOp({0:raw}).cmd) == IrValueKind::Int)",
        "Test if operand <0> (constant or instruction result) is an int", true}},
    {"native-context-offset", {"getNativeContextOffset({0:uint})", "NativeContext offset of the libm function for builtin <0>", true}},
    {"variadic?", {"lowering.function.variadic", "Test if the function being compiled is variadic", true}},
    {"to-int8", {"int8_t({0:raw})", "C++ int8_t(<0>)", true}},
    {"to-uint32", {"uint32_t({0:raw})", "C++ uint32_t(<0>)", true}},
    {"to-int", {"int({0:raw})", "C++ int(<0>)", true}},
    {"cmd-value-kind", {"getCmdValueKind(lowering.function.instOp({0:raw}).cmd)", "IrValueKind produced by the instruction behind operand <0>", true}},
    {"dirty-high-bits?", {"producesDirtyHighRegisterBits(lowering.function.instOp({0:raw}).cmd)",
        "Test if the instruction behind operand <0> may leave garbage in the upper half of its 64-bit register", true}},
    {"float-bits", {"getFloatBits(float({0:double}))", "Bit pattern of a float constant (from a constant operand)", true}},
    {"build-feature?", {"((build.features & {0:raw}) != 0)", "Test if the target CPU supports a feature (e.g. :Feature_FMA3)", true}},
    {"is-gco?", {"isGCO(lowering.tagOp({0:raw}))", "Test if a constant tag operand is a garbage-collected type", true}},

    // Labels
    {"set-label!", {"build.setLabel({0:raw});", "Bind a label (from alloc-label) to the current code position"}},
};

size_t templateArity(const char* fmt)
{
    size_t arity = 0;
    for (const char* p = fmt; *p; ++p)
    {
        if (*p == '{' && std::isdigit(static_cast<unsigned char>(p[1])))
            arity = std::max(arity, size_t(std::strtoul(p + 1, nullptr, 10)) + 1);
    }
    return arity;
}

static std::string parenthesized(const Operand& op)
{
    return op.prec < Precedence::Primary ? "(" + op.text + ")" : op.text;
}

static std::string rawText(const Operand& op)
{
    return op.rawOp.empty() ? parenthesized(op) : op.rawOp;
}

static std::string formatArg(EvalContext& ctx, const TargetBuilder& target, const SExpr& expr, const Operand& op, const std::string& kind)
{
    static const std::unordered_map<std::string, const char*> kValueFns = {
        {"int", "intOp"}, {"uint", "uintOp"}, {"int64", "int64Op"}, {"double", "doubleOp"}, {"tag", "tagOp"},
    };

    if (kind.empty())
        return op.kind == OperandKind::Mem ? target.formatMemOperand(ctx, op, "") : target.formatRegOperand(op);
    if (kind == "raw")
        return rawText(op);
    if (kind == "byte" || kind == "word" || kind == "dword" || kind == "qword" || kind == "xmmword")
    {
        if (op.kind != OperandKind::Mem)
            throw CompileError(expr, "Expected a memory operand for " + expr.elements[0].text);
        return target.formatMemOperand(ctx, op, kind);
    }
    if (auto it = kValueFns.find(kind); it != kValueFns.end())
        return op.rawOp.empty() ? parenthesized(op) : "lowering." + std::string(it->second) + "(" + op.rawOp + ")";
    if (kind[0] == '@')
        return "lowering." + kind.substr(1) + "(" + rawText(op) + ")";
    if (kind[0] == '%')
        return kind.substr(1) + "(" + rawText(op) + ")";

    throw CompileError(expr, "Unknown template placeholder kind '" + kind + "'");
}

std::string expandOpTemplate(EvalContext& ctx, const TargetBuilder& target, const SExpr& expr, const char* fmt, const std::vector<Operand>& args)
{
    std::string result;
    for (const char* p = fmt; *p; ++p)
    {
        if (*p != '{' || !std::isdigit(static_cast<unsigned char>(p[1])))
        {
            result += *p;
            continue;
        }

        const char* close = std::strchr(p, '}');
        std::string spec(p + 1, close);
        size_t colon = spec.find(':');
        size_t index = std::stoul(spec.substr(0, colon));
        std::string kind = colon == std::string::npos ? "" : spec.substr(colon + 1);

        result += formatArg(ctx, target, expr, args.at(index), kind);
        p = close;
    }
    return result;
}

const TemplateOpSpec* findCoreTemplateOp(const std::string& name)
{
    auto it = kCoreTemplateOps.find(name);
    return it == kCoreTemplateOps.end() ? nullptr : &it->second;
}

static std::string templateSyntax(std::string_view name, const TemplateOpSpec& spec)
{
    std::string syn = "(" + std::string(name);
    for (size_t i = 0; i < templateArity(spec.fmt); ++i)
        syn += " <" + std::to_string(i) + ">";
    return syn + ")";
}

void getTemplateDocs(const TemplateOpTable& table, std::vector<FormDoc>& ops, std::vector<FormDoc>& values)
{
    for (const auto& [name, spec] : table)
        (spec.isValue ? values : ops).push_back({std::string(name), templateSyntax(name, spec), spec.desc ? spec.desc : ""});
}

void getCoreTemplateDocs(std::vector<FormDoc>& ops, std::vector<FormDoc>& values)
{
    getTemplateDocs(kCoreTemplateOps, ops, values);
}

} // namespace Luwu::Dsl
