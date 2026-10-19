// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"

#include "Core/CompileError.h"

#include <algorithm>
#include "Core/Evaluator.h"
#include "Core/Record.h"
#include "Driver.h"
#include "Core/Lexer.h"
#include "Core/Parser.h"

using namespace LunaTest;
using namespace Luwu::Dsl;

namespace
{

struct EvalFixture
{
    Evaluator evaluator;
    EvalFixture() { installNcg(evaluator); }
    std::vector<std::vector<SExpr>> programs; // closures point into the forms, so they stay alive

    std::string eval(const std::string& source)
    {
        Lexer lexer(source, "test.luna");
        Parser parser(lexer);
        programs.push_back(parser.parseProgram());
        Value v = evaluator.evalProgram(programs.back());
        if (isRecord(v, "cpp"))
            return evaluator.apply(*evaluator.globals()->lookup("cpp->string"), {v}, SExpr{}).text;
        return toString(v);
    }

    // The error message (with its location) of a program that must fail
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

} // namespace

TEST_SUITE_BEGIN("LunaEval");

TEST_CASE_FIXTURE(EvalFixture, "atoms_evaluate_to_themselves")
{
    CHECK(eval("42") == "42");
    CHECK(eval("0x10") == "16");
    CHECK(eval("-3") == "-3");
    CHECK(eval("2.5") == "2.5");
    CHECK(eval("\"hi\"") == "\"hi\"");
    CHECK(eval(":TValue.tt") == ":TValue.tt");
    CHECK(eval("#t") == "#t");
    CHECK(eval("#f") == "#f");
    CHECK(eval("nil") == "()");
}

TEST_CASE_FIXTURE(EvalFixture, "arithmetic_and_comparison")
{
    CHECK(eval("(+ 1 2 3)") == "6");
    CHECK(eval("(- 10 3 2)") == "5");
    CHECK(eval("(- 4)") == "-4");
    CHECK(eval("(* 2 3.5)") == "7.0");
    CHECK(eval("(/ 7 2)") == "3");
    CHECK(eval("(/ -7 2)") == "-3");
    CHECK(eval("(/ 7.0 2)") == "3.5");
    CHECK(eval("(remainder -7 2)") == "-1");
    CHECK(eval("(< 1 2 3)") == "#t");
    CHECK(eval("(< 1 3 2)") == "#f");
    CHECK(eval("(= 2 2.0)") == "#t");
    CHECK(eval("(bit-or (shl 1 4) 1)") == "17");
    CHECK(eval("(+ 9223372036854775807 1)") == "-9223372036854775808"); // integers wrap
}

TEST_CASE_FIXTURE(EvalFixture, "special_forms")
{
    CHECK(eval("(if (> 2 1) :yes :no)") == ":yes");
    CHECK(eval("(if #f :yes)") == "()");
    CHECK(eval("(if nil :truthy :falsy)") == ":truthy"); // only false is false
    CHECK(eval("(when (> 2 1) 1 2)") == "2");
    CHECK(eval("(unless (> 2 1) 1)") == "()");
    CHECK(eval("(cond [(> 1 2) :a] [(> 2 1) :b] [else :c])") == ":b");
    CHECK(eval("(cond [#f :a] [else :c])") == ":c");
    CHECK(eval("(cond [(+ 1 2)])") == "3");
    CHECK(eval("(and 1 2 3)") == "3");
    CHECK(eval("(and 1 #f 3)") == "#f");
    CHECK(eval("(or #f 2)") == "2");
    CHECK(eval("(begin 1 2 3)") == "3");
    CHECK(eval("(quote (a :b [c 1]))") == "(a :b (c 1))");
    CHECK(eval("[+ 1 2]") == "3"); // [...] is (...)
    CHECK(eval("(let [[x 1]] [list x 2])") == "(1 2)");
}

TEST_CASE_FIXTURE(EvalFixture, "bindings")
{
    CHECK(eval("(let ([a 1] [b 2]) (+ a b))") == "3");
    CHECK(eval("(let ([a 1]) (let ([a 2] [b a]) b))") == "1"); // let inits see the outer scope
    CHECK(eval("(let* ([a 1] [b (+ a 1)]) b)") == "2");
    CHECK(eval("(letrec ([ev? (lambda (n) (if (= n 0) #t (od? (- n 1))))] [od? (lambda (n) (if (= n 0) #f (ev? (- n 1))))]) (ev? 10))") == "#t");
    CHECK(eval("(let loop ([i 0] [acc nil]) (if (= i 3) (reverse acc) (loop (+ i 1) (cons i acc))))") == "(0 1 2)");
    CHECK(eval("(define x 5) (define (sq n) (* n n)) (sq x)") == "25");
    CHECK(eval("(define (f) (define y 3) (* y 2)) (f)") == "6");
}

TEST_CASE_FIXTURE(EvalFixture, "closures_capture_their_environment")
{
    CHECK(eval("(define (adder k) (lambda (x) (+ x k))) ((adder 10) 5)") == "15");
    CHECK(eval("(map (lambda (f) (f 1)) (map (lambda (k) (lambda (x) (* x k))) (list 2 3)))") == "(2 3)");
    CHECK(eval("((lambda (a . rest) (list a rest)) 1 2 3)") == "(1 (2 3))");
    CHECK(eval("((lambda all all))") == "()");
    CHECK(eval("((lambda args args) 1 2)") == "(1 2)");
    CHECK(eval("(define (f a . more) (list a more)) (f 1 2 3)") == "(1 (2 3))");
    CHECK(eval("'(a b . c)") == "(a b . c)");
    CHECK(eval("(cons 1 2)") == "(1 . 2)");
    CHECK(eval("(let ([x '(2 3)]) `(1 . ,x))") == "(1 2 3)");
    CHECK(error("'(a . b c)").find("A . goes before the last element of a list") != std::string::npos);
    CHECK(error("(lambda (a . b c) a)").find(". takes exactly one rest parameter") != std::string::npos);
    CHECK(error("#true").find("the booleans are #t and #f") != std::string::npos);
    CHECK(eval("(apply + 1 (list 2 3))") == "6");
    CHECK(eval("(apply (lambda (a b) (- a b)) (list 5 3))") == "2");
}

TEST_CASE_FIXTURE(EvalFixture, "derived_forms_expand_to_core_forms")
{
    CHECK(eval("(expand '(let ([x 1] [y 2]) (+ x y)))") == "((lambda (x y) (+ x y)) 1 2)");
    CHECK(eval("(expand '(let* ([x 1] [y x]) y))") == "((lambda (x) ((lambda (y) y) x)) 1)");
    CHECK(eval("(expand '(and a b c))") == "(if a (if b c #f) #f)");
    CHECK(eval("(expand '(cond [a 1] [b] [else 3]))") == "(if a (begin 1) ((lambda (#:or1) (if #:or1 #:or1 (begin 3))) b))");
    CHECK(eval("(expand '(letrec ([f (lambda () (g))] [g (lambda () 1)]) (f)))") ==
          "((lambda (f g) (set! f (lambda () (g))) (set! g (lambda () 1)) (f)) (#:unassigned) (#:unassigned))");
    CHECK(eval("(expand '(define (f a . r) a))") == "(define f (lambda (a . r) a))");
    CHECK(eval("(expand '(when c 1 2))") == "(if c (begin 1 2) nil)");
    CHECK(eval("(expand '`(a ,b))") == "(#:list (quote a) b)");

    CHECK(eval("(let loop () 5)") == "5"); // a named let with no bindings is just a call
    CHECK(eval("(let ([t 5]) (or #f t))") == "5"); // or's temporary can't capture t
}

TEST_CASE_FIXTURE(EvalFixture, "set_assigns_the_nearest_binding")
{
    CHECK(eval("(define n 1) (set! n (+ n 1)) n") == "2");
    CHECK(eval("(define (counter) (let ([c 0]) (lambda () (set! c (+ c 1)) c))) (define next (counter)) (next) (next)") == "2");
    CHECK(eval("(let ([x 1]) (let ([x 2]) (set! x 3)) x)") == "1");
    CHECK(error("(set! nowhere 1)").find("set! of 'nowhere', which isn't bound") != std::string::npos);
    CHECK(error("(letrec ([a b] [b 1]) a)").find("'b' is used before its letrec value is computed") != std::string::npos);
}

TEST_CASE_FIXTURE(EvalFixture, "macro_rules")
{
    // A local binding shadows a macro of the same name
    CHECK(eval("(define-macro (twice x) `(* 2 ,x)) (let ([twice (lambda (x) (+ x 1))]) (twice 5))") == "6");
    CHECK(eval("(twice 5)") == "10");
    CHECK(error("(define (f) (define-macro (m) 1) (m))").find("define-macro is only allowed at the top level") != std::string::npos);
    CHECK(error("(map twice (list 1))").find("twice is a macro, so it can't be called as a procedure") != std::string::npos);
}

TEST_CASE_FIXTURE(EvalFixture, "begin_keeps_only_its_last_value")
{
    CHECK(eval("(begin (+ 1 1) (gen:comment \"kept\"))") == "// kept");
}

TEST_CASE_FIXTURE(EvalFixture, "gensym_makes_names_nothing_else_can_be")
{
    CHECK(eval("(gensym)") == "#:g1");
    CHECK(eval("(gensym \"tmp\")") == "#:tmp2");
    CHECK(eval("(equal? (gensym) (gensym))") == "#f");

    // A macro that names its temporary with gensym can't capture the caller's variable
    CHECK(eval("(define-macro (my-or2 a b) (let ([t (gensym)]) `(let ([,t ,a]) (if ,t ,t ,b))))\n"
               "(let ([t 5]) (my-or2 #f t))") == "5");
}

TEST_CASE_FIXTURE(EvalFixture, "quote_and_quasiquote")
{
    CHECK(eval("'(a b)") == "(a b)");
    CHECK(eval("'x") == "x");
    CHECK(eval("`(a ,(+ 1 2) c)") == "(a 3 c)");
    CHECK(eval("(let ([xs (list 1 2)]) `(Add :I32 ,@xs 3))") == "(Add :I32 1 2 3)");
    CHECK(eval("(let ([xs nil]) `(f ,@xs))") == "(f)");
    CHECK(eval("`(Guard (ICmp :ne ,(+ 1 1) 0) [fail ,(* 2 3)])") == "(Guard (ICmp :ne 2 0) (fail 6))");
    CHECK(eval("`,(+ 1 2)") == "3");
    // Nested levels, as in R7RS: the inner unquote belongs to the inner quasiquote, the innermost one to the outer
    CHECK(eval("`(1 `(2 ,(3 ,(+ 1 3))))") == "(1 (quasiquote (2 (unquote (3 4)))))");
    CHECK(eval("(define (wrap x) `(Into res ,x)) (wrap '(Load :I32 a))") == "(Into res (Load :I32 a))");
    CHECK(eval("(define list 5) `(a ,list)") == "(a 5)"); // a user's list doesn't break quasiquote
    CHECK(error(",x").find(", is only allowed inside a quasiquote") != std::string::npos);
    CHECK(error("`,@(list 1)").find(",@ splices into a list") != std::string::npos);
}

TEST_CASE_FIXTURE(EvalFixture, "lists_and_the_prelude")
{
    CHECK(eval("(cons 1 (list 2 3))") == "(1 2 3)");
    CHECK(eval("(first (list 1 2))") == "1");
    CHECK(eval("(rest (list 1 2))") == "(2)");
    CHECK(eval("(length (list 1 2 3))") == "3");
    CHECK(eval("(nth (list :a :b :c) 2)") == ":c");
    CHECK(eval("(append (list 1) (list 2 3) nil (list 4))") == "(1 2 3 4)");
    CHECK(eval("(filter (lambda (x) (> x 1)) (list 1 2 3))") == "(2 3)");
    CHECK(eval("(foldl + 0 (range 0 5))") == "10");
    CHECK(eval("(foldr cons nil (list 1 2 3))") == "(1 2 3)");
    CHECK(eval("(any? (lambda (x) (= x 2)) (list 1 2))") == "#t");
    CHECK(eval("(all? (lambda (x) (= x 2)) (list 1 2))") == "#f");
    CHECK(eval("(assoc :b (list (list :a 1) (list :b 2)))") == "(:b 2)");
    CHECK(eval("(equal? (list 1 (list 2)) (list 1 (list 2)))") == "#t");
    CHECK(eval("(str \"a\" 1 :k)") == "\"a1:k\"");
    CHECK(eval("(keyword->string :TValue.tt)") == "\"TValue.tt\"");
}

TEST_CASE_FIXTURE(EvalFixture, "tail_calls_run_in_constant_space")
{
    CHECK(eval("(define (count n acc) (if (= n 0) acc (count (- n 1) (+ acc 1)))) (count 1000000 0)") == "1000000");
    CHECK(evaluator.maxStackDepth() < 16);

    CHECK(eval("(define (ev? n) (cond [(= n 0) #t] [else (od? (- n 1))])) (define (od? n) (and (not (= n 0)) (ev? (- n 1)))) (ev? 1000001)") == "#f");
    CHECK(evaluator.maxStackDepth() < 16);

    CHECK(eval("(let loop ([i 0]) (if (< i 1000000) (begin (+ 1 1) (loop (+ i 1))) i))") == "1000000");
    CHECK(evaluator.maxStackDepth() < 16);

    CHECK(eval("(length (map (lambda (x) x) (range 0 100000)))") == "100000");
    CHECK(evaluator.maxStackDepth() < 16);
}

TEST_CASE_FIXTURE(EvalFixture, "deep_recursion_is_limited_only_by_memory")
{
    // Not a tail call: the continuation stack grows on the heap, not the native stack
    CHECK(eval("(define (sum n) (if (= n 0) 0 (+ n (sum (- n 1))))) (sum 500000)") == "125000250000");
    CHECK(evaluator.maxStackDepth() >= 500000);
}

TEST_CASE_FIXTURE(EvalFixture, "errors_point_at_the_form")
{
    CHECK(error("(+ 1 nope)") == "test.luna:1:6: error: Unbound symbol 'nope'");
    CHECK(error("\n(+ 1 :k)") == "test.luna:2:1: error: + expects a number as argument 2, got :k (a keyword)");
    CHECK(error("(define (f a) a) (f 1 2)").find("f takes 1 argument(s), got 2 (defined at test.luna:1:1)") != std::string::npos);
    CHECK(error("(1 2)") == "test.luna:1:1: error: 1 (an int) isn't a procedure");
    CHECK(error("(error \"bad\" :thing 3)") == "test.luna:1:1: error: bad :thing 3");
    CHECK(error("(first nil)").find("first expects a non-empty list") != std::string::npos);
    CHECK(error("(cond [else 1] [#t 2])").find("else must be the last cond clause") != std::string::npos);
    CHECK(error("(lambda (a a) a)").find("Duplicate parameter 'a'") != std::string::npos);
    CHECK(error("(/ 1 0)").find("Division by zero") != std::string::npos);
    CHECK(error("[]").find("[] isn't an expression") != std::string::npos);
}

TEST_CASE_FIXTURE(EvalFixture, "macros_expand_to_code_evaluated_in_their_place")
{
    CHECK(eval("(define-macro (swap-call f a b) `(,f ,b ,a)) (swap-call - 1 10)") == "9");
    CHECK(eval("(define-macro (my-or . xs) (if (null? xs) #f `(let ([t ,(first xs)]) (if t t (my-or ,@(rest xs)))))) (my-or #f 2 3)") == "2");
    CHECK(eval("(when (> 2 1) 1 2)") == "2");
    CHECK(eval("(unless (> 2 1) 1)") == "()");

    // The expansion is in tail position
    CHECK(eval("(define (down n) (when (> n 0) (down (- n 1)))) (down 1000000)") == "()");
    CHECK(evaluator.maxStackDepth() < 16);

    // Macros expand when the form using them is expanded (before it runs), as in Scheme: f keeps the first m
    CHECK(eval("(define-macro (m) 1) (define (f) (m)) (f)") == "1");
    CHECK(eval("(define-macro (m) 2) (list (f) (m))") == "(1 2)");

    // Lists from the call keep their locations
    CHECK(error("(define-macro (add1 x) `(+ 1 ,x))\n(add1 (first nil))").find("test.luna:2:7: error: first expects a non-empty list") == 0);
    CHECK(error("(define-macro (bad) car) (bad)").find("A macro expansion can't contain a procedure") != std::string::npos);
}

TEST_CASE_FIXTURE(EvalFixture, "doc_comments_wrap_what_they_document")
{
    // The reader: leading lines and trailing comments become wrappers in the expression's place
    CHECK(eval("'(f\n  ~ one\n  ~ two\n  x\n  y) ; done") == "(f (doc-comment \"one\" \"two\" x) y)");
    CHECK(eval("'(f\n  x ~ after\n  y)") == "(f (doc-comment :trailing \"after\" x) y)");
    // On part of a form that starts on its line, a trailing comment documents the form
    CHECK(eval("'(f\n  (if c ~ why\n    then\n    else))") == "(f (doc-comment :trailing \"why\" (if c then else)))");

    // Alone, a comment statement; around code, attached to it
    CHECK(eval("(gen:comment \"a\" \"b\")") == "// a\n// b");
    CHECK(eval("~ before\n(gen:comment \"stmt\") ~ after") == "// before\n// stmt // after");
    CHECK(eval("(begin\n  ~ in a body\n  (gen:comment \"x\"))") == "// in a body\n// x");

    // Luna's own let and cond make no C++, so comments on their bindings and clauses would go nowhere
    CHECK(error("(let (\n      ~ the x\n      [x 1]) x)").find("test.luna:3:7: error: A ~ comment on a let binding goes nowhere") == 0);
    CHECK(error("(cond\n  ~ never\n  [#f 1]\n  [else 2])").find("A ~ comment on a cond clause goes nowhere") != std::string::npos);

    // Comments that would go nowhere are errors
    CHECK(error("~ the answer\n42").find("This ~ comment documents 42 (an int), which isn't code") != std::string::npos);
    CHECK(error("(+ 1\n   ~ two\n   2)").find("documents 2 (an int)") != std::string::npos);

    CHECK(eval("(strip-comments '(a\n  ~ d\n  b (doc-comment \"s\") c))") == "(a b c)");
}

TEST_CASE_FIXTURE(EvalFixture, "doc_comments_document_definitions")
{
    eval("~ Doubles x\n(define (dbl x) (* 2 x))\n(define (sq x) ~ Squares x\n  (* x x))\n~ Swaps\n(define-macro (swap f a b) `(,f ,b ,a))");
    std::vector<FormDoc> docs;
    evaluator.getFormDocs(docs);
    auto find = [&](const std::string& name) -> std::string {
        for (const auto& d : docs)
            if (d.name == name)
                return d.syntax + " | " + d.desc;
        return "";
    };
    CHECK(find("dbl") == "(dbl x) | Doubles x");
    CHECK(find("swap") == "(swap f a b) | Macro. Swaps");
    CHECK(eval("(dbl 4)") == "8");
}

TEST_CASE_FIXTURE(EvalFixture, "records")
{
    eval("(define-record point (x y))\n(define p (make-point 3 4))");
    CHECK(eval("(point-x p)") == "3");
    CHECK(eval("(point? p)") == "#t");
    CHECK(eval("(point? 3)") == "#f");
    // Records are compared by identity
    CHECK(eval("(equal? p p)") == "#t");
    CHECK(eval("(equal? p (make-point 3 4))") == "#f");
    // record-with makes a new record
    CHECK(eval("(point-x (record-with p 'x 9))") == "9");
    CHECK(eval("(point-x p)") == "3");
    CHECK(error("(make-point 1)").find("make-point") != std::string::npos);
}

TEST_CASE_FIXTURE(EvalFixture, "docs_go_into_a_record_with_a_docs_field")
{
    eval("(define-record entry (name docs))\n~ The first entry\n~ (two lines)\n(define e (make-entry 'e nil))");
    CHECK(eval("(entry-docs e)") == "(\"The first entry\" \"(two lines)\")");
    // A macro call's docs go to the definition it expands to
    eval("(define-macro (define-entry name) `(define ,name (make-entry ',name nil)))\n~ Made by a macro\n(define-entry f)");
    CHECK(eval("(entry-docs f)") == "(\"Made by a macro\")");
}

TEST_CASE_FIXTURE(EvalFixture, "syntax_errors_point_at_the_macro_argument")
{
    eval("(define-macro (check-all . items)\n"
         "  (for-each (lambda (i) (unless (number? (second i)) (syntax-error i \"not a number:\" (second i)))) items)\n"
         "  nil)");
    std::string message = error("(check-all\n  [a 1]\n  [b x])");
    CHECK(message.find("test.luna:3:3: error: not a number: x") != std::string::npos);
    // A datum that didn't come from the arguments: the macro call
    eval("(define-macro (fails) (syntax-error 'x \"always\"))");
    CHECK(error("(fails)").find("test.luna:1:1: error: always") != std::string::npos);
}

TEST_CASE_FIXTURE(EvalFixture, "string_helpers")
{
    CHECK(eval("(string-upcase \"add_Int\")") == "\"ADD_INT\"");
    CHECK(eval("(string-downcase \"ADD\")") == "\"add\"");
    CHECK(eval("(string-join (list \"a\" \"b\" \"c\") \", \")") == "\"a, b, c\"");
    CHECK(eval("(string-join nil \", \")") == "\"\"");
    CHECK(eval("(substring \"size_t=\" 0 6)") == "\"size_t\"");
    CHECK(eval("(string-suffix? \"size_t=\" \"=\")") == "#t");
    CHECK(eval("(number->string 42)") == "\"42\"");
    CHECK(eval("(number->string 2.5)") == "\"2.5\"");
    CHECK(eval("(string-escape \"a \\\"b\\\"\\nc\")") == "\"\\\"a \\\\\\\"b\\\\\\\"\\\\nc\\\"\"");
}

TEST_CASE_FIXTURE(EvalFixture, "garbage_collector_collects_temporary_cells")
{
    size_t beforeCount = gcCount();
    // A loop of 10,000 iterations allocating temporary lists and recursive letrec closures
    CHECK(eval(R"(
        (let loop ([n 10000] [acc 0])
          (if (= n 0)
              acc
              (letrec ([f (lambda (x) (+ x 1))])
                (loop (- n 1) (+ acc (car (cons (f 0) (list 1 2 3))))))))
    )") == "10000");

    evaluator.collectGarbage();
    CHECK(gcCount() < beforeCount + 500);
}

TEST_CASE("the_core_evaluator_needs_no_modules")
{
    // Without the C++ module: doc comments still document definitions; comments on other values have nowhere to go
    Evaluator core;
    std::vector<std::vector<SExpr>> programs;
    auto run = [&](const std::string& source) {
        Lexer lexer(source, "core.luna");
        Parser parser(lexer);
        programs.push_back(parser.parseProgram());
        return toString(core.evalProgram(programs.back()));
    };
    CHECK(run("~ Twice x\n(define (twice x) (* 2 x))\n(twice 4)") == "8");
    std::vector<FormDoc> docs;
    core.getFormDocs(docs);
    CHECK(std::any_of(docs.begin(), docs.end(), [](const FormDoc& d) { return d.name == "twice" && d.desc == "Twice x"; }));
    CHECK_THROWS_WITH_AS(run("~ the answer\n42"), doctest::Contains("which isn't code, so it would go nowhere"), CompileError);
    CHECK_THROWS_WITH_AS(run("(gen:seq)"), doctest::Contains("Unbound symbol 'gen:seq'"), CompileError);
}

TEST_CASE_FIXTURE(LunaFixture, "define_ircmd_is_a_top_level_form")
{
    file("root.luna", "(define (f) (define-ircmd NOP (class :pseudo)))\n(f)");
    run({"root.luna", "out"});
    requireError({"root.luna:1:13: error: define-ircmd is only allowed at the top level"});
}

TEST_CASE_FIXTURE(LunaFixture, "eval_prints_the_last_value_of_a_program")
{
    file("lib.luna", "(define (twice x) (* 2 x))");
    file("root.luna", "(include \"lib.luna\")\n(define-ircmd NOP (class :pseudo))\n(twice 21)");
    run({"--eval", "root.luna"});
    requireOk();
    CHECK(out == "42\n");
}

TEST_SUITE_END();
