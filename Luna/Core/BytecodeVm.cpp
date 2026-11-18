// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Bytecode.h"
#include "Core/Evaluator.h"
#include "Core/CompileError.h"
#include "Core/Record.h"

#include <algorithm>

namespace Luwu::Dsl
{

struct Evaluator::BytecodeRun
{
    struct Frame
    {
        const BytecodeFunction* code;
        EnvPtr env;
        Value procedure;
        size_t base = 0;
        size_t pc = 0;
        size_t returnRegister = 0;
    };

    std::vector<Frame> frames;
    std::vector<Value> registers;
    Value result;
    Value callee;
    std::vector<Value> arguments;
    BytecodeRun* previous = nullptr;
};

void Evaluator::markBytecode(std::vector<const GcCell*>& worklist) const
{
    auto mark = [&](const Value& value) { if (const GcCell* cell = value.cell()) worklist.push_back(cell); };
    for (const BytecodeRun* run = activeBytecode; run; run = run->previous)
    {
        mark(run->result);
        mark(run->callee);
        for (const Value& argument : run->arguments) mark(argument);
        for (const Value& value : run->registers) mark(value);
        for (const BytecodeRun::Frame& frame : run->frames)
        {
            worklist.push_back(frame.code);
            if (frame.env) worklist.push_back(frame.env);
            mark(frame.procedure);
        }
    }
}

static Value checkedValue(const Value* value, const SExpr& expression)
{
    if (!value) throw CompileError(expression, "Unbound symbol '" + expression.text + "'");
    if (value->kind == ValueKind::Unassigned)
        throw CompileError(expression, "'" + expression.text + "' is used before its letrec value is computed");
    return *value;
}

static std::string closureName(const Closure& closure)
{
    return closure.name.empty() ? "an anonymous lambda" : closure.name;
}

static Value documentedValue(Value value, const SExpr& form)
{
    if ((value.kind == ValueKind::Closure || value.kind == ValueKind::Macro) && value.closure->name.empty())
    {
        Closure* named = gcAlloc<Closure>(*value.closure);
        named->name = form.elements[1].text;
        named->docs = form.docs;
        if (form.trailingDoc) named->docs.push_back(*form.trailingDoc);
        value.closure = named;
    }
    else if (form.hasDocs() && recordField(value, "docs"))
    {
        std::vector<Value> lines;
        for (const std::string& line : form.docs) lines.push_back(Value::string(line));
        if (form.trailingDoc) lines.push_back(Value::string(*form.trailingDoc));
        value = recordWith(value, "docs", Value::list(lines));
    }
    return value;
}

Value Evaluator::executeBytecode(const BytecodeFunction* code, EnvPtr env)
{
    return run(nullptr, env, nullptr, nullptr, nullptr, code);
}

Value Evaluator::run(const SExpr* expression, EnvPtr env, const Value* function, std::vector<Value>* args,
    const SExpr* call, const BytecodeFunction* prepared)
{
    BytecodeRun run;
    run.previous = activeBytecode;
    activeBytecode = &run;
    struct Restore
    {
        BytecodeRun*& current;
        BytecodeRun* previous;
        ~Restore() { current = previous; }
    } restore{activeBytecode, run.previous};

    bool applying = function != nullptr;
    bool tail = true;
    size_t destination = 0;
    if (applying)
    {
        run.callee = *function;
        run.arguments = std::move(*args);
    }
    else
    {
        const BytecodeFunction* code = prepared ? prepared : compileBytecode(*expression, env == global);
        run.registers.resize(code->registerCount);
        run.frames.push_back({code, env, Value::nil()});
        if (!prepared && bytecodeDump) dumpBytecode(*bytecodeDump, *code);
    }

    auto finish = [&]() {
        if (run.frames.empty()) return true;
        size_t resultRegister = run.frames.back().returnRegister;
        run.registers.resize(run.frames.back().base);
        run.frames.pop_back();
        if (run.frames.empty()) return true;
        run.registers.resize(run.frames.back().base + run.frames.back().code->registerCount);
        run.registers[resultRegister] = std::move(run.result);
        run.result = Value::nil();
        return false;
    };

    for (;;)
    {
        if (gcCount() > gcThreshold()) collectGarbage();
        maxDepth = std::max(maxDepth, run.frames.size());
        if (applying)
        {
            applying = false;
            const SExpr& source = *call;
            while (run.callee.kind == ValueKind::Builtin && run.callee.builtin->name == "apply")
            {
                if (run.arguments.size() < 2) throw CompileError(source, "apply needs a procedure and a list: (apply f a ... list)");
                Value next = run.arguments[0];
                std::vector<Value> spread(run.arguments.begin() + 1, run.arguments.end() - 1);
                if (!listToVector(run.arguments.back(), spread))
                    throw CompileError(source, "apply's last argument must be a list, got " + describeKind(run.arguments.back()));
                run.callee = std::move(next);
                run.arguments = std::move(spread);
            }
            if (run.callee.kind == ValueKind::Closure)
            {
                const Closure& closure = *run.callee.closure;
                if (run.arguments.size() < closure.params.size() || (closure.restParam.empty() && run.arguments.size() > closure.params.size()))
                    throw CompileError(source, closureName(closure) + " takes " + (closure.restParam.empty() ? "" : "at least ") +
                        std::to_string(closure.params.size()) + " argument(s), got " + std::to_string(run.arguments.size()) +
                        " (defined at " + closure.loc.toString() + ")");
                const BytecodeFunction* code = closure.bytecode;
                if (!code) throw CompileError(source, "Procedure has no compiled bytecode");
                EnvPtr scope = gcAlloc<Env>(closure.env);
                scope->layout = code;
                scope->procedure = &closure;
                scope->slots.resize(code->localNames.size());
                scope->bound.resize(code->localNames.size());
                scope->captured.resize(code->localNames.size());
                for (size_t i = 0; i < closure.params.size(); ++i)
                {
                    scope->slots[i] = std::move(run.arguments[i]);
                    scope->bound[i] = true;
                }
                if (!closure.restParam.empty())
                {
                    uint32_t rest = code->localIndices.at(closure.restParam);
                    scope->slots[rest] = Value::list(std::vector<Value>(run.arguments.begin() + closure.params.size(), run.arguments.end()));
                    scope->bound[rest] = true;
                }
                size_t resultRegister = destination;
                if (tail && !run.frames.empty()) resultRegister = run.frames.back().returnRegister;
                if (tail && !run.frames.empty())
                {
                    BytecodeRun::Frame& frame = run.frames.back();
                    frame.code = code;
                    frame.env = scope;
                    frame.procedure = run.callee;
                    frame.pc = 0;
                    run.registers.resize(frame.base);
                    run.registers.resize(frame.base + code->registerCount);
                }
                else
                {
                    size_t base = run.registers.size();
                    run.registers.resize(base + code->registerCount);
                    run.frames.push_back({code, scope, run.callee, base, 0, resultRegister});
                }
                run.callee = Value::nil();
                run.arguments.clear();
                continue;
            }
            if (run.callee.kind == ValueKind::Parameter)
            {
                if (run.arguments.size() > 1) throw CompileError(source, "A parameter takes zero or one argument");
                if (run.arguments.empty()) run.result = run.callee.parameter->value;
                else { run.callee.parameter->value = run.arguments[0]; run.result = Value::nil(); }
            }
            else if (run.callee.kind == ValueKind::Builtin)
            {
                const Builtin& builtin = *run.callee.builtin;
                int count = int(run.arguments.size());
                if (count < builtin.minArgs || (builtin.maxArgs >= 0 && count > builtin.maxArgs))
                {
                    std::string expected = builtin.minArgs == builtin.maxArgs ? std::to_string(builtin.minArgs) :
                        builtin.maxArgs < 0 ? "at least " + std::to_string(builtin.minArgs) :
                        std::to_string(builtin.minArgs) + " to " + std::to_string(builtin.maxArgs);
                    throw CompileError(source, builtin.name + " takes " + expected + " argument(s), got " + std::to_string(count));
                }
                run.result = builtin.fn(run.arguments, source);
            }
            else if (run.callee.kind == ValueKind::Macro)
                throw CompileError(source, closureName(*run.callee.closure) + " is a macro, so it can't be called as a procedure (it's expanded where it's written)");
            else throw CompileError(source, toString(run.callee) + " (" + describeKind(run.callee) + ") isn't a procedure");

            run.callee = Value::nil();
            run.arguments.clear();
            if (tail || run.frames.empty()) { if (finish()) return run.result; }
            else
            {
                run.registers.resize(run.frames.back().base + run.frames.back().code->registerCount);
                run.registers[destination] = std::move(run.result);
                run.result = Value::nil();
            }
            continue;
        }

        BytecodeRun::Frame& frame = run.frames.back();
        const BytecodeInstruction instruction = frame.code->instructions[frame.pc++];
        const SExpr& source = *instruction.source;
        Value* registers = run.registers.data() + frame.base;
        switch (instruction.op)
        {
        case BytecodeOp::Constant: registers[instruction.a] = frame.code->constants[instruction.b]; break;
        case BytecodeOp::Quote: registers[instruction.a] = datumFromSExpr(source.elements[1]); break;
        case BytecodeOp::LoadLocal: registers[instruction.a] = checkedValue(frame.env->local(instruction.b), source); break;
        case BytecodeOp::LoadUpvalue:
            registers[instruction.a] = checkedValue(frame.procedure.closure->upvalues[instruction.b]->get(), source); break;
        case BytecodeOp::LoadName:
        case BytecodeOp::LoadGlobal:
            registers[instruction.a] = checkedValue((instruction.op == BytecodeOp::LoadGlobal ? global : frame.env)->lookup(source.text), source);
            break;
        case BytecodeOp::StoreLocal:
        case BytecodeOp::StoreUpvalue:
        case BytecodeOp::SetName:
        case BytecodeOp::SetGlobal:
        {
            const SExpr& name = source.elements[1];
            Value* slot = instruction.op == BytecodeOp::StoreLocal ? frame.env->local(instruction.b) :
                instruction.op == BytecodeOp::StoreUpvalue ? frame.procedure.closure->upvalues[instruction.b]->get() :
                (instruction.op == BytecodeOp::SetGlobal ? global : frame.env)->find(name.text);
            if (!slot) throw CompileError(source, "set! of '" + name.text + "', which isn't bound (define it first)");
            *slot = registers[instruction.a];
            registers[instruction.a] = Value::nil();
            break;
        }
        case BytecodeOp::Define:
            frame.env->define(source.elements[1].text, documentedValue(registers[instruction.a], source));
            registers[instruction.a] = Value::nil();
            break;
        case BytecodeOp::Closure:
        {
            const BytecodeFunction* code = frame.code->children[instruction.b];
            Closure* closure = gcAlloc<Closure>();
            // Ordinary lexical captures live in cells, not retained Env chains.
            // Keep an external environment for eval and syntax-case fragments.
            closure->env = frame.procedure.kind == ValueKind::Closure && frame.env->layout ? frame.env->parent : frame.env;
            closure->loc = source.loc;
            closure->params = code->params;
            closure->restParam = code->restParam;
            closure->bytecode = code;
            for (const BytecodeFunction::Capture& capture : code->captures)
                closure->upvalues.push_back(capture.local ? frame.env->capture(capture.index) : frame.procedure.closure->upvalues[capture.index]);
            Value value;
            value.kind = ValueKind::Closure;
            value.closure = closure;
            registers[instruction.a] = std::move(value);
            break;
        }
        case BytecodeOp::JumpFalse:
            if (!registers[instruction.a].isTruthy()) frame.pc = instruction.b;
            registers[instruction.a] = Value::nil();
            break;
        case BytecodeOp::Jump: frame.pc = instruction.a; break;
        case BytecodeOp::Call:
        case BytecodeOp::TailCall:
        {
            run.callee = std::move(registers[instruction.b]);
            registers[instruction.b] = Value::nil();
            run.arguments.clear();
            for (uint32_t i = 0; i < instruction.c; ++i)
            {
                run.arguments.push_back(std::move(registers[instruction.b + 1 + i]));
                registers[instruction.b + 1 + i] = Value::nil();
            }
            call = &source;
            destination = frame.base + instruction.a;
            tail = instruction.op == BytecodeOp::TailCall;
            // The compiler reserves each call window above every live outer
            // temporary. Reuse its space for the callee instead of retaining a
            // full register array in each suspended frame.
            run.registers.resize(frame.base + instruction.b);
            applying = true;
            break;
        }
        case BytecodeOp::Return:
            run.result = registers[instruction.a];
            if (finish()) return run.result;
            break;
        case BytecodeOp::Syntax:
        {
            PatternEnv combined;
            for (const PatternEnv& patterns : patternEnvStack)
                for (const auto& entry : patterns) combined[entry.first] = entry.second;
            registers[instruction.a] = transcribeTemplate(source.elements[1], combined, currentIntroScope, *this, source);
            break;
        }
        case BytecodeOp::SyntaxCase:
        {
            const BytecodeCasePlan& plan = frame.code->cases[instruction.b];
            Value& target = registers[instruction.a];
            if (!target.isSyntax()) target = datumToSyntax(Value::nil(), target, source.elements[1].loc);
            bool matched = false;
            for (const BytecodeCase& clause : plan.clauses)
            {
                PatternEnv patterns;
                if (!matchPattern(clause.clause->elements[0], target, plan.literals, patterns, 0, *this, source)) continue;
                patternEnvStack.push_back(std::move(patterns));
                struct RestorePatterns
                {
                    std::vector<PatternEnv>& stack;
                    ~RestorePatterns() { stack.pop_back(); }
                } restorePatterns{patternEnvStack};
                EnvPtr patternScope = gcAlloc<Env>(frame.env);
                for (const auto& entry : patternEnvStack.back())
                    if (entry.second.depth == 0) patternScope->define(entry.first, entry.second.leaf);
                if (clause.fender && !executeBytecode(clause.fender, patternScope).isTruthy()) continue;
                target = executeBytecode(clause.body, patternScope);
                matched = true;
                break;
            }
            if (!matched) throw CompileError(source, "syntax-case: no clause matched for " + toString(syntaxToDatum(target)));
            break;
        }
        case BytecodeOp::Invalid:
            throw CompileError(source, std::string(source.kind == SExprKind::List ? "()" : "[]") + " isn't an expression; nil is the empty list");
        }
    }
}

} // namespace Luwu::Dsl
