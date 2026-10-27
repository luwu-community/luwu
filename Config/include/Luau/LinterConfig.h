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
        Code_TableLiteral = 19, // Luwu: a group of DuplicateKey, DuplicateProperty and DuplicateReadWrite
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
        Code_DiscardedValues = 58,         // Luwu: a call giving several values where only its first is kept (an if-else branch)
        // Luwu: `any` in a type, which turns off type checking. The group also reports every `any` its parts don't, and
        // is off by default; its parts are on by default in strict Luwu modules. Turning the group on or off turns them
        // on or off with it (LintWarning::withGroupParts).
        Code_Any = 59,
        Code_AnyCast = 60,                 // `x :: any` without a comment saying why
        Code_AnyAnnotation = 61,           // `x: any` on a parameter, local or property, which `unknown` would type safely
        Code_LuwuOnlyApi = 62,             // Luwu: a Luwu-only library function (`table.drop`) used in a `.luau` or `.lua` file
        Code_DuplicateKey = 63,            // Luwu: a key or index repeated in a table literal (part of TableLiteral)
        Code_DuplicateProperty = 64,       // Luwu: a property declared twice in a table type (part of TableLiteral)
        Code_DuplicateReadWrite = 65,      // Luwu: a table type property given its read or write type twice (part of TableLiteral)
        Code_ReturnSelf = 66,              // Luwu Traits: a trait method returning `self` typed as the trait, not `Self`
        Code_ReturnOnNextLine = 67,        // Luwu: a `return` in a `do` expression whose value starts on the next line
        Code_OrContinue = 68,              // Luwu: `x or continue`, which reads a binding named `continue`
        Code_KeywordShadow = 69,           // Luwu: a binding named after a contextual keyword (`class`, `continue`, ...)
        Code_BuiltinShadow = 70,           // Luwu: a binding that hides a builtin global (`type`, `typeof`, `table`, ...)
        Code_BindingSwallowsCondition = 71, // Luwu: `if local x = f() and x > 1`, where the `and` is part of the bound value
        Code_Style = 72,                   // Luwu: the group of layout lints; turning it off turns off all of its parts
        // Luwu: parts of Style that can be turned off on their own (ConstLocal, SameLineStatement and MultiLineStatement too)
        Code_OneLineStatement = 73,        // `if x then y end`, `while c do f() end`, `function f() return 1 end` on one line
        Code_OneLineGuard = 74,            // `if not x then return end` on one line, in a Luau file
        Code_InlineOrLeave = 75,           // `x or return` ending a line much longer than the lines around it
        Code_LuaGuard = 76,                // `if not x then return end` on one line, in a Luwu file, which can write `x or return`
        Code_OrReturnComma = 77,           // Luwu: `f(x or return a, b)`, where `b` is the next argument, not a second return value
        Code_MisleadingIndentation = 78,   // Luwu: a `do` expression's block reaching lines indented like the code after it

        Code_LuaStringHelper = 79,         // Luwu: `s:sub(1, #p) == p` and the like, which `s:startswith(p)` and friends do
        Code_StringOperations = 80,        // Luwu: a string library call that `""` makes pointless or wrong (`s:startswith("")`)
        Code_AmbiguousContinuation = 81,   // Luwu: a line starting with `-` or a string that continues the expression above it
        Code_UnflippedGuard = 82,          // Luwu: `x == nil or return` followed by code using `x`, which is always nil there

        Code__Count
    };

    Code code;
    Location location;
    std::string text;
    // Luwu: report it quietly. An editor shows it with the lowest severity (an LSP hint) and leaves it out of its list of
    // problems.
    bool hint = false;

    static const char* getName(Code code);
    static Code parseName(const char* name);
    // Luwu: `All`, which `--!nolint All` and `@[nolint(All)]` take to mean every lint, on purpose
    static bool isAllName(const char* name);
    static LintMask parseMask(const std::vector<HotComment>& hotcomments);
    // Luwu: the lints `--!lint Name` directives turn on, for the ones that are off by default
    static LintMask parseEnableMask(const std::vector<HotComment>& hotcomments);
    // Luwu: `mask` with the parts of the groups in it whose parts don't depend on the group being on (Any). Every place
    // that turns lints on or off by name applies it, so naming the group turns its parts on or off too.
    static LintMask withGroupParts(LintMask mask);
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
    "DiscardedValues",
    "Any",
    "AnyCast",
    "AnyAnnotation",
    "LuwuOnlyApi",
    "DuplicateKey",
    "DuplicateProperty",
    "DuplicateReadWrite",
    "ReturnSelf",
    "ReturnOnNextLine",
    "OrContinue",
    "KeywordShadow",
    "BuiltinShadow",
    "BindingSwallowsCondition",
    "Style",
    "OneLineStatement",
    "OneLineGuard",
    "InlineOrLeave",
    "LuaGuard",
    "OrReturnComma",
    "MisleadingIndentation",
    "LuaStringHelper",
    "StringOperations",
    "AmbiguousContinuation",
    "UnflippedGuard",
};
// clang-format on

// Luwu: the comment directives (`--!strict`, `--!nolint Name`), which the linter checks and autocomplete offers
inline constexpr const char* kCommentDirectives[] = {"nolint", "lint", "nocheck", "nonstrict", "strict", "optimize", "native", "trust"};

static_assert(std::size(kWarningNames) == unsigned(LintWarning::Code__Count), "did you forget to add warning to the list?");

} // namespace Luau
