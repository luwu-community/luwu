// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Bytecode.h"

#include "Core/CompileError.h"

#include <algorithm>
#include <limits>

namespace Luwu::Dsl
{

void parseParams(const SExpr& list, std::vector<std::string>& params, std::string& rest);

void BytecodeFunction::markChildren(std::vector<const GcCell*>& worklist) const
{
    for (const Value& value : constants)
        if (const GcCell* cell = value.cell()) worklist.push_back(cell);
    for (const BytecodeFunction* child : children) worklist.push_back(child);
    for (const BytecodeCasePlan& plan : cases)
        for (const BytecodeCase& clause : plan.clauses)
        {
            if (clause.fender) worklist.push_back(clause.fender);
            worklist.push_back(clause.body);
        }
}

namespace
{

struct Compiler
{
    BytecodeFunction* code = gcAlloc<BytecodeFunction>();
    Compiler* parent = nullptr;
    bool globalContext = false;
    uint32_t nextRegister = 1; // r0 is the result of the body.

    uint32_t reserve(uint32_t count)
    {
        if (count > std::numeric_limits<uint32_t>::max() - nextRegister)
            throw CompileError(*code->form, "Bytecode register limit exceeded");
        uint32_t base = nextRegister;
        nextRegister += count;
        code->registerCount = std::max(code->registerCount, nextRegister);
        return base;
    }

    size_t emit(BytecodeOp op, const SExpr& source, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0)
    {
        code->instructions.push_back({op, a, b, c, &source});
        return code->instructions.size() - 1;
    }

    void constant(const SExpr& e, uint32_t dest, Value value)
    {
        uint32_t index = uint32_t(code->constants.size());
        code->constants.push_back(std::move(value));
        emit(BytecodeOp::Constant, e, dest, index);
    }

    bool local(const SExpr& name, uint32_t& slot)
    {
        if (name.syntax && name.syntax->globalBinding) return false;
        auto found = code->localIndices.find(name.text);
        if (found == code->localIndices.end()) return false;
        slot = found->second;
        return true;
    }

    int upvalue(const SExpr& name)
    {
        if (!parent || (name.syntax && name.syntax->globalBinding)) return -1;
        uint32_t slot;
        bool isLocal = parent->local(name, slot);
        int inherited = isLocal ? -1 : parent->upvalue(name);
        if (!isLocal && inherited < 0) return -1;
        uint32_t index = isLocal ? slot : uint32_t(inherited);
        for (size_t i = 0; i < code->captures.size(); ++i)
            if (code->captures[i].local == isLocal && code->captures[i].index == index) return int(i);
        code->captures.push_back({isLocal, index, name.text});
        code->captureIndices.emplace(name.text, uint32_t(code->captures.size() - 1));
        return int(code->captures.size() - 1);
    }

    void addLocal(const std::string& name)
    {
        if (code->localIndices.count(name)) return;
        code->localIndices.emplace(name, uint32_t(code->localNames.size()));
        code->localNames.push_back(name);
    }

    void definitions(const SExpr& e)
    {
        if (!e.isList() || e.elements.empty() || e.isList("quote") || e.isList("lambda") ||
            e.isList("syntax") || e.isList("syntax-case")) return;
        if (e.isList("define")) addLocal(e.elements[1].text);
        for (const SExpr& child : e.elements) definitions(child);
    }

    void dynamicNames(const SExpr& e)
    {
        uint32_t slot;
        if (e.isSymbol()) { if (!local(e, slot)) upvalue(e); return; }
        if (!e.isList() || e.isList("quote") || e.isList("syntax")) return;
        if (e.isList("syntax-case"))
        {
            dynamicNames(e.elements[1]);
            for (size_t i = 3; i < e.elements.size(); ++i)
                for (size_t j = 1; j < e.elements[i].elements.size(); ++j) dynamicNames(e.elements[i].elements[j]);
            return;
        }
        for (const SExpr& child : e.elements) dynamicNames(child);
    }

    void body(const SExpr& form, size_t start)
    {
        if (start == form.elements.size())
        {
            constant(form, 0, Value::nil());
            emit(BytecodeOp::Return, form, 0);
        }
        for (size_t i = start; i < form.elements.size(); ++i)
            expression(form.elements[i], 0, i + 1 == form.elements.size());
    }

    const BytecodeFunction* lambda(const SExpr& form)
    {
        Compiler child;
        child.code->form = &form;
        parseParams(form.elements[1], child.code->params, child.code->restParam);
        for (const std::string& param : child.code->params) child.addLocal(param);
        if (!child.code->restParam.empty()) child.addLocal(child.code->restParam);
        for (size_t i = 2; i < form.elements.size(); ++i) child.definitions(form.elements[i]);
        child.parent = this;
        child.globalContext = globalContext;
        // Internal defines bind only when executed. Before then, lookup/set!
        // still sees an outer binding of the same name, if one exists.
        for (size_t i = child.code->params.size(); i < child.code->localNames.size(); ++i)
            if (child.code->localNames[i] != child.code->restParam)
                child.upvalue(SExpr::symbol(child.code->localNames[i], form.loc));
        child.body(form, 2);
        return child.code;
    }

    void expression(const SExpr& e, uint32_t dest, bool tail)
    {
        uint32_t saved = nextRegister;
        switch (e.kind)
        {
        case SExprKind::Number:
        {
            const std::string& t = e.text;
            bool hex = t.size() > 2 && (t[1] == 'x' || t[1] == 'X' || (t.size() > 3 && (t[2] == 'x' || t[2] == 'X')));
            constant(e, dest, !hex && t.find_first_of(".eE") != std::string::npos ? Value::real(std::stod(t)) : Value::integer(e.intValue));
            break;
        }
        case SExprKind::String: constant(e, dest, Value::string(e.text)); break;
        case SExprKind::Keyword: constant(e, dest, Value::keyword(e.text)); break;
        case SExprKind::Symbol:
        {
            uint32_t slot;
            if (e.text == "#t" || e.text == "#f") constant(e, dest, Value::boolean(e.text == "#t"));
            else if (local(e, slot)) emit(BytecodeOp::LoadLocal, e, dest, slot);
            else
            {
                int captured = upvalue(e);
                if (captured >= 0) emit(BytecodeOp::LoadUpvalue, e, dest, uint32_t(captured));
                else emit(globalContext || (e.syntax && e.syntax->globalBinding) ? BytecodeOp::LoadGlobal : BytecodeOp::LoadName, e, dest);
            }
            break;
        }
        case SExprKind::List:
        case SExprKind::BracketList:
        {
            if (e.elements.empty()) { emit(BytecodeOp::Invalid, e); break; }
            const SExpr& head = e.elements[0];
            if (head.isSymbol("quote")) emit(BytecodeOp::Quote, e, dest);
            else if (head.isSymbol("lambda"))
            {
                uint32_t index = uint32_t(code->children.size());
                code->children.push_back(lambda(e));
                emit(BytecodeOp::Closure, e, dest, index);
            }
            else if (head.isSymbol("if"))
            {
                uint32_t condition = reserve(1);
                expression(e.elements[1], condition, false);
                size_t jump = emit(BytecodeOp::JumpFalse, e, condition);
                nextRegister = saved;
                expression(e.elements[2], dest, tail);
                size_t end = emit(BytecodeOp::Jump, e);
                code->instructions[jump].b = uint32_t(code->instructions.size());
                if (e.elements.size() == 4) expression(e.elements[3], dest, tail);
                else { constant(e, dest, Value::nil()); if (tail) emit(BytecodeOp::Return, e, dest); }
                code->instructions[end].a = uint32_t(code->instructions.size());
                nextRegister = saved;
                return;
            }
            else if (head.isSymbol("begin"))
            {
                if (e.elements.size() == 1) constant(e, dest, Value::nil());
                for (size_t i = 1; i < e.elements.size(); ++i)
                    expression(e.elements[i], dest, tail && i + 1 == e.elements.size());
                if (e.elements.size() > 1) return;
            }
            else if (head.isSymbol("define") || head.isSymbol("set!"))
            {
                expression(e.elements[2], dest, false);
                uint32_t slot;
                if (head.isSymbol("define")) emit(BytecodeOp::Define, e, dest);
                else if (local(e.elements[1], slot)) emit(BytecodeOp::StoreLocal, e, dest, slot);
                else
                {
                    int captured = upvalue(e.elements[1]);
                    if (captured >= 0) emit(BytecodeOp::StoreUpvalue, e, dest, uint32_t(captured));
                    else emit(globalContext || (e.elements[1].syntax && e.elements[1].syntax->globalBinding) ?
                        BytecodeOp::SetGlobal : BytecodeOp::SetName, e, dest);
                }
            }
            else if (head.isSymbol("syntax")) emit(BytecodeOp::Syntax, e, dest);
            else if (head.isSymbol("syntax-case"))
            {
                expression(e.elements[1], dest, false);
                BytecodeCasePlan plan;
                plan.form = &e;
                plan.literals = e.elements[2].elements;
                for (size_t i = 3; i < e.elements.size(); ++i)
                {
                    const SExpr& clause = e.elements[i];
                    for (size_t j = 1; j < clause.elements.size(); ++j) dynamicNames(clause.elements[j]);
                    // A syntax-case introduces a dynamic pattern scope. Compile
                    // its fragments against that environment, not outer depths.
                    plan.clauses.push_back({&clause,
                        clause.elements.size() == 3 ? compileBytecode(clause.elements[1]) : nullptr,
                        compileBytecode(clause.elements.back())});
                }
                uint32_t index = uint32_t(code->cases.size());
                code->cases.push_back(std::move(plan));
                emit(BytecodeOp::SyntaxCase, e, dest, index);
            }
            else
            {
                if (e.elements.size() > std::numeric_limits<uint32_t>::max()) throw CompileError(e, "Too many call arguments");
                uint32_t base = reserve(uint32_t(e.elements.size()));
                for (size_t i = 0; i < e.elements.size(); ++i) expression(e.elements[i], base + uint32_t(i), false);
                emit(tail ? BytecodeOp::TailCall : BytecodeOp::Call, e, dest, base, uint32_t(e.elements.size() - 1));
                nextRegister = saved;
                if (tail) return;
            }
            break;
        }
        }
        nextRegister = saved;
        if (tail) emit(BytecodeOp::Return, e, dest);
    }
};

} // namespace

const BytecodeFunction* compileBytecode(const SExpr& expression, bool globalContext)
{
    Compiler compiler;
    compiler.globalContext = globalContext;
    compiler.code->form = &expression;
    compiler.expression(expression, 0, true);
    return compiler.code;
}

} // namespace Luwu::Dsl
