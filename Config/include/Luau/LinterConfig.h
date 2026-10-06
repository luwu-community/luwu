// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Luau/Location.h"

#include <bitset>
#include <string>
#include <vector>

#include <stdint.h>

namespace Luau
{

struct HotComment;

// Luwu: one bit per lint, indexed by LintWarning::Code. Upstream's masks are a uint64_t, which Luwu's lints outgrew; raise
// the bit count when the static_assert below fires.
constexpr size_t kLintMaskBits = 128;
using LintMask = std::bitset<kLintMaskBits>;

struct LintWarning
{
    // Make sure any new lint codes are documented here: https://luau.org/lint
    // Note that in Studio, the active set of lint warnings is determined by FStringStudioLuauLints
    enum Code
    {
        Code_Unknown = 0,

        Code_UnknownGlobal = 1, // superseded by type checker
        Code_DeprecatedGlobal = 2,
        Code_GlobalUsedAsLocal = 3,
        Code_LocalShadow = 4,       // disabled in Studio
        Code_SameLineStatement = 5, // disabled in Studio
        Code_MultiLineStatement = 6,
        Code_LocalUnused = 7,    // disabled in Studio
        Code_FunctionUnused = 8, // disabled in Studio
        Code_ImportUnused = 9,   // disabled in Studio
        Code_BuiltinGlobalWrite = 10,
        Code_PlaceholderRead = 11,
        Code_UnreachableCode = 12,
        Code_UnknownType = 13,
        Code_ForRange = 14,
        Code_UnbalancedAssignment = 15,
        Code_ImplicitReturn = 16, // disabled in Studio, superseded by type checker in strict mode
        Code_DuplicateLocal = 17,
        Code_FormatString = 18,
        Code_TableLiteral = 19,
        Code_UninitializedLocal = 20,
        Code_DuplicateFunction = 21,
        Code_DeprecatedApi = 22,
        Code_TableOperations = 23,
        Code_DuplicateCondition = 24,
        Code_MisleadingAndOr = 25,
        Code_CommentDirective = 26,
        Code_IntegerParsing = 27,
        Code_ComparisonPrecedence = 28,
        Code_RedundantNativeAttribute = 29,
        Code_NilNoneComparison = 30, // Luwu: comparing with nil where only none is possible, or the reverse
        Code_VarargCast = 31,        // Luwu: `f(... :: T)` passes only the first value
        Code_DeclareMismatch = 32,   // Luwu: `declare x: T` gives a global from the loaded definitions a different type
        Code_OptimizationHint = 33,  // Luwu: code the compiler could optimize better if it were written differently
        // Luwu: parts of OptimizationHint that can be turned off on their own. Turning OptimizationHint off turns these off too.
        Code_LoopConcat = 34,              // `s ..= x` in a loop
        Code_InefficientTableInsert = 35,  // `table.insert(t, 1, v)` in a loop
        Code_InefficientTableRemove = 36,  // `table.remove(t, 1)` in a loop
        Code_BareNolint = 37,              // Luwu: `--!nolint` or `@nolint` without lint names
        Code_RemoveWhileIterating = 38,    // Luwu: `table.remove(t, i)` in a forward loop over `t` by `i`
        Code_LoopVariableWrite = 39,       // Luwu: assigning a loop variable, which doesn't affect the loop
        Code_IteratedTableWrite = 40,      // Luwu: writing other keys of a table while iterating it
        Code_ForeverLoop = 41,             // Luwu: a `while`/`repeat` whose condition nothing in the loop changes
        Code_UselessLoop = 42,             // Luwu: a loop whose body always leaves it on the first iteration
        Code_StringIndexZero = 43,         // Luwu: `string.byte(s, 0)` / `string.sub(s, 0, ...)`
        Code_NewValueComparison = 44,      // Luwu: `x == {}` or `x == function() end`, which can never be equal
        Code_TableTruthiness = 45,         // Luwu: `if t then` on an array or map, which is truthy even when empty
        Code_DiscardedResult = 46,         // Luwu: a pure builtin or `@nodiscard` function called for nothing
        Code_ConstLocal = 47,              // Luwu: a `local` never reassigned, which could be `const`. Off by default.
        Code_FloatIndex = 48,              // Luwu: `xs[#xs / 2]`, an index computed with `/`, which gives a float
        Code_LuaIterators = 49,            // Luwu: Lua's iterator functions, which generalized iteration replaces
        // Luwu: parts of LuaIterators that can be turned off on their own. Turning LuaIterators off turns these off too.
        Code_Pairs = 50,                   // `pairs(t)`
        Code_Ipairs = 51,                  // `ipairs(t)`
        Code_LuaAndOr = 52,                // Luwu: `a and b or c`, which is `if a then b else c` only when `b` is truthy
        Code_SelfAssignment = 53,          // Luwu: `x = x`, which does nothing
        Code_DeadStore = 54,               // Luwu: a computed value overwritten before anything reads it
        // Luwu: more parts of OptimizationHint
        Code_MethodsNotInlined = 55,       // code that keeps the compiler from proving a class, so its methods aren't inlined
        Code_FloorDivision = 56,           // `math.floor(a / b)`, which is `a // b`
        Code_FenvDeoptimization = 57,      // `getfenv`/`setfenv`, which deoptimize the whole module
        Code_ReturnSelf = 58,              // Luwu Traits: a trait method returning `self` typed as the trait, not `Self`
        Code_ReturnOnNextLine = 59,        // Luwu: a `return` in a `do` expression whose value starts on the next line
        Code_OrContinue = 60,              // Luwu: `x or continue`, which reads a binding named `continue`
        Code_KeywordShadow = 61,           // Luwu: a binding named after a contextual keyword (`class`, `continue`, ...)
        Code_BuiltinShadow = 62,           // Luwu: a binding that hides a builtin global (`type`, `typeof`, `table`, ...)

        Code__Count
    };

    Code code;
    Location location;
    std::string text;

    static const char* getName(Code code);
    static Code parseName(const char* name);
    // Luwu: `All`, which `--!nolint All` and `@[nolint(All)]` take to mean every lint, on purpose
    static bool isAllName(const char* name);
    static LintMask parseMask(const std::vector<HotComment>& hotcomments);
    // Luwu: the lints `--!lint Name` directives turn on, for the ones that are off by default
    static LintMask parseEnableMask(const std::vector<HotComment>& hotcomments);
};

static_assert(LintWarning::Code__Count <= kLintMaskBits, "more lints than LintMask has bits; raise kLintMaskBits");

struct LintOptions
{
    LintMask warningMask;

    void enableWarning(LintWarning::Code code)
    {
        warningMask.set(code);
    }
    void disableWarning(LintWarning::Code code)
    {
        warningMask.reset(code);
    }

    bool isEnabled(LintWarning::Code code) const
    {
        return warningMask.test(code);
    }

    void setDefaults();
};

// clang-format off
inline constexpr const char* kWarningNames[] = {
    "Unknown",

    "UnknownGlobal",
    "DeprecatedGlobal",
    "GlobalUsedAsLocal",
    "LocalShadow",
    "SameLineStatement",
    "MultiLineStatement",
    "LocalUnused",
    "FunctionUnused",
    "ImportUnused",
    "BuiltinGlobalWrite",
    "PlaceholderRead",
    "UnreachableCode",
    "UnknownType",
    "ForRange",
    "UnbalancedAssignment",
    "ImplicitReturn",
    "DuplicateLocal",
    "FormatString",
    "TableLiteral",
    "UninitializedLocal",
    "DuplicateFunction",
    "DeprecatedApi",
    "TableOperations",
    "DuplicateCondition",
    "MisleadingAndOr",
    "CommentDirective",
    "IntegerParsing",
    "ComparisonPrecedence",
    "RedundantNativeAttribute",
    "NilNoneComparison",
    "VarargCast",
    "DeclareMismatch",
    "OptimizationHint",
    "LoopConcat",
    "InefficientTableInsert",
    "InefficientTableRemove",
    "BareNolint",
    "RemoveWhileIterating",
    "LoopVariableWrite",
    "IteratedTableWrite",
    "ForeverLoop",
    "UselessLoop",
    "StringIndexZero",
    "NewValueComparison",
    "TableTruthiness",
    "DiscardedResult",
    "ConstLocal",
    "FloatIndex",
    "LuaIterators",
    "Pairs",
    "Ipairs",
    "LuaAndOr",
    "SelfAssignment",
    "DeadStore",
    "MethodsNotInlined",
    "FloorDivision",
    "FenvDeoptimization",
    "ReturnSelf",
    "ReturnOnNextLine",
    "OrContinue",
    "KeywordShadow",
    "BuiltinShadow",
};
// clang-format on

static_assert(std::size(kWarningNames) == unsigned(LintWarning::Code__Count), "did you forget to add warning to the list?");

} // namespace Luau
