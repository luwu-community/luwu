// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "RuleCompiler.h"

#include <sstream>
#include <stdexcept>

namespace Luwu::Dsl
{

static std::string getRawOp(const EvalContext& ctx, const SExpr& elem)
{
    if (elem.isSymbol())
    {
        auto it = ctx.rawOps.find(elem.text);
        if (it != ctx.rawOps.end())
            return it->second;
    }
    return elem.text;
}

#include <string_view>

static std::string evalPredicate(const EvalContext& ctx, const SExpr& expr, const std::string& destReg);

using PredicateTrampoline = std::string (*)(const EvalContext& ctx, const SExpr& expr, const std::string& destReg);

static const std::unordered_map<std::string_view, PredicateTrampoline> kPredicateTrampolines = {
    {"constant?", [](const EvalContext& ctx, const SExpr& expr, const std::string&) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("constant? requires 1 argument");
        return getRawOp(ctx, expr.elements[1]) + ".kind == IrOpKind::Constant";
    }},
    {"inst?", [](const EvalContext& ctx, const SExpr& expr, const std::string&) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("inst? requires 1 argument");
        return getRawOp(ctx, expr.elements[1]) + ".kind == IrOpKind::Inst";
    }},
    {"reused?", [](const EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("reused? requires 1 argument");
        return destReg + " == lowering.regOp(" + getRawOp(ctx, expr.elements[1]) + ")";
    }},
    {"not-reused?", [](const EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("not-reused? requires 1 argument");
        return destReg + " != lowering.regOp(" + getRawOp(ctx, expr.elements[1]) + ")";
    }},
    {"=", [](const EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> std::string {
        if (expr.elements.size() != 3)
            throw std::runtime_error("= requires 2 arguments");
        std::string lhs = evalPredicate(ctx, expr.elements[1], destReg);
        std::string rhs = expr.elements[2].text;
        return lhs + " == " + rhs;
    }},
    {"int-val", [](const EvalContext& ctx, const SExpr& expr, const std::string&) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("int-val requires 1 argument");
        return "lowering.intOp(" + getRawOp(ctx, expr.elements[1]) + ")";
    }},
    {"int64-val", [](const EvalContext& ctx, const SExpr& expr, const std::string&) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("int64-val requires 1 argument");
        return "lowering.int64Op(" + getRawOp(ctx, expr.elements[1]) + ")";
    }},
    {"fits-i32?", [](const EvalContext& ctx, const SExpr& expr, const std::string&) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("fits-i32? requires 1 argument");
        std::string raw = getRawOp(ctx, expr.elements[1]);
        return "lowering.int64Op(" + raw + ") >= INT32_MIN && lowering.int64Op(" + raw + ") <= INT32_MAX";
    }},
    {"fits-imm-a64-32?", [](const EvalContext& ctx, const SExpr& expr, const std::string&) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("fits-imm-a64-32? requires 1 argument");
        return "unsigned(lowering.intOp(" + getRawOp(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate";
    }},
    {"fits-imm-a64-64?", [](const EvalContext& ctx, const SExpr& expr, const std::string&) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("fits-imm-a64-64? requires 1 argument");
        return "uint64_t(lowering.int64Op(" + getRawOp(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate";
    }},
    {"and", [](const EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> std::string {
        if (expr.elements.size() < 2)
            throw std::runtime_error("and requires at least 1 argument");
        std::string res;
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            if (i > 1) res += " && ";
            res += evalPredicate(ctx, expr.elements[i], destReg);
        }
        return res;
    }},
    {"or", [](const EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> std::string {
        if (expr.elements.size() < 2)
            throw std::runtime_error("or requires at least 1 argument");
        std::string res;
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            if (i > 1) res += " || ";
            res += evalPredicate(ctx, expr.elements[i], destReg);
        }
        return res;
    }},
    {"not", [](const EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> std::string {
        if (expr.elements.size() != 2)
            throw std::runtime_error("not requires 1 argument");
        return "!(" + evalPredicate(ctx, expr.elements[1], destReg) + ")";
    }}
};

static std::string evalPredicate(const EvalContext& ctx, const SExpr& expr, const std::string& destReg)
{
    if (!expr.isList() || expr.elements.empty())
        throw std::runtime_error("Predicate must be a list: " + expr.text);

    auto it = kPredicateTrampolines.find(expr.elements[0].text);
    if (it != kPredicateTrampolines.end())
        return it->second(ctx, expr, destReg);

    throw std::runtime_error("Unknown predicate: " + expr.elements[0].text);
}

using ExprTrampoline = Operand (*)(std::ostream& os, EvalContext& ctx, TargetBuilder& target,
                                  const SExpr& expr, const std::string& destReg, const std::string& indent);

static Operand evalExplicitWidthRequired(std::ostream&, EvalContext&, TargetBuilder&, const SExpr& expr, const std::string&, const std::string&)
{
    const std::string& head = expr.elements[0].text;
    std::string base = head;
    if (base == "add-int" || base == "add-int64")
        base = "add";
    else if (base == "sub-int" || base == "sub-int64")
        base = "sub";
    throw std::runtime_error(head + " requires explicit width suffix: use (" + base + "-32 ...) or (" + base + "-64 ...)");
}

static Operand evalBitwiseBinOp(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent, const char* op, int bits)
{
    if (expr.elements.size() != 3)
        throw std::runtime_error(expr.elements[0].text + " requires 2 arguments");
    std::string rawOpA = getRawOp(ctx, expr.elements[1]);
    std::string rawOpB = getRawOp(ctx, expr.elements[2]);
    return target.emitBitwiseBinOp(os, indent, ctx, op, bits, rawOpA, rawOpB, destReg);
}

static Operand evalAddReg(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent, int bits)
{
    if (expr.elements.size() == 2)
        target.emitAddOp(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
    else if (expr.elements.size() == 3)
        target.emitAddReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
    else
        throw std::runtime_error(expr.elements[0].text + " requires 1 or 2 arguments");
    return Operand{};
}

static Operand evalAddImm(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent, int bits)
{
    if (expr.elements.size() == 2)
        target.emitAddImmOp(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
    else if (expr.elements.size() == 3)
        target.emitAddImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
    else
        throw std::runtime_error(expr.elements[0].text + " requires 1 or 2 arguments");
    return Operand{};
}

static Operand evalSubReg(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent, int bits)
{
    if (expr.elements.size() == 2)
        target.emitSubOp(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
    else if (expr.elements.size() == 3)
        target.emitSubReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
    else
        throw std::runtime_error(expr.elements[0].text + " requires 1 or 2 arguments");
    return Operand{};
}

static Operand evalSubImm(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent, int bits)
{
    if (expr.elements.size() == 2)
        target.emitSubImmOp(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
    else if (expr.elements.size() == 3)
        target.emitSubImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
    else
        throw std::runtime_error(expr.elements[0].text + " requires 1 or 2 arguments");
    return Operand{};
}

static const std::unordered_map<std::string_view, ExprTrampoline> kExprTrampolines = {
    // Memory & Loads
    {"code-address", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("code-address takes 1 argument");
        Operand offsetOp = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        return target.emitCodeAddress(os, indent, ctx, offsetOp.text, destReg);
    }},
    {"load-ptr", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("load-ptr takes 1 argument");
        auto [base, offset] = evalMem(os, ctx, target, expr.elements[1], destReg, indent);
        return target.emitLoad(os, indent, ctx, TypeKind::Ptr, base, offset, destReg);
    }},
    {"load-u8", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("load-u8 takes 1 argument");
        auto [base, offset] = evalMem(os, ctx, target, expr.elements[1], destReg, indent);
        return target.emitLoad(os, indent, ctx, TypeKind::U8, base, offset, destReg);
    }},
    {"load-u32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("load-u32 takes 1 argument");
        auto [base, offset] = evalMem(os, ctx, target, expr.elements[1], destReg, indent);
        return target.emitLoad(os, indent, ctx, TypeKind::U32, base, offset, destReg);
    }},
    {"sign-extend-8", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("sign-extend-8 takes 1 argument");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        return target.emitSignExtend(os, indent, ctx, 8, val, destReg);
    }},
    {"sign-extend-16", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("sign-extend-16 takes 1 argument");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        return target.emitSignExtend(os, indent, ctx, 16, val, destReg);
    }},
    {"store-ptr!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("store-ptr! requires (store! (mem base offset) val)");
        auto [base, offset] = evalMem(os, ctx, target, expr.elements[1], "", indent);
        Operand val = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        std::string valName = expr.elements[2].isSymbol() ? expr.elements[2].text : "";
        target.emitStore(os, indent, ctx, TypeKind::Ptr, base, offset, val, valName);
        return Operand{};
    }},
    {"store-u8!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("store-u8! requires (store! (mem base offset) val)");
        auto [base, offset] = evalMem(os, ctx, target, expr.elements[1], "", indent);
        Operand val = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        std::string valName = expr.elements[2].isSymbol() ? expr.elements[2].text : "";
        target.emitStore(os, indent, ctx, TypeKind::U8, base, offset, val, valName);
        return Operand{};
    }},

    // Control flow & Branches
    {"branch-if-not-zero!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("branch-if-not-zero! requires val and label");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand labelOp = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        target.emitBranchIfZero(os, indent, ctx, true, val, labelOp.text);
        return Operand{};
    }},
    {"branch-if-zero!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("branch-if-zero! requires val and label");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand labelOp = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        target.emitBranchIfZero(os, indent, ctx, false, val, labelOp.text);
        return Operand{};
    }},
    {"branch-if-equal!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&, const std::string& indent) -> Operand {
        if (expr.elements.size() != 4)
            throw std::runtime_error("branch-if-equal! requires val, expected, and label");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand expected = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        Operand labelOp = evalExpr(os, ctx, target, expr.elements[3], "", indent);
        target.emitBranchIfEqual(os, indent, ctx, false, val, expected, labelOp.text);
        return Operand{};
    }},
    {"branch-if-not-equal!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&, const std::string& indent) -> Operand {
        if (expr.elements.size() != 4)
            throw std::runtime_error("branch-if-not-equal! requires val, expected, and label");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand expected = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        Operand labelOp = evalExpr(os, ctx, target, expr.elements[3], "", indent);
        target.emitBranchIfEqual(os, indent, ctx, true, val, expected, labelOp.text);
        return Operand{};
    }},
    {"branch-if-negative!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("branch-if-negative! requires val and label");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand labelOp = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        target.emitBranchIfNegative(os, indent, ctx, val, labelOp.text);
        return Operand{};
    }},
    {"truncate-uint", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("truncate-uint requires 1 argument");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        return target.emitTruncateUint(os, indent, ctx, val, destReg);
    }},
    {"is-equal", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("is-equal requires 2 arguments");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand expected = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        return target.emitIsEqual(os, indent, ctx, val, expected, destReg);
    }},

    // Byteswap & Bitwise
    {"byteswap-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("byteswap-32 requires 1 argument");
        return target.emitByteSwap(os, indent, ctx, 32, getRawOp(ctx, expr.elements[1]), destReg);
    }},
    {"byteswap-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("byteswap-64 requires 1 argument");
        return target.emitByteSwap(os, indent, ctx, 64, getRawOp(ctx, expr.elements[1]), destReg);
    }},
    {"byteswap", evalExplicitWidthRequired},

    {"bitnot-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("bitnot-32 requires 1 argument");
        return target.emitBitwiseNot(os, indent, ctx, 32, getRawOp(ctx, expr.elements[1]), destReg);
    }},
    {"bitwise-not-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("bitwise-not-32 requires 1 argument");
        return target.emitBitwiseNot(os, indent, ctx, 32, getRawOp(ctx, expr.elements[1]), destReg);
    }},
    {"bitnot-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("bitnot-64 requires 1 argument");
        return target.emitBitwiseNot(os, indent, ctx, 64, getRawOp(ctx, expr.elements[1]), destReg);
    }},
    {"bitwise-not-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("bitwise-not-64 requires 1 argument");
        return target.emitBitwiseNot(os, indent, ctx, 64, getRawOp(ctx, expr.elements[1]), destReg);
    }},
    {"bitnot", evalExplicitWidthRequired},
    {"bitwise-not", evalExplicitWidthRequired},

    {"bitand-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "and", 32);
    }},
    {"bitwise-and-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "and", 32);
    }},
    {"bitand-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "and", 64);
    }},
    {"bitwise-and-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "and", 64);
    }},

    {"bitor-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "or", 32);
    }},
    {"bitwise-or-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "or", 32);
    }},
    {"bitor-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "or", 64);
    }},
    {"bitwise-or-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "or", 64);
    }},

    {"bitxor-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "xor", 32);
    }},
    {"bitwise-xor-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "xor", 32);
    }},
    {"bitxor-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "xor", 64);
    }},
    {"bitwise-xor-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalBitwiseBinOp(os, ctx, target, expr, destReg, indent, "xor", 64);
    }},

    {"bitand", evalExplicitWidthRequired},
    {"bitwise-and", evalExplicitWidthRequired},
    {"bitor", evalExplicitWidthRequired},
    {"bitwise-or", evalExplicitWidthRequired},
    {"bitxor", evalExplicitWidthRequired},
    {"bitwise-xor", evalExplicitWidthRequired},

    {"mul-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("mul-32 requires 2 arguments");
        return target.emitMul(os, indent, ctx, 32, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg);
    }},
    {"mul-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("mul-64 requires 2 arguments");
        return target.emitMul(os, indent, ctx, 64, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg);
    }},
    {"mul", evalExplicitWidthRequired},

    // Control flow structures
    {"target-case", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            const auto& branch = expr.elements[i];
            if (!branch.isList() || branch.elements.empty())
                throw std::runtime_error("target-case branch must be [:arch-name expr...]");
            std::string arch = branch.elements[0].text;
            if (!arch.empty() && arch[0] == ':')
                arch = arch.substr(1);
            for (char& c : arch)
                c = tolower(c);
            std::string targetArch = target.archName();
            for (char& c : targetArch)
                c = tolower(c);
            if (arch == targetArch)
            {
                for (size_t j = 1; j < branch.elements.size(); ++j)
                {
                    evalExpr(os, ctx, target, branch.elements[j], destReg, indent);
                }
                return Operand{};
            }
        }
        return Operand{};
    }},
    {"cond", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            const auto& clause = expr.elements[i];
            if (!clause.isList() || clause.elements.empty())
                throw std::runtime_error("cond clause must be [test expr...]");
            const auto& test = clause.elements[0];
            bool isElse = (test.text == "else");
            if (isElse)
            {
                os << indent << "else\n";
                os << indent << "{\n";
            }
            else
            {
                std::string condStr = evalPredicate(ctx, test, destReg);
                std::string prefix = (i == 1) ? "if (" : "else if (";
                os << indent << prefix << condStr << ")\n";
                os << indent << "{\n";
            }
            for (size_t j = 1; j < clause.elements.size(); ++j)
            {
                evalExpr(os, ctx, target, clause.elements[j], destReg, indent + "    ");
            }
            os << indent << "}\n";
        }
        return Operand{};
    }},
    {"if", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() < 3 || expr.elements.size() > 4)
            throw std::runtime_error("if requires condition, then-branch, and optional else-branch");
        std::string condStr = evalPredicate(ctx, expr.elements[1], destReg);
        os << indent << "if (" << condStr << ")\n";
        os << indent << "{\n";
        evalExpr(os, ctx, target, expr.elements[2], destReg, indent + "    ");
        os << indent << "}\n";
        if (expr.elements.size() == 4)
        {
            os << indent << "else\n";
            os << indent << "{\n";
            evalExpr(os, ctx, target, expr.elements[3], destReg, indent + "    ");
            os << indent << "}\n";
        }
        return Operand{};
    }},
    {"begin", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            evalExpr(os, ctx, target, expr.elements[i], destReg, indent);
        }
        return Operand{};
    }},
    {"unsupported!", [](std::ostream& os, EvalContext&, TargetBuilder& target, const SExpr&, const std::string&, const std::string& indent) -> Operand {
        target.emitUnsupported(os, indent);
        return Operand{};
    }},

    // Fine-grained target builders
    {"lea-reg-32!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("lea-reg-32! requires base and offset");
        target.emitLeaReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, 32);
        return Operand{};
    }},
    {"lea-reg-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("lea-reg-64! requires base and offset");
        target.emitLeaReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, 64);
        return Operand{};
    }},
    {"lea-imm-32!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("lea-imm-32! requires base and offset");
        target.emitLeaImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, 32);
        return Operand{};
    }},
    {"lea-imm-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("lea-imm-64! requires base and offset");
        target.emitLeaImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, 64);
        return Operand{};
    }},
    {"lea-sub-32!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("lea-sub-32! requires base and offset");
        target.emitLeaSub(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, 32);
        return Operand{};
    }},
    {"lea-sub-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("lea-sub-64! requires base and offset");
        target.emitLeaSub(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, 64);
        return Operand{};
    }},

    {"add-reg-32!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalAddReg(os, ctx, target, expr, destReg, indent, 32);
    }},
    {"add-reg-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalAddReg(os, ctx, target, expr, destReg, indent, 64);
    }},
    {"add-imm-32!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalAddImm(os, ctx, target, expr, destReg, indent, 32);
    }},
    {"add-imm-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalAddImm(os, ctx, target, expr, destReg, indent, 64);
    }},

    {"sub-reg-32!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalSubReg(os, ctx, target, expr, destReg, indent, 32);
    }},
    {"sub-reg-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalSubReg(os, ctx, target, expr, destReg, indent, 64);
    }},
    {"sub-imm-32!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalSubImm(os, ctx, target, expr, destReg, indent, 32);
    }},
    {"sub-imm-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        return evalSubImm(os, ctx, target, expr, destReg, indent, 64);
    }},

    {"inc!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr&, const std::string& destReg, const std::string& indent) -> Operand {
        target.emitInc(os, indent, ctx, destReg);
        return Operand{};
    }},
    {"mov-reg!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("mov-reg! requires 1 argument");
        target.emitMovReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg);
        return Operand{};
    }},
    {"mov-imm-32!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("mov-imm-32! requires 1 argument");
        target.emitMovImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, 32);
        return Operand{};
    }},
    {"mov-imm-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("mov-imm-64! requires 1 argument");
        target.emitMovImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, 64);
        return Operand{};
    }},

    {"add-tmp-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("add-tmp-64! requires 1 argument");
        target.emitAddTmp64(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg);
        return Operand{};
    }},
    {"sub-tmp-64!", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 2)
            throw std::runtime_error("sub-tmp-64! requires 1 argument");
        target.emitSubTmp64(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg);
        return Operand{};
    }},

    // Legacy / High-level
    {"add-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("add-32 requires 2 arguments");
        return target.emitAddInt(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg);
    }},
    {"add-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("add-64 requires 2 arguments");
        return target.emitAddInt64(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg);
    }},
    {"add", evalExplicitWidthRequired},
    {"add-int", evalExplicitWidthRequired},
    {"add-int64", evalExplicitWidthRequired},

    {"sub-32", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("sub-32 requires 2 arguments");
        return target.emitSubInt(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg);
    }},
    {"sub-64", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() != 3)
            throw std::runtime_error("sub-64 requires 2 arguments");
        return target.emitSubInt64(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg);
    }},
    {"sub", evalExplicitWidthRequired},
    {"sub-int", evalExplicitWidthRequired},
    {"sub-int64", evalExplicitWidthRequired},

    // Bindings
    {"let", [](std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent) -> Operand {
        if (expr.elements.size() < 3)
            throw std::runtime_error("let requires bindings and body");

        const SExpr& bindings = expr.elements[1];
        if (!bindings.isList())
            throw std::runtime_error("let bindings must be a list");

        auto oldEnv = ctx.env;

        for (const auto& binding : bindings.elements)
        {
            if (!binding.isList() || binding.elements.size() != 2)
                throw std::runtime_error("let binding must be [var expr]");

            std::string varName = binding.elements[0].text;
            const SExpr& valExpr = binding.elements[1];

            if (!destReg.empty() && bindings.elements.size() == 1)
            {
                // Tail destination register reuse (inst.regA64 / inst.regX64)
                Operand op = evalExpr(os, ctx, target, valExpr, destReg, indent);
                ctx.env[varName] = Operand::makeReg(op.type, destReg);
            }
            else
            {
                // Lazy evaluation:
                // Stores OperandKind::Mem or OperandKind::Reg in ctx.env[varName]
                ctx.env[varName] = evalExpr(os, ctx, target, valExpr, "", indent);
            }
        }

        Operand result;
        for (size_t i = 2; i < expr.elements.size(); ++i)
        {
            std::string d = (i == expr.elements.size() - 1) ? destReg : "";
            result = evalExpr(os, ctx, target, expr.elements[i], d, indent);
        }

        ctx.env = oldEnv;
        return result;
    }},
};

Operand evalExpr(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg, const std::string& indent)
{
    if (expr.isSymbol())
    {
        auto it = ctx.env.find(expr.text);
        if (it != ctx.env.end())
            return it->second;
        return Operand::makeReg(TypeKind::Ptr, expr.text);
    }
    if (expr.isNumber())
    {
        return Operand::makeImm(TypeKind::I32, std::to_string(expr.intValue));
    }
    if (expr.isKeyword())
    {
        return Operand::makeImm(TypeKind::I32, ctx.resolveOffset(expr.text));
    }

    if (!expr.isList())
        throw std::runtime_error("Unexpected SExpr kind in evalExpr");

    const std::string& head = expr.elements[0].text;

    auto it = kExprTrampolines.find(head);
    if (it != kExprTrampolines.end())
        return it->second(os, ctx, target, expr, destReg, indent);

    if (head.rfind("%produce-", 0) == 0)
    {
        bool isReuse = head.find("-reuse") != std::string::npos;
        TypeKind tk = TypeKind::Ptr;
        if (head.find("i32") != std::string::npos)
            tk = TypeKind::I32;
        else if (head.find("u32") != std::string::npos)
            tk = TypeKind::U32;
        else if (head.find("i64") != std::string::npos)
            tk = TypeKind::I64;
        else if (head.find("u64") != std::string::npos)
            tk = TypeKind::U64;

        std::vector<std::string> rawOps;
        const SExpr* innerExpr = nullptr;
        if (isReuse)
        {
            if (expr.elements.size() != 3)
                throw std::runtime_error(head + " requires [reuse-param] and [expr]");
            const auto& reuseElem = expr.elements[1];
            if (reuseElem.isList())
            {
                for (const auto& item : reuseElem.elements)
                {
                    auto it = ctx.rawOps.find(item.text);
                    if (it == ctx.rawOps.end())
                        throw std::runtime_error("Unknown reuse parameter: " + item.text);
                    rawOps.push_back(it->second);
                }
            }
            else
            {
                auto it = ctx.rawOps.find(reuseElem.text);
                if (it == ctx.rawOps.end())
                    throw std::runtime_error("Unknown reuse parameter: " + reuseElem.text);
                rawOps.push_back(it->second);
            }
            innerExpr = &expr.elements[2];
        }
        else
        {
            if (expr.elements.size() != 2)
                throw std::runtime_error(head + " requires [expr]");
            innerExpr = &expr.elements[1];
        }

        target.emitAllocProduce(os, indent, tk, isReuse, rawOps);
        evalExpr(os, ctx, target, *innerExpr, target.destRegVar(), indent);
        return Operand::makeReg(tk, target.destRegVar());
    }

    throw std::runtime_error("Unsupported expression: " + head);
}

std::pair<std::string, std::string> evalMem(std::ostream& os, EvalContext& ctx, TargetBuilder& target, const SExpr& memExpr, const std::string& destReg, const std::string& indent)
{
    if (!memExpr.isList("mem") || memExpr.elements.size() != 3)
        throw std::runtime_error("Expected (mem base offset)");

    const SExpr& baseExpr = memExpr.elements[1];
    Operand baseOp = evalExpr(os, ctx, target, baseExpr, destReg, indent);
    std::string base = target.resolveMemBase(os, indent, ctx, baseExpr, baseOp, destReg);
    std::string offset = ctx.resolveOffset(memExpr.elements[2].text);
    return {base, offset};
}

void compileRules(std::ostream& os, TargetBuilder& target, const std::vector<Rule>& rules)
{
    target.emitPreamble(os);

    static const char* kOpNames[] = {"OP_A(inst)", "OP_B(inst)", "OP_C(inst)", "OP_D(inst)"};

    for (const auto& rule : rules)
    {
        std::string fnName = "lower_" + toUpperIdent(rule.name);
        os << "inline void " << fnName << "(" << target.loweringClass() << "& lowering, IrInst& inst, uint32_t index, const IrBlock& next)\n";
        os << "{\n";
        os << "    auto& build = lowering.build;\n";
        os << "    auto& regs = lowering.regs;\n";
        os << "    (void)build;\n";
        os << "    (void)regs;\n";
        os << "    (void)inst;\n";
        os << "    (void)index;\n";
        os << "    (void)next;\n\n";

        EvalContext ctx;

        for (size_t i = 0; i < rule.params.size(); ++i)
        {
            if (i >= 4)
                throw std::runtime_error("Too many parameters in rule: " + rule.name);

            const auto& p = rule.params[i];
            std::string opName = kOpNames[i];
            ctx.rawOps[p.name] = opName;

            TypeKind tk = parseTypeKind(p.type);
            if (p.type == "uint")
                ctx.env[p.name] = Operand::makeReg(TypeKind::U32, "lowering.uintOp(" + opName + ")");
            else if (p.type == "int")
                ctx.env[p.name] = Operand::makeReg(TypeKind::I32, "lowering.intOp(" + opName + ")");
            else if (p.type == "reg")
                ctx.env[p.name] = Operand::makeReg(TypeKind::Ptr, "lowering.regOp(" + opName + ")");
            else if (p.type == "tag")
                ctx.env[p.name] = Operand::makeReg(TypeKind::Tag, "lowering.tagOp(" + opName + ")");
            else if (p.type == "label" || p.type == "op")
                ctx.env[p.name] = Operand::makeReg(TypeKind::Void, opName);
            else
                throw std::runtime_error("Unknown parameter type: " + p.type);
        }

        std::ostringstream body;
        for (const auto& expr : rule.body)
        {
            evalExpr(body, ctx, target, expr, "", "    ");
        }

        for (const auto& decl : ctx.decls)
        {
            os << "    " << decl << "\n";
        }
        if (!ctx.decls.empty() && !body.str().empty())
        {
            os << "\n";
        }
        os << body.str();

        os << "}\n\n";
    }

    target.emitPostamble(os);
}

} // namespace Luwu::Dsl
