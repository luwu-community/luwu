// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"

#include "Core/CompileError.h"
#include "Core/Evaluator.h"
#include "Core/Record.h"
#include "Driver.h"
#include "Core/Lexer.h"
#include "Core/Parser.h"

using namespace LunaTest;
using namespace Luwu::Dsl;

namespace
{

// Evaluates Luna source and prints the C++ term it makes
struct GenFixture
{
    Evaluator evaluator;
    std::vector<std::vector<SExpr>> programs;

    GenFixture()
    {
        installNcg(evaluator);
        gen("(define (emit insn . args) (apply gen:method (gen:name :build) insn args))");
    }

    std::string gen(const std::string& source)
    {
        Lexer lexer(source, "test.luna");
        Parser parser(lexer);
        programs.push_back(parser.parseProgram());
        Value v = evaluator.evalProgram(programs.back());
        if (isRecord(v, "cpp"))
            return evaluator.apply(*evaluator.globals()->lookup("cpp->string"), {v}, SExpr{}).stringValue();
        return toString(v);
    }

    std::string error(const std::string& source)
    {
        try
        {
            gen(source);
        }
        catch (const CompileError& e)
        {
            return e.what();
        }
        FAIL("expected an error");
        return "";
    }
};

} // namespace

TEST_SUITE_BEGIN("LunaGen");

TEST_CASE_FIXTURE(GenFixture, "names_and_literals")
{
    CHECK(gen("(gen:name :rBase)") == "rBase");
    CHECK(gen("(gen:enum :IrOpKind :Constant)") == "IrOpKind::Constant");
    CHECK(gen("(gen:offsetof :TValue :tt)") == "offsetof(TValue, tt)");
    CHECK(gen("(gen:offsetof :Closure.l.uprefs)") == "offsetof(Closure, l.uprefs)");
    CHECK(gen("(gen:sizeof :TValue)") == "sizeof(TValue)");
    CHECK(gen("(gen:+ :TValue.value 4)") == "offsetof(TValue, value) + 4"); // keywords with a dot are offsets
    CHECK(gen("(gen:== (gen:name :x) :LUA_TNIL)") == "x == LUA_TNIL");
    CHECK(gen("(gen:int -5)") == "-5");
    CHECK(gen("(gen:uint 7)") == "7u");
    CHECK(gen("(gen:i64 5)") == "int64_t(5)");
    CHECK(gen("(gen:i64 4294967296)") == "int64_t(4294967296ll)");
    CHECK(gen("(gen:i64 -9223372036854775808)") == "int64_t((-9223372036854775807ll - 1))");
    CHECK(gen("(gen:+ (gen:name :x) 4294967296)") == "x + int64_t(4294967296ll)");
    CHECK(gen("(gen:double 0.5)") == "0.5");
    CHECK(gen("(gen:double 2)") == "2.0");
    CHECK(gen("(gen:bool #f)") == "false");
    CHECK(gen("(gen:string \"a \\\"b\\\"\")") == "\"a \\\"b\\\"\"");
}

TEST_CASE_FIXTURE(GenFixture, "operators_get_parentheses_by_precedence")
{
    CHECK(gen("(gen:* (gen:+ 1 2) 3)") == "(1 + 2) * 3");
    CHECK(gen("(gen:+ 1 (gen:* 2 3))") == "1 + 2 * 3");
    CHECK(gen("(gen:- 1 (gen:- 2 3))") == "1 - (2 - 3)"); // left-associative
    CHECK(gen("(gen:- (gen:- 1 2) 3)") == "1 - 2 - 3");
    CHECK(gen("(gen:+ 1 2 3)") == "1 + 2 + 3");
    CHECK(gen("(gen:- (gen:- (gen:name :x)))") == "-(-x)");
    CHECK(gen("(gen:! (gen:== 1 2))") == "!(1 == 2)");
    CHECK(gen("(gen:compl (gen:name :m))") == "~m");
    CHECK(gen("(gen:? (gen:< 1 2) 3 (gen:? (gen:name :c) 4 5))") == "1 < 2 ? 3 : c ? 4 : 5");
    // Parentheses compilers warn about without
    CHECK(gen("(gen:|| (gen:name :a) (gen:&& (gen:name :b) (gen:name :c)))") == "a || (b && c)");
    CHECK(gen("(gen:| (gen:name :a) (gen:& (gen:name :b) 1))") == "a | (b & 1)");
    CHECK(gen("(gen:& (gen:== (gen:name :a) 1) (gen:name :b))") == "(a == 1) & b");
    CHECK(gen("(gen:<< 1 (gen:+ (gen:name :n) 1))") == "1 << (n + 1)");
}

TEST_CASE_FIXTURE(GenFixture, "calls_and_members")
{
    CHECK(gen("(gen:call :getConditionInt (gen:name :cond))") == "getConditionInt(cond)");
    CHECK(gen("(gen:method (gen:name :lowering) :regOp (gen:call :OP_A (gen:name :inst)))") == "lowering.regOp(OP_A(inst))");
    CHECK(gen("(emit :add (gen:name :x0) (gen:name :x1) 1)") == "build.add(x0, x1, 1)"); // a call is an expression
    CHECK(gen("(gen:seq (emit :add (gen:name :x0) (gen:name :x1) 1))") == "build.add(x0, x1, 1);"); // and a statement in a sequence
    CHECK(gen("(gen:field (gen:name :inst) :regA64)") == "inst.regA64");
    CHECK(gen("(gen:index (gen:name :dword) (gen:+ (gen:name :rBase) 16))") == "dword[rBase + 16]");
    CHECK(gen("(gen:cast :uint16_t (gen:+ (gen:name :x) 1))") == "uint16_t(x + 1)");
    CHECK(gen("(gen:field (gen:+ (gen:name :a) (gen:name :b)) :kind)") == "(a + b).kind");
    CHECK(gen("(let ([insn :mov]) (emit insn (gen:name :x0) 2))") == "build.mov(x0, 2)"); // names are evaluated
}

TEST_CASE_FIXTURE(GenFixture, "statements")
{
    CHECK(gen("(gen:seq (emit :nop) nil (gen:seq (emit :brk 1)))") == "build.nop();\nbuild.brk(1);");
    CHECK(gen("(gen:block (emit :nop))") == "{\n    build.nop();\n}");
    CHECK(gen("(gen:if (gen:name :c) (emit :nop))") == "if (c)\n{\n    build.nop();\n}");
    CHECK(gen("(gen:if (gen:name :c) (emit :nop) (gen:if (gen:name :d) nil (emit :brk 1)))") ==
          "if (c)\n{\n    build.nop();\n}\nelse if (d)\n{\n}\nelse\n{\n    build.brk(1);\n}");
    CHECK(gen("(gen:assert (gen:< (gen:name :n) 4))") == "CODEGEN_ASSERT(n < 4);");
    CHECK(gen("(gen:cond [(gen:name :a) (emit :nop)] [(gen:name :b)] [else (emit :udf)])") ==
          "if (a)\n{\n    build.nop();\n}\nelse if (b)\n{\n}\nelse\n{\n    build.udf();\n}");
    CHECK(gen("(gen:switch (gen:name :k) [:A (emit :nop)] [(:B 3) (emit :udf)] [else])") ==
          "switch (k)\n{\ncase A:\n{\n    build.nop();\n    break;\n}\ncase B:\ncase 3:\n{\n    build.udf();\n    break;\n}\ndefault:\n{\n    break;\n}\n}");
}

TEST_CASE_FIXTURE(GenFixture, "locals")
{
    CHECK(gen("(gen:let ([x 5] [y (:type int64_t) (gen:+ x 1)] [z (:type Label)] [r (:ref) (gen:name :lbl)]) (emit :mov y x))") ==
          "auto x = 5;\nint64_t y = x + 1;\nLabel z;\nauto& r = lbl;\nbuild.mov(y, x);");
    CHECK(gen("(gen:let ([t (:type ScopedRegX64) (:args (gen:name :regs) (gen:enum :SizeX64 :qword))]) (emit :mov t 1))") ==
          "ScopedRegX64 t{regs, SizeX64::qword};\nbuild.mov(t, 1);");

    // Names: the Luna name, a suffix only on a clash with an enclosing local or a name the code uses
    CHECK(gen("(gen:let ([x 1]) (gen:let ([x 2]) (emit :mov x 3)))") == "auto x = 1;\nauto x_2 = 2;\nbuild.mov(x_2, 3);");
    CHECK(gen("(gen:let ([build 1] [temp-reg 2]) (emit :mov build temp-reg))") == "auto build_2 = 1;\nauto temp_reg = 2;\nbuild.mov(build_2, temp_reg);");
    CHECK(gen("(gen:seq (gen:block (gen:let ([t 1]) (emit :nop t))) (gen:block (gen:let ([t 2]) (emit :nop t))))") ==
          "{\n    auto t = 1;\n    build.nop(t);\n}\n{\n    auto t = 2;\n    build.nop(t);\n}"); // sibling scopes reuse names
    CHECK(gen("(gen:let ([x (gen:name :x)]) (emit :nop x))") == "auto x_2 = x;\nbuild.nop(x_2);");

    CHECK(gen("(gen:let ([x (:type int)]) (gen:set! x 4) (gen:set! (gen:field (gen:name :inst) :a) x))") == "int x;\nx = 4;\ninst.a = x;");
    CHECK(gen("(gen:label (done) (emit :b done) (emit :setLabel done))") == "Label done;\nbuild.b(done);\nbuild.setLabel(done);");
    CHECK(gen("(gen:for (i 0 (gen:name :n)) (emit :nop i))") == "for (int i = 0; i < n; ++i)\n{\n    build.nop(i);\n}");
}

TEST_CASE_FIXTURE(GenFixture, "values_with_statements")
{
    // The statements of a gen:value go before the statement that uses it
    gen("(define (temp v) (gen:let ([t (gen:method (gen:name :regs) :allocTemp)]) (gen:value (emit :mov t v) t)))");
    CHECK(gen("(gen:seq (emit :add (gen:name :x0) (temp 1) (temp 2)))") ==
          "auto t = regs.allocTemp();\nbuild.mov(t, 1);\nauto t_2 = regs.allocTemp();\nbuild.mov(t_2, 2);\nbuild.add(x0, t, t_2);");
    CHECK(gen("(gen:if (gen:== (temp 1) 0) (emit :nop))") == "auto t = regs.allocTemp();\nbuild.mov(t, 1);\nif (t == 0)\n{\n    build.nop();\n}");
    CHECK(gen("(gen:seq (emit :nop) (temp 3))") == "build.nop();\nauto t = regs.allocTemp();\nbuild.mov(t, 3);"); // a value in statement position

    CHECK(error("(gen:if (gen:&& (gen:name :c) (temp 1)) (emit :nop))").find("it's in the conditional part of an &&") != std::string::npos);
    CHECK(error("(gen:seq (temp 1) (emit :nop))").find("is a gen:value whose value would be lost") != std::string::npos);
    CHECK(error("(gen:block (temp 1))").find("a gen:block has no value") != std::string::npos);
}

TEST_CASE_FIXTURE(GenFixture, "comments")
{
    CHECK(gen("(gen:seq\n  ~ first\n  (emit :nop)\n  (emit :brk 1) ~ second\n  )") == "// first\nbuild.nop();\nbuild.brk(1); // second");
    CHECK(gen("(gen:if (gen:name :c) ~ why\n  (emit :nop))") == "if (c) // why\n{\n    build.nop();\n}");
    CHECK(gen("(gen:seq (emit :add (gen:name :x)\n  ~ the operand\n  (gen:name :y)))") == "// the operand\nbuild.add(x, y);"); // comments on parts go on the statement
    CHECK(gen("(gen:+ (gen:name :x) 1) ~ alone") == "x + 1 // alone");
    CHECK(gen("(gen:let (\n  ~ the temp\n  [t 1]) (emit :nop t))") == "// the temp\nauto t = 1;\nbuild.nop(t);");
}

TEST_CASE_FIXTURE(GenFixture, "errors")
{
    CHECK(error("(gen:seq (gen:name :x))").find("is an expression that does nothing as a statement") != std::string::npos);
    CHECK(error("(gen:seq 1)").find("gen:seq takes statements, got 1") != std::string::npos);
    CHECK(gen("(gen:+ (gen:call :f) 1)") == "f() + 1"); // a call is an expression
    CHECK(error("(gen:+ (gen:block) 1)").find("expects an expression, got a statement") != std::string::npos);
    CHECK(error("(gen:name \"not a name\")").find("isn't a C++ name") != std::string::npos);
    CHECK(error("(gen:+ \"s\" 1)").find("use gen:string for a string literal") != std::string::npos);
    CHECK(error("(gen:+ 'x 1)").find("use a keyword for a C++ name") != std::string::npos);
    CHECK(error("(emit :nop (%gen-local (%gen-decl \"x\" nil #f 1 (list))))").find("The local 'x' is used outside the gen:let") != std::string::npos);
    CHECK(error("(gen:let ([x]) x)").find("needs a (:type T)") != std::string::npos);
    CHECK(error("(gen:let ([x (:bogus) 1]) x)").find("Unknown gen:let option") != std::string::npos);
}

TEST_SUITE_END();
