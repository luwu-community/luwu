// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"

#include "Core/CompileError.h"
#include "Core/Evaluator.h"
#include "Core/Record.h"
#include "Driver.h"
#include "Core/Lexer.h"
#include "Core/Loader.h"
#include "Core/Parser.h"

#include <filesystem>
#include <fstream>

using namespace LunaTest;
using namespace Luwu::Dsl;

namespace
{

// The Luna directory (tests run from it, but fixtures change the working directory)
const std::filesystem::path kLunaDir = std::filesystem::absolute(std::filesystem::path(__FILE__).parent_path().parent_path());

// An evaluator with the real definitions (micro-ops, bindings, assembler signatures) and the test cases loaded
struct MicroOpFixture
{
    Evaluator evaluator;
    std::vector<std::string> ncgFiles = installNcg(evaluator);
    SourceLoader loader;
    std::vector<SExpr> program;
    std::vector<std::vector<SExpr>> snippets;

    MicroOpFixture()
    {
        auto cwd = std::filesystem::current_path();
        std::filesystem::current_path(kLunaDir);
        program = loader.load("defs/luwu.luna");
        std::vector<SExpr> cases = loader.load("tests/microops-cases.luna");
        program.insert(program.end(), cases.begin(), cases.end());
        std::filesystem::current_path(cwd);
        evaluator.evalProgram(program);
    }

    Value eval(const std::string& source)
    {
        Lexer lexer(source, "test.luna");
        Parser parser(lexer);
        snippets.push_back(parser.parseProgram());
        return evaluator.evalProgram(snippets.back());
    }

    // The C++ code `source` (Luna making micro-op terms) instantiates to for target (a64 or x64)
    std::string instantiate(const std::string& source, const std::string& target)
    {
        Value v = eval("(instantiate (seq " + source + ") :" + target + ")");
        if (isRecord(v, "cpp"))
            return evaluator.apply(*evaluator.globals()->lookup("cpp->string"), {v}, SExpr{}).text;
        return toString(v);
    }

    std::string error(const std::string& source)
    {
        try
        {
            eval(source);
        }
        catch (const CompileError& e)
        {
            return e.what();
        }
        FAIL("expected an error");
        return "";
    }
};

bool contains(const std::string& text, const std::string& part)
{
    INFO(text);
    return text.find(part) != std::string::npos;
}

} // namespace

TEST_SUITE_BEGIN("LunaMicroOps");

TEST_CASE_FIXTURE(MicroOpFixture, "all_60_micro_ops_are_defined")
{
    // the 56 agreed, AddressOf, Label, Copy, and Interrupt
    CHECK(toString(eval("(length %microops)")) == "60");
}

TEST_CASE_FIXTURE(MicroOpFixture, "every_case_instantiates_for_both_targets")
{
    std::vector<Value> cases;
    listToVector(*evaluator.globals()->lookup("cases"), cases);
    REQUIRE(cases.size() >= 34);
    for (const auto& target : {"a64", "x64"})
    {
        for (const auto& c : cases)
        {
            std::vector<Value> parts;
            listToVector(c, parts);
            CAPTURE(parts[0].text);
            CAPTURE(target);
            CHECK_NOTHROW(eval("(instantiate ((nth (assoc \"" + parts[0].text + "\" cases) 1)) :" + target + ")"));
        }
    }
}

TEST_CASE_FIXTURE(MicroOpFixture, "templates_choose_by_argument_class")
{
    // An immediate when it fits, decided when generating for a literal and when lowering for an IR constant
    CHECK(contains(instantiate("(sink :I32 (Add :I32 a 5))", "a64"), "build.add(add, lowering.tempInt(OP_A(inst)), uint16_t(5));"));
    std::string ir = instantiate("(sink :I32 (Add :I32 a b))", "a64");
    CHECK(contains(ir, "OP_B(inst).kind == IrOpKind::Constant && uint64_t(lowering.intOp(OP_B(inst))) <= AssemblyBuilderA64::kMaxImmediate"));
    // The zero register for a zero
    CHECK(contains(instantiate("(Store :I32 (Field a :TValue.tt) 0)", "a64"), "build.str(wzr, "));
    // A TValue operand: VM register, constant or pointer, decided when lowering
    CHECK(contains(instantiate("(sink :I32 (Load :I32 (Field a :TValue.tt)))", "a64"), "lowering.tempAddr(OP_A(inst), offsetof(TValue, tt))"));
    CHECK(contains(instantiate("(sink :I32 (Load :I32 (Field a :TValue.tt)))", "x64"), "dword[rBase + (vmRegOp(OP_A(inst)) * sizeof(TValue) + offsetof(TValue, tt))]"));
}

TEST_CASE_FIXTURE(MicroOpFixture, "comparisons_fold_into_their_users")
{
    std::string guard = instantiate("(Guard (ICmp :eq :I32 (Load :I32 (Field a :TValue.tt)) 4) c)", "x64");
    CHECK(contains(guard, "build.cmp(load.reg, 4);"));
    CHECK(contains(guard, "lowering.jumpOrAbortOnUndef(getConditionInt(IrCondition::NotEqual), OP_C(inst), index, next);"));
    std::string a64 = instantiate("(Guard (ICmp :eq :I32 a 4) c)", "a64");
    CHECK(contains(a64, "build.cmp(lowering.tempInt(OP_A(inst)), uint16_t(4));"));
    CHECK(contains(a64, "build.b(getConditionInt(IrCondition::NotEqual), lowering.getTargetLabel(OP_C(inst), index, fresh));"));
    CHECK(contains(a64, "lowering.finalizeTargetLabel(OP_C(inst), index, fresh);"));
}

TEST_CASE_FIXTURE(MicroOpFixture, "values_are_shared_and_freed_after_their_last_use")
{
    // A term bound once and used twice is computed once
    std::string shared = instantiate("(let ([sum (Add :I64 a b)]) (seq (sink :I64 sum) (sink :I64 sum)))", "a64");
    CHECK(contains(shared, "auto add = "));
    CHECK(!contains(shared, "add_2"));
    CHECK(contains(shared, "regs.freeTemp(add);"));
    // A variable written by a micro-op is that micro-op's register, without a copy
    std::string variable = instantiate("(let ([r (variable :I64)]) (seq (Set r (Add :I64 a b)) (sink :I64 r)))", "a64");
    CHECK(contains(variable, "build.add(add, "));
    CHECK(!contains(variable, "build.mov("));
}

TEST_CASE_FIXTURE(MicroOpFixture, "nothing_may_live_across_a_call")
{
    CHECK(error("(instantiate (let ([v (Load :I64 (Field a :TValue.value))])"
                "  (seq (sink :I64 v) (CallC :None (Field (Root :NativeContext) :NativeContext.luaC_step) (list :Ptr (Root :State))) (sink :I64 v)))"
                " :a64)")
              .find("CallC with values live across it") != std::string::npos);
}

TEST_CASE_FIXTURE(MicroOpFixture, "errors")
{
    CHECK(error("(Add :F64 a b)").find("Add's t is one of") != std::string::npos);
    CHECK(error("(instantiate (seq (Field a 0)) :a64)").find("Field is folded into the micro-op that uses it") != std::string::npos);
    CHECK(error("(instantiate (seq (sink :I32 (ICmp :eq :I32 a b))) :a64)").find("ICmp is a comparison, not a value") != std::string::npos);
    CHECK(error("(define-microop (Bad [x :value]) (result :none) (effects :pure) [:a64 (gen:build :add x)] [:x64 (gen:build :add x)])"
                "(instantiate (seq (Bad a)) :a64)")
              .find("No :a64 overload of build.add takes (IrOp)") != std::string::npos);
    CHECK(error("(define-microop (Bad2 [x :value]) (result :none) (effects :pure) [:a64 (gen:build :frob x)] [:x64 (gen:build :frob x)])"
                "(instantiate (seq (Bad2 a)) :x64)")
              .find("build.frob isn't a declared :x64 instruction") != std::string::npos);
    CHECK(error("(define-microop (Half) (result :none) (effects :pure) [:a64 nil])").find("Half needs a [ :x64 ...] template") != std::string::npos);
    // Backward branch extends live range of values live at the loop header to the back-edge
    std::string loop = instantiate(
        "(let ([v (variable :I32)]) (seq (Set v (Add :I32 a 1)) (labels (top) (Label top) (sink :I32 v) (sink :I32 b) (Goto top))))", "a64"
    );
    CHECK(loop.find("build.b(top);") < loop.find("regs.freeTemp(add);"));
}

TEST_SUITE_END();
