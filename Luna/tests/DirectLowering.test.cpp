// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"
#include "Core/Evaluator.h"
#include "Core/Lexer.h"
#include "Core/Loader.h"
#include "Core/Parser.h"
#include "Driver.h"

using namespace LunaTest;
using namespace Luwu::Dsl;

namespace
{
struct DirectLoweringFixture
{
    Evaluator evaluator;
    std::vector<std::string> definitions = installNcg(evaluator);
    SourceLoader loader;
    std::vector<SExpr> program;
    std::vector<std::vector<SExpr>> snippets;

    DirectLoweringFixture()
    {
        auto previous = std::filesystem::current_path();
        std::filesystem::current_path(std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path().parent_path()));
        program = loader.load("defs/luwu.luna");
        std::filesystem::current_path(previous);
        evaluator.evalProgram(program);
    }
    Value eval(const std::string& source)
    {
        Lexer lexer(source, "direct-test.luna");
        Parser parser(lexer);
        snippets.push_back(parser.parseProgram());
        return evaluator.evalProgram(snippets.back());
    }

    std::string lower(const std::string& command, const std::string& target)
    {
        return eval("(%lowering-case %ircmd:" + command + " :" + target + ")").stringValue();
    }
};

size_t count(const std::string& text, const std::string& part)
{
    size_t result = 0;
    for (size_t at = 0; (at = text.find(part, at)) != std::string::npos; at += part.size())
        ++result;
    return result;
}
} // namespace

TEST_SUITE_BEGIN("LunaDirectLowering");

TEST_CASE_FIXTURE(DirectLoweringFixture, "all_commands_use_explicit_target_emission")
{
    CHECK(toString(eval("(length (program-ircmds))")) == "231");
    for (const char* symbol : {"define-microop", "define-fusion", "%microops", "%fusions", "emit-program",
             "%semantic-build", "%allocate-target-regions", "native-call", "register-local", "define-lowering"})
        CHECK(evaluator.globals()->lookup(symbol) == nullptr);
    for (const std::string target : {"a64", "x64"})
    {
        std::string header = eval("(ircmd-lowering-header :" + target + " (program-ircmds))").stringValue();
        CHECK(count(header, "    case IrCmd::") == 231);
        CHECK(header.find("inline void lower_") == std::string::npos);
        for (const char* obsolete : {"ScopedRegA64", "takeTemp", "spillForCall", "selectScratchRegister", "parked"})
            CHECK(header.find(obsolete) == std::string::npos);
    }
}

TEST_CASE_FIXTURE(DirectLoweringFixture, "calls_run_before_stack_reloads")
{
    for (const std::string target : {"a64", "x64"})
    {
        std::string call = lower("CALL", target);
        if (target == "a64")
        {
            REQUIRE(call.find("callProlog") != std::string::npos);
            CHECK(call.find("callProlog") < call.find("emitUpdateBase(build)"));
        }
        else
            CHECK(call.find("emitInstCall(") != std::string::npos);
        std::string fallback = lower("FORGLOOP_FALLBACK", target);
        for (const char* helper : {"forgLoopNonTableFallback)", "forgLoopNonTableFallback_DEPRECATED)"})
        {
            size_t position = fallback.find(helper);
            REQUIRE(position != std::string::npos);
            CHECK(fallback.find("emitUpdateBase(build)", position) != std::string::npos);
        }
        // Builtins retain their handwritten native implementations (including modf's side effects).
        CHECK(lower("FASTCALL", target).find("emitBuiltin(") != std::string::npos);
    }
}

TEST_CASE_FIXTURE(DirectLoweringFixture, "register_widths_and_guard_targets_are_preserved")
{
    std::string extract = lower("EXTRACT_VEC", "a64");
    CHECK(extract.find("build.dup_4s(castReg(KindA64::q, inst.regA64)") != std::string::npos);
    CHECK(extract.find("lowering.intOp(OP_B(inst)) == 0") != std::string::npos);
    std::string object = lower("CHECK_OBJECT_CLASS", "a64");
    CHECK(object.find("KindA64::x") != std::string::npos);
    CHECK(object.find("KindA64::w") == std::string::npos);
    CHECK(lower("STORE_INT", "x64").find("luauRegValueInt(") != std::string::npos);
    CHECK(lower("STORE_TAG", "a64").find("regOp(OP_B(inst))") != std::string::npos);
    CHECK(lower("STORE_TAG", "x64").find("memRegTagOp(OP_B(inst))") != std::string::npos);
    std::string guard = lower("CHECK_TAG", "x64");
    CHECK(guard.find("jumpOrAbortOnUndef") != std::string::npos);
}

TEST_CASE_FIXTURE(DirectLoweringFixture, "a64_fallbacks_spill_before_native_calls")
{
    for (const char* command : {"FALLBACK_DUPCLOSURE", "FALLBACK_NEWOBJECT", "FALLBACK_NEWCLASSMEMBER", "FALLBACK_GETGLOBAL"})
    {
        std::string code = lower(command, "a64");
        REQUIRE(code.find("regs.spill(index)") != std::string::npos);
        REQUIRE(code.find("emitFallback(") != std::string::npos);
        CHECK(code.find("regs.spill(index)") < code.find("emitFallback("));
    }
    std::string vector = lower("NEW_VECTOR", "a64");
    CHECK(vector.find("regs.spill(") < vector.find("emitMoveDoubleArguments3("));
    CHECK(vector.find("emitMoveDoubleArguments3(") < vector.find("build.blr("));
}

TEST_CASE_FIXTURE(DirectLoweringFixture, "native_allocation_and_instruction_idioms_are_explicit")
{
    CHECK(lower("ROUND_NUM", "a64").find("build.frinta(") != std::string::npos);
    CHECK(lower("ADD_INT", "x64").find("build.lea(") != std::string::npos);
    std::string division = lower("IDIV_INT64", "x64");
    CHECK(division.find("build.idiv(") != std::string::npos);
    CHECK(division.find("build.imul(") == std::string::npos);
    std::string call = lower("TABLE_LEN", "a64");
    CHECK(call.find("regs.takeReg(w0, index)") != std::string::npos);
    CHECK(call.find("regs.restore(") == std::string::npos);
    CHECK(lower("CMP_ANY", "a64").find("regs.restore(") != std::string::npos);
    CHECK(lower("JUMP", "a64").find("jumpOrFallthrough") != std::string::npos);
}

TEST_CASE_FIXTURE(DirectLoweringFixture, "ordinary_scheme_helpers_preserve_explicit_statement_order")
{
    eval(R"(
        (define (emit-ordered source)
          (gen:let ([captured (gen:method cpp:lowering :tempInt source)])
            (gen:build :mov (a64:reg :w0) captured)
            (gen:build :blr (a64:reg :x3))
            (gen:call :emitUpdateBase cpp:build)))
        (define-ircmd ORDERED (operands [source :int]) (class :effect)
          (lowering (emit-ordered source))))");
    std::string code = lower("ORDERED", "a64");
    CHECK(count(code, "lowering.tempInt(OP_A(inst))") == 1);
    CHECK(code.find("lowering.tempInt(") < code.find("build.mov("));
    CHECK(code.find("build.mov(") < code.find("build.blr("));
    CHECK(code.find("build.blr(") < code.find("emitUpdateBase(build)"));
}

TEST_SUITE_END();
