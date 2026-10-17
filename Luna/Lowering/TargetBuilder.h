// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"
#include "Types.h"
#include "Scope.h"
#include "CodeWriter.h"
#include "CompileError.h"
#include "Describe.h"

#include <string>
#include <map>
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

    // Raw C++ names (e.g. rState, rClosure) that may appear unbound in rule bodies
    std::set<std::string> globalSymbols;

    // Name for the C++ variable declared by the binding currently being evaluated in a let
    std::string bindingName;

    // One Label per distinct guard target (A64) so different VM exits never share a label
    std::map<std::string, std::string> freshLabels;

    // C++ locals declared by let directly in each open block, innermost last (the function body is the first)
    std::vector<std::set<std::string>> blockLocals;

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

    // Takes the pending let binding name if any, otherwise a fresh temporary name
    std::string takeBindingName(const std::string& prefix = "tmp")
    {
        std::string name = std::move(bindingName);
        bindingName.clear();
        return name.empty() ? nextTemp(prefix) : name;
    }

    std::string freshLabelFor(const std::string& labelOp)
    {
        auto it = freshLabels.find(labelOp);
        if (it != freshLabels.end())
            return it->second;

        std::string name = freshLabels.empty() ? "fresh" : "fresh" + std::to_string(freshLabels.size() + 1);
        freshLabels[labelOp] = name;
        addDecl("Label " + name + ";");
        return name;
    }

    // Resolves a symbol that is not a bound variable; only known C++ globals may pass through
    const std::string& requireGlobal(const SExpr& sym) const
    {
        if (!globalSymbols.count(sym.text))
            throw CompileError(sym, "Unbound symbol '" + sym.text + "'");
        return sym.text;
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
        return ctx.requireGlobal(elem);
    }
    return elem.text;
}

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

// An operation defined by a C++ template. Placeholders {N} or {N:kind} are replaced by the N-th argument:
//   {N}         register operand (IR operands become lowering.regOp(...)), or a memory operand
//   {N:raw}     the argument as written (IrOp handles such as OP_A(inst), labels, C++ constants)
//   {N:int} {N:uint} {N:int64} {N:double} {N:tag}
//               constant value of an IR operand (lowering.intOp(...) etc.); literals pass through
//   {N:@fn}     lowering.fn(<raw>)          {N:%fn}  fn(<raw>)
//   {N:byte} {N:word} {N:dword} {N:qword} {N:xmmword}
//               memory operand with an explicit access size (X64)
// Statement templates are complete C++ statements (lines separated by '\n'); value templates are a C++ expression,
// which a let binding stores into an auto local.
struct TemplateOpSpec
{
    const char* fmt = nullptr;
    const char* desc = nullptr;
    bool isValue = false;
    bool bindsReference = false; // a let binding refers to the result (auto&) instead of copying it, e.g. Label&
};

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

    // Result register taken over from a physical register that already holds the value (e.g. a call's return register)
    virtual void emitTakeResult(CodeWriter& out, EvalContext& ctx, const std::string& physReg) = 0;

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

    // Raw C++ names (global registers / stack slots) that rules may reference without binding
    virtual std::set<std::string> globalSymbols() const
    {
        return {};
    }

    // C++ type of a local holding a register
    virtual std::string regVarType() const = 0;

    // Memory operand in the target's assembler syntax; size is an optional explicit access size (X64)
    virtual std::string formatMemOperand(EvalContext& ctx, const Operand& op, const std::string& size) const = 0;

    // Template-defined operations of this target (e.g. a64:fabs!)
    virtual const TemplateOpSpec* findTemplateOp(const std::string& name) const
    {
        return nullptr;
    }

    virtual void getTemplateOpDocs(std::vector<FormDoc>& ops, std::vector<FormDoc>& values) const {}

    // C++ expression for a register operand (e.g. lowering.regOp(OP_A(inst)) for an IR operand)
    virtual std::string formatRegOperand(const Operand& op) const = 0;

    // C++ expression for a temporary register that is only allocated when cond holds, for a conditional binding named
    // varName; may emit setup code first (e.g. an X64 ScopedRegX64 that allocates under the condition)
    virtual std::string conditionalTempExpr(CodeWriter& out, EvalContext& ctx, TypeKind type, const std::string& cond, const std::string& varName) = 0;

    virtual void getArchOpDocs(std::vector<FormDoc>& out) const {}
    virtual void getArchPredicateDocs(std::vector<FormDoc>& out) const {}
};

} // namespace Luwu::Dsl
