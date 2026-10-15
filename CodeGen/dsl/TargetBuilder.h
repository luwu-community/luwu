// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"
#include "Types.h"
#include "Scope.h"
#include "CodeWriter.h"
#include "CompileError.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Luwu::Dsl
{

inline std::string resolveOffset(const std::string& text)
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

struct EvalContext
{
    Scope* scope = nullptr;
    CodeWriter* writer = nullptr;
    int tempCount = 0;
    std::string currentTargetArch;

    void addDecl(const std::string& line)
    {
        if (writer)
            writer->addDecl(line);
    }

    const Operand* lookup(const std::string& name) const
    {
        return scope ? scope->lookup(name) : nullptr;
    }

    void bind(const std::string& name, Operand op)
    {
        if (scope)
            scope->bind(name, std::move(op));
    }

    void updateOrBind(const std::string& name, Operand op)
    {
        if (scope)
            scope->updateOrBind(name, std::move(op));
    }

    std::string nextTemp(const std::string& prefix = "tmp_")
    {
        return prefix + std::to_string(tempCount++);
    }

    std::string resolveOffset(const std::string& text) const
    {
        return Dsl::resolveOffset(text);
    }
};

class TargetBuilder
{
public:
    virtual ~TargetBuilder() = default;

    virtual std::string archName() const = 0;
    virtual std::string loweringClass() const = 0;
    virtual std::string destRegVar() const = 0;

    virtual void emitPreamble(CodeWriter& out) = 0;
    virtual void emitPostamble(CodeWriter& out) = 0;

    virtual void emitAllocProduce(CodeWriter& out, TypeKind type, bool isReuse, const std::vector<std::string>& rawOps) = 0;

    virtual std::string toReg(CodeWriter& out, EvalContext& ctx, const Operand& op, const std::string& destReg = "", const std::string& preferredKind = "") = 0;

    virtual std::string resolveMemBase(CodeWriter& out, EvalContext& ctx, const SExpr& baseExpr, const Operand& baseOp, const std::string& destReg = "") = 0;

    virtual Operand emitCodeAddress(CodeWriter& out, EvalContext& ctx, const std::string& offset, const std::string& destReg) = 0;

    virtual Operand emitLoad(CodeWriter& out, EvalContext& ctx, TypeKind type, const std::string& base, const std::string& offset, const std::string& destReg) = 0;

    virtual void emitStore(CodeWriter& out, EvalContext& ctx, TypeKind type, const std::string& base, const std::string& offset, const Operand& val, const std::string& valName = "") = 0;

    virtual void emitBranchIfZero(CodeWriter& out, EvalContext& ctx, bool notZero, const Operand& val, const std::string& labelOp) = 0;

    virtual void emitBranchIfEqual(CodeWriter& out, EvalContext& ctx, bool notEqual, const Operand& val, const Operand& expected, const std::string& labelOp) = 0;

    virtual void emitBranchIfNegative(CodeWriter& out, EvalContext& ctx, const Operand& val, const std::string& labelOp) = 0;

    virtual Operand emitSignExtend(CodeWriter& out, EvalContext& ctx, int bits, const Operand& val, const std::string& destReg) = 0;

    virtual Operand emitTruncateUint(CodeWriter& out, EvalContext& ctx, const Operand& val, const std::string& destReg) = 0;

    virtual Operand emitIsEqual(CodeWriter& out, EvalContext& ctx, const Operand& val, const Operand& expected, const std::string& destReg) = 0;

    virtual Operand emitByteSwap(CodeWriter& out, EvalContext& ctx, int bits, const std::string& rawOp, const std::string& destReg) = 0;

    virtual Operand emitBitwiseNot(CodeWriter& out, EvalContext& ctx, int bits, const std::string& rawOp, const std::string& destReg) = 0;

    virtual Operand emitBitwiseBinOp(CodeWriter& out, EvalContext& ctx, const std::string& op, int bits, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg) = 0;

    virtual Operand emitMul(CodeWriter& out, EvalContext& ctx, int bits, const std::string& rawOpA, const std::string& rawOpB, const std::string& destReg) = 0;

    virtual void emitUnsupported(CodeWriter& out)
    {
        out.line("CODEGEN_ASSERT(!\"Unsupported instruction form\");");
    }

    // Architecture-dependent operation dispatch hook
    virtual bool emitArchOp(CodeWriter& out, EvalContext& ctx, const std::string& opName, const std::vector<std::string>& args)
    {
        return false;
    }
};

} // namespace Luwu::Dsl
