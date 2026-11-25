// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"
#include "Core/Bytecode.h"
#include "Core/CompileError.h"
#include "Core/Evaluator.h"
#include "Core/Lexer.h"
#include "Core/Parser.h"

using namespace Luwu::Dsl;

namespace
{
Value evaluate(Evaluator& evaluator, const std::string& source)
{
    Lexer lexer(source, "bytecode-test.luna");
    Parser parser(lexer);
    return evaluator.evalProgram(parser.parseProgram());
}

std::string outcome(Evaluator& evaluator, const std::string& source)
{
    try { return toString(evaluate(evaluator, source)); }
    catch (const CompileError& error) { return error.what(); }
}

void installGc(Evaluator& evaluator)
{
    defineBuiltin(*evaluator.globals(), "test-gc", 0, 0, [&evaluator](const std::vector<Value>&, const SExpr&) {
        evaluator.collectGarbage();
        return Value::nil();
    });
}
}

TEST_SUITE_BEGIN("LunaBytecode");

TEST_CASE("register_vm_preserves_values_effect_order_and_diagnostics")
{
    Evaluator bytecode;
    const struct { const char* source; const char* expected; } cases[] = {
        {"(let ([x 0]) (list (begin (set! x (+ x 1)) x) (begin (set! x (+ x 1)) x)))", "(1 2)"},
        {"(let ([x 5]) (define x (+ x 1)) x)", "6"},
        {"(let ([x 5]) ((lambda () (if #f (define x 9)) (set! x 7))) x)", "7"},
        {"(letrec ([even (lambda (n) (if (= n 0) #t (odd (- n 1))))] [odd (lambda (n) (if (= n 0) #f (even (- n 1))))]) (even 5001))", "#f"},
        {"((lambda (x . x) x) 1 2 3)", "(2 3)"},
        {"(apply apply (list + (list 1 2 3)))", "6"},
        {"(letrec ([a b] [b 1]) a)", "bytecode-test.luna:1:1: error: 'b' is used before its letrec value is computed"},
        {"(set! nowhere 1)", "bytecode-test.luna:1:1: error: set! of 'nowhere', which isn't bound (define it first)"},
        {"(+ 1 nope)", "bytecode-test.luna:1:6: error: Unbound symbol 'nope'"},
        {"((lambda (x) x))", "bytecode-test.luna:1:1: error: an anonymous lambda takes 1 argument(s), got 0 (defined at bytecode-test.luna:1:2)"},
        {"(apply + 1 2)", "bytecode-test.luna:1:1: error: apply's last argument must be a list, got an int"},
        {"(+ :bad 1)", "bytecode-test.luna:1:1: error: + expects a number as argument 1, got :bad (a keyword)"},
        {"(if #f () 7)", "7"},
        {"(if #t [] 7)", "bytecode-test.luna:1:8: error: [] isn't an expression; nil is the empty list"},
    };
    for (const auto& test : cases)
    {
        INFO(test.source);
        CHECK(outcome(bytecode, test.source) == test.expected);
    }
}

TEST_CASE("upvalues_share_mutation_and_forward_through_intermediate_closures")
{
    Evaluator evaluator;
    evaluate(evaluator, "(define accessors (let ([x 10]) (list (lambda () x) (lambda (v) (set! x v)))))");
    std::vector<Value> accessors;
    REQUIRE(listToVector(*evaluator.globals()->lookup("accessors"), accessors));
    REQUIRE(accessors[0].closure->upvalues.size() == 1);
    REQUIRE(accessors[1].closure->upvalues.size() == 1);
    CHECK(accessors[0].closure->upvalues[0] == accessors[1].closure->upvalues[0]);
    CHECK(accessors[0].closure->env == evaluator.globals());
    CHECK(toString(evaluate(evaluator, "((car (cdr accessors)) 42) ((car accessors))")) == "42");
    Value forwarded = evaluate(evaluator, "(let ([x 77]) ((lambda () (lambda () x))))");
    Evaluator::Roots root(evaluator, {forwarded});
    REQUIRE(forwarded.closure->upvalues.size() == 1);
    REQUIRE(forwarded.closure->bytecode->captures.size() == 1);
    CHECK(!forwarded.closure->bytecode->captures[0].local);
    evaluator.collectGarbage();
    CHECK(toString(evaluator.apply(forwarded, {}, SExpr{})) == "77");
}

TEST_CASE("tail_frame_replacement_preserves_each_captured_binding")
{
    Evaluator evaluator;
    CHECK(toString(evaluate(evaluator,
        "(define fs (let loop ([n 0] [out nil])"
        " (if (= n 2000) (reverse out) (loop (+ n 1) (cons (lambda () n) out)))))"
        " (list ((car fs)) ((car (cdr fs))) ((car (reverse fs))))")) == "(0 1 1999)");
    CHECK(evaluator.maxStackDepth() < 16);
}

TEST_CASE("bytecode_gc_roots_cover_arguments_upvalues_constants_and_native_reentry")
{
    Evaluator evaluator;
    installGc(evaluator);
    CHECK(toString(evaluate(evaluator,
        "(define (save x) (lambda () (test-gc) x))"
        " (define get (save (vector \"retained\")))"
        " (test-gc) (list (get) (begin (test-gc) \"literal\"))")) == "(#(\"retained\") \"literal\")");
    // The outer call's first argument is live while the second reenters the VM.
    CHECK(toString(evaluate(evaluator,
        "(define p (make-parameter :outer))"
        " (list (vector 1 2) (parameterize ([p :inner]) (test-gc) (p)) (p))")) == "(#(1 2) :inner :outer)");
    CHECK(outcome(evaluator, "(parameterize ([p :broken]) (test-gc) (error \"failure\"))").find("failure") != std::string::npos);
    CHECK(toString(evaluate(evaluator, "(p)")) == ":outer");
}

TEST_CASE("register_temporaries_are_reused_and_capture_descriptors_are_explicit")
{
    Evaluator evaluator;
    std::string source = "(lambda (x) ";
    for (size_t i = 0; i < 1000; ++i) source += "(+ x 1) ";
    source += "x)";
    Value procedure = evaluate(evaluator, source);
    REQUIRE(procedure.closure->bytecode != nullptr);
    CHECK(procedure.closure->bytecode->registerCount < 10);
    CHECK(procedure.closure->upvalues.empty());
    bool indexed = false;
    for (const auto& instruction : procedure.closure->bytecode->instructions)
        indexed |= instruction.op == BytecodeOp::LoadLocal;
    CHECK(indexed);
}

TEST_CASE("unreachable_recursive_upvalue_cycles_are_collected")
{
    Evaluator evaluator;
    evaluator.collectGarbage();
    size_t before = gcCount();
    for (size_t i = 0; i < 100; ++i)
        evaluate(evaluator, "(letrec ([self (lambda () self)]) nil)");
    CHECK(gcCount() > before);
    evaluator.collectGarbage();
    CHECK(gcCount() <= before);
}

TEST_CASE("syntax_transformers_run_on_bytecode_with_fenders_and_gc")
{
    Evaluator bytecode;
    installGc(bytecode);
    const std::string source =
        "(define-syntax pick (lambda (stx) (syntax-case stx ()"
        " [(_ x) (begin (test-gc) #f) #'invalid]"
        " [(_ x) (begin (test-gc) #'(let ([tmp 9]) x))])))"
        " (let ([tmp 42]) (pick tmp))";
    CHECK(outcome(bytecode, source) == "42");
    CHECK(outcome(bytecode, "(define-macro (twice x) `(+ ,x ,x)) (twice 6)") == "12");
    const std::string capturedFender =
        "(define-syntax captured (let ([expected 7]) (lambda (stx)"
        " (syntax-case stx () [(_ x) (= expected 7) #'x])))) (captured 91)";
    CHECK(outcome(bytecode, capturedFender) == "91");
}

TEST_CASE("bytecode_dump_is_deterministic_and_explains_registers_captures_and_control_flow")
{
    Evaluator evaluator;
    Value value = evaluate(evaluator, "(lambda (x) (lambda (n) (if n (begin (set! x n) x) (list x))))");
    std::ostringstream first, second;
    dumpBytecode(first, *value.closure->bytecode);
    dumpBytecode(second, *value.closure->bytecode);
    CHECK(first.str() == second.str());
    for (const char* fragment : {"registers=", ".local l0 x", ".upvalue u0 <- parent.l0 x", "GETUPVAL", "SETUPVAL", "JUMPFALSE", "TAILCALL", "bytecode-test.luna:1:"})
        CHECK(first.str().find(fragment) != std::string::npos);
}

TEST_CASE_FIXTURE(LunaTest::LunaFixture, "bytecode_cli_dump_keeps_evaluation_output_separate")
{
    file("root.luna", "((lambda (x) (+ x 1)) 4)");
    run({"--eval", "--dump-bytecode", "root.luna"});
    requireOk();
    CHECK(out == "5\n");
    CHECK(err.find(".function f0") != std::string::npos);
    CHECK(err.find(".local l0 x") != std::string::npos);
    run({"--eval", "root.luna"});
    requireOk();
    CHECK(out == "5\n");
    CHECK(err.empty());
    for (const char* option : {"--engine=ast", "--engine=bytecode"})
    {
        run({"--eval", option, "root.luna"});
        requireError({"unknown option", option});
    }
}

TEST_SUITE_END();
