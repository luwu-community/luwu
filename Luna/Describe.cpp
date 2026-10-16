// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Describe.h"
#include "Lowering/RuleCompiler.h"
#include "Lowering/Emitter.h"

#include <iostream>
#include <vector>
#include <algorithm>

namespace Luwu::Dsl
{

static void printSection(std::ostream& os, const std::string& title, std::vector<FormDoc>& items)
{
    if (items.empty())
        return;

    std::sort(items.begin(), items.end(), [](const FormDoc& a, const FormDoc& b) {
        return a.name < b.name;
    });

    os << "## " << title << "\n\n";
    os << "| Name | Syntax | Description |\n";
    os << "| :--- | :--- | :--- |\n";
    for (const auto& item : items)
    {
        os << "| `" << item.name << "` | `" << item.syntax << "` | " << item.desc << " |\n";
    }
    os << "\n";
}

static void printTypeSection(std::ostream& os, const std::string& title, std::vector<TypeDoc>& items)
{
    if (items.empty())
        return;

    std::sort(items.begin(), items.end(), [](const TypeDoc& a, const TypeDoc& b) {
        return a.name < b.name;
    });

    os << "## " << title << "\n\n";
    os << "| Type | Context / Category | Description |\n";
    os << "| :--- | :--- | :--- |\n";
    for (const auto& item : items)
    {
        os << "| `" << item.name << "` | " << item.context << " | " << item.desc << " |\n";
    }
    os << "\n";
}

static void getCoreTypeDocs(std::vector<TypeDoc>& out)
{
    out = {
        {":any", "Subrules", "Untyped token or raw inlined AST expression (valid only in `define-subrule`)"},
        {":cond", "Rules & Subrules", "Comparison condition code (`IrCondition`, e.g. `IrCondition::Equal`)"},
        {":double", "Value / Storage", "Alias for `:f64` (64-bit IEEE-754 double-precision float)"},
        {":dword", "Value / Storage", "Alias for `:i32` (32-bit signed integer / dword)"},
        {":f32", "Value / Storage", "32-bit single-precision IEEE-754 floating point register or value"},
        {":f64", "Value / Storage", "64-bit double-precision IEEE-754 floating point register or value"},
        {":float", "Value / Storage", "Alias for `:f32` (32-bit IEEE-754 single-precision float)"},
        {":i16", "Value / Storage", "16-bit signed integer (halfword)"},
        {":i32", "Value / Storage", "32-bit signed integer (dword)"},
        {":i64", "Value / Storage", "64-bit signed integer (quadword)"},
        {":int", "Parameters & Values", "Signed 32-bit integer operand (`lowering.intOp`) or 32-bit signed integer"},
        {":label", "Rule Signatures", "Branch target label or raw IR operand handle"},
        {":op", "Rule Signatures", "Raw IR operand handle (alias for `:label`)"},
        {":ptr", "Value / Storage", "64-bit general-purpose pointer or memory address"},
        {":qword", "Value / Storage", "Alias for `:i64` (64-bit signed integer / quadword)"},
        {":reg", "Parameters & Values", "Hardware register operand (`lowering.regOp`) or general-purpose register"},
        {":tag", "Rule Signatures", "8-bit Luau data type tag operand (`lowering.tagOp`)"},
        {":u8", "Value / Storage", "8-bit unsigned integer (byte)"},
        {":u32", "Value / Storage", "32-bit unsigned integer (dword)"},
        {":u64", "Value / Storage", "64-bit unsigned integer (quadword)"},
        {":uint", "Parameters & Values", "Unsigned 32-bit integer operand (`lowering.uintOp`) or 32-bit unsigned integer"},
        {":upvalue", "Rule Signatures", "VM closure upvalue operand index (`vmUpvalueOp`)"},
        {":vec", "Value / Storage", "128-bit SIMD vector register"},
        {":vector", "Value / Storage", "Alias for `:vec` (128-bit SIMD vector register)"},
        {":void", "Value / Storage", "Untyped or effect-only operation without a produced value"},
        {":32", "Value / Storage", "Alias for `:i32` (32-bit integer)"},
        {":64", "Value / Storage", "Alias for `:i64` (64-bit integer)"},
    };
}

void printDocs(std::ostream& os, const std::string& archFilter)
{
    os << "# Luna DSL Forms and Predicates Reference\n\n";

    bool showCore = archFilter.empty() || archFilter == "all" || archFilter == "core";
    bool showA64 = archFilter.empty() || archFilter == "all" || archFilter == "a64";
    bool showX64 = archFilter.empty() || archFilter == "all" || archFilter == "x64";

    if (showCore)
    {
        std::vector<FormDoc> coreDocs;
        getCommonCoreDocs(coreDocs);
        printSection(os, "Core Forms & Expressions", coreDocs);

        std::vector<FormDoc> predDocs;
        getCommonPredicateDocs(predDocs);
        printSection(os, "Common Inspection Predicates", predDocs);

        std::vector<TypeDoc> typeDocs;
        getCoreTypeDocs(typeDocs);
        printTypeSection(os, "Core Types", typeDocs);
    }

    if (showA64)
    {
        std::vector<FormDoc> a64Ops, a64Preds;
        getA64Docs(a64Ops, a64Preds);
        printSection(os, "AArch64 (a64) Operations", a64Ops);
        printSection(os, "AArch64 (a64) Predicates", a64Preds);
    }

    if (showX64)
    {
        std::vector<FormDoc> x64Ops, x64Preds;
        getX64Docs(x64Ops, x64Preds);
        printSection(os, "x86-64 (x64) Operations", x64Ops);
        printSection(os, "x86-64 (x64) Predicates", x64Preds);
    }
}

} // namespace Luwu::Dsl
