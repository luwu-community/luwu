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
            "(define-ircmd SECOND (operands [a :any] [b :any]) (class :guard) (flags :deprecated/is-unsafe-to-sink) (lowering (two a b)))\n"
            "\n"
            "(define-ircmd THIRD (class :terminator) (heap-effects :invalidates-safe-env) (flags :dirty-high-bits :deprecated/has-side-effects)\n"
            "  (lowering (none)))\n"
            "\n" +
            std::string(kRule2) + "(define-rule (none) (unsupported!))");
    requireOk();
    CHECK(out.find("4 output(s) updated") != std::string::npos);

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
            "  (class :guard) (lowering (guard a b c d)))\n"
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
        compile("(define-ircmd C (operands [x :const-int]) (target-specific [:x64 [x :Rn]]) (class :effect) (lowering (lit x)))\n" + rule);
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
