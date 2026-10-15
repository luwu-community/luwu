// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"
#include "Types.h"

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace Luwu::Dsl
{

struct EvalContext
{
    std::unordered_map<std::string, Operand> env;
    std::unordered_map<std::string, std::string> rawOps;
    std::vector<std::string> decls;
    int tempCount = 0;

    void addDecl(const std::string& line)
    {
        for (const auto& d : decls)
        {
            if (d == line)
                return;
        }
        decls.push_back(line);
    }

    std::string nextTemp(const std::string& prefix = "tmp_")
    {
        return prefix + std::to_string(tempCount++);
    }

    std::string resolveOffset(const std::string& text) const
    {
        std::string s = text;
        if (!s.empty() && s[0] == ':')
            s = s.substr(1);

        size_t dot = s.find('.');
        if (dot != std::string::npos)
        {
            std::string structName = s.substr(0, dot);
            std::string memberName = s.substr(dot + 1);
            return "offsetof(" + structName + ", " + memberName + ")";
        }
        return s;
    }
};

class TargetBuilder
{
public:
    virtual ~TargetBuilder() = default;

    virtual std::string archName() const = 0;
    virtual std::string loweringClass() const = 0;
    virtual std::string destRegVar() const = 0;

    virtual void emitPreamble(std::ostream& os) = 0;
    virtual void emitPostamble(std::ostream& os) = 0;

    virtual void emitAllocProduce(std::ostream& os, const std::string& indent, TypeKind type, bool isReuse, const std::vector<std::string>& rawOps) = 0;

    virtual std::string toReg(std::ostream& os, const std::string& indent, EvalContext& ctx, const Operand& op, const std::string& destReg = "", const std::string& preferredKind = "") = 0;

    virtual std::string resolveMemBase(std::ostream& os, const std::string& indent, EvalContext& ctx, const SExpr& baseExpr, const Operand& baseOp, const std::string& destReg = "") = 0;

    virtual Operand emitCodeAddress(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& offset, const std::string& destReg) = 0;

    virtual Operand emitLoad(std::ostream& os, const std::string& indent, EvalContext& ctx, TypeKind type, const std::string& base, const std::string& offset, const std::string& destReg) = 0;

    virtual void emitStore(std::ostream& os, const std::string& indent, EvalContext& ctx, TypeKind type, const std::string& base, const std::string& offset, const Operand& val, const std::string& valName = "") = 0;

    virtual void emitBranchIfZero(std::ostream& os, const std::string& indent, EvalContext& ctx, bool notZero, const Operand& val, const std::string& labelOp) = 0;

    virtual void emitBranchIfEqual(std::ostream& os, const std::string& indent, EvalContext& ctx, bool notEqual, const Operand& val, const Operand& expected, const std::string& labelOp) = 0;

    virtual void emitBranchIfNegative(std::ostream& os, const std::string& indent, EvalContext& ctx, const Operand& val, const std::string& labelOp) = 0;

    virtual Operand emitSignExtend(std::ostream& os, const std::string& indent, EvalContext& ctx, int bits, const Operand& val, const std::string& destReg) = 0;

    virtual Operand emitTruncateUint(std::ostream& os, const std::string& indent, EvalContext& ctx, const Operand& val, const std::string& destReg) = 0;

    virtual Operand emitIsEqual(std::ostream& os, const std::string& indent, EvalContext& ctx, const Operand& val, const Operand& expected, const std::string& destReg) = 0;

    virtual Operand emitByteSwap(std::ostream& os, const std::string& indent, EvalContext& ctx, int bits, const std::string& rawOp, const std::string& destReg) = 0;

    virtual Operand emitBitwiseNot(std::ostream& os, const std::string& indent, EvalContext& ctx, int bits, const std::string& rawOp, const std::string& destReg) = 0;

    virtual Operand emitBitwiseBinOp(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& op, int bits, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg) = 0;

    virtual Operand emitMul(std::ostream& os, const std::string& indent, EvalContext& ctx, int bits, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg) = 0;

    virtual Operand emitAddInt(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg) = 0;

    virtual Operand emitSubInt(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg) = 0;

    virtual Operand emitAddInt64(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg) = 0;

    virtual Operand emitSubInt64(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg) = 0;

    virtual void emitUnsupported(std::ostream& os, const std::string& indent)
    {
        os << indent << "CODEGEN_ASSERT(!\"Unsupported instruction form\");\n";
    }

    // X64 fine-grained builder hooks
    virtual void emitLeaReg(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpBase, const std::string& rawOpOffset, const std::string& destReg, int bits) {}
    virtual void emitLeaImm(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpBase, const std::string& rawOpOffset, const std::string& destReg, int bits) {}
    virtual void emitLeaSub(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpBase, const std::string& rawOpOffset, const std::string& destReg, int bits) {}
    virtual void emitAddOp(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOp, const std::string& destReg, int bits) {}
    virtual void emitAddImmOp(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOp, const std::string& destReg, int bits) {}
    virtual void emitSubOp(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOp, const std::string& destReg, int bits) {}
    virtual void emitSubImmOp(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOp, const std::string& destReg, int bits) {}
    virtual void emitInc(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& destReg) {}
    virtual void emitMovReg(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOp, const std::string& destReg) {}
    virtual void emitMovImm(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOp, const std::string& destReg, int bits) {}
    virtual void emitAddTmp64(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOp, const std::string& destReg) {}
    virtual void emitSubTmp64(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOp, const std::string& destReg) {}

    // A64 fine-grained builder hooks
    virtual void emitAddImm(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg, int bits) {}
    virtual void emitAddReg(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg, int bits) {}
    virtual void emitSubImm(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg, int bits) {}
    virtual void emitSubReg(std::ostream& os, const std::string& indent, EvalContext& ctx, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg, int bits) {}
};

} // namespace Luwu::Dsl
