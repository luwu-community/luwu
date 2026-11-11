// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Bytecode.h"

#include <iomanip>
#include <set>
#include <sstream>

namespace Luwu::Dsl
{

void dumpBytecode(std::ostream& out, const BytecodeFunction& function)
{
    std::vector<const BytecodeFunction*> functions;
    std::unordered_map<const BytecodeFunction*, size_t> ids;
    std::function<void(const BytecodeFunction*)> visit = [&](const BytecodeFunction* code) {
        if (!code || ids.count(code)) return;
        ids.emplace(code, functions.size());
        functions.push_back(code);
        for (const auto* child : code->children) visit(child);
        for (const auto& plan : code->cases)
            for (const auto& clause : plan.clauses) { visit(clause.fender); visit(clause.body); }
    };
    visit(&function);
    std::ostringstream text;
    for (const BytecodeFunction* code : functions)
    {
        text << ".function f" << ids.at(code) << " (";
        for (size_t i = 0; i < code->params.size(); ++i) text << (i ? " " : "") << code->params[i];
        if (!code->restParam.empty()) text << (code->params.empty() ? "" : " . ") << code->restParam;
        text << ") registers=" << code->registerCount << " ; " << code->form->loc.toString() << '\n';
        for (size_t i = 0; i < code->localNames.size(); ++i) text << "  .local l" << i << " " << code->localNames[i] << '\n';
        for (size_t i = 0; i < code->captures.size(); ++i)
        {
            const auto& capture = code->captures[i];
            text << "  .upvalue u" << i << " <- parent." << (capture.local ? "l" : "u") << capture.index << " " << capture.name << '\n';
        }
        for (size_t i = 0; i < code->constants.size(); ++i) text << "  .constant k" << i << " " << toString(code->constants[i]) << '\n';
        for (size_t i = 0; i < code->cases.size(); ++i)
        {
            text << "  .syntax-case " << i << '\n';
            for (const auto& clause : code->cases[i].clauses)
            {
                text << "    pattern ";
                clause.clause->elements[0].dump(text);
                if (clause.fender) text << " fender=f" << ids.at(clause.fender);
                text << " body=f" << ids.at(clause.body) << '\n';
            }
        }
        std::set<uint32_t> labels;
        for (const auto& instruction : code->instructions)
        {
            if (instruction.op == BytecodeOp::Jump) labels.insert(instruction.a);
            if (instruction.op == BytecodeOp::JumpFalse) labels.insert(instruction.b);
        }
        for (size_t pc = 0; pc <= code->instructions.size(); ++pc)
        {
            if (labels.count(uint32_t(pc))) text << "L" << pc << ":\n";
            if (pc == code->instructions.size()) break;
            const auto& instruction = code->instructions[pc];
            const SExpr& source = *instruction.source;
            std::ostringstream operands;
            const char* name = "";
            auto reg = [&]() { operands << "r" << instruction.a; };
            switch (instruction.op)
            {
            case BytecodeOp::Constant: name = "CONST"; reg(); operands << ", k" << instruction.b; break;
            case BytecodeOp::Quote: name = "QUOTE"; reg(); operands << ", "; source.elements[1].dump(operands); break;
            case BytecodeOp::LoadLocal: name = "GETLOCAL"; reg(); operands << ", l" << instruction.b; break;
            case BytecodeOp::LoadUpvalue: name = "GETUPVAL"; reg(); operands << ", u" << instruction.b; break;
            case BytecodeOp::LoadName:
            case BytecodeOp::LoadGlobal:
                name = instruction.op == BytecodeOp::LoadGlobal ? "GETGLOBAL" : "GETNAME";
                reg(); operands << ", " << source.text; break;
            case BytecodeOp::StoreLocal: name = "SETLOCAL"; operands << "l" << instruction.b << ", "; reg(); break;
            case BytecodeOp::StoreUpvalue: name = "SETUPVAL"; operands << "u" << instruction.b << ", "; reg(); break;
            case BytecodeOp::SetName:
            case BytecodeOp::SetGlobal:
                name = instruction.op == BytecodeOp::SetGlobal ? "SETGLOBAL" : "SETNAME";
                operands << source.elements[1].text << ", "; reg(); break;
            case BytecodeOp::Define: name = "DEFINE"; operands << source.elements[1].text << ", "; reg(); break;
            case BytecodeOp::Closure: name = "CLOSURE"; reg(); operands << ", f" << ids.at(code->children[instruction.b]); break;
            case BytecodeOp::JumpFalse: name = "JUMPFALSE"; reg(); operands << ", L" << instruction.b; break;
            case BytecodeOp::Jump: name = "JUMP"; operands << "L" << instruction.a; break;
            case BytecodeOp::Call:
            case BytecodeOp::TailCall:
                name = instruction.op == BytecodeOp::Call ? "CALL" : "TAILCALL";
                reg(); operands << ", r" << instruction.b << ", argc=" << instruction.c; break;
            case BytecodeOp::Return: name = "RETURN"; reg(); break;
            case BytecodeOp::Syntax: name = "SYNTAX"; reg(); operands << ", "; source.elements[1].dump(operands); break;
            case BytecodeOp::SyntaxCase: name = "SYNTAXCASE"; reg(); operands << ", plan=" << instruction.b; break;
            case BytecodeOp::Invalid: name = "INVALID"; source.dump(operands); break;
            }
            text << "  " << std::right << std::setw(4) << pc << "  " << std::left << std::setw(12) << name
                 << operands.str() << " ; " << source.loc.toString() << '\n';
        }
        text << ".end\n\n";
    }
    out << text.str();
}

} // namespace Luwu::Dsl
