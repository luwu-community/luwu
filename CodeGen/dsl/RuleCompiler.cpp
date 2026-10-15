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

static std::string evalPredicate(const EvalContext& ctx, const SExpr& expr, const std::string& destReg)
{
    if (!expr.isList() || expr.elements.empty())
        throw std::runtime_error("Predicate must be a list: " + expr.text);

    const std::string& head = expr.elements[0].text;
    if (head == "constant?")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("constant? requires 1 argument");
        return getRawOp(ctx, expr.elements[1]) + ".kind == IrOpKind::Constant";
    }
    else if (head == "inst?")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("inst? requires 1 argument");
        return getRawOp(ctx, expr.elements[1]) + ".kind == IrOpKind::Inst";
    }
    else if (head == "reused?")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("reused? requires 1 argument");
        return destReg + " == lowering.regOp(" + getRawOp(ctx, expr.elements[1]) + ")";
    }
    else if (head == "not-reused?")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("not-reused? requires 1 argument");
        return destReg + " != lowering.regOp(" + getRawOp(ctx, expr.elements[1]) + ")";
    }
    else if (head == "=")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error("= requires 2 arguments");
        std::string lhs = evalPredicate(ctx, expr.elements[1], destReg);
        std::string rhs = expr.elements[2].text;
        return lhs + " == " + rhs;
    }
    else if (head == "int-val")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("int-val requires 1 argument");
        return "lowering.intOp(" + getRawOp(ctx, expr.elements[1]) + ")";
    }
    else if (head == "int64-val")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("int64-val requires 1 argument");
        return "lowering.int64Op(" + getRawOp(ctx, expr.elements[1]) + ")";
    }
    else if (head == "fits-i32?")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("fits-i32? requires 1 argument");
        std::string raw = getRawOp(ctx, expr.elements[1]);
        return "lowering.int64Op(" + raw + ") >= INT32_MIN && lowering.int64Op(" + raw + ") <= INT32_MAX";
    }
    else if (head == "fits-imm-a64-32?")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("fits-imm-a64-32? requires 1 argument");
        return "unsigned(lowering.intOp(" + getRawOp(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate";
    }
    else if (head == "fits-imm-a64-64?")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("fits-imm-a64-64? requires 1 argument");
        return "uint64_t(lowering.int64Op(" + getRawOp(ctx, expr.elements[1]) + ")) <= AssemblyBuilderA64::kMaxImmediate";
    }
    else if (head == "and")
    {
        if (expr.elements.size() < 2)
            throw std::runtime_error("and requires at least 1 argument");
        std::string res;
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            if (i > 1) res += " && ";
            res += evalPredicate(ctx, expr.elements[i], destReg);
        }
        return res;
    }
    else if (head == "or")
    {
        if (expr.elements.size() < 2)
            throw std::runtime_error("or requires at least 1 argument");
        std::string res;
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            if (i > 1) res += " || ";
            res += evalPredicate(ctx, expr.elements[i], destReg);
        }
        return res;
    }
    else if (head == "not")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("not requires 1 argument");
        return "!(" + evalPredicate(ctx, expr.elements[1], destReg) + ")";
    }
    else
    {
        throw std::runtime_error("Unknown predicate: " + head);
    }
}

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

    if (head == "code-address")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("code-address takes 1 argument");
        Operand offsetOp = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        return target.emitCodeAddress(os, indent, ctx, offsetOp.text, destReg);
    }
    else if (head == "load-ptr" || head == "load-u8" || head == "load-u32")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error(head + " takes 1 argument");
        TypeKind tk = (head == "load-ptr") ? TypeKind::Ptr : ((head == "load-u8") ? TypeKind::U8 : TypeKind::U32);
        auto [base, offset] = evalMem(os, ctx, target, expr.elements[1], destReg, indent);
        return target.emitLoad(os, indent, ctx, tk, base, offset, destReg);
    }
    else if (head == "sign-extend-8" || head == "sign-extend-16")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error(head + " takes 1 argument");
        int bits = (head == "sign-extend-8") ? 8 : 16;
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        return target.emitSignExtend(os, indent, ctx, bits, val, destReg);
    }
    else if (head == "store-ptr!" || head == "store-u8!")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires (store! (mem base offset) val)");
        TypeKind tk = (head == "store-ptr!") ? TypeKind::Ptr : TypeKind::U8;
        auto [base, offset] = evalMem(os, ctx, target, expr.elements[1], "", indent);
        Operand val = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        std::string valName = expr.elements[2].isSymbol() ? expr.elements[2].text : "";
        target.emitStore(os, indent, ctx, tk, base, offset, val, valName);
        return Operand{};
    }
    else if (head == "branch-if-not-zero!" || head == "branch-if-zero!")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires val and label");
        bool notZero = (head == "branch-if-not-zero!");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand labelOp = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        target.emitBranchIfZero(os, indent, ctx, notZero, val, labelOp.text);
        return Operand{};
    }
    else if (head == "branch-if-equal!" || head == "branch-if-not-equal!")
    {
        if (expr.elements.size() != 4)
            throw std::runtime_error(head + " requires val, expected, and label");
        bool notEqual = (head == "branch-if-not-equal!");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand expected = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        Operand labelOp = evalExpr(os, ctx, target, expr.elements[3], "", indent);
        target.emitBranchIfEqual(os, indent, ctx, notEqual, val, expected, labelOp.text);
        return Operand{};
    }
    else if (head == "branch-if-negative!")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error("branch-if-negative! requires val and label");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand labelOp = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        target.emitBranchIfNegative(os, indent, ctx, val, labelOp.text);
        return Operand{};
    }
    else if (head == "truncate-uint")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("truncate-uint requires 1 argument");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        return target.emitTruncateUint(os, indent, ctx, val, destReg);
    }
    else if (head == "is-equal")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error("is-equal requires 2 arguments");
        Operand val = evalExpr(os, ctx, target, expr.elements[1], "", indent);
        Operand expected = evalExpr(os, ctx, target, expr.elements[2], "", indent);
        return target.emitIsEqual(os, indent, ctx, val, expected, destReg);
    }
    else if (head == "byteswap-32" || head == "byteswap-64")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error(head + " requires 1 argument");
        int bits = (head == "byteswap-64") ? 64 : 32;
        std::string rawOp = getRawOp(ctx, expr.elements[1]);
        return target.emitByteSwap(os, indent, ctx, bits, rawOp, destReg);
    }
    else if (head == "byteswap")
    {
        throw std::runtime_error("byteswap requires explicit width suffix: use (byteswap-32 ...) or (byteswap-64 ...)");
    }
    else if (head == "bitnot-32" || head == "bitnot-64" || head == "bitwise-not-32" || head == "bitwise-not-64")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error(head + " requires 1 argument");
        int bits = (head.find("-64") != std::string::npos) ? 64 : 32;
        std::string rawOp = getRawOp(ctx, expr.elements[1]);
        return target.emitBitwiseNot(os, indent, ctx, bits, rawOp, destReg);
    }
    else if (head == "bitnot" || head == "bitwise-not")
    {
        throw std::runtime_error(head + " requires explicit width suffix: use (" + head + "-32 ...) or (" + head + "-64 ...)");
    }
    else if (head == "bitand-32" || head == "bitand-64" || head == "bitwise-and-32" || head == "bitwise-and-64" ||
             head == "bitor-32"  || head == "bitor-64"  || head == "bitwise-or-32"  || head == "bitwise-or-64"  ||
             head == "bitxor-32" || head == "bitxor-64" || head == "bitwise-xor-32" || head == "bitwise-xor-64")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires 2 arguments");
        std::string op = (head.find("and") != std::string::npos) ? "and" : ((head.find("or") != std::string::npos && head.find("xor") == std::string::npos) ? "or" : "xor");
        int bits = (head.find("-64") != std::string::npos) ? 64 : 32;
        std::string rawOpA = getRawOp(ctx, expr.elements[1]);
        std::string rawOpB = getRawOp(ctx, expr.elements[2]);
        return target.emitBitwiseBinOp(os, indent, ctx, op, bits, rawOpA, rawOpB, destReg);
    }
    else if (head == "bitand" || head == "bitwise-and" || head == "bitor" || head == "bitwise-or" || head == "bitxor" || head == "bitwise-xor")
    {
        throw std::runtime_error(head + " requires explicit width suffix: use (" + head + "-32 ...) or (" + head + "-64 ...)");
    }
    else if (head == "mul-32" || head == "mul-64")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires 2 arguments");
        int bits = (head == "mul-64") ? 64 : 32;
        std::string rawOpA = getRawOp(ctx, expr.elements[1]);
        std::string rawOpB = getRawOp(ctx, expr.elements[2]);
        return target.emitMul(os, indent, ctx, bits, rawOpA, rawOpB, destReg);
    }
    else if (head == "mul")
    {
        throw std::runtime_error("mul requires explicit width suffix: use (mul-32 ...) or (mul-64 ...)");
    }
    else if (head == "target-case")
    {
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
    }
    else if (head == "cond")
    {
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
    }
    else if (head == "if")
    {
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
    }
    else if (head == "begin")
    {
        for (size_t i = 1; i < expr.elements.size(); ++i)
        {
            evalExpr(os, ctx, target, expr.elements[i], destReg, indent);
        }
        return Operand{};
    }
    else if (head == "unsupported!")
    {
        target.emitUnsupported(os, indent);
        return Operand{};
    }
    else if (head == "lea-reg-32!" || head == "lea-reg-64!")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires base and offset");
        int bits = (head == "lea-reg-64!") ? 64 : 32;
        target.emitLeaReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
        return Operand{};
    }
    else if (head == "lea-imm-32!" || head == "lea-imm-64!")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires base and offset");
        int bits = (head == "lea-imm-64!") ? 64 : 32;
        target.emitLeaImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
        return Operand{};
    }
    else if (head == "lea-sub-32!" || head == "lea-sub-64!")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires base and offset");
        int bits = (head == "lea-sub-64!") ? 64 : 32;
        target.emitLeaSub(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
        return Operand{};
    }
    else if (head == "add-reg-32!" || head == "add-reg-64!")
    {
        int bits = (head == "add-reg-64!") ? 64 : 32;
        if (expr.elements.size() == 2)
            target.emitAddOp(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
        else if (expr.elements.size() == 3)
            target.emitAddReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
        else
            throw std::runtime_error(head + " requires 1 or 2 arguments");
        return Operand{};
    }
    else if (head == "add-imm-32!" || head == "add-imm-64!")
    {
        int bits = (head == "add-imm-64!") ? 64 : 32;
        if (expr.elements.size() == 2)
            target.emitAddImmOp(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
        else if (expr.elements.size() == 3)
            target.emitAddImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
        else
            throw std::runtime_error(head + " requires 1 or 2 arguments");
        return Operand{};
    }
    else if (head == "sub-reg-32!" || head == "sub-reg-64!")
    {
        int bits = (head == "sub-reg-64!") ? 64 : 32;
        if (expr.elements.size() == 2)
            target.emitSubOp(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
        else if (expr.elements.size() == 3)
            target.emitSubReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
        else
            throw std::runtime_error(head + " requires 1 or 2 arguments");
        return Operand{};
    }
    else if (head == "sub-imm-32!" || head == "sub-imm-64!")
    {
        int bits = (head == "sub-imm-64!") ? 64 : 32;
        if (expr.elements.size() == 2)
            target.emitSubImmOp(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
        else if (expr.elements.size() == 3)
            target.emitSubImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), getRawOp(ctx, expr.elements[2]), destReg, bits);
        else
            throw std::runtime_error(head + " requires 1 or 2 arguments");
        return Operand{};
    }
    else if (head == "inc!")
    {
        target.emitInc(os, indent, ctx, destReg);
        return Operand{};
    }
    else if (head == "mov-reg!")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("mov-reg! requires 1 argument");
        target.emitMovReg(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg);
        return Operand{};
    }
    else if (head == "mov-imm-32!" || head == "mov-imm-64!")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error(head + " requires 1 argument");
        int bits = (head == "mov-imm-64!") ? 64 : 32;
        target.emitMovImm(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg, bits);
        return Operand{};
    }
    else if (head == "add-tmp-64!")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("add-tmp-64! requires 1 argument");
        target.emitAddTmp64(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg);
        return Operand{};
    }
    else if (head == "sub-tmp-64!")
    {
        if (expr.elements.size() != 2)
            throw std::runtime_error("sub-tmp-64! requires 1 argument");
        target.emitSubTmp64(os, indent, ctx, getRawOp(ctx, expr.elements[1]), destReg);
        return Operand{};
    }
    else if (head == "add-32" || head == "add-64")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires 2 arguments");
        bool is64 = (head == "add-64");
        std::string rawOpA = getRawOp(ctx, expr.elements[1]);
        std::string rawOpB = getRawOp(ctx, expr.elements[2]);
        if (is64)
            return target.emitAddInt64(os, indent, ctx, rawOpA, rawOpB, destReg);
        else
            return target.emitAddInt(os, indent, ctx, rawOpA, rawOpB, destReg);
    }
    else if (head == "add" || head == "add-int" || head == "add-int64")
    {
        throw std::runtime_error(head + " requires explicit width suffix: use (add-32 ...) or (add-64 ...)");
    }
    else if (head == "sub-32" || head == "sub-64")
    {
        if (expr.elements.size() != 3)
            throw std::runtime_error(head + " requires 2 arguments");
        bool is64 = (head == "sub-64");
        std::string rawOpA = getRawOp(ctx, expr.elements[1]);
        std::string rawOpB = getRawOp(ctx, expr.elements[2]);
        if (is64)
            return target.emitSubInt64(os, indent, ctx, rawOpA, rawOpB, destReg);
        else
            return target.emitSubInt(os, indent, ctx, rawOpA, rawOpB, destReg);
    }
    else if (head == "sub" || head == "sub-int" || head == "sub-int64")
    {
        throw std::runtime_error(head + " requires explicit width suffix: use (sub-32 ...) or (sub-64 ...)");
    }
    else if (head == "let")
    {
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
    }
    else if (head.rfind("%produce-", 0) == 0)
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
    else
    {
        throw std::runtime_error("Unsupported expression: " + head);
    }
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
