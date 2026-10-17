// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "RuleCompiler.h"

#include <algorithm>
#include "OpTemplates.h"
#include "CompileError.h"
#include "Scope.h"

#include <algorithm>
#include <functional>
#include <regex>
#include <sstream>
#include <string_view>

namespace Luwu::Dsl
{

static std::string getOpText(EvalContext& ctx, const SExpr& elem)
{
    if (elem.isList())
        throw CompileError(elem, "Expected symbol or literal operand, got list expression");
    if (elem.isKeyword())
        return ctx.resolveOffset(elem.text);
    if (elem.isSymbol())
    {
        const Operand* op = ctx.lookup(elem.text);
        if (op && !op->text.empty())
            return op->text;
        if (!op)
            return ctx.requireGlobal(elem);
    }
    return elem.text;
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

// Opens a C++ block (header may be empty for a bare scope) and tracks the locals declared directly inside it
template <typename Fn>
static void emitBlock(CodeWriter& out, EvalContext& ctx, const std::string& header, Fn&& fn)
{
    ctx.blockLocals.emplace_back();
    if (header.empty())
        out.scope(fn);
    else
        out.block(header, fn);
    ctx.blockLocals.pop_back();
}

static std::string docLine(const std::string& text)
{
    return text.empty() ? "//" : "// " + text;
}

// Writes the doc comments attached to `holder` around the C++ that `emit` generates: leading docs as lines before it,
// a trailing doc at the end of the line when exactly one line was generated (otherwise it leads as well)
template <typename Fn>
static Operand withDocs(CodeWriter& out, const SExpr& holder, Fn&& emit)
{
    for (const auto& doc : holder.docs)
        out.line(docLine(doc));

    if (!holder.trailingDoc)
        return emit(out);

    StringCodeWriter temp(out.indentLevel());
    Operand result = emit(temp);
    for (const auto& decl : temp.decls())
        out.addDecl(decl);

    std::string text = temp.str();
    if (std::count(text.begin(), text.end(), '\n') == 1)
    {
        text.pop_back();
        out.raw(text + " " + docLine(*holder.trailingDoc) + "\n");
    }
    else
    {
        out.line(docLine(*holder.trailingDoc));
        out.raw(text);
    }
    return result;
}

// Generated code without // comments (outside string literals), for scanning which locals the code uses
static std::string stripLineComments(const std::string& code)
{
    std::string result;
    result.reserve(code.size());
    for (size_t i = 0; i < code.size(); ++i)
    {
        char c = code[i];
        if (c == '"' || c == '\'')
        {
            size_t j = i + 1;
            while (j < code.size() && code[j] != c && code[j] != '\n')
                j += code[j] == '\\' ? 2 : 1;
            result.append(code, i, std::min(j + 1, code.size()) - i);
            i = j;
        }
        else if (c == '/' && i + 1 < code.size() && code[i + 1] == '/')
        {
            while (i < code.size() && code[i] != '\n')
                ++i;
            if (i < code.size())
                result += '\n';
        }
        else
        {
            result += c;
        }
    }
    return result;
}

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

static PredicateResult evalPredicate(EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg);

static const TemplateOpSpec* findTemplateOp(const TargetBuilder& target, const std::string& name)
{
    if (const TemplateOpSpec* spec = findCoreTemplateOp(name))
        return spec;
    return target.findTemplateOp(name);
}

// Formats one side of a binary arithmetic expression, parenthesized only where C++ precedence requires it. The right
// side is also parenthesized at equal precedence so the generated grouping (and integer promotion order) follows the DSL.
static std::string formatArithOperand(const Operand& op, Precedence parent, bool isRight)
{
    bool needsParens = isRight ? op.prec <= parent : op.prec < parent;
    return needsParens ? "(" + op.text + ")" : op.text;
}

static std::string evalPredicateArg(EvalContext& ctx, TargetBuilder& target, const SExpr& elem, const std::string& destReg)
{
    if (elem.isList())
        return formatChild(evalPredicate(ctx, target, elem, destReg), Precedence::Primary);
    std::string text = getOpText(ctx, elem);
    if (text.find("inst") != std::string::npos)
        ctx.use("inst");
    return text;
}

using PredicateTrampoline = PredicateResult (*)(EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg);

struct PredicateTrampolineSpec
{
    const char* syntax;
    const char* desc;
    PredicateTrampoline fn;
};

static const std::unordered_map<std::string_view, PredicateTrampolineSpec> kPredicateTrampolines = {
    {"constant?", {
        "(constant? <op>)",
        "Test if IR operand is a compile-time constant",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "constant? requires 1 argument");
            return {getRawOpText(ctx, expr.elements[1]) + ".kind == IrOpKind::Constant", Precedence::Primary};
        }
    }},
    {"inst?", {
        "(inst? <op>)",
        "Test if IR operand is an instruction result",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "inst? requires 1 argument");
            return {getRawOpText(ctx, expr.elements[1]) + ".kind == IrOpKind::Inst", Precedence::Primary};
        }
    }},
    {"undef?", {
        "(undef? <op>)",
        "Test if IR operand is undefined",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "undef? requires 1 argument");
            return {getRawOpText(ctx, expr.elements[1]) + ".kind == IrOpKind::Undef", Precedence::Primary};
        }
    }},
    {"kind?", {
        "(kind? <op> :<IrOpKind>)",
        "Test the kind of an IR operand (e.g. :VmReg, :VmConst, :Inst, :Constant, :Undef)",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            requireArity(expr, 2);
            if (!expr.elements[2].isKeyword())
                throw CompileError(expr.elements[2], "kind? expects an IrOpKind keyword such as :VmReg");
            return {getRawOpText(ctx, expr.elements[1]) + ".kind == IrOpKind::" + expr.elements[2].text.substr(1), Precedence::Relational};
        }
    }},
    {"same-op?", {
        "(same-op? <op> <op>)",
        "Test if two IR operands refer to the same value",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            requireArity(expr, 2);
            return {getRawOpText(ctx, expr.elements[1]) + " == " + getRawOpText(ctx, expr.elements[2]), Precedence::Relational};
        }
    }},
    {"true?", {
        "(true? <value>)",
        "Test a boolean value (e.g. a let-bound result of a value template)",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            requireArity(expr, 1);
            return {getOpText(ctx, expr.elements[1]), Precedence::Primary};
        }
    }},
    {"has-op?", {
        "(has-op? <param>)",
        "Test if an optional instruction operand is present (HAS_OP_X(inst))",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            requireArity(expr, 1);
            std::string raw = getRawOpText(ctx, expr.elements[1]);
            if (raw.rfind("OP_", 0) != 0)
                throw CompileError(expr.elements[1], "has-op? requires a rule parameter");
            ctx.use("inst");
            return {"HAS_" + raw, Precedence::Primary};
        }
    }},
    {"vm-exit?", {
        "(vm-exit? <op>)",
        "Test if IR operand is VM exit point",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "vm-exit? requires 1 argument");
            return {getRawOpText(ctx, expr.elements[1]) + ".kind == IrOpKind::VmExit", Precedence::Primary};
        }
    }},
    {"fallthrough?", {
        "(fallthrough? <blockOp>)",
        "Test if target block is next fallthrough block",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "fallthrough? requires 1 argument");
            ctx.use("next");
            return {"lowering.isFallthroughBlock(lowering.blockOp(" + getRawOpText(ctx, expr.elements[1]) + "), next)", Precedence::Primary};
        }
    }},
    {"cmd-is?", {
        "(cmd-is? <instOp> :<cmd>)",
        "Test if instruction operand matches IrCmd::<cmd>",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            requireArity(expr, 2);
            std::string cmd = expr.elements[2].text;
            if (!cmd.empty() && cmd[0] == ':')
                cmd = cmd.substr(1);
            return {"lowering.function.instOp(" + getRawOpText(ctx, expr.elements[1]) + ").cmd == IrCmd::" + cmd, Precedence::Relational};
        }
    }},
    {"reused?", {
        "(reused? [<dest>] <op>)",
        "Test if destination register was reused from operand register",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (!destReg.empty() && destReg.find("inst") != std::string::npos)
                ctx.use("inst");
            if (expr.elements.size() == 3)
            {
                std::string opText = getRawOpText(ctx, expr.elements[2]);
                return {"(" + opText + ".kind == IrOpKind::Inst && " + evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == lowering.regOp(" + opText + "))", Precedence::Relational};
            }
            if (expr.elements.size() == 2)
            {
                if (destReg.empty())
                    throw CompileError(expr, "1-argument reused? requires an active destination register context");
                std::string opText = getRawOpText(ctx, expr.elements[1]);
                return {"(" + opText + ".kind == IrOpKind::Inst && " + destReg + " == lowering.regOp(" + opText + "))", Precedence::Relational};
            }
            throw CompileError(expr, "reused? requires 1 or 2 arguments");
        }
    }},
    {"not-reused?", {
        "(not-reused? [<dest>] <op>)",
        "Test if destination register was not reused from operand register",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (!destReg.empty() && destReg.find("inst") != std::string::npos)
                ctx.use("inst");
            if (expr.elements.size() == 3)
            {
                std::string opText = getRawOpText(ctx, expr.elements[2]);
                return {"(" + opText + ".kind != IrOpKind::Inst || " + evalPredicateArg(ctx, target, expr.elements[1], destReg) + " != lowering.regOp(" + opText + "))", Precedence::Relational};
            }
            if (expr.elements.size() == 2)
            {
                if (destReg.empty())
                    throw CompileError(expr, "1-argument not-reused? requires an active destination register context");
                std::string opText = getRawOpText(ctx, expr.elements[1]);
                return {"(" + opText + ".kind != IrOpKind::Inst || " + destReg + " != lowering.regOp(" + opText + "))", Precedence::Relational};
            }
            throw CompileError(expr, "not-reused? requires 1 or 2 arguments");
        }
    }},
    {"=", {
        "(= <lhs> <rhs>)",
        "Structural equality comparison",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() != 3)
                throw CompileError(expr, "= requires 2 arguments");
            std::string lhs = evalPredicateArg(ctx, target, expr.elements[1], destReg);
            std::string rhs = evalPredicateArg(ctx, target, expr.elements[2], destReg);
            return {lhs + " == " + rhs, Precedence::Relational};
        }
    }},
    {"==", {
        "(== <lhs> <rhs>)",
        "Structural equality comparison (alias)",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() != 3)
                throw CompileError(expr, "== requires 2 arguments");
            std::string lhs = evalPredicateArg(ctx, target, expr.elements[1], destReg);
            std::string rhs = evalPredicateArg(ctx, target, expr.elements[2], destReg);
            return {lhs + " == " + rhs, Precedence::Relational};
        }
    }},
    {"!=", {
        "(!= <lhs> <rhs>)",
        "Structural inequality comparison",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() != 3)
                throw CompileError(expr, "!= requires 2 arguments");
            std::string lhs = evalPredicateArg(ctx, target, expr.elements[1], destReg);
            std::string rhs = evalPredicateArg(ctx, target, expr.elements[2], destReg);
            return {lhs + " != " + rhs, Precedence::Relational};
        }
    }},
    {"<", {
        "(< <lhs> <rhs>)",
        "Signed numerical less-than comparison",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() != 3)
                throw CompileError(expr, "< requires 2 arguments");
            std::string lhs = evalPredicateArg(ctx, target, expr.elements[1], destReg);
            std::string rhs = evalPredicateArg(ctx, target, expr.elements[2], destReg);
            return {lhs + " < " + rhs, Precedence::Relational};
        }
    }},
    {"<=", {
        "(<= <lhs> <rhs>)",
        "Signed numerical less-than-or-equal comparison",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() != 3)
                throw CompileError(expr, "<= requires 2 arguments");
            std::string lhs = evalPredicateArg(ctx, target, expr.elements[1], destReg);
            std::string rhs = evalPredicateArg(ctx, target, expr.elements[2], destReg);
            return {lhs + " <= " + rhs, Precedence::Relational};
        }
    }},
    {">", {
        "(> <lhs> <rhs>)",
        "Signed numerical greater-than comparison",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() != 3)
                throw CompileError(expr, "> requires 2 arguments");
            std::string lhs = evalPredicateArg(ctx, target, expr.elements[1], destReg);
            std::string rhs = evalPredicateArg(ctx, target, expr.elements[2], destReg);
            return {lhs + " > " + rhs, Precedence::Relational};
        }
    }},
    {">=", {
        "(>= <lhs> <rhs>)",
        "Signed numerical greater-than-or-equal comparison",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() != 3)
                throw CompileError(expr, ">= requires 2 arguments");
            std::string lhs = evalPredicateArg(ctx, target, expr.elements[1], destReg);
            std::string rhs = evalPredicateArg(ctx, target, expr.elements[2], destReg);
            return {lhs + " >= " + rhs, Precedence::Relational};
        }
    }},
    {"fits-i32?", {
        "(fits-i32? <op>)",
        "Test if 64-bit integer fits in signed 32-bit range",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "fits-i32? requires 1 argument");
            std::string raw = getRawOpText(ctx, expr.elements[1]);
            return {"lowering.int64Op(" + raw + ") >= INT32_MIN && lowering.int64Op(" + raw + ") <= INT32_MAX", Precedence::And};
        }
    }},
    // IR Condition predicates
    {"ir-cond-eq?", {
        "(ir-cond-eq? <cond>)",
        "Test if IrCondition is Equal",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::Equal", Precedence::Relational};
        }
    }},
    {"ir-cond-ne?", {
        "(ir-cond-ne? <cond>)",
        "Test if IrCondition is NotEqual",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::NotEqual", Precedence::Relational};
        }
    }},
    {"ir-cond-neq?", {
        "(ir-cond-neq? <cond>)",
        "Test if IrCondition is NotEqual (alias)",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::NotEqual", Precedence::Relational};
        }
    }},
    {"ir-cond-lt?", {
        "(ir-cond-lt? <cond>)",
        "Test if IrCondition is Less",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::Less", Precedence::Relational};
        }
    }},
    {"ir-cond-nlt?", {
        "(ir-cond-nlt? <cond>)",
        "Test if IrCondition is NotLess",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::NotLess", Precedence::Relational};
        }
    }},
    {"ir-cond-le?", {
        "(ir-cond-le? <cond>)",
        "Test if IrCondition is LessEqual",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::LessEqual", Precedence::Relational};
        }
    }},
    {"ir-cond-nle?", {
        "(ir-cond-nle? <cond>)",
        "Test if IrCondition is NotLessEqual",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::NotLessEqual", Precedence::Relational};
        }
    }},
    {"ir-cond-gt?", {
        "(ir-cond-gt? <cond>)",
        "Test if IrCondition is Greater",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::Greater", Precedence::Relational};
        }
    }},
    {"ir-cond-ngt?", {
        "(ir-cond-ngt? <cond>)",
        "Test if IrCondition is NotGreater",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::NotGreater", Precedence::Relational};
        }
    }},
    {"ir-cond-ge?", {
        "(ir-cond-ge? <cond>)",
        "Test if IrCondition is GreaterEqual",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::GreaterEqual", Precedence::Relational};
        }
    }},
    {"ir-cond-nge?", {
        "(ir-cond-nge? <cond>)",
        "Test if IrCondition is NotGreaterEqual",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::NotGreaterEqual", Precedence::Relational};
        }
    }},
    {"ir-cond-ult?", {
        "(ir-cond-ult? <cond>)",
        "Test if IrCondition is UnsignedLess",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::UnsignedLess", Precedence::Relational};
        }
    }},
    {"ir-cond-ule?", {
        "(ir-cond-ule? <cond>)",
        "Test if IrCondition is UnsignedLessEqual",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::UnsignedLessEqual", Precedence::Relational};
        }
    }},
    {"ir-cond-ugt?", {
        "(ir-cond-ugt? <cond>)",
        "Test if IrCondition is UnsignedGreater",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::UnsignedGreater", Precedence::Relational};
        }
    }},
    {"ir-cond-uge?", {
        "(ir-cond-uge? <cond>)",
        "Test if IrCondition is UnsignedGreaterEqual",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            requireArity(expr, 1);
            return {evalPredicateArg(ctx, target, expr.elements[1], destReg) + " == IrCondition::UnsignedGreaterEqual", Precedence::Relational};
        }
    }},
    {"fflag", {
        "(fflag :<flag-name>)",
        "Test if fast flag is enabled at compile/lowering time",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
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
        }
    }},
    {"fflag?", {
        "(fflag? :<flag-name>)",
        "Test if fast flag is enabled (alias)",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            return kPredicateTrampolines.at("fflag").fn(ctx, target, expr, destReg);
        }
    }},
    {"dfflag", {
        "(dfflag :<flag-name>)",
        "Test if dynamic fast flag is enabled at compile/lowering time",
        [](EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> PredicateResult {
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
        }
    }},
    {"dfflag?", {
        "(dfflag? :<flag-name>)",
        "Test if dynamic fast flag is enabled (alias)",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            return kPredicateTrampolines.at("dfflag").fn(ctx, target, expr, destReg);
        }
    }},
    {"and", {
        "(and <pred>...)",
        "Logical short-circuit AND conjunction",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() < 2)
                throw CompileError(expr, "and requires at least 1 argument");
            std::string res;
            for (size_t i = 1; i < expr.elements.size(); ++i)
            {
                if (i > 1) res += " && ";
                PredicateResult child = evalPredicate(ctx, target, expr.elements[i], destReg);
                res += formatChild(child, Precedence::And);
            }
            return {res, Precedence::And};
        }
    }},
    {"or", {
        "(or <pred>...)",
        "Logical short-circuit OR disjunction",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() < 2)
                throw CompileError(expr, "or requires at least 1 argument");
            std::string res;
            for (size_t i = 1; i < expr.elements.size(); ++i)
            {
                if (i > 1) res += " || ";
                PredicateResult child = evalPredicate(ctx, target, expr.elements[i], destReg);
                res += formatChild(child, Precedence::Or);
            }
            return {res, Precedence::Or};
        }
    }},
    {"not", {
        "(not <pred>)",
        "Logical boolean negation",
        [](EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> PredicateResult {
            if (expr.elements.size() != 2)
                throw CompileError(expr, "not requires 1 argument");
            return {"!(" + evalPredicate(ctx, target, expr.elements[1], destReg).text + ")", Precedence::Primary};
        }
    }}
};

// Evaluates a template operation: statements are written out, values are returned as an expression operand
static std::optional<Operand> evalTemplateOp(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr)
{
    const std::string& head = expr.elements[0].text;
    const TemplateOpSpec* spec = findTemplateOp(target, head);
    if (!spec)
        return std::nullopt;

    size_t arity = templateArity(spec->fmt);
    if (expr.elements.size() - 1 != arity)
        throw CompileError(expr, head + " requires " + std::to_string(arity) + (arity == 1 ? " argument" : " arguments") + ", got " +
                                     std::to_string(expr.elements.size() - 1));

    std::vector<Operand> args;
    for (size_t i = 1; i < expr.elements.size(); ++i)
        args.push_back(evalExpr(out, ctx, target, expr.elements[i], ""));

    std::string text = expandOpTemplate(ctx, target, expr, spec->fmt, args);
    if (spec->isValue)
        return Operand::makeImm(TypeKind::Void, text);

    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find('\n', start);
        out.line(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return Operand{};
}

static PredicateResult evalPredicate(EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg)
{
    if (!expr.isList() || expr.elements.empty())
        throw CompileError(expr, "Predicate must be a non-empty list");

    const std::string& head = expr.elements[0].text;
    validateArchTarget(ctx, expr, head, "predicate");

    auto it = kPredicateTrampolines.find(head);
    if (it != kPredicateTrampolines.end())
        return it->second.fn(ctx, target, expr, destReg);

    if (auto archPred = target.evalArchPredicate(ctx, expr, destReg))
        return *archPred;

    // Value templates (e.g. (vm-reg a)) and arithmetic can appear wherever a predicate or comparison operand can
    const TemplateOpSpec* spec = findTemplateOp(target, head);
    if ((spec && spec->isValue) || head == "+" || head == "-" || head == "*" || head == "sizeof")
    {
        Operand value = evalExpr(*ctx.writer, ctx, target, expr, "");
        return {value.text, value.prec};
    }

    throw CompileError(expr, "Unknown predicate: " + head);
}

static TypeKind parseRegType(const SExpr& typeExpr, const std::string& formName)
{
    TypeKind tk = parseTypeKind(typeExpr.text);
    if (!typeExpr.isKeyword() || tk == TypeKind::Void || tk == TypeKind::Any || tk == TypeKind::Tag || tk == TypeKind::Cond)
        throw CompileError(typeExpr, "Unknown register type for " + formName + ": " + typeExpr.text);
    return tk;
}

// (alloc-result <type>) / (alloc-result-reuse <type> <cand-or-[cands]>): allocates the instruction's result register
static Operand evalAllocResult(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, bool isReuse)
{
    const std::string& head = expr.elements[0].text;
    if (expr.elements.size() != (isReuse ? 3u : 2u))
        throw CompileError(expr, isReuse ? head + " requires a type and reuse candidates: (" + head + " <type> <cand-or-[cands]>)"
                                         : head + " requires a type: (" + head + " <type>)");

    TypeKind tk = parseRegType(expr.elements[1], head);

    std::vector<std::string> rawOps;
    if (isReuse)
    {
        const SExpr& reuseElem = expr.elements[2];
        std::vector<const SExpr*> cands;
        if (reuseElem.isList())
        {
            if (reuseElem.elements.empty())
                throw CompileError(reuseElem, head + " requires at least one reuse candidate");
            for (const auto& item : reuseElem.elements)
                cands.push_back(&item);
        }
        else
        {
            cands.push_back(&reuseElem);
        }

        for (const SExpr* item : cands)
        {
            const Operand* op = item->isSymbol() ? ctx.lookup(item->text) : nullptr;
            if (!op || op->rawOp.empty())
                throw CompileError(*item, "Reuse candidate must be a rule parameter: " + item->text);
            rawOps.push_back(op->rawOp);
        }
    }

    target.emitAllocProduce(out, ctx, tk, isReuse, rawOps);
    ctx.use("inst");
    return Operand::makeReg(tk, target.destRegVar());
}

// [var (if <pred> <a> <b>)] in a let: declares var as whichever register the predicate selects; each branch must be
// an alloc-temp or a plain operand, since a C++ conditional expression cannot contain statements
static Operand evalConditionalBinding(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& varName)
{
    if (expr.elements.size() != 4)
        throw CompileError(expr, "if used as a let binding value requires both a then and an else branch");

    std::string cond = evalPredicate(ctx, target, expr.elements[1], "").text;
    TypeKind type = TypeKind::Void;

    // Allocating the result register one of two ways: the allocations are statements, the binding is the result register
    auto isResultAlloc = [](const SExpr& e) {
        return e.isList("alloc-result") || e.isList("alloc-result-reuse");
    };
    if (isResultAlloc(expr.elements[2]) && isResultAlloc(expr.elements[3]))
    {
        Operand result;
        emitBlock(out, ctx, "if (" + cond + ")", [&]() {
            result = evalExpr(out, ctx, target, expr.elements[2], "");
        });
        emitBlock(out, ctx, "else", [&]() {
            evalExpr(out, ctx, target, expr.elements[3], "");
        });
        return result;
    }

    bool allocates = false;
    int holderCount = 0;

    // Each branch is an alloc-temp, an operand/value, or a nested if (a chain of conditions); `taken` is the full
    // condition under which the branch is selected, for targets that allocate conditional temps up front
    std::function<std::string(const SExpr&, const std::string&)> branch = [&](const SExpr& e, const std::string& taken) -> std::string {
        if (e.isList("alloc-temp"))
        {
            requireArity(e, 1, "alloc-temp requires 1 argument: (alloc-temp <type>)");
            TypeKind tk = parseRegType(e.elements[1], "alloc-temp");
            type = tk;
            allocates = true;
            std::string holder = holderCount++ == 0 ? varName : varName + std::to_string(holderCount);
            return target.conditionalTempExpr(out, ctx, tk, taken, holder);
        }
        if (e.isList("if"))
        {
            if (e.elements.size() != 4)
                throw CompileError(e, "if used as a let binding value requires both a then and an else branch");
            std::string inner = evalPredicate(ctx, target, e.elements[1], "").text;
            std::string thenStr = branch(e.elements[2], "(" + taken + ") && (" + inner + ")");
            std::string elseStr = branch(e.elements[3], "(" + taken + ") && !(" + inner + ")");
            return "(" + inner + " ? " + thenStr + " : " + elseStr + ")";
        }
        bool isOperand = !e.isList() || e.isList("mem");
        if (!isOperand)
        {
            const TemplateOpSpec* spec = findTemplateOp(target, e.elements[0].text);
            isOperand = spec && spec->isValue;
        }
        if (!isOperand)
            throw CompileError(e, "if branches in a let binding must be an alloc-temp, an operand, a value or a nested if");

        Operand op = evalExpr(out, ctx, target, e, "");
        if (type == TypeKind::Void)
            type = op.type;
        return op.kind == OperandKind::Mem ? target.formatMemOperand(ctx, op, "") : target.formatRegOperand(op);
    };

    std::string thenStr = branch(expr.elements[2], cond);
    std::string elseStr = branch(expr.elements[3], "!(" + cond + ")");

    // Choosing between registers declares a register local; choosing between values (offsets, indices) uses auto
    out.line((allocates ? target.regVarType() : "auto") + " " + varName + " = " + cond + " ? " + thenStr + " : " + elseStr + ";");
    return Operand::makeReg(type, varName);
}

using ExprTrampoline = Operand (*)(CodeWriter& out, EvalContext& ctx, TargetBuilder& target,
                                  const SExpr& expr, const std::string& destReg);

struct ExprTrampolineSpec
{
    const char* syntax;
    const char* desc;
    ExprTrampoline fn;
};

static const std::unordered_map<std::string_view, ExprTrampolineSpec> kExprTrampolines = {
    // Memory & Loads
    {"mem", {
        "(mem <base-reg> <offset> [:<type>])",
        "Construct memory operand address [base + offset] (offset may be :Struct.member)",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            if (expr.elements.size() != 3 && expr.elements.size() != 4)
                throw CompileError(expr, "mem requires base and offset: (mem base offset [type])");
            const SExpr& baseExpr = expr.elements[1];
            Operand baseOp = evalExpr(out, ctx, target, baseExpr, destReg);
            std::string base = target.resolveMemBase(out, ctx, baseExpr, baseOp, destReg);
            const SExpr& offsetExpr = expr.elements[2];
            std::string offset;
            if (offsetExpr.isList())
                offset = evalExpr(out, ctx, target, offsetExpr, "").text;
            else if (offsetExpr.isSymbol() && ctx.lookup(offsetExpr.text))
                offset = ctx.lookup(offsetExpr.text)->text;
            else
                offset = ctx.resolveOffset(offsetExpr.text);
            TypeKind type = TypeKind::Void;
            if (expr.elements.size() == 4)
            {
                std::string typeStr = expr.elements[3].text;
                if (!typeStr.empty() && typeStr[0] == ':')
                    typeStr = typeStr.substr(1);
                type = parseTypeKind(typeStr);
                if (type == TypeKind::Void)
                    throw CompileError(expr.elements[3], "Unknown memory type: " + expr.elements[3].text);
            }
            return Operand::makeMem(type, base, offset);
        }
    }},
    {"sizeof", {
        "(sizeof :<type>)",
        "Compute sizeof(<type>) as a compile-time constant",
        [](CodeWriter&, EvalContext&, TargetBuilder&, const SExpr& expr, const std::string&) -> Operand {
            requireArity(expr, 1, "sizeof requires 1 argument: (sizeof :Type)");
            std::string typeName = expr.elements[1].text;
            if (!typeName.empty() && typeName[0] == ':')
                typeName = typeName.substr(1);
            return Operand::makeImm(TypeKind::U64, "sizeof(" + typeName + ")");
        }
    }},
    {"+", {
        "(+ <lhs> <rhs>)",
        "Compute constant addition expression",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
            requireArity(expr, 2, "+ requires 2 arguments");
            Operand a = evalExpr(out, ctx, target, expr.elements[1], "");
            Operand b = evalExpr(out, ctx, target, expr.elements[2], "");
            return Operand::makeImm(TypeKind::U64,
                formatArithOperand(a, Precedence::Additive, false) + " + " + formatArithOperand(b, Precedence::Additive, true),
                Precedence::Additive);
        }
    }},
    {"-", {
        "(- <lhs> <rhs>)",
        "Compute constant subtraction expression",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
            requireArity(expr, 2, "- requires 2 arguments");
            Operand a = evalExpr(out, ctx, target, expr.elements[1], "");
            Operand b = evalExpr(out, ctx, target, expr.elements[2], "");
            return Operand::makeImm(TypeKind::U64,
                formatArithOperand(a, Precedence::Additive, false) + " - " + formatArithOperand(b, Precedence::Additive, true),
                Precedence::Additive);
        }
    }},
    {"*", {
        "(* <lhs> <rhs>)",
        "Compute constant multiplication expression",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
            requireArity(expr, 2, "* requires 2 arguments");
            Operand a = evalExpr(out, ctx, target, expr.elements[1], "");
            Operand b = evalExpr(out, ctx, target, expr.elements[2], "");
            return Operand::makeImm(TypeKind::U64,
                formatArithOperand(a, Precedence::Multiplicative, false) + " * " + formatArithOperand(b, Precedence::Multiplicative, true),
                Precedence::Multiplicative);
        }
    }},
    {"jump-or-fallthrough!", {
        "(jump-or-fallthrough! <blockOp>)",
        "Branch to block if not fallthrough",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
            requireArity(expr, 1, "jump-or-fallthrough! requires block");
            Operand blockOp = evalExpr(out, ctx, target, expr.elements[1], "");
            target.emitJumpOrFallthrough(out, ctx, blockOp.text);
            return Operand{};
        }
    }},

    // Control flow structures
    {"target-case", {
        "(target-case [:<arch> <body>...]...)",
        "Branch on target architecture (:a64, :x64) for architecture-specific lowering",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            std::string targetArch = target.archName();
            for (char& c : targetArch)
                c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

            const SExpr* selected = nullptr;
            std::set<std::string> seenArchs;
            for (size_t i = 1; i < expr.elements.size(); ++i)
            {
                const auto& branch = expr.elements[i];
                if (!branch.isList() || branch.elements.empty() || !branch.elements[0].isKeyword())
                    throw CompileError(branch, "target-case branch must be [:arch-name expr...]");
                std::string arch = branch.elements[0].text.substr(1);
                for (char& c : arch)
                    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

                if (std::find(knownTargets().begin(), knownTargets().end(), arch) == knownTargets().end())
                    throw CompileError(branch.elements[0], "Unknown target-case architecture :" + arch + " (expected :a64 or :x64)");
                if (!seenArchs.insert(arch).second)
                    throw CompileError(branch.elements[0], "Duplicate target-case branch for :" + arch);

                if (arch == targetArch)
                    selected = &branch;
            }

            if (!selected)
                throw CompileError(expr, "target-case has no branch for :" + targetArch);

            // Like begin, the selected branch evaluates to its last expression (e.g. an alloc-result in a let binding)
            TargetArchGuard guard(ctx, targetArch);
            return withDocs(out, *selected, [&](CodeWriter& w) {
                Operand result;
                for (size_t j = 1; j < selected->elements.size(); ++j)
                    result = evalExpr(w, ctx, target, selected->elements[j], destReg);
                return result;
            });
        }
    }},
    {"cond", {
        "(cond [<cond> <body>...]... [else <body>...])",
        "Multi-way conditional branching statement",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
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
                    std::string condStr = evalPredicate(ctx, target, test, destReg).text;
                    std::string prefix = (i == 1) ? "if (" : "else if (";
                    header = prefix + condStr + ")";
                }
                // A clause's docs go before its if/else if/else line
                for (const auto& doc : clause.docs)
                    out.line(docLine(doc));
                if (clause.trailingDoc)
                    out.line(docLine(*clause.trailingDoc));

                emitBlock(out, ctx, header, [&]() {
                    for (size_t j = 1; j < clause.elements.size(); ++j)
                    {
                        evalExpr(out, ctx, target, clause.elements[j], destReg);
                    }
                });
            }
            return Operand{};
        }
    }},
    {"if", {
        "(if <cond> <then> [<else>])",
        "Conditional branching expression",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            if (expr.elements.size() < 3 || expr.elements.size() > 4)
                throw CompileError(expr, "if requires condition, then-branch, and optional else-branch");
            std::string condStr = evalPredicate(ctx, target, expr.elements[1], destReg).text;
            emitBlock(out, ctx, "if (" + condStr + ")", [&]() {
                evalExpr(out, ctx, target, expr.elements[2], destReg);
            });
            if (expr.elements.size() == 4)
            {
                emitBlock(out, ctx, "else", [&]() {
                    evalExpr(out, ctx, target, expr.elements[3], destReg);
                });
            }
            return Operand{};
        }
    }},
    {"begin", {
        "(begin <expr>...)",
        "Sequence multiple expressions in a single body block",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            Operand result;
            for (size_t i = 1; i < expr.elements.size(); ++i)
                result = evalExpr(out, ctx, target, expr.elements[i], destReg);
            return result;
        }
    }},
    {"block", {
        "(block <body>...)",
        "Evaluate the body in its own C++ block, so RAII helpers (spill guards, call wrappers, temps) end at its close",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            Operand result;
            ScopeGuard guard(ctx.scope);
            emitBlock(out, ctx, "", [&]() {
                for (size_t i = 1; i < expr.elements.size(); ++i)
                    result = evalExpr(out, ctx, target, expr.elements[i], destReg);
            });
            return result;
        }
    }},
    {"for-range", {
        "(for-range [<var> <from> <to>] <body>...)",
        "Repeat the body at lowering time for <var> = <from> .. <to> - 1 (a C++ for loop, e.g. to copy a fixed number of values)",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            if (expr.elements.size() < 3 || !expr.elements[1].isList() || expr.elements[1].elements.size() != 3 || !expr.elements[1].elements[0].isSymbol())
                throw CompileError(expr, "for-range requires [var from to] and a body");
            const SExpr& spec = expr.elements[1];
            std::string var = toCppVarName(spec.elements[0].text);
            Operand from = evalExpr(out, ctx, target, spec.elements[1], "");
            Operand to = evalExpr(out, ctx, target, spec.elements[2], "");

            ScopeGuard guard(ctx.scope);
            ctx.bind(spec.elements[0].text, Operand::makeImm(TypeKind::I32, var));
            emitBlock(out, ctx, "for (int " + var + " = " + from.text + "; " + var + " < " + to.text + "; ++" + var + ")", [&]() {
                for (size_t i = 2; i < expr.elements.size(); ++i)
                    evalExpr(out, ctx, target, expr.elements[i], destReg);
            });
            return Operand{};
        }
    }},
    {"unsupported!", {
        "(unsupported!)",
        "Emit assertion failure for unhandled instruction form",
        [](CodeWriter& out, EvalContext&, TargetBuilder& target, const SExpr&, const std::string&) -> Operand {
            target.emitUnsupported(out);
            return Operand{};
        }
    }},

    {"alloc-temp", {
        "(alloc-temp <type>)",
        "Allocate a temporary register (:ptr, :i64, :u64, :i32, :u32, :f32, :f64, :vec) declared in the enclosing C++ block; bind it with let",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            requireArity(expr, 1, "alloc-temp requires 1 argument: (alloc-temp <type>)");
            std::string typeStr = expr.elements[1].text;
            if (!typeStr.empty() && typeStr[0] == ':')
                typeStr = typeStr.substr(1);
            TypeKind tk = parseTypeKind(typeStr);
            if (tk == TypeKind::Void)
                throw CompileError(expr.elements[1], "Unknown type for alloc-temp: " + expr.elements[1].text);
            return target.emitAllocTemp(out, ctx, tk, ctx.takeBindingName("tmp"));
        }
    }},

    {"alloc-label", {
        "(alloc-label)",
        "Declare a local Label for branches within the lowering; bind it with let and place it with set-label!",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> Operand {
            requireArity(expr, 0, "alloc-label takes no arguments");
            std::string name = ctx.takeBindingName("label");
            out.line("Label " + name + ";");
            return Operand::makeReg(TypeKind::Void, name);
        }
    }},
    {"alloc-result", {
        "(alloc-result <type>)",
        "Allocate the instruction's result register (inst.regA64/inst.regX64) and return it; bind it with let",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
            return evalAllocResult(out, ctx, target, expr, false);
        }
    }},
    {"alloc-result-in", {
        "(alloc-result-in <physical-register> <type>)",
        "Use the value already in a physical register (e.g. a call's return register) as the result: regs.takeReg(reg, index)",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
            requireArity(expr, 2, "alloc-result-in requires a physical register and a type: (alloc-result-in <reg> <type>)");
            Operand reg = evalExpr(out, ctx, target, expr.elements[1], "");
            TypeKind tk = parseRegType(expr.elements[2], "alloc-result-in");
            target.emitTakeResult(out, ctx, target.formatRegOperand(reg));
            ctx.use("inst");
            return Operand::makeReg(tk, target.destRegVar());
        }
    }},
    {"alloc-result-reuse", {
        "(alloc-result-reuse <type> <cand-or-[cands]>)",
        "Allocate the result register, reusing the register of one of the candidate operands if it dies at this instruction",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string&) -> Operand {
            return evalAllocResult(out, ctx, target, expr, true);
        }
    }},
    {"assert!", {
        "(assert! <pred>)",
        "Emit CODEGEN_ASSERT(<pred>)",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            requireArity(expr, 1, "assert! requires 1 argument: (assert! <pred>)");
            out.line("CODEGEN_ASSERT(" + evalPredicate(ctx, target, expr.elements[1], destReg).text + ");");
            return Operand{};
        }
    }},

    // Bindings
    {"let", {
        "(let ([<var> <expr>]...) <body>...)",
        "Lexically scoped local binding; a binding [var (if <pred> <a> <b>)] picks one of two registers, where each branch is an operand or (alloc-temp <type>)",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            if (expr.elements.size() < 3)
                throw CompileError(expr, "let requires bindings and body");

            const SExpr& bindings = expr.elements[1];
            if (!bindings.isList())
                throw CompileError(bindings, "let bindings must be a list");

            // Bindings that declare a C++ local: temporaries and register choices made with if
            auto isValueTemplate = [&](const SExpr& valExpr) {
                const TemplateOpSpec* spec = valExpr.isList() && !valExpr.elements.empty() ? findTemplateOp(target, valExpr.elements[0].text) : nullptr;
                return spec && spec->isValue;
            };
            auto isEager = [&](const SExpr& valExpr) {
                return valExpr.isList("alloc-temp") || valExpr.isList("alloc-label") || valExpr.isList("if") || isValueTemplate(valExpr);
            };

            // [var (let ([x V]) stmts... x)] binds var to V and then runs stmts with x naming the same value, so a
            // subrule can both choose a register and fill it (e.g. a64:double-operand) in a single binding
            auto isOperandBlock = [](const SExpr& valExpr) {
                if (!valExpr.isList("let") || valExpr.elements.size() < 3 || !valExpr.elements[1].isList() || valExpr.elements[1].elements.size() != 1)
                    return false;
                const SExpr& inner = valExpr.elements[1].elements[0];
                return inner.isList() && inner.elements.size() == 2 && inner.elements[0].isSymbol() && valExpr.elements.back().isSymbol(inner.elements[0].text);
            };
            auto boundValue = [&](const SExpr& valExpr) -> const SExpr& {
                return isOperandBlock(valExpr) ? valExpr.elements[1].elements[0].elements[1] : valExpr;
            };

            std::set<std::string> names;
            std::vector<std::string> declaredLocals;
            for (const auto& binding : bindings.elements)
            {
                if (!binding.isList() || binding.elements.size() != 2 || !binding.elements[0].isSymbol())
                    throw CompileError(binding, "let binding must be [var expr]");
                if (!names.insert(binding.elements[0].text).second)
                    throw CompileError(binding.elements[0], "Duplicate let binding '" + binding.elements[0].text + "'");
                if (isEager(boundValue(binding.elements[1])))
                    declaredLocals.push_back(toCppVarName(binding.elements[0].text));
            }

            ScopeGuard guard(ctx.scope);
            Operand result;

            auto body = [&]() {
                for (const auto& binding : bindings.elements)
                {
                    // A binding's docs go next to its declaration
                    withDocs(out, binding, [&](CodeWriter& bindingOut) {
                        const std::string& varName = binding.elements[0].text;
                        const SExpr& valExpr = boundValue(binding.elements[1]);

                        if (valExpr.isList("if"))
                        {
                            ctx.bind(varName, evalConditionalBinding(bindingOut, ctx, target, valExpr, toCppVarName(varName)));
                        }
                        else if (isValueTemplate(valExpr))
                        {
                            // Helper results (addresses, call wrappers, indices) are computed once into a local
                            std::string cppName = toCppVarName(varName);
                            bool byReference = findTemplateOp(target, valExpr.elements[0].text)->bindsReference;
                            Operand value = evalExpr(bindingOut, ctx, target, valExpr, "");
                            bindingOut.line((byReference ? "auto& " : "auto ") + cppName + " = " + value.text + ";");
                            ctx.bind(varName, Operand::makeReg(value.type, cppName));
                        }
                        else if (isEager(valExpr))
                        {
                            ctx.bindingName = toCppVarName(varName);
                            Operand op = evalExpr(bindingOut, ctx, target, valExpr, "");
                            ctx.bindingName.clear();
                            ctx.bind(varName, op);
                        }
                        else
                        {
                            ctx.bind(varName, evalExpr(bindingOut, ctx, target, valExpr, ""));
                        }

                        if (isOperandBlock(binding.elements[1]))
                        {
                            const SExpr& block = binding.elements[1];
                            ScopeGuard innerGuard(ctx.scope);
                            ctx.bind(block.elements[1].elements[0].elements[0].text, *ctx.lookup(varName));
                            for (size_t i = 2; i + 1 < block.elements.size(); ++i)
                                evalExpr(bindingOut, ctx, target, block.elements[i], "");
                        }
                        return Operand{};
                    });
                }

                for (size_t i = 2; i < expr.elements.size(); ++i)
                    result = evalExpr(out, ctx, target, expr.elements[i], destReg);
            };

            // Locals declared by the bindings (temp registers) go straight into the enclosing C++ block; a new block
            // is only opened when one of the names was already declared there by a sibling let
            std::set<std::string>& enclosing = ctx.blockLocals.back();
            bool clashes = false;
            for (const auto& name : declaredLocals)
                clashes |= enclosing.count(name) > 0;

            if (clashes)
            {
                emitBlock(out, ctx, "", [&]() {
                    ctx.blockLocals.back().insert(declaredLocals.begin(), declaredLocals.end());
                    body();
                });
            }
            else
            {
                enclosing.insert(declaredLocals.begin(), declaredLocals.end());
                body();
            }

            return result;
        }
    }},

    {"fflag", {
        "(fflag :<flag-name>)",
        "Reference fast flag in expression context",
        [](CodeWriter&, EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> Operand {
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
        }
    }},
    {"fflag?", {
        "(fflag? :<flag-name>)",
        "Reference fast flag in expression context (alias)",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            return kExprTrampolines.at("fflag").fn(out, ctx, target, expr, destReg);
        }
    }},
    {"dfflag", {
        "(dfflag :<flag-name>)",
        "Reference dynamic fast flag in expression context",
        [](CodeWriter&, EvalContext& ctx, TargetBuilder&, const SExpr& expr, const std::string&) -> Operand {
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
        }
    }},
    {"dfflag?", {
        "(dfflag? :<flag-name>)",
        "Reference dynamic fast flag in expression context (alias)",
        [](CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg) -> Operand {
            return kExprTrampolines.at("dfflag").fn(out, ctx, target, expr, destReg);
        }
    }}
};

struct SourceLocGuard
{
    EvalContext& ctx;
    SourceLocation prev;
    SourceLocGuard(EvalContext& c, SourceLocation next) : ctx(c), prev(c.loc)
    {
        if (next.line > 0)
            ctx.loc = next;
    }
    ~SourceLocGuard()
    {
        ctx.loc = prev;
    }
};

static Operand evalExprNoDocs(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg);

Operand evalExpr(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg)
{
    // Only statements carry docs (checked when parsing rules), so this is where a statement's docs get written
    if (!expr.hasDocs())
        return evalExprNoDocs(out, ctx, target, expr, destReg);

    return withDocs(out, expr, [&](CodeWriter& w) {
        return evalExprNoDocs(w, ctx, target, expr, destReg);
    });
}

static Operand evalExprNoDocs(CodeWriter& out, EvalContext& ctx, TargetBuilder& target, const SExpr& expr, const std::string& destReg)
{
    SourceLocGuard locGuard(ctx, expr.loc);

    if (expr.isSymbol())
    {
        const Operand* op = ctx.lookup(expr.text);
        if (op)
            return *op;
        return Operand::makeReg(TypeKind::Ptr, ctx.requireGlobal(expr));
    }
    if (expr.isNumber())
    {
        // Keep the literal's spelling (hex, floating point) in the generated code
        bool isFloat = expr.text.find_first_of(".eE") != std::string::npos && expr.text.find_first_of("xX") == std::string::npos;
        std::string text = !expr.text.empty() && expr.text[0] == '+' ? expr.text.substr(1) : expr.text;
        return Operand::makeImm(isFloat ? TypeKind::F64 : TypeKind::I32, text);
    }
    if (expr.isKeyword())
    {
        return Operand::makeImm(TypeKind::I32, ctx.resolveOffset(expr.text));
    }

    if (!expr.isList() || expr.elements.empty())
        throw CompileError(expr, "Expected non-empty list in evalExpr");

    const std::string& head = expr.elements[0].text;

    auto it = kExprTrampolines.find(head);
    if (it != kExprTrampolines.end())
    {
        validateArchTarget(ctx, expr, head, "expression");
        return it->second.fn(out, ctx, target, expr, destReg);
    }

    size_t colonPos = head.find(':');
    if (colonPos != std::string::npos)
        validateArchTarget(ctx, expr, head, "operation");

    if (auto result = evalTemplateOp(out, ctx, target, expr))
        return *result;

    if (colonPos != std::string::npos)
    {
        std::vector<Operand> args;
        for (size_t i = 1; i < expr.elements.size(); ++i)
            args.push_back(evalExpr(out, ctx, target, expr.elements[i], ""));

        if (!target.emitArchOp(out, ctx, head, args))
            throw CompileError(expr, "Unknown architecture-dependent operation: " + head);

        return Operand{};
    }

    if (head.rfind("%produce-", 0) == 0)
        throw CompileError(expr, head + " has been removed; allocate the result explicitly with (let ([res (alloc-result <type>)]) ...)");

    throw CompileError(expr, "Unsupported expression: " + head);
}



void compileRules(std::ostream& os, TargetBuilder& target, const std::vector<LoweringInstance>& lowerings, const std::vector<DispatchEntry>& dispatch)
{
    CodeWriter writer(os);

    static const char* kOpNames[] = {"OP_A(inst)", "OP_B(inst)", "OP_C(inst)", "OP_D(inst)", "OP_E(inst)", "OP_F(inst)", "OP_G(inst)"};

    struct CompiledRule
    {
        std::string fnName;
        std::vector<std::string> decls;
        std::string body;
        std::set<std::string> usedSymbols;
        std::vector<std::string> docs;
    };

    std::set<std::string> allFFlags;
    std::set<std::string> allDFFlags;
    std::vector<CompiledRule> compiledRules;

    for (const auto& lowering : lowerings)
    {
        const Rule& rule = *lowering.rule;
        std::string fnName = "lower_" + lowering.cmdName;
        StringCodeWriter bodyWriter(1);

        EvalContext ctx;
        Scope rootScope;
        ctx.scope = &rootScope;
        ctx.writer = &bodyWriter;
        ctx.loc = rule.loc;
        ctx.globalSymbols = target.globalSymbols();
        ctx.blockLocals.emplace_back();

        for (size_t i = 0; i < rule.params.size(); ++i)
        {
            const auto& p = rule.params[i];

            // Bound to a literal in the command's lowering clause: the parameter is that value
            if (lowering.operandIndex[i] < 0)
            {
                ctx.bind(p.name, evalExpr(bodyWriter, ctx, target, lowering.literals[i], ""));
                continue;
            }

            std::string opName = kOpNames[lowering.operandIndex[i]];

            if (p.type == "uint")
                ctx.bind(p.name, Operand::makeReg(TypeKind::U32, "lowering.uintOp(" + opName + ")", opName));
            else if (p.type == "int")
                ctx.bind(p.name, Operand::makeReg(TypeKind::I32, "lowering.intOp(" + opName + ")", opName));
            else if (p.type == "reg")
                ctx.bind(p.name, Operand::makeReg(TypeKind::Ptr, "lowering.regOp(" + opName + ")", opName));
            else if (p.type == "tag")
                ctx.bind(p.name, Operand::makeReg(TypeKind::Tag, "lowering.tagOp(" + opName + ")", opName));
            else if (p.type == "upvalue")
                ctx.bind(p.name, Operand::makeReg(TypeKind::U32, "vmUpvalueOp(" + opName + ")", opName));
            else if (p.type == "cond")
                ctx.bind(p.name, Operand::makeReg(TypeKind::Cond, "conditionOp(" + opName + ")", opName));
            else if (p.type == "label" || p.type == "op")
                ctx.bind(p.name, Operand::makeReg(TypeKind::Void, opName, opName));
            else
                throw CompileError(p.loc, "Unknown parameter type: " + p.type);
        }

        for (const auto& expr : rule.body)
        {
            evalExpr(bodyWriter, ctx, target, expr, "");
        }

        // Template operations reference these locals directly, so detect their use from the generated code
        static const std::regex kLocalUse(R"((^|[^.\w])(build|regs|function|inst|index|next)\b)");
        std::string generated = stripLineComments(bodyWriter.str());
        for (const auto& decl : bodyWriter.decls())
            generated += "\n" + decl;
        for (auto it = std::sregex_iterator(generated.begin(), generated.end(), kLocalUse); it != std::sregex_iterator(); ++it)
            ctx.use((*it)[2].str());

        allFFlags.insert(ctx.fflags.begin(), ctx.fflags.end());
        allDFFlags.insert(ctx.dfflags.begin(), ctx.dfflags.end());
        compiledRules.push_back({std::move(fnName), bodyWriter.decls(), bodyWriter.str(), std::move(ctx.usedSymbols), rule.docs});
    }

    target.emitPreamble(writer, {allFFlags.begin(), allFFlags.end()}, {allDFFlags.begin(), allDFFlags.end()});

    for (const auto& cr : compiledRules)
    {
        for (const auto& doc : cr.docs)
            writer.line(docLine(doc));
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
            if (cr.usedSymbols.count("function"))
            {
                writer.line("auto& function = lowering.function;");
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

    // Full switch (no default), so -Wswitch catches a command without a case
    writer.line("// Lowers `inst` with the rule its define-ircmd names; pseudo-instructions are never lowered");
    writer.block("inline void lowerInstGenerated(" + target.loweringClass() + "& lowering, IrInst& inst, uint32_t index, const IrBlock& next)", [&]() {
        writer.line("switch (inst.cmd)");
        writer.line("{");
        std::vector<std::string> pseudo;
        for (const auto& entry : dispatch)
        {
            if (!entry.lowered)
            {
                pseudo.push_back(entry.cmdName);
                continue;
            }
            writer.line("case IrCmd::" + entry.cmdName + ":");
            writer.indent();
            writer.line("lower_" + entry.cmdName + "(lowering, inst, index, next);");
            writer.line("break;");
            writer.dedent();
        }
        for (const auto& name : pseudo)
            writer.line("case IrCmd::" + name + ":");
        if (!pseudo.empty())
        {
            writer.indent();
            writer.line("CODEGEN_ASSERT(!\"Pseudo instructions should not be lowered\");");
            writer.line("break;");
            writer.dedent();
        }
        writer.line("}");
    });
    writer.line();

    target.emitPostamble(writer);
}

void getCommonCoreDocs(std::vector<FormDoc>& out)
{
    out.push_back({"define-ircmd", "(define-ircmd NAME (operands [<name> :<kind>|...]...) (target-specific [:<target> [<name> :<kind>|...]...]...) (flag-specific [:<flag> [<name> :<kind>|...]...]...) (result :<kind>) (class :<class>) (flags :<flag>...) (heap-effects :<effect>...) (lowering (<rule> <arg>...)))",
        "Declare an IR command (an IrCmd enumerator, in declaration order with NOP first). "
        "operands: each operand's kinds, joined with |: :int :int64 :double :float :tag (an instruction result or a matching constant), :pointer :tvalue, "
        ":inst-<kind> (only an instruction result), :const-int :const-uint :const-int64 :const-double :const-tag :import, :Rn :Kn :UPn, :block :vmexit :undef, :cond, "
        ":none (optional; trailing operands only), :any. target-specific / flag-specific: extra kinds when compiling for a target (:a64, :x64) / with a flag (:DebugLuauAbortingChecks). "
        "The IR verifier (FFlag::DebugCodegenVerifyIr, on in tests) checks IR against these, and a lowering rule parameter's type must be able to read its operand's kinds. result: :tag :int :int64 :pointer :float :double :tvalue :unknown (none when absent). "
        "class: :value (has a result), :effect (none), :guard (may branch to a target and continue), :terminator (ends a block), :pseudo (never lowered)."
        "flags: :dirty-high-bits, :deprecated/has-side-effects (pot dangerous), :deprecated/is-unsafe-to-sink (pot dangerous), :invalidates-safe-env (heap effect)"
        "lowering names the define-rule that lowers it (generating lower_NAME), passing each rule parameter explicitly: an operand name or a number/keyword literal. "
    });
    out.push_back({"define-rule", "(define-rule (<name> [<param> <type>]...) <body>...)", "Define an IR instruction lowering rule"});
    out.push_back({"define-subrule", "(define-subrule (<name> [<param> <type>]...) <body>...)", "Define compile-time macro template inlined at expansion sites"});
    out.push_back({"~", "~ <text>", "Doc comment: to the end of the line, kept and written into the generated C++ as a // comment (; comments stay Luna-only). Leading ones go on their own lines before a statement, a trailing one after code on the same line (it moves up to the statement when it lands on an argument of a statement starting on that line, e.g. (if (constant? a) ~ ...). Allowed on define-rule (written above lower_<NAME>) and statements: rule/subrule body items, let bindings, cond clauses, target-case branches and the bodies of let/begin/block/if/cond/target-case/for-range"});
    out.push_back({"include", "(include \"<path>.luna\")", "Top-level only: splice in another file's definitions at this point. The path is relative to the including file and may not leave the root file's directory; each file is included once, and include cycles are errors"});

    for (const auto& [name, spec] : kExprTrampolines)
    {
        out.push_back({std::string(name), spec.syntax ? spec.syntax : "", spec.desc ? spec.desc : ""});
    }

    std::vector<FormDoc> values;
    getCoreTemplateDocs(out, values);
    out.insert(out.end(), values.begin(), values.end());
}

void getCommonPredicateDocs(std::vector<FormDoc>& out)
{
    for (const auto& [name, spec] : kPredicateTrampolines)
    {
        out.push_back({std::string(name), spec.syntax ? spec.syntax : "", spec.desc ? spec.desc : ""});
    }
}

} // namespace Luwu::Dsl
