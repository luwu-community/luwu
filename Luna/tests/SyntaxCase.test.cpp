// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "LunaFixture.h"
#include "Core/CompileError.h"
#include "Core/Evaluator.h"
#include "Core/Lexer.h"
#include "Core/Parser.h"
#include "Driver.h"

using namespace Luwu::Dsl;

namespace
{

struct SyntaxCaseFixture
{
    Evaluator evaluator;
    std::vector<std::vector<SExpr>> programs;

    SyntaxCaseFixture()
    {
        installNcg(evaluator);
        defineBuiltin(*evaluator.globals(), "test-gc", 0, 0, [this](const std::vector<Value>&, const SExpr&) {
            evaluator.collectGarbage(); return Value::nil();
        });
    }

    std::string eval(const std::string& source)
    {
        Lexer lexer(source, "syntax_case_test.luna");
        Parser parser(lexer);
        programs.push_back(parser.parseProgram());
        Value v = evaluator.evalProgram(programs.back());
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

} // namespace

TEST_SUITE_BEGIN("LunaSyntaxCase");

TEST_CASE_FIXTURE(SyntaxCaseFixture, "syntax_objects_and_predicates")
{
    CHECK(eval("(syntax? #'foo)") == "#t");
    CHECK(eval("(syntax? 'foo)") == "#f");
    CHECK(eval("(syntax? 42)") == "#f");
    CHECK(eval("(identifier? #'foo)") == "#t");
    CHECK(eval("(identifier? #'(1 2 3))") == "#f");
    CHECK(eval("(identifier? 123)") == "#f");
    CHECK(eval("(identifier? #'#f)") == "#f");
    CHECK(eval("(syntax->datum #'(+ 1 2))") == "(+ 1 2)");
    CHECK(eval("(syntax->datum #'sym)") == "sym");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "basic_syntax_case_and_syntax_template")
{
    CHECK(eval(R"(
        (define-syntax add-two
          (lambda (stx)
            (syntax-case stx ()
              [(_ a b) #'(+ a b)])))
        (add-two 3 4)
    )") == "7");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "ellipsis_matching_and_splicing")
{
    CHECK(eval(R"(
        (define-syntax my-list
          (lambda (stx)
            (syntax-case stx ()
              [(_) #'nil]
              [(_ x ...) #'(list x ...)])))
        (my-list 1 2 3 4)
    )") == "(1 2 3 4)");

    CHECK(eval(R"(
        (my-list)
    )") == "()");

    CHECK(eval(R"(
        (my-list 42)
    )") == "(42)");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "fenders_and_multi_clause_dispatch")
{
    CHECK(eval(R"(
        (define-syntax test-fender
          (lambda (stx)
            (syntax-case stx ()
              [(_ n) (equal? (syntax->datum #'n) 0) #':zero]
              [(_ n) (int? (syntax->datum #'n)) #':integer]
              [(_ other) #':other])))
        (list (test-fender 0) (test-fender 42) (test-fender "hello"))
    )") == "(:zero :integer :other)");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "hygiene_prevents_accidental_capture")
{
    // The classic hygiene acid test:
    // A macro introduces temporary variable 't'.
    // The caller invokes the macro passing its own variable named 't'.
    // In an unhygienic macro, the introduced 't' captures caller's 't'.
    // In a hygienic macro, the introduced 't' and caller's 't' do not interfere.
    CHECK(eval(R"(
        (define-syntax my-swap!
          (lambda (stx)
            (syntax-case stx ()
              [(_ a b)
               #'(let ([t a])
                   (set! a b)
                   (set! b t))])))

        (let ([t 10] [u 20])
          (my-swap! t u)
          (list t u))
    )") == "(20 10)");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "hygiene_preserves_referential_transparency")
{
    // The macro references global 'list'.
    // The caller shadows 'list' with a local non-function binding.
    // The macro must still resolve 'list' to global 'list', not the caller's local variable.
    CHECK(eval(R"(
        (define-syntax make-pair
          (lambda (stx)
            (syntax-case stx ()
              [(_ a b) #'(list a b)])))

        (let ([list 999])
          (make-pair 1 2))
    )") == "(1 2)");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "intentional_hygiene_breaking_with_datum_to_syntax")
{
    // Anaphoric if: 'it' is injected into the caller's scope via datum->syntax.
    CHECK(eval(R"(
        (define-syntax aif
          (lambda (stx)
            (syntax-case stx ()
              [(_ test then else)
               (with-syntax ([it (datum->syntax stx 'it)])
                 #'(let ([it test])
                     (if it then else)))])))

        (aif (+ 10 20) (+ it 5) 0)
    )") == "35");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "syntax_rules_macro")
{
    CHECK(eval(R"(
        (define-syntax my-cond
          (syntax-rules (else)
            [(_) nil]
            [(_ [else body ...]) (begin body ...)]
            [(_ [test body ...] rest ...)
             (if test (begin body ...) (my-cond rest ...))]))

        (my-cond
          [(= 1 2) :first]
          [(= 2 2) :second]
          [else :third])
    )") == ":second");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "generate_temporaries")
{
    CHECK(eval(R"(
        (define-syntax bind-pairs
          (lambda (stx)
            (syntax-case stx ()
              [(_ (val ...))
               (with-syntax ([(t ...) (generate-temporaries #'(val ...))])
                 #'(let ([t val] ...)
                     (list t ...)))])))

        (bind-pairs (1 2 3))
    )") == "(1 2 3)");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "nested_ellipses")
{
    CHECK(eval(R"(
        (define-syntax matrix
          (syntax-rules ()
            [(_ ((x ...) ...))
             (list (list x ...) ...)]))

        (matrix ((1 2) (3 4) (5 6)))
    )") == "((1 2) (3 4) (5 6))");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "syntax_case_error_handling")
{
    CHECK(error(R"(
        (define-syntax bad-syntax
          (lambda (stx)
            (syntax-case stx ()
              [(_ a b) #'(+ a b)])))
        (bad-syntax 1)
    )").find("syntax-case: no clause matched") != std::string::npos);

    CHECK(error(R"(
        (define-syntax mismatch-ellipsis
          (lambda (stx)
            (syntax-case stx ()
              [(_ (a ...) (b ...))
               #'(list (list a b) ...)])))
        (mismatch-ellipsis (1 2) (3 4 5))
    )").find("Incompatible ellipsis match counts") != std::string::npos);
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "hygiene_survives_internal_definitions_legacy_macros_and_assignment")
{
    CHECK(eval(R"(
        (define x 1)
        (define-macro (relay form) form)
        (define-syntax set-global (syntax-rules () [(_) (set! x 8)]))
        (define-syntax private (syntax-rules () [(_ body) ((lambda () (define x 99) (relay body)))]))
        (let ([x 7]) (set-global) (private x))
    )") == "7");
    CHECK(eval("x") == "8");
    CHECK(eval("(let ([x 3]) (list (private x) (private x)))") == "(3 3)");
    CHECK(eval("(define-macro (legacy x) `(list ,x)) (let ([list (lambda (x) (+ x 1))]) (legacy 4))") == "5");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "literals_and_free_identifiers_follow_lexical_bindings")
{
    CHECK(eval(R"(
        (define-syntax keyword (syntax-rules (else) [(_ else) :literal] [(_ other) :variable]))
        (list (keyword else) (let ([else 1]) (keyword else)))
    )") == "(:literal :variable)");
    CHECK(eval("(free-identifier=? #'unbound ((lambda (x) #'unbound) 1))") == "#t");
    CHECK(eval("(free-identifier=? #'x (let ([x 1]) #'x))") == "#f");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "dotted_patterns_templates_empty_repetitions_and_boolean_literals")
{
    CHECK(eval(R"(
        (define-syntax pack (syntax-rules () [(_ . args) (list . args)]))
        (list (pack) (pack 1 2 3))
    )") == "(() (1 2 3))");
    CHECK(eval(R"(
        (define-syntax boolean (syntax-rules () [(_ #f) :false] [(_ #t) :true] [(_ x) :other]))
        (list (boolean #f) (boolean #t) (boolean 3))
    )") == "(:false :true :other)");
    CHECK(eval("(syntax->datum #'(a . b))") == "(a . b)");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "syntax_case_roots_patterns_and_restores_expansion_context")
{
    CHECK(eval(R"(
        (syntax-case (datum->syntax #f '(1 2)) ()
          [(a b) (begin (test-gc) (syntax->datum #'(b a)))])
    )") == "(2 1)");
    CHECK(eval(R"(
        (define-syntax identifier-only (lambda (stx)
          (syntax-case stx () [(_ x) (identifier? x) #':identifier] [(_ x) #':value])))
        (list (identifier-only abc) (identifier-only 4))
    )") == "(:identifier :value)");
    CHECK(eval(R"(
        (define-syntax keep (lambda (stx) (syntax-case stx ()
          [(_ arg) (begin (test-gc) #'(list arg arg))])))
        (keep 12)
    )") == "(12 12)");
    CHECK(error(R"(
        (define-syntax fail (lambda (stx) (syntax-case stx () [(_ x) (error "macro failed")])))
        (fail 1)
    )").find("macro failed") != std::string::npos);
    CHECK(evaluator.currentIntroScope == 0);
    CHECK(evaluator.patternEnvStack.empty());
    CHECK(evaluator.expansions.empty());
    CHECK(eval("(keep 4)") == "(4 4)");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "hygienic_definition_validation_and_documentation")
{
    CHECK(error("(define-syntax bad 1)").find("procedure transformer") != std::string::npos);
    CHECK(error("(define-syntax bad (lambda (stx) 1)) (bad)").find("syntax object") != std::string::npos);
    CHECK(error("(with-syntax ([x]) #'x)").find("binding") != std::string::npos);
    CHECK(error("(syntax-case 1 () [x #t #t #t])").find("clause") != std::string::npos);
    eval("~ A hygienic identity\n(define-syntax identity (syntax-rules () [(_ x) x]))");
    std::vector<FormDoc> docs;
    evaluator.getFormDocs(docs);
    bool found = false;
    for (const FormDoc& doc : docs) if (doc.name == "identity" && doc.desc.find("A hygienic identity") != std::string::npos) found = true;
    CHECK(found);
    CHECK(eval("(identity 9)") == "9");
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "datum_macros_and_syntax_macros_can_replace_each_other")
{
    CHECK(eval("(define-macro (choice x) `(+ ,x 1)) (choice 4)") == "5");
    CHECK(eval("(define-syntax choice (syntax-rules () [(_ x) (* x 2)])) (choice 4)") == "8");
    CHECK(eval("(define-macro (choice . xs) `(list ,@xs)) (choice 4 5)") == "(4 5)");
    CHECK(eval("(define-macro (empty)) (empty)") == "()");
    CHECK(eval("(define-macro (rest-only . xs) `(quote ,xs)) (rest-only a b)") == "(a b)");
    std::string arityError = error("(define-macro (one x) x) (one)");
    CHECK(arityError.find("one takes 1 argument(s)") != std::string::npos);
    CHECK(arityError.find("defined at syntax_case_test.luna:1:") != std::string::npos);
    CHECK(error("((lambda () (define-macro (local) 1)))").find("define-macro is only allowed at the top level") != std::string::npos);
}

TEST_CASE_FIXTURE(SyntaxCaseFixture, "datum_transformers_keep_signatures_locations_and_gc_roots")
{
    eval("~ Datum macro docs\n(define-macro (documented first . rest) `(list ,first ,@rest))");
    std::vector<FormDoc> docs;
    evaluator.getFormDocs(docs);
    bool found = false;
    for (const FormDoc& doc : docs)
        if (doc.name == "documented")
        {
            CHECK(doc.syntax == "(documented first . rest)");
            CHECK(doc.desc.find("Datum macro docs") != std::string::npos);
            found = true;
        }
    CHECK(found);
    CHECK(eval(R"(
        (define-macro (preserve form)
          (begin
            (test-gc)
            (expand '(let ([x 1]) x))
            (test-gc)
            `(list ,form ,form)))
        (preserve (+ 2 3))
    )") == "(5 5)");
    CHECK(error("(define-macro (reject form) (begin (expand '(let ([x 1]) x)) (syntax-error form \"bad argument\")))\n(reject\n  (+ 1 2))").find("syntax_case_test.luna:3:3") != std::string::npos);
    CHECK(evaluator.expansions.empty());
    CHECK(evaluator.currentIntroScope == 0);
    CHECK(eval("(preserve 6)") == "(6 6)");
}

TEST_SUITE_END();
