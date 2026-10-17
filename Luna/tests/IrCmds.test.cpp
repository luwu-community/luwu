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
            "(define-ircmd FIRST (operands a b) (result :int) (class :value) (lowering (two a b)))\n"
            "~ Shared docs\n"
            "~ A: int\n"
            "(define-ircmd SECOND (operands a b) (class :guard) (flags :deprecated/is-unsafe-to-sink) (lowering (two a b)))\n"
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
    compile("(define-ircmd FIRST (operands a b) (class :effect) (lowering (two a b)))\n"
            "(define-ircmd SECOND (operands a b) (class :effect) (lowering (two a b)))\n" +
            std::string(kRule2));
    requireOk();
    CHECK(lowering("FIRST").find("OP_A(inst).kind == IrOpKind::Constant") != std::string::npos);
    CHECK(lowering("SECOND").find("OP_A(inst).kind == IrOpKind::Constant") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "arguments_bind_rule_parameters_to_the_named_operands")
{
    compile("(define-ircmd SWAPPED (operands x y) (class :effect) (lowering (two y x)))\n" + std::string(kRule2));
    requireOk();
    CHECK(lowering("SWAPPED").find("if (OP_B(inst).kind == IrOpKind::Constant)") != std::string::npos);
}

TEST_CASE_FIXTURE(IrCmdFixture, "literal_arguments_bind_constants")
{
    compile("(define-ircmd WITH_LITERAL (operands x) (class :effect) (lowering (lit x 3)))\n"
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

    std::string cmd = "(define-ircmd C (operands a b) (class :effect) (lowering (two a b)))\n";
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
        {"too many operands", "(define-ircmd C (operands a b c d e f g h) (class :pseudo))", "C has 8 operands"},
        {"duplicate operand", "(define-ircmd C (operands a a) (class :pseudo))", "Duplicate operand 'a'"},
        {"unknown rule", "(define-ircmd C (class :effect) (lowering (nope)))", "C is lowered by 'nope', which isn't a define-rule"},
        {"wrong argument count", "(define-ircmd C (operands a) (class :effect) (lowering (two a)))\n" + std::string(kRule2), "Rule two takes 2 argument(s), but C passes 1"},
        {"unknown operand", "(define-ircmd C (operands a) (class :effect) (lowering (two a z)))\n" + std::string(kRule2), "'z' isn't an operand of C (operands: a)"},
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
