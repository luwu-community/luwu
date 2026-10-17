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

    // The generated A64 function for `cmd`
    std::string lowering(const std::string& cmd)
    {
        std::string text = read("out/LoweringRulesA64.h");
        size_t start = text.find("inline void lower_" + cmd + "(");
        REQUIRE(start != std::string::npos);
        return text.substr(start, text.find("\n}\n", start) - start);
    }
};

const char* kRule2 = "(define-rule (two [a :op] [b :op]) (if (constant? a) (unsupported!)))\n";

} // namespace

TEST_SUITE_BEGIN("LunaIrCmds");

TEST_CASE_FIXTURE(IrCmdFixture, "commands_generate_the_enum_and_command_information")
{
    compile("(define-ircmd NOP (class :pseudo))\n"
            "\n"
            "~ Shared docs\n"
            "~ A: int\n"
            "(define-ircmd FIRST (operands [a :any] [b :any]) (result :int) (class :value) (lowering (two a b)))\n"
            "~ Shared docs\n"
            "~ A: int\n"
            "(define-ircmd SECOND (operands [a :any] [b :any]) (class :guard) (flags :deprecated/is-unsafe-to-sink) (dead-store (hook guard-uses)) (lowering (two a b)))\n"
            "\n"
            "(define-ircmd THIRD (class :terminator) (heap-effects :invalidates-safe-env) (flags :dirty-high-bits :deprecated/has-side-effects)\n"
            "  (lowering (none)))\n"
            "\n" +
            std::string(kRule2) + "(define-rule (none) (unsupported!))");
    requireOk();
    CHECK(out.find("5 output(s) updated") != std::string::npos);

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

TEST_CASE_FIXTURE(IrCmdFixture, "a_rule_shared_by_two_commands_generates_a_function_for_each")
{
    compile("(define-ircmd FIRST (operands [a :any] [b :any]) (class :effect) (lowering (two a b)))\n"
            "(define-ircmd SECOND (operands [a :any] [b :any]) (class :effect) (lowering (two a b)))\n" +
            std::string(kRule2));
    requireOk();
    CHECK(lowering("FIRST").find("OP_A(inst).kind == IrOpKind::Constant") != std::string::npos);
    CHECK(lowering("SECOND").find("OP_A(inst).kind == IrOpKind::Constant") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "arguments_bind_rule_parameters_to_the_named_operands")
{
    compile("(define-ircmd SWAPPED (operands [x :any] [y :any]) (class :effect) (lowering (two y x)))\n" + std::string(kRule2));
    requireOk();
    CHECK(lowering("SWAPPED").find("if (OP_B(inst).kind == IrOpKind::Constant)") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "literal_arguments_bind_constants")
{
    compile("(define-ircmd WITH_LITERAL (operands [x :any]) (class :effect) (lowering (lit x 3)))\n"
            "(define-rule (lit [a :op] [n :int]) (if (= n 3) (unsupported!)))");
    requireOk();
    CHECK(lowering("WITH_LITERAL").find("if (3 == 3)") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "command_errors")
{
    struct Case
    {
        const char* name;
        std::string program;
        const char* error;
    };

    std::string cmd = "(define-ircmd C (operands [a :any] [b :any]) (class :effect) (lowering (two a b)))\n";
    std::string manyPseudo;
    for (int i = 0; i < 257; ++i)
        manyPseudo += "(define-ircmd C" + std::to_string(i) + " (class :pseudo))\n";

    Case cases[] = {
        {"duplicate command", cmd + cmd + kRule2, "Duplicate define-ircmd C"},
        {"NOP not first", cmd + "(define-ircmd NOP (class :pseudo))\n" + kRule2, "NOP must be the first define-ircmd"},
        {"lowercase name", "(define-ircmd lower (class :pseudo))", "needs an UPPER_SNAKE command name"},
        {"too many commands", manyPseudo, "More than 256 IR commands"},
        {"unknown clause", "(define-ircmd C (class :pseudo) (bogus))", "Unknown define-ircmd clause 'bogus'"},
        {"duplicate clause", "(define-ircmd C (class :pseudo) (class :pseudo))", "Duplicate class clause"},
        {"unknown class", "(define-ircmd C (class :bogus))", "class expects one of"},
        {"unknown result", "(define-ircmd C (result :bogus) (class :pseudo))", "result expects one of"},
        {"unknown flag", "(define-ircmd C (class :pseudo) (flags :bogus))", "Unknown flag ':bogus'"},
        {"unknown heap effect", "(define-ircmd C (class :pseudo) (heap-effects :bogus))", "Unknown heap effect ':bogus'"},
        {"missing class", "(define-ircmd C (result :int))", "C needs a (class ...) clause"},
        {"value without result", "(define-ircmd C (class :value) (lowering (none)))\n(define-rule (none) (unsupported!))", "is a :value command but has no (result ...)"},
        {"effect with result", "(define-ircmd C (result :int) (class :effect) (lowering (none)))\n(define-rule (none) (unsupported!))", "is an :effect command but has a result"},
        {"pseudo with lowering", "(define-ircmd C (class :pseudo) (lowering (none)))\n(define-rule (none) (unsupported!))", "C is a pseudo-instruction and isn't lowered"},
        {"missing lowering", "(define-ircmd C (class :effect))", "C needs a (lowering (<rule> <args>...)) clause"},
        {"semantics lowering", "(define-ircmd C (class :effect) (lowering :semantics))", "(lowering :semantics) isn't supported yet"},
        {"too many operands", "(define-ircmd C (operands [a :any] [b :any] [c :any] [d :any] [e :any] [f :any] [g :any] [h :any]) (class :pseudo))", "C has 8 operands"},
        {"duplicate operand", "(define-ircmd C (operands [a :any] [a :any]) (class :pseudo))", "Duplicate operand 'a'"},
        {"unknown rule", "(define-ircmd C (class :effect) (lowering (nope)))", "C is lowered by 'nope', which isn't a define-rule"},
        {"wrong argument count", "(define-ircmd C (operands [a :any]) (class :effect) (lowering (two a)))\n" + std::string(kRule2), "Rule two takes 2 argument(s), but C passes 1"},
        {"unknown operand", "(define-ircmd C (operands [a :any]) (class :effect) (lowering (two a z)))\n" + std::string(kRule2), "'z' isn't an operand of C (operands: a)"},
        {"unreferenced rule", "(define-ircmd NOP (class :pseudo))\n" + std::string(kRule2), "define-rule two isn't used by any define-ircmd"},
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
            "  (class :guard) (vm-effects (maybe-use a)) (location-tracking :none) (dead-store :live-ins) (lowering (guard a b c d)))\n"
            "(define-rule (guard [a :op] [b :op] [c :label] [d :op]) (unsupported!))");
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

TEST_CASE_FIXTURE(IrCmdFixture, "rule_parameter_types_must_be_able_to_read_the_operand_kinds")
{
    std::string rule = "(define-rule (lit [n :int]) (unsupported!))\n";

    SUBCASE("an :int parameter can't read an instruction result")
    {
        compile("(define-ircmd C (operands [x :int]) (class :effect) (lowering (lit x)))\n" + rule);
        requireError({"C operand 'x' can be :int, but rule parameter 'n' is :int, which can't read that"});
    }

    SUBCASE("constants, absent operands and literals are fine")
    {
        compile("(define-ircmd C (operands [x :const-int|:none]) (class :effect) (lowering (lit x)))\n"
                "(define-ircmd D (operands [x :any]) (class :effect) (lowering (lit 3)))\n" +
                rule);
        requireOk();
    }

    SUBCASE("extra kinds are checked too")
    {
        compile("(define-ircmd C (operands [x :const-int]) (target-specific [:x64 [x :Rn]]) (class :effect) (location-tracking :none) (lowering (lit x)))\n" + rule);
        requireError({"can be :Rn (on x64)"});
    }
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

TEST_CASE_FIXTURE(IrCmdFixture, "effects_generate_the_effects_table")
{
    compile("(define-ircmd NOP (class :pseudo))\n"
            "(define-ircmd STEP (operands [ra :Rn] [k :Rn|:Kn] [n :const-int] [m :const-uint] [fail :block|:vmexit|:undef])\n"
            "  (class :guard)\n"
            "  (vm-effects (def-range (+ ra 3) (low-byte n)) (use ra) (maybe-use k) (use-range (+ ra 1) m) (def ra) (hook step-uses))\n"
            "  (location-tracking (invalidate-range (+ ra 2) :to-top) (restore-from-load k) (hook-before step-before))\n"
            "  (dead-store :live-ins (store-hint ra k :double))\n"
            "  (heap-effects :reenters-vm)\n"
            "  (lowering (any5 ra k n m fail)))\n"
            "(define-ircmd GC (operands [ra :Rn]) (class :effect) (vm-effects :none) (location-tracking :none) (heap-effects :may-gc)\n"
            "  (dead-store (writes-allocation ra)) (lowering (any1 ra)))\n"
            "(define-rule (any5 [a :op] [b :op] [c :op] [d :op] [e :label]) (unsupported!))\n"
            "(define-rule (any1 [a :op]) (unsupported!))");
    requireOk();

    std::string effects = read("out/IrCmdEffects.h");
    // Uses come first, in declaration order, then the defs
    CHECK(effects.find("    // STEP\n"
                       "    IrCmdEffects{}.vm(IrVmEffectKind::Use, 0).vm(IrVmEffectKind::MaybeUse, 1).vm(IrVmEffectKind::UseRange, 0, 1, {IrEffectCountKind::UintOperand, 3})"
                       ".vm(IrVmEffectKind::DefRange, 0, 3, {IrEffectCountKind::LowByteOfIntOperand, 2}).vm(IrVmEffectKind::Def, 0)"
                       ".vmHookIs(IrVmEffectsHook::StepUses)"
                       ".before({IrLocationEffectKind::InvalidateRange, 0, 2, 0, {IrEffectCountKind::Literal, -1}})"
                       ".after({IrLocationEffectKind::RestoreFromLoad, 1, 0, 0, {}})"
                       ".beforeHook(IrLocationHook::StepBefore)"
                       ".deadStoreIs(IrDeadStoreKind::LiveIns, 4).storeHint(0, 1, IrValueKind::Double).reentersVmIs(),\n") != std::string::npos);
    CHECK(effects.find("    // GC\n    IrCmdEffects{}.vmNone().locationsNone().writesAllocation(0).mayGcIs(),\n") != std::string::npos);
    CHECK(effects.find("    // NOP\n    IrCmdEffects{},\n") != std::string::npos);
    CHECK(effects.find("enum class IrVmEffectsHook : uint8_t\n{\n    None,\n    StepUses,\n};") != std::string::npos);
    CHECK(effects.find("inline constexpr int kMaxIrVmEffects = 5;") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "effect_errors")
{
    struct Case
    {
        const char* name;
        std::string program;
        const char* error;
    };

    std::string rule = "(define-rule (any1 [a :op]) (unsupported!))\n";
    auto cmd = [&](const std::string& operands, const std::string& clauses, const char* cls = ":effect") {
        return "(define-ircmd C (operands " + operands + ") (class " + cls + ") " + clauses + " (lowering (any1 a)))\n" + rule;
    };

    Case cases[] = {
        {"vm-effects required", cmd("[a :Rn]", "(location-tracking :none)"), "C can have VM register operands, so it needs (vm-effects ...)"},
        {"location-tracking required", cmd("[a :Rn]", "(vm-effects :none)"), "so it needs (location-tracking ...)"},
        {"location-tracking required for target-specific Rn",
            cmd("[a :double]", "(target-specific [:x64 [a :Rn]])"), "so it needs (location-tracking ...)"},
        {"use needs Rn only", cmd("[a :Rn|:Kn]", "(vm-effects (use a)) (location-tracking :none)"), "can be something other than :Rn; use maybe-use/maybe-def"},
        {"maybe-use needs Rn", cmd("[a :double]", "(vm-effects (maybe-use a))"), "can't be a VM register, so maybe-use never applies"},
        {"count must be constant", "(define-ircmd C (operands [a :Rn] [n :int]) (class :effect) (vm-effects (use-range a n)) (location-tracking :none) (lowering (any2 a n)))\n"
            "(define-rule (any2 [a :op] [b :op]) (unsupported!))", "Count operand 'n' has to be :const-int or :const-uint"},
        {"low-byte must be const-int", "(define-ircmd C (operands [a :Rn] [n :const-uint]) (class :effect) (vm-effects (use-range a (low-byte n))) (location-tracking :none) (lowering (any2 a n)))\n"
            "(define-rule (any2 [a :op] [b :op]) (unsupported!))", "(low-byte ...) needs a :const-int operand"},
        {"none with other effects", cmd("[a :Rn]", "(vm-effects :none (use a)) (location-tracking :none)"), "vm-effects :none can't be combined with other effects"},
        {"unknown vm effect", cmd("[a :Rn]", "(vm-effects (touch a)) (location-tracking :none)"), "Unknown vm-effects item 'touch'"},
        {"two vm hooks", cmd("[a :Rn]", "(vm-effects (hook x) (hook y)) (location-tracking :none)"), "Only one vm-effects hook per command"},
        {"unknown location effect", cmd("[a :Rn]", "(vm-effects :none) (location-tracking (forget a))"), "Unknown location-tracking item 'forget'"},
        {"two effects before lowering", cmd("[a :Rn]", "(vm-effects :none) (location-tracking (invalidate a) (invalidate-keep-values a))"),
            "Only one location-tracking effect before lowering per command"},
        {"guard without live-ins", "(define-ircmd C (operands [f :block]) (class :guard) (lowering (any1 f)))\n(define-rule (any1 [a :label]) (unsupported!))",
            "C is a guard, so dead-store elimination has to check its target's live-ins"},
        {"live-ins on a non-guard", cmd("[a :any]", "(dead-store :live-ins)"), "dead-store :live-ins is for guards"},
        {"live-outs on a non-terminator", cmd("[a :any]", "(dead-store :live-outs)"), "dead-store :live-outs is for terminators"},
        {"two dead-store kinds", cmd("[a :any]", "(dead-store :visit :none)"), "dead-store takes one of"},
        {"may-gc needs visit", cmd("[a :any]", "(dead-store :none) (heap-effects :may-gc)"), "only flushes GC object registers for :may-gc commands it visits"},
        {"bad store-hint kind", cmd("[a :any]", "(dead-store (store-hint a a :bogus))"), "(store-hint <destination> <value> :double|:int|:int64|:pointer|:tvalue|:inferred)"},
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
    compile("(define-ircmd NOP (class :pseudo))\n(define-ircmd MARK (operands [a :Rn] [n :const-int]) (class :pseudo) (vm-effects (use-range a n)))");
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
    CHECK(fs::exists(dir / "out/LoweringRulesA64.h"));
    CHECK(!fs::exists(dir / "out/IrCmd.h"));
}

TEST_SUITE_END();
