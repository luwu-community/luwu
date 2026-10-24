// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "doctest.h"
#include "Core/Evaluator.h"
#include "Core/CompileError.h"
#include "Core/Lexer.h"
#include "Core/Parser.h"
#include "Core/Record.h"

using namespace Luwu::Dsl;
namespace
{
struct CollectionFixture
{
    Evaluator evaluator;
    Value value(const std::string& source)
    {
        Lexer lexer(source, "collections.luna");
        Parser parser(lexer);
        // Evaluator::keep owns every expanded form used by a closure.
        return evaluator.evalProgram(parser.parseProgram());
    }
    std::string eval(const std::string& source) { return toString(value(source)); }
    bool fails(const std::string& source, const std::string& fragment)
    {
        try { value(source); }
        catch (const CompileError& e) { return std::string(e.what()).find(fragment) != std::string::npos; }
        return false;
    }
    CollectionFixture()
    {
        defineBuiltin(*evaluator.globals(), "test-gc", 0, 0, [this](const std::vector<Value>&, const SExpr&) {
            evaluator.collectGarbage(); return Value::nil();
        });
    }
};
}

TEST_SUITE_BEGIN("LunaCollections");
TEST_CASE_FIXTURE(CollectionFixture, "vectors_mutation_copy_bounds_and_cycles")
{
    CHECK(eval("(define v (make-vector 3 :empty)) (vector-set! v 1 42) (vector->list v)") == "(:empty 42 :empty)");
    CHECK(eval("(define w (vector-copy v)) (vector-set! w 0 :changed) (list (vector-ref v 0) (vector-length w))") == "(:empty 3)");
    CHECK(eval("(equal? (list->vector '(1 2)) (vector 1 2))") == "#t");
    CHECK(eval("(eq? (vector 1) (vector 1))") == "#f");
}

TEST_CASE_FIXTURE(CollectionFixture, "vector_validation_and_gc")
{
    CHECK(eval("(list (vector? (vector)) (vector-length (vector)))") == "(#t 0)");
    CHECK(fails("(vector-ref (vector 1) -1)", "index in range"));
    CHECK(fails("(vector-ref (vector 1) 1)", "index in range"));
    CHECK(fails("(vector-set! (vector) 0 1)", "index in range"));
    CHECK(fails("(make-vector -1)", "length"));
    CHECK(fails("(list->vector '(1 . 2))", "proper list"));
    CHECK(eval("(define cycle (vector nil)) (vector-set! cycle 0 cycle) (test-gc) cycle") == "#(#<cycle>)");
    CHECK(eval("(define other (vector nil)) (vector-set! other 0 other) (equal? cycle other)") == "#t");
    CHECK(eval("(define nested (vector (vector \"alive\"))) (test-gc) (vector-ref (vector-ref nested 0) 0)") == "\"alive\"");
}

TEST_CASE_FIXTURE(CollectionFixture, "match_lists_literals_repeated_bindings_and_tail_calls")
{
    CHECK(eval("(match '(def 7 9) [('def id x) (+ id x)] [else 0])") == "16");
    CHECK(eval("(match '(1 2 3) [(list a . tail) (cons a tail)])") == "(1 2 3)");
    CHECK(eval("(match '(1 . 2) [(cons a b) (+ a b)])") == "3");
    CHECK(eval("(match '(3 3) [(list x x) x] [else #f])") == "3");
    CHECK(eval("(match '(3 4) [(list x x) x] [else #f])") == "#f");
    CHECK(eval("(match nil [nil :empty] [else :other])") == ":empty");
    CHECK(eval("(match #f [#f :false] [_ :other])") == ":false");
    CHECK(eval("(define count 0) (match (begin (set! count (+ count 1)) :x) [:x count])") == "1");
    CHECK(eval("(let loop ([n 10000]) (match n [0 :done] [n (loop (- n 1))]))") == ":done");
    CHECK(fails("(match 1 [2 :no])", "no matching clause"));
    CHECK(fails("(match 1 [else 0] [_ 2])", "else must be"));
}

TEST_CASE_FIXTURE(CollectionFixture, "match_records_vectors_guards_and_predicates")
{
    CHECK(eval("(define-record node (name children)) (define n (make-node :add (vector 2 3)))"
               "(match n [(record node [children (vector a b)] [name :add]) (+ a b)])") == "5");
    CHECK(eval("(match n [(record node [absent x]) :bad] [else :ok])") == ":ok");
    CHECK(eval("(match (vector 1 2) [(vector x) :bad] [(vector x y) (when (> y x)) y])") == "2");
    CHECK(eval("(match 7 [(? int? x) (when (< x 0)) :negative] [(and (? int?) x) x])") == "7");
    CHECK(eval("(let ([x :outer]) (match-let ([(list x y) '(1 2)] [z x]) (list x y z)))") == "(1 2 :outer)");
    CHECK(fails("(match 1 [(cons a) a])", "cons match pattern"));
    CHECK(fails("(match-let ([1 2]) :bad)", "no matching clause"));
}

TEST_CASE_FIXTURE(CollectionFixture, "parameters_restore_nested_bindings_and_errors")
{
    CHECK(eval("(define p (make-parameter :outer)) (list (parameter? p) (procedure? p) (p))") == "(#t #t :outer)");
    CHECK(eval("(parameterize ([p :inner]) (list (p) (parameterize ([p :deep]) (p)) (p)))") == "(:inner :deep :inner)");
    CHECK(eval("(p)") == ":outer");
    CHECK(eval("(parameterize ([p :inner]) (p :changed) (p))") == ":changed");
    CHECK(eval("(p)") == ":outer");
    CHECK(fails("(parameterize ([p :inner]) (test-gc) (error \"boom\"))", "boom"));
    CHECK(eval("(p)") == ":outer");
    CHECK(fails("(parameterize ([p 1] [p 2]) :bad)", "twice"));
    CHECK(fails("(parameterize ([1 2]) :bad)", "require a parameter"));
    CHECK(fails("(p 1 2)", "zero or one"));
    CHECK(fails("(parameterize ([p (error \"initializer\")]) :bad)", "initializer"));
    CHECK(eval("(p)") == ":outer");
    CHECK(eval("(define q (make-parameter :q)) (parameterize ([p :new] [q (p)]) (q))") == ":outer");
}

TEST_CASE_FIXTURE(CollectionFixture, "parameters_saved_values_are_gc_roots")
{
    CHECK(eval("(define p (make-parameter (vector \"saved\")))"
               "(parameterize ([p :temporary]) (test-gc) (let loop ([i 5000]) (unless (= i 0) (vector i) (loop (- i 1)))))"
               "(vector-ref (p) 0)") == "\"saved\"");
    CHECK(fails("(parameterize ([p :temporary]) (test-gc) (error \"unwind\"))", "unwind"));
    CHECK(eval("(vector-ref (p) 0)") == "\"saved\"");
}

TEST_CASE_FIXTURE(CollectionFixture, "tables_comparison_missing_false_order_and_copy")
{
    CHECK(eval("(define t (make-table)) (table-set! t '(a 1) #f) (table-set! t :second 2)"
               "(list (table-has? t '(a 1)) (table-ref t '(a 1)) (table-ref t :missing nil))") == "(#t #f ())");
    CHECK(eval("(table-set! t '(a 1) 3) (table-keys t)") == "((a 1) :second)");
    CHECK(eval("(table-update! t :counter (lambda (n) (+ n 1)) 0)") == "1");
    CHECK(eval("(define copy (table-copy t)) (table-delete! copy :second) (list (table-size t) (table-size copy))") == "(3 2)");
    CHECK(fails("(table-ref t :missing)", "missing key"));
    CHECK(fails("(table-update! t :missing (lambda (x) x))", "missing key"));
    CHECK(eval("(define ids (make-table :identity)) (define k '(a)) (table-set! ids k 1)"
               "(list (table-ref ids k) (table-has? ids (list 'a)))") == "(1 #f)");
    CHECK(eval("(table-set! t 1 :one) (table-ref t 1.0)") == ":one");
    CHECK(fails("(table-set! t (vector 1) 3)", "Mutable vectors"));
    CHECK(fails("(table-set! t (list (vector 1)) 3)", "Mutable vectors"));
    CHECK(fails("(table-set! t (/ 0.0 0.0) 3)", "NaN"));
    CHECK(eval("(define key (vector 1)) (table-set! ids key 4) (vector-set! key 0 9) (table-ref ids key)") == "4");
    CHECK(eval("(table-set! ids :self ids) (test-gc) (eq? ids (table-ref ids :self))") == "#t");
    CHECK(eval("(table-delete! t :second) (table-set! t :second 5) (last (table-keys t))") == ":second");
}

TEST_CASE_FIXTURE(CollectionFixture, "sets_are_deterministic_and_combinations_are_pure")
{
    CHECK(eval("(define a (list->set '(3 1 3 2))) (define b (list->set '(2 4))) (set->list a)") == "(3 1 2)");
    CHECK(eval("(set->list (set-union a b))") == "(3 1 2 4)");
    CHECK(eval("(set->list (set-intersect a b))") == "(2)");
    CHECK(eval("(set->list (set-difference a b))") == "(3 1)");
    CHECK(eval("(set->list a)") == "(3 1 2)");
    CHECK(eval("(set=? a (list->set '(2 3 1)))") == "#t");
    CHECK(eval("(list (set-add! a 3) (set-delete! a 3) (set-has? a 3))") == "(#f #t #f)");
    CHECK(fails("(set-union a (make-set :identity))", "modes must match"));
    CHECK(eval("(let loop ([i 200]) (unless (= i 0) (set-add! a i) (set-delete! a i) (loop (- i 1)))) (set->list a)") == "()");
}

TEST_CASE_FIXTURE(CollectionFixture, "record_update_and_list_utilities")
{
    CHECK(eval("(define-record point (x y)) (define p (make-point 1 2))"
               "(define q (record-update p [x 8] [y 9])) (list (point-x p) (point-x q) (point-y q))") == "(1 8 9)");
    CHECK(fails("(record-update p [z 3])", "no field"));
    CHECK(fails("(record-update p [x 3] [x 4])", "repeats field"));
    CHECK(eval("(find (lambda (x) (> x 2)) '(1 3 4))") == "3");
    CHECK(eval("(find int? nil :absent)") == ":absent");
    CHECK(eval("(filter-map (lambda (x) x) '(#f () 0 2))") == "(() 0 2)");
    CHECK(eval("(append-map (lambda (x) (list x x)) '(1 2))") == "(1 1 2 2)");
    CHECK(eval("(partition int? '(1 :a 2 :b))") == "((1 2) (:a :b))");
    CHECK(eval("(unique-by first '((1 :a) (2 :b) (1 :c)))") == "((1 :a) (2 :b))");
    CHECK(eval("(define groups (group-by first '((1 :a) (2 :b) (1 :c))))"
               "(list (table-keys groups) (table-ref groups 1))") == "((1 2) ((1 :a) (1 :c)))");
}

TEST_CASE_FIXTURE(CollectionFixture, "worklist_propagation_cycles_and_nonconvergence")
{
    CHECK(eval("(define facts (worklist-fixpoint '(0 1 2)"
               " (lambda (n) (if (< n 2) (list (+ n 1)) nil))"
               " (lambda (n facts) (if (= n 0) 1 (+ 1 (table-ref facts (- n 1)))))"
               " (lambda (_) 0))) (map (lambda (n) (table-ref facts n)) '(0 1 2))") == "(1 2 3)");
    CHECK(eval("(define cycle (worklist-fixpoint '(0 1) (lambda (n) (list (- 1 n)))"
               " (lambda (n facts) (max (table-ref facts n) (table-ref facts (- 1 n)))) (lambda (n) n)))"
               "(table-values cycle)") == "(1 1)");
    CHECK(fails("(worklist-fixpoint '(0) (lambda (_) '(0)) (lambda (n f) (+ 1 (table-ref f n))) (lambda (_) 0) 5)", "step budget"));
    CHECK(fails("(worklist-fixpoint '(0) (lambda (_) '(1)) (lambda (_ f) 1) (lambda (_) 0))", "Unknown worklist"));
    CHECK(eval("(define sets (worklist-fixpoint '(0) (lambda (_) '(0))"
               " (lambda (_ f) (list->set '(1 2))) (lambda (_) (make-set)) 5 set=?)) (set->list (table-ref sets 0))") == "(1 2)");
}

TEST_CASE_FIXTURE(CollectionFixture, "rope_concat_is_lazy_shared_and_stack_safe")
{
    Value rope = value("(str \"hello\" \" \" \"world\")");
    REQUIRE(rope.kind == ValueKind::String);
    CHECK_FALSE(rope.rope->flat);
    CHECK(rope.stringSize() == 11);
    CHECK_FALSE(rope.rope->flat);
    CHECK(rope.stringValue() == "hello world");
    CHECK(rope.rope->flat);
    CHECK(eval("(define big (let loop ([i 20000] [s \"\"]) (if (= i 0) s (loop (- i 1) (str s \"x\")))))"
               "(test-gc) (list (string-length big) (substring big 19995) (string-search big \"xx\" 19998))") == "(20000 \"xxxxx\" 19998)");
    CHECK(eval("(string-join '(\"a\" \"b\" \"c\") \"/\")") == "\"a/b/c\"");
    CHECK(eval("(str :tag 42 #f)") == "\":tag42#f\"");
    CHECK(eval("(list (string-upcase (str \"a\" \"b\")) (string-suffix? (str \"a\" \"b\") \"b\"))") == "(\"AB\" #t)");
}
TEST_SUITE_END();
