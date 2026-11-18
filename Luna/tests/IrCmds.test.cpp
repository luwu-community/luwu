// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"

using namespace LunaTest;

namespace
{

struct IrCmdFixture : LunaFixture
{
    void compile(const std::string& program)
    {
        file("root.luna", program);
        run({"root.luna", "out"});
    }
};

} // namespace

TEST_SUITE_BEGIN("LunaIrCmds");

TEST_CASE_FIXTURE(IrCmdFixture, "commands_generate_the_enum_and_command_information")
{
    compile("(define-ircmd NOP (class :pseudo))\n"
            "\n"
            "~ Shared docs\n"
            "~ A: int\n"
            "(define-ircmd FIRST (operands [a :any] [b :any]) (result :int) (class :value) (lowering 0))\n"
            "~ Shared docs\n"
            "~ A: int\n"
            "(define-ircmd SECOND (operands [a :any] [b :block]) (class :guard) (flags :deprecated/is-unsafe-to-sink) (lowering (gen:seq)))\n"
            "\n"
            "(define-ircmd THIRD (class :terminator) (heap-effects :invalidates-safe-env) (flags :dirty-high-bits :deprecated/has-side-effects)\n"
            "  (lowering (gen:seq)))");
    requireOk();
    CHECK(out.find("3 output(s) updated") != std::string::npos);

    // Identical consecutive docs share one comment block
    std::string enumHeader = read("out/IrCmd.h");
    CHECK(enumHeader.find("enum class IrCmd : uint8_t\n"
                          "{\n"
                          "    NOP,\n"
                          "\n"
                          "    // Shared docs\n"
                          "    // A: int\n"
                          "    FIRST,\n"
                          "    SECOND,\n"
                          "\n"
                          "    THIRD,\n"
                          "};\n") != std::string::npos);

    std::string info = read("out/IrCmdInfo.h");
    CHECK(info.find("inline constexpr size_t kIrCmdCount = 4;") != std::string::npos);
    CHECK(info.find("    case IrCmd::FIRST:\n        return IrValueKind::Int;") != std::string::npos);
    CHECK(info.find("inline bool isNonTerminatingJump(IrCmd cmd)\n{\n    switch (cmd)\n    {\n    case IrCmd::SECOND:\n        return true;") !=
          std::string::npos);
    CHECK(info.find("inline bool isBlockTerminator(IrCmd cmd)\n{\n    switch (cmd)\n    {\n    case IrCmd::THIRD:\n        return true;") !=
          std::string::npos);
    CHECK(info.find("inline bool canInvalidateSafeEnv(IrCmd cmd)\n{\n    switch (cmd)\n    {\n    case IrCmd::THIRD:\n") != std::string::npos);
    CHECK(info.find("inline bool isUnsafeToSink(IrCmd cmd)\n{\n    switch (cmd)\n    {\n    case IrCmd::SECOND:\n") != std::string::npos);
    CHECK(info.find("inline bool producesDirtyHighRegisterBits(IrCmd cmd)\n{\n    switch (cmd)\n    {\n    case IrCmd::THIRD:\n") != std::string::npos);

    // hasSideEffects: the flag, or no result; never for pseudo-instructions
    CHECK(info.find("inline bool hasSideEffects(IrCmd cmd)\n{\n    switch (cmd)\n    {\n    case IrCmd::SECOND:\n    case IrCmd::THIRD:\n        return true;") !=
          std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "command_errors")
{
    struct Case
    {
        const char* name;
        std::string program;
        const char* error;
    };

    std::string cmd = "(define-ircmd C (operands [a :any] [b :any]) (class :effect) (lowering (gen:seq)))\n";
    std::string manyPseudo;
    for (int i = 0; i < 257; ++i)
        manyPseudo += "(define-ircmd C" + std::to_string(i) + " (class :pseudo))\n";

    Case cases[] = {
        {"duplicate command", cmd + cmd, "Duplicate define-ircmd C"},
        {"NOP not first", cmd + "(define-ircmd NOP (class :pseudo))\n", "NOP must be the first define-ircmd"},
        {"lowercase name", "(define-ircmd lower (class :pseudo))", "needs an UPPER_SNAKE command name"},
        {"too many commands", manyPseudo, "More than 256 IR commands"},
        {"unknown clause", "(define-ircmd C (class :pseudo) (bogus))", "Unknown define-ircmd clause 'bogus'"},
        {"duplicate clause", "(define-ircmd C (class :pseudo) (class :pseudo))", "Duplicate class clause"},
        {"unknown class", "(define-ircmd C (class :bogus))", "class expects one of"},
        {"unknown result", "(define-ircmd C (result :bogus) (class :pseudo))", "result expects one of"},
        {"unknown flag", "(define-ircmd C (class :pseudo) (flags :bogus))", "Unknown flag ':bogus'"},
        {"unknown heap effect", "(define-ircmd C (class :pseudo) (heap-effects :bogus))", "Unknown heap effect ':bogus'"},
        {"missing class", "(define-ircmd C (result :int))", "C needs a (class ...) clause"},
        {"value without result", "(define-ircmd C (class :value) (lowering (gen:seq)))", "is a :value command but has no (result ...)"},
        {"effect with result", "(define-ircmd C (result :int) (class :effect) (lowering (gen:seq)))", "is an :effect command but has a result"},
        {"pseudo with lowering", "(define-ircmd C (class :pseudo) (lowering (gen:seq)))", "C is a pseudo-instruction and isn't lowered"},
        {"missing lowering", "(define-ircmd C (class :effect))", "C needs a (lowering ...) clause"},
        {"too many operands", "(define-ircmd C (operands [a :any] [b :any] [c :any] [d :any] [e :any] [f :any] [g :any] [h :any]) (class :pseudo))", "C has 8 operands"},
        {"duplicate operand", "(define-ircmd C (operands [a :any] [a :any]) (class :pseudo))", "Duplicate operand 'a'"},
        {"docs inside the form", "(define-ircmd C\n  ~ the class\n  (class :pseudo))", "Doc comments (~) inside define-ircmd aren't supported yet"},
    };

    for (const auto& c : cases)
    {
        CAPTURE(c.name);
        compile(c.program);
        requireError({c.error});
    }
}

TEST_CASE_FIXTURE(IrCmdFixture, "operand_kinds_generate_the_operand_table")
{
    compile("(define-ircmd NOP (class :pseudo))\n"
            "(define-ircmd GUARD (operands [a :Rn|:Kn] [b :const-tag] [c :block|:vmexit|:undef] [d :int|:none])\n"
            "  (target-specific [:x64 [a :inst-pointer]] [:a64 [d :Kn]])\n"
            "  (flag-specific [:DebugLuauAbortingChecks [b :tag]])\n"
            "  (class :guard) (facts (reads (tvalue a))) (lowering (gen:seq)))");
    requireOk();

    std::string info = read("out/IrCmdInfo.h");
    CHECK(info.find("    // NOP\n    {0, {}, {}, {}, {}},\n") != std::string::npos);
    CHECK(info.find("    // GUARD\n"
                    "    {4, {{irOpKinds(IrOpKind::VmReg, IrOpKind::VmConst), irValueKinds(), irConstKinds()}, "
                    "{irOpKinds(IrOpKind::Constant), irValueKinds(), irConstKinds(IrConstKind::Tag)}, "
                    "{irOpKinds(IrOpKind::Block, IrOpKind::VmExit, IrOpKind::Undef), irValueKinds(), irConstKinds()}, "
                    "{irOpKinds(IrOpKind::Inst, IrOpKind::Constant, IrOpKind::None), irValueKinds(IrValueKind::Int), irConstKinds(IrConstKind::Int)}}, "
                    "{{}, {}, {}, {irOpKinds(IrOpKind::VmConst), irValueKinds(), irConstKinds()}}, "
                    "{{irOpKinds(IrOpKind::Inst), irValueKinds(IrValueKind::Pointer), irConstKinds()}, {}, {}, {}}, "
                    "{{}, {irOpKinds(IrOpKind::Inst, IrOpKind::Constant), irValueKinds(IrValueKind::Tag), irConstKinds(IrConstKind::Tag)}, {}, {}}},\n") !=
          std::string::npos);
    CHECK(info.find("IrOperandSpec targetA64[7];") != std::string::npos);
    CHECK(info.find("IrOperandSpec targetX64[7];") != std::string::npos);
    CHECK(info.find("IrOperandSpec flagDebugLuauAbortingChecks[7];") != std::string::npos);
    CHECK(info.find("inline const IrCmdOperands& getCmdOperands(IrCmd cmd)") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "operand_errors")
{
    struct Case
    {
        const char* name;
        std::string program;
        const char* error;
    };

    Case cases[] = {
        {"bare name", "(define-ircmd C (operands a) (class :pseudo))", "Operand must be [name :kind]"},
        {"unknown kind", "(define-ircmd C (operands [a :bogus]) (class :pseudo))", "Unknown operand kind ':bogus'"},
        {"unknown kind in a union", "(define-ircmd C (operands [a :Rn|:bogus]) (class :pseudo))", "Unknown operand kind ':bogus'"},
        {"duplicate kind", "(define-ircmd C (operands [a :Rn|:Rn]) (class :pseudo))", "Duplicate operand kind :Rn"},
        {"required after optional", "(define-ircmd C (operands [a :int|:none] [b :int]) (class :pseudo))", "Operand 'b' follows optional operand 'a'"},
        {"extra kinds for an unknown operand", "(define-ircmd C (operands [a :Rn]) (target-specific [:x64 [z :Kn]]) (class :pseudo))", "'z' isn't an operand of C"},
        {"duplicate extra kinds", "(define-ircmd C (operands [a :Rn]) (flag-specific [:DebugLuauAbortingChecks [a :Kn] [a :tag]]) (class :pseudo))",
            "Duplicate extra kinds for 'a' in the :DebugLuauAbortingChecks group"},
        {"no groups", "(define-ircmd C (operands [a :Rn]) (target-specific) (class :pseudo))", "target-specific needs at least one [:x64 [operand :kind]...] group"},
        {"empty group", "(define-ircmd C (operands [a :Rn]) (target-specific [:x64]) (class :pseudo))", "The :x64 group needs at least one [operand :kind]"},
        {"group without a key", "(define-ircmd C (operands [a :Rn]) (target-specific [a :Kn]) (class :pseudo))", "target-specific groups look like [:x64 [operand :kind]...]"},
        {"unknown target", "(define-ircmd C (operands [a :Rn]) (target-specific [:riscv [a :Kn]]) (class :pseudo))", "Unknown target :riscv (expected :a64, :x64)"},
        {"unknown flag", "(define-ircmd C (operands [a :Rn]) (flag-specific [:DebugBogus [a :Kn]]) (class :pseudo))", "Unknown flag :DebugBogus (expected :DebugLuauAbortingChecks)"},
        {"repeated target", "(define-ircmd C (operands [a :Rn]) (target-specific [:x64 [a :Kn]] [:x64 [a :tag]]) (class :pseudo))", "Duplicate target-specific group for :x64"},
    };

    for (const auto& c : cases)
    {
        CAPTURE(c.name);
        compile(c.program);
        requireError({c.error});
    }
}

TEST_CASE_FIXTURE(IrCmdFixture, "facts_generate_the_effects_table")
{
    compile("(define-ircmd NOP (class :pseudo))\n"
            // VM register ranges, a TValue read, a may-write wider than the defs and the guard default
            "(define-ircmd STEP (operands [ra :Rn] [k :Rn|:Kn] [n :const-int] [m :const-uint] [fail :block|:vmexit|:undef])\n"
            "  (class :guard)\n"
            "  (facts (writes (vm-slots (+ ra 3) (low-byte n))) (reads (vm-slot ra) (tvalue k) (vm-slots (+ ra 1) m)) (writes (vm-slot ra))\n"
            "    (may-write (vm-slots (+ ra 2) :to-top)))\n"
            "  (heap-effects :reenters-vm)\n"
            "  (lowering (gen:seq)))\n"
            // A load whose result is the field restores the location; one whose result isn't doesn't
            "(define-ircmd LOADD (operands [a :Rn|:Kn]) (result :double) (class :value) (facts (reads (tvalue a :value :double))) (lowering 0.0))\n"
            "(define-ircmd CMPT (operands [a :Rn|:tag]) (result :int) (class :value) (facts (reads (tvalue a :tag))) (lowering 0))\n"
            // Stores pick the dead-store engine, the store hint and the location effects from the field
            "(define-ircmd STORED (operands [a :Rn|:inst-pointer] [val :double]) (class :effect) (facts (writes (tvalue a :value :double) val)) (lowering (gen:seq)))\n"
            "(define-ircmd STORET (operands [a :Rn] [tag :tag]) (class :effect) (facts (writes (tvalue a :tag) tag)) (lowering (gen:seq)))\n"
            "(define-ircmd STORES (operands [a :Rn|:pointer] [tag :tag] [val :int] [offset :const-int]) (class :effect)\n"
            "  (facts (writes (tvalue a offset :split) tag val)) (lowering (gen:seq)))\n"
            // Buffers and allocations
            "(define-ircmd BUFW (operands [a :pointer] [b :int] [v :int]) (class :effect) (facts (writes (buffer a b 4) v)) (heap-effects :may-gc) (lowering (gen:seq)))\n"
            "(define-ircmd ALLOC (operands [a :int]) (result :pointer) (class :value) (facts (allocates :buffer-data)) (lowering 0))\n"
            "(define-ircmd CHK (operands [a :tag] [b :const-tag] [fail :block]) (class :guard) (facts (establishes (tag-of a) b)) (lowering (gen:seq)))\n"
            // Kills, and results recomputed from an operand
            "(define-ircmd DEAD (operands [a :Rn] [n :const-int]) (class :pseudo) (facts (no-vm-effects) (kills (vm-slots a n))))\n"
            "(define-ircmd TONUM (operands [a :int]) (result :double) (class :value) (lowering 0.0))\n"
            "(define-ircmd TOINT (operands [a :double]) (result :int) (class :value) (facts (rematerialize-from a :via TONUM)) (lowering 0))\n"
            // Class defaults and overrides
            "(define-ircmd UPV (operands [a :Rn]) (class :effect) (facts (no-vm-effects)) (lowering (gen:seq)))\n"
            "(define-ircmd JMP (operands [a :block]) (class :terminator) (lowering (gen:seq)))\n"
            "(define-ircmd LOOP (operands [a :block]) (class :terminator) (dead-store :visit) (lowering (gen:seq)))");
    requireOk();

    std::string effects = read("out/IrCmdEffects.h");
    auto entry = [&](const std::string& cmd) {
        size_t start = effects.find("    // " + cmd + "\n");
        REQUIRE(start != std::string::npos);
        start = effects.find('\n', start) + 1;
        return effects.substr(start, effects.find('\n', start) - start);
    };

    // Uses come first, in declaration order, then the defs; the may-write gives the invalidation
    CHECK(entry("STEP") ==
          "    IrCmdEffects{}.vm(IrVmEffectKind::Use, 0).vm(IrVmEffectKind::MaybeUse, 1).vm(IrVmEffectKind::UseRange, 0, 1, {IrEffectCountKind::UintOperand, 3})"
          ".vm(IrVmEffectKind::DefRange, 0, 3, {IrEffectCountKind::LowByteOfIntOperand, 2}).vm(IrVmEffectKind::Def, 0)"
          ".before({IrLocationEffectKind::InvalidateRange, 0, 2, 0, {IrEffectCountKind::Literal, -1}})"
          ".deadStoreIs(IrDeadStoreKind::LiveIns, 4).reentersVmIs(),");
    CHECK(entry("LOADD") == "    IrCmdEffects{}.vm(IrVmEffectKind::MaybeUse, 0).after({IrLocationEffectKind::RestoreFromLoad, 0, 0, 0, {}}),");
    CHECK(entry("CMPT") == "    IrCmdEffects{}.vm(IrVmEffectKind::MaybeUse, 0).locationsNone(),");
    CHECK(entry("STORED") ==
          "    IrCmdEffects{}.vm(IrVmEffectKind::MaybeDef, 0).before({IrLocationEffectKind::Invalidate, 0, 0, 0, {}})"
          ".after({IrLocationEffectKind::RestoreFromStore, 0, 0, 1, {}}).deadStoreEngineIs(IrDeadStoreEngine::StoreValue).storeHint(0, 1, IrValueKind::Double),");
    CHECK(entry("STORET") ==
          "    IrCmdEffects{}.vm(IrVmEffectKind::MaybeDef, 0).before({IrLocationEffectKind::InvalidateKeepValues, 0, 0, 0, {}})"
          ".deadStoreEngineIs(IrDeadStoreEngine::StoreTag),");
    CHECK(entry("STORES") ==
          "    IrCmdEffects{}.vm(IrVmEffectKind::MaybeDef, 0).before({IrLocationEffectKind::Invalidate, 0, 0, 0, {}})"
          ".after({IrLocationEffectKind::RestoreFromStore, 0, 0, 2, {}}).deadStoreEngineIs(IrDeadStoreEngine::StoreSplitTvalue).storeHint(0, 2, IrValueKind::Unknown),");
    CHECK(entry("BUFW") == "    IrCmdEffects{}.writesAllocation(0).mayGcIs(),");
    CHECK(entry("ALLOC") == "    IrCmdEffects{}.deadStoreIs(IrDeadStoreKind::Allocation),");
    CHECK(entry("CHK") == "    IrCmdEffects{}.deadStoreEngineIs(IrDeadStoreEngine::CheckTag),");
    CHECK(entry("DEAD") == "    IrCmdEffects{}.vmNone().killsIs({IrVmEffectKind::DefRange, 0, 0, {IrEffectCountKind::IntOperand, 1}}),");
    CHECK(entry("TOINT") == "    IrCmdEffects{}.after({IrLocationEffectKind::Rematerialize, 0, 0, 0, {}, IrCmd::TONUM}),");
    CHECK(entry("UPV") == "    IrCmdEffects{}.vmNone().locationsNone(),");
    CHECK(entry("JMP") == "    IrCmdEffects{}.deadStoreIs(IrDeadStoreKind::LiveOuts),");
    CHECK(entry("LOOP") == "    IrCmdEffects{},");
    CHECK(entry("NOP") == "    IrCmdEffects{},");
    CHECK(effects.find("enum class IrDeadStoreEngine : uint8_t\n{\n    None,\n    StoreValue,\n    StoreTag,\n    StoreSplitTvalue,\n    CheckTag,\n};") != std::string::npos);
    // Without conditional facts, every instruction's effects are its command's table entry
    CHECK(effects.find("inline const IrCmdEffects& getInstEffects(IrFunction& function, IrInst& inst)\n{\n    return kIrCmdEffects[unsigned(inst.cmd)];\n}") !=
          std::string::npos);
    CHECK(effects.find("inline constexpr int kMaxIrVmEffects = 5;") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "conditional_facts_generate_variants_and_a_selector")
{
    compile("(define-ircmd NOP (class :pseudo))\n"
            "(define-ircmd NEWOBJ (operands [ra :Rn] [form :const-int] [count :const-int]) (class :effect)\n"
            "  (facts (cond [(= form :FORM_INIT) (when (> count 0) (reads (vm-slots (+ ra 3) count))) (writes (vm-slots ra 3))]\n"
            "               [else (writes (vm-slot ra))]))\n"
            "  (lowering (gen:seq)))\n"
            "(define-ircmd CAP (operands [ra :Rn|:UPn] [kind :const-uint]) (class :effect)\n"
            "  (facts (reads (tvalue ra)) (when (and (= kind 1) (not (kind? ra :VmUpvalue))) (captures (vm-slot ra))))\n"
            "  (lowering (gen:seq)))");
    requireOk();

    std::string effects = read("out/IrCmdEffects.h");
    CHECK(effects.find("    // NEWOBJ: depends on its operands, see kIrCmdEffectVariants\n    IrCmdEffects{},\n") != std::string::npos);

    // One entry per combination of branches, each derived like an unconditional command
    CHECK(effects.find("inline constexpr IrCmdEffects kIrCmdEffectVariants[] = {\n"
                       "    // NEWOBJ: (= form :FORM_INIT), (> count 0)\n"
                       "    IrCmdEffects{}.vm(IrVmEffectKind::UseRange, 0, 3, {IrEffectCountKind::IntOperand, 2}).vm(IrVmEffectKind::DefRange, 0, 0, "
                       "{IrEffectCountKind::Literal, 3}).before({IrLocationEffectKind::InvalidateRange, 0, 0, 0, {IrEffectCountKind::Literal, 3}}),\n"
                       "    // NEWOBJ: (= form :FORM_INIT), (not (> count 0))\n"
                       "    IrCmdEffects{}.vm(IrVmEffectKind::DefRange, 0, 0, {IrEffectCountKind::Literal, 3}).before({IrLocationEffectKind::InvalidateRange, 0, 0, 0, "
                       "{IrEffectCountKind::Literal, 3}}),\n"
                       "    // NEWOBJ: (not (= form :FORM_INIT))\n"
                       "    IrCmdEffects{}.vm(IrVmEffectKind::Def, 0).before({IrLocationEffectKind::Invalidate, 0, 0, 0, {}}),\n"
                       "    // CAP: (and (= kind 1) (not (kind? ra :VmUpvalue)))\n"
                       "    IrCmdEffects{}.vm(IrVmEffectKind::MaybeUse, 0).vm(IrVmEffectKind::Capture, 0).locationsNone(),\n"
                       "    // CAP: (not (and (= kind 1) (not (kind? ra :VmUpvalue))))\n"
                       "    IrCmdEffects{}.vm(IrVmEffectKind::MaybeUse, 0).locationsNone(),\n"
                       "};") != std::string::npos);

    // Tried in order, so each variant only tests its own branches' conditions
    CHECK(effects.find("    case IrCmd::NEWOBJ:\n"
                       "        if (function.intOp(OP_B(inst)) == FORM_INIT && function.intOp(OP_C(inst)) > 0)\n"
                       "            return kIrCmdEffectVariants[0];\n"
                       "        if (function.intOp(OP_B(inst)) == FORM_INIT)\n"
                       "            return kIrCmdEffectVariants[1];\n"
                       "        return kIrCmdEffectVariants[2]; // (not (= form :FORM_INIT))\n"
                       "    case IrCmd::CAP:\n"
                       "        if ((function.uintOp(OP_B(inst)) == 1 && !(OP_A(inst).kind == IrOpKind::VmUpvalue)))\n"
                       "            return kIrCmdEffectVariants[3];\n") != std::string::npos);
    CHECK(effects.find("    default:\n        return kIrCmdEffects[unsigned(inst.cmd)];\n    }") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "fact_and_effect_errors")
{
    struct Case
    {
        const char* name;
        std::string program;
        const char* error;
    };

    auto cmd = [&](const std::string& operands, const std::string& clauses, const char* cls = ":effect") {
        return "(define-ircmd C (operands " + operands + ") (class " + cls + ") " + clauses + " (lowering (gen:seq)))\n";
    };

    Case cases[] = {
        // Facts
        {"facts required", cmd("[a :Rn]", ""), "C can have VM register operands, so it needs (reads ...)/(writes ...) facts"},
        {"location-tracking required for target-specific Rn", cmd("[a :double]", "(target-specific [:x64 [a :Rn]])"),
            "can have VM register operands on some targets, so it needs facts or (location-tracking :none)"},
        {"facts outside the block", cmd("[a :Rn]", "(reads (vm-slot a))"), "(reads ...) goes inside the (facts ...) block"},
        {"empty block", cmd("[a :Rn]", "(facts)"), "(facts <item>...) needs at least one item"},
        {"two blocks", cmd("[a :Rn]", "(facts (reads (vm-slot a))) (facts (reads (vm-slot a)))"), "Duplicate facts clause"},
        {"unknown item", cmd("[a :Rn]", "(facts (touches (vm-slot a)))"), "Unknown facts item 'touches'"},
        {"unknown location", cmd("[a :Rn]", "(facts (reads (register a)))"), "Unknown or malformed location 'register'"},
        {"vm-slot needs Rn only", cmd("[a :Rn|:Kn]", "(facts (reads (vm-slot a)))"), "(vm-slot ...) needs a :Rn operand; use (tvalue a)"},
        {"tvalue needs a TValue operand", cmd("[a :double]", "(facts (reads (tvalue a)))"), "Operand 'a' can't refer to a TValue"},
        {"count must be constant", cmd("[a :Rn] [n :int]", "(facts (reads (vm-slots a n)))"), "Count operand 'n' has to be :const-int or :const-uint"},
        {"low-byte must be const-int", cmd("[a :Rn] [n :const-uint]", "(facts (reads (vm-slots a (low-byte n))))"), "(low-byte ...) needs a :const-int operand"},
        {"fields only for TValues", cmd("[a :Rn]", "(facts (reads (vm-slot a :tag)))"), "Fields only apply to (tvalue ...) locations"},
        {"unknown field", cmd("[a :Rn]", "(facts (reads (tvalue a :bogus)))"), "Unknown field :bogus"},
        {"value needs a kind", cmd("[a :Rn]", "(facts (reads (tvalue a :value)))"), ":value needs a kind"},
        {"one field", cmd("[a :Rn]", "(facts (reads (tvalue a :tag :extra)))"), "takes one field"},
        {"written value missing", cmd("[a :Rn]", "(facts (writes (tvalue a :tag)))"), "Writing field :tag names 1 value operand(s) after the location"},
        {"reads take no values", cmd("[a :Rn]", "(facts (reads (tvalue a :tag) a))"), "reads takes locations only"},
        {"may-write takes no field", cmd("[a :Rn]", "(facts (may-write (tvalue a :tag)))"), "may-write covers whole locations"},
        {"varargs are only read", cmd("[a :Rn]", "(facts (writes (varargs a)))"), "(varargs ...) can only be read"},
        {"several invalidations", cmd("[a :Rn] [b :Rn]", "(facts (writes (vm-slot a) (vm-slot b)))"),
            "C writes several locations; add one (may-write (vm-slots ...)) covering them"},
        {"no-vm-effects with reads", cmd("[a :Rn]", "(facts (no-vm-effects) (reads (vm-slot a)))"), "C has (no-vm-effects) but reads or writes VM registers"},
        {"establishes", cmd("[a :tag]", "(facts (establishes a a))"), "(establishes (tag-of <operand>) <tag operand>)"},
        {"allocates twice", cmd("[a :int]", "(facts (allocates :buffer-data) (allocates :buffer-data))"), "(allocates :buffer-data), once"},
        // Conditions
        {"condition on a non-constant", cmd("[a :Rn] [n :int]", "(facts (when (= n 1) (reads (vm-slot a))))"),
            "Conditions compare constant operands; 'n' has to be :const-int or :const-uint"},
        {"unknown condition", cmd("[a :Rn] [n :const-int]", "(facts (when (like n 1) (reads (vm-slot a))))"), "Unknown condition 'like'"},
        {"unknown operand kind", cmd("[a :Rn] [n :const-int]", "(facts (when (kind? a :Bogus) (reads (vm-slot a))))"), "(kind? <operand> :<IrOpKind>)"},
        {"compared value", cmd("[a :Rn] [n :const-int]", "(facts (when (= n :lower) (reads (vm-slot a))))"), "Compared values are numbers or C++ constants"},
        {"else not last", cmd("[a :Rn] [n :const-int]", "(facts (cond [else (reads (vm-slot a))] [(= n 1) (reads (vm-slot a))]))"),
            "[else ...] has to be the last cond clause"},
        {"whole-command fact in a branch", cmd("[a :Rn] [n :const-int]", "(facts (when (= n 1) (no-vm-effects)))"), "(no-vm-effects ...) can't be inside when/cond"},
        // Captures, kills, rematerialization
        {"captures outside the block", cmd("[a :Rn]", "(captures (vm-slot a))"), "(captures ...) goes inside the (facts ...) block"},
        {"captures one register", cmd("[a :Rn]", "(facts (reads (vm-slot a)) (captures (vm-slots a 2)))"), "captures takes (vm-slot r)"},
        {"kills registers", cmd("[a :Rn]", "(facts (reads (vm-slot a)) (kills (tvalue a)))"), "kills takes (vm-slot r) or (vm-slots r count)"},
        {"rematerialize needs a result", cmd("[a :double]", "(facts (rematerialize-from a :via C))"), "C has no result to rematerialize"},
        {"rematerialize names a command", "(define-ircmd C (operands [a :double]) (result :int) (class :value) (facts (rematerialize-from a :via NOPE)) (lowering 0))\n",
            "C: (rematerialize-from ... :via NOPE) names no define-ircmd"},
        // What's left of the explicit clauses
        {"vm-effects is gone", cmd("[a :Rn]", "(vm-effects (hook x))"), "Unknown define-ircmd clause 'vm-effects'"},
        {"location-tracking is only :none", cmd("[a :Rn]", "(facts (reads (vm-slot a))) (location-tracking (hook-before x))"),
            "(location-tracking :none) is the only form"},
        {"guard without a target", cmd("[a :any]", "", ":guard"), "C has no :block/:vmexit operand for dead-store :live-ins"},
        {"class default kinds aren't written", cmd("[a :any] [f :block]", "(dead-store :live-ins)", ":guard"), "dead-store takes :visit, :none or :live-ins-no-exit-sync"},
        {"redundant override", cmd("[a :any]", "(dead-store :visit)"), "C: the dead-store clause says what the class and facts already give; remove it"},
        {"contradicts the facts", cmd("[a :Rn] [v :int]", "(facts (writes (tvalue a :value :int) v)) (dead-store :none)"),
            "C: dead-store derived from the facts is (store-value), but the dead-store clause says :none"},
        {"two dead-store kinds", cmd("[a :any]", "(dead-store :visit :none)"), "dead-store takes one item"},
        {"no-exit-sync on a non-guard", cmd("[a :any]", "(dead-store :live-ins-no-exit-sync)"), "dead-store :live-ins-no-exit-sync is for guards"},
        {"may-gc needs visit", cmd("[a :any]", "(dead-store :none) (heap-effects :may-gc)"), "only flushes GC object registers for :may-gc commands it visits"},
    };

    for (const auto& c : cases)
    {
        CAPTURE(c.name);
        compile(c.program);
        requireError({c.error});
    }
}

TEST_CASE_FIXTURE(IrCmdFixture, "pseudo_instructions_need_no_location_tracking")
{
    compile("(define-ircmd NOP (class :pseudo))\n(define-ircmd MARK (operands [a :Rn] [n :const-int]) (class :pseudo) (facts (reads (vm-slots a n))))");
    requireOk();
}

TEST_CASE_FIXTURE(IrCmdFixture, "trailing_doc_on_the_header_line_documents_the_command")
{
    compile("(define-ircmd NOP ~ Does nothing\n  (class :pseudo))");
    requireOk();
    CHECK(read("out/IrCmd.h").find("    // Does nothing\n    NOP,\n") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "public_headers_can_go_to_a_separate_directory")
{
    file("root.luna", "(define-ircmd NOP (class :pseudo))");
    fs::create_directories(dir / "public");
    run({"root.luna", "out", "--public-output", "public"});
    requireOk();
    CHECK(fs::exists(dir / "public/IrCmd.h"));
    CHECK(fs::exists(dir / "public/IrCmdInfo.h"));
    CHECK(fs::exists(dir / "public/IrCmdEffects.h"));
    CHECK(!fs::exists(dir / "out/IrCmd.h"));
}

TEST_SUITE_END();
