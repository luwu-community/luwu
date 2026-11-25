// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <string>
#include <vector>

namespace Luau
{
namespace CodeGen
{

struct IrFunction;

enum class IrValidationTarget
{
    A64,
    // Operands may also have their target-specific :x64 kinds: IR translation and OptimizeFinalX64 use VM operands directly
    // on X64
    X64,
};

// Checks IR against the operand kinds declared with define-ircmd (Luna/defs/ir) and the basic block structure.
// Returns a description of every problem found; empty when the IR is valid.
std::vector<std::string> validateIr(const IrFunction& function, IrValidationTarget target);

// When FFlag::DebugCodegenVerifyIr is enabled: validates, and on failure prints the problems and asserts.
// `after` names the step that produced the IR.
void verifyIr(const IrFunction& function, IrValidationTarget target, const char* after);

} // namespace CodeGen
} // namespace Luau
