// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"
#include "Types.h"
#include "Scope.h"
#include "CodeWriter.h"
#include "CompileError.h"
#include "Describe.h"

#include <string>
#include <set>
#include <unordered_map>
#include <vector>
#include <optional>

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
    SourceLocation loc;
    std::string currentTargetArch;
    std::set<std::string> fflags;
    std::set<std::string> dfflags;
    std::set<std::string> usedSymbols;

    void use(const std::string& name)
    {
        usedSymbols.insert(name);
    }

    bool isUsed(const std::string& name) const
    {
        return usedSymbols.count(name) > 0;
    }

    void addDecl(const std::string& line)
    {
        if (writer)
            writer->addDecl(line);
    }

    const Operand* lookup(const std::string& name)
    {
        const Operand* op = scope ? scope->lookup(name) : nullptr;
        if (op)
        {
            if (!op->rawOp.empty() || op->text.find("inst") != std::string::npos)
                use("inst");
        }
        return op;
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

inline std::string getRawOpText(EvalContext& ctx, const SExpr& elem)
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

inline std::string formatChild(const PredicateResult& child, Precedence parentPrec)
{
    if (child.prec < parentPrec)
        return "(" + child.text + ")";
    return child.text;
}

class TargetBuilder
{
public:
    virtual ~TargetBuilder() = default;

    virtual std::string archName() const = 0;
    virtual std::string loweringClass() const = 0;
    virtual std::string destRegVar() const = 0;

    virtual void emitPreamble(CodeWriter& out, const std::vector<std::string>& fflags = {}, const std::vector<std::string>& dfflags = {}) = 0;
    virtual void emitPostamble(CodeWriter& out) = 0;

    virtual void emitAllocProduce(CodeWriter& out, EvalContext& ctx, TypeKind type, bool isReuse, const std::vector<std::string>& rawOps) = 0;

    virtual Operand emitAllocTemp(CodeWriter& out, EvalContext& ctx, TypeKind type, const std::string& varName) = 0;

    virtual std::string resolveMemBase(CodeWriter& out, EvalContext& ctx, const SExpr& baseExpr, const Operand& baseOp, const std::string& destReg = "") = 0;

    virtual void emitJumpOrFallthrough(CodeWriter& out, EvalContext& ctx, const std::string& blockOp)
    {
        ctx.use("next");
        out.line("lowering.jumpOrFallthrough(lowering.blockOp(" + blockOp + "), next);");
    }

    virtual void emitUnsupported(CodeWriter& out)
    {
        out.line("CODEGEN_ASSERT(!\"Unsupported instruction form\");");
    }

    // Architecture-dependent operation dispatch hook
    virtual bool emitArchOp(CodeWriter& out, EvalContext& ctx, const std::string& opName, const std::vector<Operand>& args)
    {
        return false;
    }

    // Architecture-dependent predicate hook
    virtual std::optional<PredicateResult> evalArchPredicate(EvalContext& ctx, const SExpr& expr, const std::string& destReg)
    {
        return std::nullopt;
    }

    // Architecture-dependent expression / operand materialization hook
    virtual std::optional<Operand> evalArchExpr(CodeWriter& out, EvalContext& ctx, const SExpr& expr, const std::string& destReg)
    {
        return std::nullopt;
    }

    // Whether this expression must be eagerly materialized into a local variable in let bindings
    virtual bool isEagerBindingExpr(const std::string& opName) const
    {
        return false;
    }

    virtual void getArchOpDocs(std::vector<FormDoc>& out) const {}
    virtual void getArchPredicateDocs(std::vector<FormDoc>& out) const {}
};

} // namespace Luwu::Dsl
