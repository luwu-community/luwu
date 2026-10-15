// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "RuleCompiler.h"
#include "CompileError.h"
#include "Scope.h"

#include <sstream>
#include <string_view>

namespace Luwu::Dsl
{

static std::string getOpText(EvalContext& ctx, const SExpr& elem)
{
    if (elem.isList())
        throw CompileError(elem, "Expected symbol or literal operand, got list expression");
    if (elem.isSymbol())
    {
        const Operand* op = ctx.lookup(elem.text);
        if (op && !op->text.empty())
            return op->text;
        if (elem.text.find("inst") != std::string::npos)
            ctx.use("inst");
    }
    return elem.text;
}

static std::string getRawOpText(EvalContext& ctx, const SExpr& elem)
{
    if (elem.isList())
        throw CompileError(elem, "Expected symbol or literal operand, got list expression");
    if (elem.isSymbol())
    {
        const Operand* op = ctx.lookup(elem.text);
        if (op)
        {
            if (!op->rawOp.empty())
                return op->rawOp;
            if (!op->text.empty())
                return op->text;
        }
        if (elem.text.find("inst") != std::string::npos)
            ctx.use("inst");
    }
    return elem.text;
}

enum class Precedence : int
{
    Lowest = 0,
    Or = 1,          // ||
    And = 2,         // &&
    Relational = 3,  // ==, !=, <, <=, >, >=
    Primary = 4      // constant?, inst?, not, atoms
};

struct PredicateResult
{
    std::string text;
    Precedence prec = Precedence::Primary;
};

static std::string formatChild(const PredicateResult& child, Precedence parentPrec)
{
    if (child.prec < parentPrec)
        return "(" + child.text + ")";
    return child.text;
}

class TargetArchGuard
{
public:
    TargetArchGuard(EvalContext& ctx, std::string newArch)
        : ctx_(ctx), prevArch_(ctx.currentTargetArch)
    {
        ctx_.currentTargetArch = std::move(newArch);
    }

    ~TargetArchGuard()
    {
        ctx_.currentTargetArch = std::move(prevArch_);
    }

private:
    EvalContext& ctx_;
    std::string prevArch_;
};

static void validateArchTarget(const EvalContext& ctx, const SExpr& expr, const std::string& head, const char* constructKind)
{
    size_t colonPos = head.find(':');
    if (colonPos == std::string::npos)
        return;

    std::string opArch = head.substr(0, colonPos);
    for (char& c : opArch)
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

    if (ctx.currentTargetArch.empty())
        throw CompileError(expr, std::string("Architecture-dependent ") + constructKind + " '" + head + "' must be inside a target-case block");

    if (opArch != ctx.currentTargetArch)
        throw CompileError(expr, std::string("Architecture-dependent ") + constructKind + " '" + head + "' cannot be used inside target-case branch for :" + ctx.currentTargetArch);
}

static void requireArity(const SExpr& expr, size_t expectedArgs, const std::string& customMsg = "")
{
    if (expr.elements.size() != expectedArgs + 1)
    {
        if (!customMsg.empty())
            throw CompileError(expr, customMsg);
        std::string name = expr.elements.empty() ? "" : expr.elements[0].text;
        throw CompileError(expr, name + " requires " + std::to_string(expectedArgs) +
                                (expectedArgs == 1 ? " argument" : " arguments"));
    }
}

static PredicateResult evalPredicate(EvalContext& ctx, const SExpr& expr, const std::string& destReg);

static std::string evalPredicateArg(EvalContext& ctx, const SExpr& elem, const std::string& destReg)
{
    if (elem.isList())
        return evalPredicate(ctx, elem, destReg).text;
    std::string text = getOpText(ctx, elem);
    if (text.find("inst") != std::string::npos)
        ctx.use("inst");
    return text;
}

using PredicateTrampoline = PredicateResult (*)(EvalContext& ctx, const SExpr& expr, const std::string& destReg);

static const std::unordered_map<std::string_view, PredicateTrampoline> kPredicateTrampolines = {
    {"constant?", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "constant? requires 1 argument");
        return {getRawOpText(ctx, expr.elements[1]) + ".kind == IrOpKind::Constant", Precedence::Primary};
    }},
    {"inst?", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "inst? requires 1 argument");
        return {getRawOpText(ctx, expr.elements[1]) + ".kind == IrOpKind::Inst", Precedence::Primary};
    }},
    {"reused?", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (!destReg.empty() && destReg.find("inst") != std::string::npos)
            ctx.use("inst");
        if (expr.elements.size() == 3)
        {
            std::string opText = getRawOpText(ctx, expr.elements[2]);
            return {"(" + opText + ".kind == IrOpKind::Inst && " + evalPredicateArg(ctx, expr.elements[1], destReg) + " == lowering.regOp(" + opText + "))", Precedence::Relational};
        }
        if (expr.elements.size() == 2)
        {
            if (destReg.empty())
                throw CompileError(expr, "1-argument reused? requires an active destination register context");
            std::string opText = getRawOpText(ctx, expr.elements[1]);
            return {"(" + opText + ".kind == IrOpKind::Inst && " + destReg + " == lowering.regOp(" + opText + "))", Precedence::Relational};
        }
        throw CompileError(expr, "reused? requires 1 or 2 arguments");
    }},
    {"not-reused?", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (!destReg.empty() && destReg.find("inst") != std::string::npos)
            ctx.use("inst");
        if (expr.elements.size() == 3)
        {
            std::string opText = getRawOpText(ctx, expr.elements[2]);
            return {"(" + opText + ".kind != IrOpKind::Inst || " + evalPredicateArg(ctx, expr.elements[1], destReg) + " != lowering.regOp(" + opText + "))", Precedence::Relational};
        }
        if (expr.elements.size() == 2)
        {
            if (destReg.empty())
                throw CompileError(expr, "1-argument not-reused? requires an active destination register context");
            std::string opText = getRawOpText(ctx, expr.elements[1]);
            return {"(" + opText + ".kind != IrOpKind::Inst || " + destReg + " != lowering.regOp(" + opText + "))", Precedence::Relational};
        }
        throw CompileError(expr, "not-reused? requires 1 or 2 arguments");
    }},
    {"=", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() != 3)
            throw CompileError(expr, "= requires 2 arguments");
        std::string lhs = evalPredicateArg(ctx, expr.elements[1], destReg);
        std::string rhs = evalPredicateArg(ctx, expr.elements[2], destReg);
        return {lhs + " == " + rhs, Precedence::Relational};
    }},
    {"!=", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() != 3)
            throw CompileError(expr, "!= requires 2 arguments");
        std::string lhs = evalPredicateArg(ctx, expr.elements[1], destReg);
        std::string rhs = evalPredicateArg(ctx, expr.elements[2], destReg);
        return {lhs + " != " + rhs, Precedence::Relational};
    }},
    {"<", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() != 3)
            throw CompileError(expr, "< requires 2 arguments");
        std::string lhs = evalPredicateArg(ctx, expr.elements[1], destReg);
        std::string rhs = evalPredicateArg(ctx, expr.elements[2], destReg);
        return {lhs + " < " + rhs, Precedence::Relational};
    }},
    {"<=", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() != 3)
            throw CompileError(expr, "<= requires 2 arguments");
        std::string lhs = evalPredicateArg(ctx, expr.elements[1], destReg);
        std::string rhs = evalPredicateArg(ctx, expr.elements[2], destReg);
        return {lhs + " <= " + rhs, Precedence::Relational};
    }},
    {">", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() != 3)
            throw CompileError(expr, "> requires 2 arguments");
        std::string lhs = evalPredicateArg(ctx, expr.elements[1], destReg);
        std::string rhs = evalPredicateArg(ctx, expr.elements[2], destReg);
        return {lhs + " > " + rhs, Precedence::Relational};
    }},
    {">=", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() != 3)
            throw CompileError(expr, ">= requires 2 arguments");
        std::string lhs = evalPredicateArg(ctx, expr.elements[1], destReg);
        std::string rhs = evalPredicateArg(ctx, expr.elements[2], destReg);
        return {lhs + " >= " + rhs, Precedence::Relational};
    }},
    {"int-val", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "int-val requires 1 argument");
        return {"lowering.intOp(" + getRawOpText(ctx, expr.elements[1]) + ")", Precedence::Primary};
    }},
    {"int64-val", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "int64-val requires 1 argument");
        return {"lowering.int64Op(" + getRawOpText(ctx, expr.elements[1]) + ")", Precedence::Primary};
    }},
    {"fits-i32?", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "fits-i32? requires 1 argument");
        std::string raw = getRawOpText(ctx, expr.elements[1]);
        return {"lowering.int64Op(" + raw + ") >= INT32_MIN && lowering.int64Op(" + raw + ") <= INT32_MAX", Precedence::And};
    }},
    {"a64:fits-imm-32?", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "a64:fits-imm-32? requires 1 argument");
        return {"unsigned(lowering.intOp(" + getRawOpText(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate", Precedence::Relational};
    }},
    {"a64:fits-imm-64?", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "a64:fits-imm-64? requires 1 argument");
        return {"uint64_t(lowering.int64Op(" + getRawOpText(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate", Precedence::Relational};
    }},
    {"a64:fits-mask-32?", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "a64:fits-mask-32? requires 1 argument");
        return {"AssemblyBuilderA64::isMaskSupported(unsigned(lowering.intOp(" + getRawOpText(ctx, expr.elements[1]) + ")))", Precedence::Primary};
    }},
    {"a64:fits-mask?", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "a64:fits-mask? requires 1 argument");
        return {"AssemblyBuilderA64::isMaskSupported(unsigned(lowering.intOp(" + getRawOpText(ctx, expr.elements[1]) + ")))", Precedence::Primary};
    }},
    {"fflag", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        requireArity(expr, 1);
        std::string flag = expr.elements[1].text;
        if (!flag.empty() && flag[0] == ':')
            flag = flag.substr(1);
        if (flag.empty())
            throw CompileError(expr, "fflag requires a valid flag name");
        if (flag.rfind("FFlag::", 0) == 0)
        {
            ctx.fflags.insert(flag.substr(7));
            return {flag, Precedence::Primary};
        }
        if (flag.rfind("DFFlag::", 0) == 0)
        {
            ctx.dfflags.insert(flag.substr(8));
            return {flag, Precedence::Primary};
        }
        ctx.fflags.insert(flag);
        return {"FFlag::" + flag, Precedence::Primary};
    }},
    {"fflag?", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        return kPredicateTrampolines.at("fflag")(ctx, expr, destReg);
    }},
    {"dfflag", [](EvalContext& ctx, const SExpr& expr, const std::string&) -> PredicateResult {
        requireArity(expr, 1);
        std::string flag = expr.elements[1].text;
        if (!flag.empty() && flag[0] == ':')
            flag = flag.substr(1);
        if (flag.empty())
            throw CompileError(expr, "dfflag requires a valid flag name");
        if (flag.rfind("DFFlag::", 0) == 0)
        {
            ctx.dfflags.insert(flag.substr(8));
            return {flag, Precedence::Primary};
        }
        ctx.dfflags.insert(flag);
        return {"DFFlag::" + flag, Precedence::Primary};
    }},
    {"dfflag?", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        return kPredicateTrampolines.at("dfflag")(ctx, expr, destReg);
    }},
    {"and", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() < 2)
            throw CompileError(expr, "and requires at least 1 argument");
        std::string res;
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            if (i > 1) res += " && ";
            PredicateResult child = evalPredicate(ctx, expr.elements[i], destReg);
            res += formatChild(child, Precedence::And);
        }
        return {res, Precedence::And};
    }},
    {"or", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() < 2)
            throw CompileError(expr, "or requires at least 1 argument");
        std::string res;
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            if (i > 1) res += " || ";
            PredicateResult child = evalPredicate(ctx, expr.elements[i], destReg);
            res += formatChild(child, Precedence::Or);
        }
        return {res, Precedence::Or};
    }},
    {"not", [](EvalContext& ctx, const SExpr& expr, const std::string& destReg) -> PredicateResult {
        if (expr.elements.size() != 2)
            throw CompileError(expr, "not requires 1 argument");
        return {"!(" + evalPredicate(ctx, expr.elements[1], destReg).text + ")", Precedence::Primary};
    }}
};

static PredicateResult evalPredicate(EvalContext& ctx, const SExpr& expr, const std::string& destReg)
{
    if (!expr.isList() || expr.elements.empty())
        throw CompileError(expr, "Predicate must be a non-empty list");

    const std::string& head = expr.elements[0].text;
    validateArchTarget(ctx, expr, head, "predicate");

    auto it = kPredicateTrampolines.find(head);
    if (it != kPredicateTrampolines.end())
        return it->second(ctx, expr, destReg);

    throw CompileError(expr, "Unknown predicate: " + head);
}

using ExprTrampoline = Operand (*)(CodeWriter& out, EvalContext& ctx, TargetBuilder& target,
                                  const SExpr& expr, const std::string& destReg);


static const std::unordered_map<std::string_view, ExprTrampoline> kExprTrampolines = {
    // Memory & Loads
    {"code-address", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 1);
        Operand offsetOp = evalExpr(out, ctx, target, expr.elements[1], "");
        return target.emitCodeAddress(out, ctx, offsetOp.text, destReg);
    }},
    {"load-ptr", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 1);
        auto [base, offset] = evalMem(out, ctx, target, expr.elements[1], destReg);
        return target.emitLoad(out, ctx, TypeKind::Ptr, base, offset, destReg);
    }},
    {"load-u8", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 1);
        auto [base, offset] = evalMem(out, ctx, target, expr.elements[1], destReg);
        return target.emitLoad(out, ctx, TypeKind::U8, base, offset, destReg);
    }},
    {"load-u32", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 1);
        auto [base, offset] = evalMem(out, ctx, target, expr.elements[1], destReg);
        return target.emitLoad(out, ctx, TypeKind::U32, base, offset, destReg);
    }},
    {"sign-extend-8", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 1);
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        return target.emitSignExtend(out, ctx, 8, val, destReg);
    }},
    {"sign-extend-16", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 1);
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        return target.emitSignExtend(out, ctx, 16, val, destReg);
    }},
    {"store-ptr!", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 2, "store-ptr! requires (store! (mem base offset) val)");
        auto [base, offset] = evalMem(out, ctx, target, expr.elements[1], "");
        Operand val = evalExpr(out, ctx, target, expr.elements[2], "");
        std::string valName = expr.elements[2].isSymbol() ? expr.elements[2].text : "";
        target.emitStore(out, ctx, TypeKind::Ptr, base, offset, val, valName);
        return Operand{};
    }},
    {"store-u8!", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 2, "store-u8! requires (store! (mem base offset) val)");
        auto [base, offset] = evalMem(out, ctx, target, expr.elements[1], "");
        Operand val = evalExpr(out, ctx, target, expr.elements[2], "");
        std::string valName = expr.elements[2].isSymbol() ? expr.elements[2].text : "";
        target.emitStore(out, ctx, TypeKind::U8, base, offset, val, valName);
        return Operand{};
    }},

    // Control flow & Branches
    {"branch-if-not-zero!", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 2, "branch-if-not-zero! requires val and label");
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        Operand labelOp = evalExpr(out, ctx, target, expr.elements[2], "");
        target.emitBranchIfZero(out, ctx, true, val, labelOp.text);
        return Operand{};
    }},
    {"branch-if-zero!", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 2, "branch-if-zero! requires val and label");
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        Operand labelOp = evalExpr(out, ctx, target, expr.elements[2], "");
        target.emitBranchIfZero(out, ctx, false, val, labelOp.text);
        return Operand{};
    }},
    {"branch-if-equal!", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 3, "branch-if-equal! requires val, expected, and label");
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        Operand expected = evalExpr(out, ctx, target, expr.elements[2], "");
        Operand labelOp = evalExpr(out, ctx, target, expr.elements[3], "");
        target.emitBranchIfEqual(out, ctx, false, val, expected, labelOp.text);
        return Operand{};
    }},
    {"branch-if-not-equal!", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 3, "branch-if-not-equal! requires val, expected, and label");
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        Operand expected = evalExpr(out, ctx, target, expr.elements[2], "");
        Operand labelOp = evalExpr(out, ctx, target, expr.elements[3], "");
        target.emitBranchIfEqual(out, ctx, true, val, expected, labelOp.text);
        return Operand{};
    }},
    {"branch-if-negative!", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 2, "branch-if-negative! requires val and label");
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        Operand labelOp = evalExpr(out, ctx, target, expr.elements[2], "");
        target.emitBranchIfNegative(out, ctx, val, labelOp.text);
        return Operand{};
    }},
    {"truncate-uint", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 1);
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        return target.emitTruncateUint(out, ctx, val, destReg);
    }},
    {"is-equal", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 2);
        Operand val = evalExpr(out, ctx, target, expr.elements[1], "");
        Operand expected = evalExpr(out, ctx, target, expr.elements[2], "");
        return target.emitIsEqual(out, ctx, val, expected, destReg);
    }},

    // Control flow structures
    {"target-case", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        std::string targetArch = target.archName();
        for (char& c : targetArch)
            c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            const auto& branch = expr.elements[i];
            if (!branch.isList() || branch.elements.empty())
                throw CompileError(branch, "target-case branch must be [:arch-name expr...]");
            std::string arch = branch.elements[0].text;
            if (!arch.empty() && arch[0] == ':')
                arch = arch.substr(1);
            for (char& c : arch)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

            if (arch == targetArch)
            {
                TargetArchGuard guard(ctx, arch);
                for (size_t j = 1; j < branch.elements.size(); ++j)
                {
                    evalExpr(out, ctx, target, branch.elements[j], destReg);
                }
                return Operand{};
            }
        }
        return Operand{};
    }},
    {"cond", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            const auto& clause = expr.elements[i];
            if (!clause.isList() || clause.elements.empty())
                throw CompileError(clause, "cond clause must be [test expr...]");
            const auto& test = clause.elements[0];
            bool isElse = (test.text == "else");
            std::string header;
            if (isElse)
            {
                if (i == 1)
                    throw CompileError(clause, "'else' cannot be the first clause in cond");
                if (i != expr.elements.size() - 1)
                    throw CompileError(clause, "'else' must be the last clause in cond");
                header = "else";
            }
            else
            {
                std::string condStr = evalPredicate(ctx, test, destReg).text;
                std::string prefix = (i == 1) ? "if (" : "else if (";
                header = prefix + condStr + ")";
            }
            out.block(header, [&]() {
                for (size_t j = 1; j < clause.elements.size(); ++j)
                {
                    evalExpr(out, ctx, target, clause.elements[j], destReg);
                }
            });
        }
        return Operand{};
    }},
    {"if", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        if (expr.elements.size() < 3 || expr.elements.size() > 4)
            throw CompileError(expr, "if requires condition, then-branch, and optional else-branch");
        std::string condStr = evalPredicate(ctx, expr.elements[1], destReg).text;
        out.block("if (" + condStr + ")", [&]() {
            evalExpr(out, ctx, target, expr.elements[2], destReg);
        });
        if (expr.elements.size() == 4)
        {
            out.block("else", [&]() {
                evalExpr(out, ctx, target, expr.elements[3], destReg);
            });
        }
        return Operand{};
    }},
    {"begin", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        Operand result;
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            std::string d = (i == expr.elements.size() - 1) ? destReg : "";
            result = evalExpr(out, ctx, target, expr.elements[i], d);
        }
        return result;
    }},
    {"unsupported!", [](CodeWriter& out, EvalContext&, TargetBuilder& target, const SExpr&, const std::string&) -> Operand {
        target.emitUnsupported(out);
        return Operand{};
    }},

    {"alloc-temp", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        requireArity(expr, 1, "alloc-temp requires 1 argument: (alloc-temp <type>)");
        std::string typeStr = expr.elements[1].text;
        if (!typeStr.empty() && typeStr[0] == ':')
            typeStr = typeStr.substr(1);
        TypeKind tk = parseTypeKind(typeStr);
        if (tk == TypeKind::Void)
            throw CompileError(expr.elements[1], "Unknown type for alloc-temp: " + expr.elements[1].text);
        std::string varName = destReg.empty() ? ctx.nextTemp("tmp") : destReg;
        return target.emitAllocTemp(out, ctx, tk, varName);
    }},

    // Bindings
    {"let", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        if (expr.elements.size() < 3)
            throw CompileError(expr, "let requires bindings and body");

        const SExpr& bindings = expr.elements[1];
        if (!bindings.isList())
            throw CompileError(bindings, "let bindings must be a list");

        ScopeGuard guard(ctx.scope);

        for (const auto& binding : bindings.elements)
        {
            if (!binding.isList() || binding.elements.size() != 2)
                throw CompileError(binding, "let binding must be [var expr]");

            std::string varName = binding.elements[0].text;
            const SExpr& valExpr = binding.elements[1];

            if (valExpr.isList() && !valExpr.elements.empty() && valExpr.elements[0].text == "alloc-temp")
            {
                Operand op = evalExpr(out, ctx, target, valExpr, toCppIdent(varName));
                ctx.bind(varName, op);
            }
            else if (!destReg.empty() && bindings.elements.size() == 1)
            {
                // Tail destination register reuse (inst.regA64 / inst.regX64)
                Operand op = evalExpr(out, ctx, target, valExpr, destReg);
                ctx.bind(varName, op);
            }
            else
            {
                // Lazy evaluation:
                // Stores OperandKind::Mem or OperandKind::Reg in ctx.scope
                ctx.bind(varName, evalExpr(out, ctx, target, valExpr, ""));
            }
        }

        Operand result;
        for (size_t i = 2; i < expr.elements.size(); ++i)
        {
            std::string d = (i == expr.elements.size() - 1) ? destReg : "";
            result = evalExpr(out, ctx, target, expr.elements[i], d);
        }

        return result;
    }},

    {"fflag", [](CodeWriter&, EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 1);
        std::string flag = expr.elements[1].text;
        if (!flag.empty() && flag[0] == ':')
            flag = flag.substr(1);
        if (flag.empty())
            throw CompileError(expr, "fflag requires a valid flag name");
        if (flag.rfind("FFlag::", 0) == 0)
        {
            ctx.fflags.insert(flag.substr(7));
            return Operand::makeReg(TypeKind::I32, flag);
        }
        if (flag.rfind("DFFlag::", 0) == 0)
        {
            ctx.dfflags.insert(flag.substr(8));
            return Operand::makeReg(TypeKind::I32, flag);
        }
        ctx.fflags.insert(flag);
        return Operand::makeReg(TypeKind::I32, "FFlag::" + flag);
    }},
    {"fflag?", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        return kExprTrampolines.at("fflag")(out, ctx, target, expr, destReg);
    }},
    {"dfflag", [](CodeWriter&, EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> Operand {
        requireArity(expr, 1);
        std::string flag = expr.elements[1].text;
        if (!flag.empty() && flag[0] == ':')
            flag = flag.substr(1);
        if (flag.empty())
            throw CompileError(expr, "dfflag requires a valid flag name");
        if (flag.rfind("DFFlag::", 0) == 0)
        {
            ctx.dfflags.insert(flag.substr(8));
            return Operand::makeReg(TypeKind::I32, flag);
        }
        ctx.dfflags.insert(flag);
        return Operand::makeReg(TypeKind::I32, "DFFlag::" + flag);
    }},
    {"dfflag?", [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
        return kExprTrampolines.at("dfflag")(out, ctx, target, expr, destReg);
    }},
};

static Operand evalProduceForm(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& head)
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
    else if (head != "%produce-ptr" && head != "%produce-ptr-reuse")
        throw CompileError(expr, "Unknown produce form: " + head);

    std::string resName;
    std::vector<std::string> rawOps;
    size_t bodyStartIndex = 0;

    if (isReuse)
    {
        if (expr.elements.size() < 4)
            throw CompileError(expr, head + " requires [ident], [reuse-candidates], and body");
        if (!expr.elements[1].isSymbol())
            throw CompileError(expr.elements[1], head + " first argument must be an identifier name (e.g. res)");

        resName = expr.elements[1].text;
        const auto& reuseElem = expr.elements[2];
        if (reuseElem.isList())
        {
            for (const auto& item : reuseElem.elements)
            {
                const Operand* op = ctx.lookup(item.text);
                if (!op || op->rawOp.empty())
                    throw CompileError(item, "Unknown reuse parameter: " + item.text);
                rawOps.push_back(op->rawOp);
            }
        }
        else
        {
            const Operand* op = ctx.lookup(reuseElem.text);
            if (!op || op->rawOp.empty())
                throw CompileError(reuseElem, "Unknown reuse parameter: " + reuseElem.text);
            rawOps.push_back(op->rawOp);
        }
        bodyStartIndex = 3;
    }
    else
    {
        if (expr.elements.size() < 3)
            throw CompileError(expr, head + " requires [ident] and body");
        if (!expr.elements[1].isSymbol())
            throw CompileError(expr.elements[1], head + " first argument must be an identifier name (e.g. res)");

        resName = expr.elements[1].text;
        bodyStartIndex = 2;
    }

    target.emitAllocProduce(out, ctx, tk, isReuse, rawOps);
    ctx.use("inst");

    ScopeGuard guard(ctx.scope);
    ctx.bind(resName, Operand::makeReg(tk, target.destRegVar()));

    for (size_t i = bodyStartIndex; i < expr.elements.size(); ++i)
    {
        evalExpr(out, ctx, target, expr.elements[i], target.destRegVar());
    }

    return Operand::makeReg(tk, target.destRegVar());
}

Operand evalExpr(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg)
{
    if (expr.isSymbol())
    {
        const Operand* op = ctx.lookup(expr.text);
        if (op)
            return *op;
        if (expr.text.find("inst") != std::string::npos)
            ctx.use("inst");
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

    if (!expr.isList() || expr.elements.empty())
        throw CompileError(expr, "Expected non-empty list in evalExpr");

    const std::string& head = expr.elements[0].text;

    size_t colonPos = head.find(':');
    if (colonPos != std::string::npos)
    {
        validateArchTarget(ctx, expr, head, "operation");

        std::vector<std::string> args;
        for (size_t i = 1; i < expr.elements.size(); ++i)
            args.push_back(getOpText(ctx, expr.elements[i]));

        if (!target.emitArchOp(out, ctx, head, args))
            throw CompileError(expr, "Unknown architecture-dependent operation: " + head);

        return Operand{};
    }

    auto it = kExprTrampolines.find(head);
    if (it != kExprTrampolines.end())
        return it->second(out, ctx, target, expr, destReg);

    if (head.rfind("%produce-", 0) == 0)
        return evalProduceForm(out, ctx, target, expr, head);

    throw CompileError(expr, "Unsupported expression: " + head);
}

std::pair<std::string, std::string> evalMem(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& memExpr, const std::string& destReg)
{
    if (!memExpr.isList("mem") || memExpr.elements.size() != 3)
        throw CompileError(memExpr, "Expected (mem base offset)");

    const SExpr& baseExpr = memExpr.elements[1];
    Operand baseOp = evalExpr(out, ctx, target, baseExpr, destReg);
    std::string base = target.resolveMemBase(out, ctx, baseExpr, baseOp, destReg);
    std::string offset = ctx.resolveOffset(memExpr.elements[2].text);
    return {base, offset};
}

void compileRules(std::ostream& os, TargetBuilder& target, const std::vector<Rule>& rules)
{
    CodeWriter writer(os);

    static const char* kOpNames[] = {"OP_A(inst)", "OP_B(inst)", "OP_C(inst)", "OP_D(inst)"};

    struct CompiledRule
    {
        std::string fnName;
        std::vector<std::string> decls;
        std::string body;
        std::set<std::string> usedSymbols;
    };

    std::set<std::string> allFFlags;
    std::set<std::string> allDFFlags;
    std::vector<CompiledRule> compiledRules;

    for (const auto& rule : rules)
    {
        std::string fnName = "lower_" + toUpperIdent(rule.name);
        StringCodeWriter bodyWriter(1);

        EvalContext ctx;
        Scope rootScope;
        ctx.scope = &rootScope;
        ctx.writer = &bodyWriter;

        for (size_t i = 0; i < rule.params.size(); ++i)
        {
            if (i >= 4)
                throw CompileError(rule.params[i].loc, "Too many parameters in rule: " + rule.name);

            const auto& p = rule.params[i];
            std::string opName = kOpNames[i];

            if (p.type == "uint")
                ctx.bind(p.name, Operand::makeReg(TypeKind::U32, "lowering.uintOp(" + opName + ")", opName));
            else if (p.type == "int")
                ctx.bind(p.name, Operand::makeReg(TypeKind::I32, "lowering.intOp(" + opName + ")", opName));
            else if (p.type == "reg")
                ctx.bind(p.name, Operand::makeReg(TypeKind::Ptr, "lowering.regOp(" + opName + ")", opName));
            else if (p.type == "tag")
                ctx.bind(p.name, Operand::makeReg(TypeKind::Tag, "lowering.tagOp(" + opName + ")", opName));
            else if (p.type == "label" || p.type == "op")
                ctx.bind(p.name, Operand::makeReg(TypeKind::Void, opName, opName));
            else
                throw CompileError(p.loc, "Unknown parameter type: " + p.type);
        }

        for (const auto& expr : rule.body)
        {
            evalExpr(bodyWriter, ctx, target, expr, "");
        }

        allFFlags.insert(ctx.fflags.begin(), ctx.fflags.end());
        allDFFlags.insert(ctx.dfflags.begin(), ctx.dfflags.end());
        compiledRules.push_back({std::move(fnName), bodyWriter.decls(), bodyWriter.str(), std::move(ctx.usedSymbols)});
    }

    target.emitPreamble(writer, {allFFlags.begin(), allFFlags.end()}, {allDFFlags.begin(), allDFFlags.end()});

    for (const auto& cr : compiledRules)
    {
        writer.block("inline void " + cr.fnName + "(" + target.loweringClass() + "& lowering, IrInst& inst, uint32_t index, const IrBlock& next)", [&]() {
            bool hasHeader = false;
            if (cr.usedSymbols.count("build"))
            {
                writer.line("auto& build = lowering.build;");
                hasHeader = true;
            }
            if (cr.usedSymbols.count("regs"))
            {
                writer.line("auto& regs = lowering.regs;");
                hasHeader = true;
            }
            if (!cr.usedSymbols.count("inst"))
            {
                writer.line("(void)inst;");
                hasHeader = true;
            }
            if (!cr.usedSymbols.count("index"))
            {
                writer.line("(void)index;");
                hasHeader = true;
            }
            if (!cr.usedSymbols.count("next"))
            {
                writer.line("(void)next;");
                hasHeader = true;
            }

            if (hasHeader && (!cr.decls.empty() || !cr.body.empty()))
            {
                writer.line();
            }

            for (const auto& decl : cr.decls)
            {
                writer.line(decl);
            }
            if (!cr.decls.empty() && !cr.body.empty())
            {
                writer.line();
            }
            writer.raw(cr.body);
        });

        writer.line();
    }

    target.emitPostamble(writer);
}

} // namespace Luwu::Dsl
