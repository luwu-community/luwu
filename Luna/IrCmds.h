// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include "Ast.h"
#include "Lowering/Emitter.h"

#include <optional>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace Luwu::Dsl
{

// (lowering (<rule> <args>...)): the rule that lowers a command, with every rule parameter given explicitly
struct IrCmdLowering
{
    std::string rule;
    std::vector<SExpr> args;
    SourceLocation loc;
};

// [name :kind|:kind...]: an operand and the operand kinds it may have
struct IrOperandDef
{
    std::string name;
    std::vector<std::string> kinds; // e.g. {"Rn", "Kn"}
    SourceLocation loc;
};

// One [:key [operand :kinds]...] group of target-specific or flag-specific extra operand kinds
struct IrOperandExtras
{
    std::string key; // target ("x64") or flag ("DebugLuauAbortingChecks")
    SourceLocation loc;
    std::vector<IrOperandDef> operands;
};

// A register or count in an effect: an operand (by index), optionally plus an offset, or a literal
struct IrEffectCountDef
{
    std::string kind = "none"; // "none", "literal", "int" (intOp), "uint" (uintOp), "low-byte" (uint8_t(intOp))
    int value = 0;             // the literal, or the operand index
};

// A VM register effect (derived from the facts)
struct IrVmEffectDef
{
    std::string kind; // "use", "maybe-use", "use-range", "use-varargs", "capture", "def", "maybe-def", "def-range"
    int op = 0;
    int offset = 0;
    IrEffectCountDef count;
};

// A value location effect (derived from the facts)
struct IrLocationEffectDef
{
    // "invalidate", "invalidate-keep-values", "invalidate-range", "restore-from-load", "restore-from-store", "rematerialize"
    std::string kind;
    int op = 0;
    int offset = 0;
    int valueOp = 0; // restore-from-store: the stored value
    IrEffectCountDef count;
    std::string conversion; // rematerialize: the IrCmd its restore location records
};

// A memory location in a fact: (tvalue a [offset] [field]), (vm-slot r), (vm-slots r count), (varargs r), (buffer ptr offset size)
struct IrFactLocationDef
{
    std::string kind;
    int op = 0;        // tvalue/buffer: the pointer or register operand; vm-*: the register operand
    int offset = 0;    // vm-*: (+ r N)
    int offsetOp = -1; // tvalue/buffer: the offset operand
    IrEffectCountDef count; // vm-slots
    int size = 0;           // buffer: bytes accessed
};

// One location of a (reads ...), (writes ...), (may-write ...), (captures ...) or (kills ...) item in (facts ...)
struct IrFactDef
{
    std::string kind; // "reads", "writes", "may-write", "captures", "kills"
    IrFactLocationDef loc;
    std::string field;     // (tvalue ...) field: "", "tag", "value", "extra", "tvalue", "split", "vector", "component"
    std::string valueKind; // :value fields: "double", "int", "int64", "pointer"
    int componentOp = -1;  // (component <offset operand>)
    std::vector<int> values; // writes: the operands holding what's written
    SourceLocation src;
};

// One combination of the conditions in a (facts ...) block (when/cond), with the facts that hold under it
struct IrFactVariantDef
{
    std::vector<std::string> conditions;   // C++ conditions on the instruction's operands, all of which hold
    std::vector<std::string> descriptions; // the same conditions as written in Luna
    // Only the conditions that aren't negations of earlier branches: variants are in decision-tree order, so trying them
    // in order, the first whose tests hold is the one whose conditions hold
    std::vector<std::string> tests;
    std::vector<IrFactDef> memory;         // in declaration order
};

// The (facts ...) block: everything a command declares about memory and what it establishes
struct IrCmdFacts
{
    bool declared = false;                       // a (facts ...) block was given
    std::vector<IrFactVariantDef> variants{1};   // without when/cond, one variant without conditions
    bool allocatesBufferData = false;            // (allocates :buffer-data)
    int establishesTagOf = -1;                   // (establishes (tag-of a) b): operand indices of a and b
    int establishesTag = -1;
    bool noVmEffects = false;                    // (no-vm-effects): references VM registers without reading or writing them
    int rematerializeOp = -1;                    // (rematerialize-from a :via CMD)
    std::string rematerializeVia;
};

// What the passes see for one variant of a command, derived from its facts, class and overrides
struct IrCmdEffectsDef
{
    std::vector<std::string> descriptions; // from the facts variant
    std::vector<std::string> tests;

    // VM register uses, captures and defs, in that order
    bool hasVmEffects = false;
    std::vector<IrVmEffectDef> vmEffects;

    // Saved value locations, before and after lowering
    bool hasLocationTracking = false;
    std::vector<IrLocationEffectDef> locationBefore;
    std::vector<IrLocationEffectDef> locationAfter;

    // Dead-store elimination
    std::string deadStore = "visit"; // "visit", "none", "live-ins", "live-ins-no-exit-sync", "live-outs", "allocation", "engine", "kills"
    int deadStoreTarget = -1;        // live-ins: the guard's target operand
    std::string deadStoreEngine;     // engine: "store-tag", ..., "check-tag"
    IrVmEffectDef kills;             // kills: the register range whose contents are dead
    int allocationWriteOp = -1;      // (writes (buffer a ...)): writes into fresh allocations
    int storeHintDest = -1;          // stores of a value: the destination and the value
    int storeHintValue = -1;
    std::string storeHintKind;       // IrValueKind enumerator, or "Unknown" to infer it from the value
};

// An IR command declared with define-ircmd
struct IrCmdDef
{
    std::string name; // e.g. "ADD_INT"
    SourceLocation loc;
    std::vector<std::string> docs;     // doc comments (~) before the form: the enum entry's comment
    std::vector<IrOperandDef> operands; // positional (A..G)
    // (target-specific [:x64 [a :Rn] ...] ...) and (flag-specific [:DebugLuauAbortingChecks [a :Rn] ...] ...): extra kinds
    // an operand may have when compiling for that target, or when that flag is enabled
    std::vector<IrOperandExtras> targetSpecific;
    std::vector<IrOperandExtras> flagSpecific;
    std::string result;                // IrValueKind enumerator ("Int", "Double", ...), empty for None
    std::string cls;                   // "value", "effect", "guard", "terminator" or "pseudo"
    std::set<std::string> flags;       // e.g. "deprecated/has-side-effects"
    std::set<std::string> heapEffects; // e.g. "invalidates-safe-env"
    std::optional<IrCmdLowering> lowering;

    // What the instruction reads, writes and establishes
    IrCmdFacts facts;

    // Overrides of what the class and facts give: (location-tracking :none) and (dead-store :visit|:none|:live-ins-no-exit-sync)
    bool locationsNone = false;
    std::string deadStoreOverride;

    // What the passes see: one entry per facts variant, in the order the generated selector tries them
    std::vector<IrCmdEffectsDef> effects;
};

// Parses the define-ircmd forms of a program (other top-level forms are ignored) and checks them
std::vector<IrCmdDef> parseIrCmds(const std::vector<SExpr>& program);

// Pairs each command with the rule its lowering clause names, binding the rule's parameters to the arguments.
// `rules` are the (expanded) define-rules; the result follows rule order, then command order.
std::vector<LoweringInstance> linkLowerings(const std::vector<IrCmdDef>& cmds, const std::vector<Rule>& rules);

// Every command in declaration order, for the generated lowering dispatch
std::vector<DispatchEntry> loweringDispatch(const std::vector<IrCmdDef>& cmds);

// CodeGen/include/Luau/generated/IrCmdInfo.h: per-command information (names, result kinds, classification)
void emitIrCmdInfo(std::ostream& os, const std::vector<IrCmdDef>& cmds);

// CodeGen/include/Luau/generated/IrCmdEffects.h: per-command VM register, value location and dead-store effects, with
// variants (and the getInstEffects selector) for commands whose facts depend on their operands
void emitIrCmdEffects(std::ostream& os, const std::vector<IrCmdDef>& cmds);

// CodeGen/include/Luau/generated/IrCmd.h: the IrCmd enum with its doc comments
void emitIrCmdEnum(std::ostream& os, const std::vector<IrCmdDef>& cmds);

} // namespace Luwu::Dsl
