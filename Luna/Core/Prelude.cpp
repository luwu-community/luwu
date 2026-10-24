// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Evaluator.h"

namespace Luwu::Dsl
{

// Contains the standard library prelude written in Luna, evaluated into every evaluator's global environment.
const char* lunaPrelude()
{
    return R"luna(
(define-syntax define-macro
  (lambda (stx)
    (syntax-case stx ()
      [(_ (name . params) body ...)
       (identifier? name)
       (if (macro-at-top-level?)
           #'(define-syntax name (%make-datum-transformer 'name (lambda params body ...)))
           (syntax-error stx "define-macro is only allowed at the top level (where the expander can see it before its uses)"))]
      [_ (syntax-error stx "define-macro is (define-macro (name params...) body...)")])))

(define (%map-acc f xs acc)
  (if (null? xs)
      (reverse acc)
      (%map-acc f (rest xs) (cons (f (first xs)) acc))))

(define (%map f xs)
  (%map-acc f xs '()))

~ Returns the second element of list xs.
(define (second xs) (first (rest xs)))

(define (%check-binding b)
  (if (if (pair? b) (equal? (first b) 'doc-comment) #f)
      (syntax-error b "A ~ comment on a let binding goes nowhere (it makes no C++); use ; for a Luna comment (forms that make code place theirs in it)")
      (if (if (pair? b) (if (list? b) (if (equal? (length b) 2) (symbol? (first b)) #f) #f) #f)
          #t
          (syntax-error b "A binding is [name value]"))))

(define (%check-bindings bindings who)
  (if (not (list? bindings))
      (syntax-error 0 (str who " takes bindings and a body: (" who " ([x value]...) body...)"))
      (if (null? bindings)
          #t
          (begin
            (%check-binding (first bindings))
            (%check-bindings (rest bindings) who)))))

(define-macro (and . xs)
  (if (null? xs)
      #t
      (if (null? (rest xs))
          (first xs)
          `(if ,(first xs) (and ,@(rest xs)) #f))))

(define-macro (letrec . args)
  (if (null? args)
      (syntax-error 0 "letrec takes bindings and a body: (letrec ([f value]...) body...)")
      ((lambda (bindings body)
         (begin
           (%check-bindings bindings "letrec")
           `((lambda ,(%map first bindings)
               ,@(%map (lambda (b) `(set! ,(first b) ,(second b))) bindings)
               ,@body)
             ,@(%map (lambda (_) (list (string->symbol "#:unassigned"))) bindings))))
       (first args) (rest args))))

(define-macro (let . args)
  (if (null? args)
      (syntax-error 0 "let takes bindings and a body: (let ([x value]...) body...)")
      (if (symbol? (first args))
          (if (null? (rest args))
              (syntax-error 0 "let takes bindings and a body: (let ([x value]...) body...)")
              ((lambda (name bindings body)
                 (begin
                   (%check-bindings bindings "let")
                   `((letrec ([,name (lambda ,(%map first bindings) ,@body)]) ,name) ,@(%map second bindings))))
               (first args) (second args) (rest (rest args))))
          ((lambda (bindings body)
             (begin
               (%check-bindings bindings "let")
               `((lambda ,(%map first bindings) ,@body) ,@(%map second bindings))))
           (first args) (rest args)))))

(define-macro (let* . args)
  (if (null? args)
      (syntax-error 0 "let* takes bindings and a body: (let* ([x value]...) body...)")
      ((lambda (bindings body)
         (begin
           (%check-bindings bindings "let*")
           (if (if (null? bindings) #t (null? (rest bindings)))
               `(let ,bindings ,@body)
               `(let (,(first bindings)) (let* ,(rest bindings) ,@body)))))
       (first args) (rest args))))

(define-macro (or . xs)
  (if (null? xs)
      #f
      (if (null? (rest xs))
          (first xs)
          ((lambda (t)
             `(let ([,t ,(first xs)]) (if ,t ,t (or ,@(rest xs)))))
           (gensym "or")))))

(define-macro (cond . clauses)
  (if (null? clauses)
      'nil
      ((lambda (clause rest-clauses)
         (if (if (pair? clause) (equal? (first clause) 'doc-comment) #f)
             (syntax-error clause "A ~ comment on a cond clause goes nowhere (it makes no C++); use ; for a Luna comment (forms that make code place theirs in it)")
             (if (if (pair? clause) (list? clause) #f)
                 (if (equal? (first clause) 'else)
                     (if (not (null? rest-clauses))
                         (syntax-error clause "else must be the last cond clause")
                         `(begin ,@(rest clause)))
                     (if (null? (rest clause))
                         `(or ,(first clause) (cond ,@rest-clauses))
                         `(if ,(first clause) (begin ,@(rest clause)) (cond ,@rest-clauses))))
                 (syntax-error clause "A cond clause is [test body...]"))))
       (first clauses) (rest clauses))))

~ Evaluates the body expressions when condition c holds, otherwise returns nil.
(define-macro (when c . body) `(if ,c (begin ,@body) nil))
~ Evaluates the body expressions unless condition c holds, otherwise returns nil.
(define-macro (unless c . body) `(if ,c nil (begin ,@body)))

(define (map f xs)
  (let loop ([xs xs] [acc nil])
    (if (null? xs) (reverse acc) (loop (rest xs) (cons (f (first xs)) acc)))))

(define (for-each f xs)
  (if (null? xs) nil (begin (f (first xs)) (for-each f (rest xs)))))

(define (filter keep? xs)
  (let loop ([xs xs] [acc nil])
    (cond [(null? xs) (reverse acc)]
          [(keep? (first xs)) (loop (rest xs) (cons (first xs) acc))]
          [else (loop (rest xs) acc)])))

(define (foldl f init xs)
  (if (null? xs) init (foldl f (f init (first xs)) (rest xs))))

(define (foldr f init xs)
  (foldl (lambda (acc x) (f x acc)) init (reverse xs)))

(define (any? pred xs)
  (cond [(null? xs) #f] [(pred (first xs)) #t] [else (any? pred (rest xs))]))

(define (all? pred xs)
  (cond [(null? xs) #t] [(pred (first xs)) (all? pred (rest xs))] [else #f]))

~ Defines a record type: (define-record name (field ...)).
~ Creates constructor (make-name field ...), predicate (name? x), and accessors (name-field r). Records are compared by identity.
(define-macro (define-record name fields)
  (let ([n (symbol->string name)])
    `(begin
       (define (,(string->symbol (str "make-" n)) ,@fields) (%make-record ',name ',fields ,@fields))
       (define (,(string->symbol (str n "?")) x) (record? x ',name))
       ,@(map (lambda (f) `(define (,(string->symbol (str n "-" (symbol->string f))) r) (record-ref r ',f))) fields))))

~ Sorts list xs in ascending order according to comparator less? using a stable merge sort.
(define (sort xs less?)
  (define (merge a b)
    (cond [(null? a) b]
          [(null? b) a]
          [(less? (first b) (first a)) (cons (first b) (merge a (rest b)))]
          [else (cons (first a) (merge (rest a) b))]))
  (let ([n (length xs)])
    (if (< n 2)
        xs
        (merge (sort (take xs (/ n 2)) less?) (sort (drop xs (/ n 2)) less?)))))

~ Returns list xs without its first n elements.
(define (drop xs n)
  (cond [(= n 0) xs] [(null? xs) xs] [else (drop (rest xs) (- n 1))]))

~ Combines elements of lists xs and ys into a list of pairs up to the length of the shorter list.
(define (zip xs ys)
  (cond [(null? xs) nil] [(null? ys) nil] [else (cons (list (first xs) (first ys)) (zip (rest xs) (rest ys)))]))

~ Returns the first n elements of list xs, or all elements if the list contains fewer.
(define (take xs n)
  (cond [(= n 0) nil] [(null? xs) nil] [else (cons (first xs) (take (rest xs) (- n 1)))]))

(define (range from to)
  (let loop ([i (- to 1)] [acc nil])
    (if (< i from) acc (loop (- i 1) (cons i acc)))))

~ Strips doc-comment forms from AST d, unwrapping documented forms and removing standalone comments.
(define (strip-comments d)
  (define (comment? x) (and (pair? x) (equal? (first x) 'doc-comment)))
  (define (standalone? x) (and (comment? x) (string? (last x))))
  (cond [(and (comment? d) (not (standalone? d))) (strip-comments (last d))]
        [(pair? d) (map strip-comments (filter (lambda (x) (not (standalone? x))) d))]
        [else d]))


~ Returns true if element x is a member of list xs according to equal?, otherwise false.
(define (member? x xs)
  (cond [(null? xs) #f] [(equal? x (first xs)) #t] [else (member? x (rest xs))]))

~ Dispatches on key matching clauses: (case key [(k ...) body ...] ... [else body ...]).
~ Matches keys using equal?. A clause may specify a single key or a parenthesized list of keys.
(define-macro (case key . clauses)
  (let ([k (gensym "key")])
    `(let ([,k ,key])
       (cond ,@(map (lambda (c)
                      (cond [(equal? (first c) 'else) c]
                            [(pair? (first c)) `[(member? ,k ',(first c)) ,@(rest c)]]
                            [else `[(equal? ,k ',(first c)) ,@(rest c)]]))
                    clauses)))))

(define (assoc key alist)
  (cond [(null? alist) #f] [(equal? (first (first alist)) key) (first alist)] [else (assoc key (rest alist))]))

~ Evaluates body with temporary dynamic parameter bindings; bindings are restored on return or error.
(define-macro (parameterize bindings . body)
  (unless (and (list? bindings) (all? (lambda (b) (and (list? b) (= (length b) 2))) bindings))
    (error "parameterize bindings are [parameter-expression value-expression]"))
  `(%call-with-parameters (list ,@(map (lambda (b) `(list ,(first b) ,(second b))) bindings))
                         (lambda () ,@body)))

;; Pattern compilation uses continuations at expansion time. Each projection is
;; evaluated once; clause failure is a thunk, avoiding exponential expansions.
(define (%match-bindings bindings body)
  `(let ,(map (lambda (b) (list (first b) (second b))) (reverse bindings)) ,@body))

(define (%match-components patterns expressions bindings yes no)
  (if (null? patterns) (yes bindings)
      (%match-pattern (first patterns) (first expressions) bindings
        (lambda (more) (%match-components (rest patterns) (rest expressions) more yes no)) no)))

(define (%match-list patterns value bindings yes no)
  (cond [(null? patterns) `(if (null? ,value) ,(yes bindings) ,no)]
        [(not (pair? patterns)) (%match-pattern patterns value bindings yes no)]
        [else `(if (pair? ,value)
                   ,(%match-pattern (first patterns) `(first ,value) bindings
                      (lambda (more) (%match-pattern-list-tail (rest patterns) `(rest ,value) more yes no)) no)
                   ,no)]))
(define (%match-pattern-list-tail patterns expression bindings yes no)
  (let ([tail (gensym "tail")])
    `(let ([,tail ,expression]) ,(%match-list patterns tail bindings yes no))))

(define (%match-fields fields value bindings yes no)
  (if (null? fields) (yes bindings)
      (let ([field (first fields)])
        (unless (and (list? field) (= (length field) 2) (symbol? (first field)))
          (error "A record pattern field is [field pattern]"))
        `(if (record-has? ,value ',(first field))
             ,(%match-pattern (second field) `(record-ref ,value ',(first field)) bindings
                (lambda (more) (%match-fields (rest fields) value more yes no)) no)
             ,no))))

(define (%match-pattern pattern expression bindings yes no)
  (let ([value (gensym "matched")])
    `(let ([,value ,expression])
       ,(cond
          [(equal? pattern '_) (yes bindings)]
          [(equal? pattern 'nil) `(if (null? ,value) ,(yes bindings) ,no)]
          [(and (symbol? pattern) (not (equal? pattern 'nil)))
           (let ([known (assoc pattern bindings)])
             (if known `(if (equal? ,value ,(second known)) ,(yes bindings) ,no)
                 (yes (cons (list pattern value) bindings))))]
          [(not (pair? pattern)) `(if (equal? ,value ',pattern) ,(yes bindings) ,no)]
          [(equal? (first pattern) 'quote)
           (unless (= (length pattern) 2) (error "A quoted match pattern contains one datum"))
           `(if (equal? ,value ',(second pattern)) ,(yes bindings) ,no)]
          [(equal? (first pattern) 'cons)
           (unless (= (length pattern) 3) (error "A cons match pattern has two parts"))
           `(if (pair? ,value)
                ,(%match-components (rest pattern) (list `(first ,value) `(rest ,value)) bindings yes no) ,no)]
          [(equal? (first pattern) 'vector)
           `(if (and (vector? ,value) (= (vector-length ,value) ,(- (length pattern) 1)))
                ,(%match-components (rest pattern)
                    (map (lambda (i) `(vector-ref ,value ,i)) (range 0 (- (length pattern) 1))) bindings yes no) ,no)]
          [(equal? (first pattern) 'record)
           (unless (and (> (length pattern) 1) (symbol? (second pattern)))
             (error "A record pattern is (record type [field pattern]...)"))
           `(if (record? ,value ',(second pattern))
                ,(%match-fields (drop pattern 2) value bindings yes no) ,no)]
          [(equal? (first pattern) '?)
           (unless (or (= (length pattern) 2) (= (length pattern) 3)) (error "A predicate pattern is (? predicate [pattern])"))
           `(if (,(second pattern) ,value)
                ,(if (= (length pattern) 3) (%match-pattern (nth pattern 2) value bindings yes no) (yes bindings)) ,no)]
          [(equal? (first pattern) 'and)
           (%match-components (rest pattern) (map (lambda (_) value) (rest pattern)) bindings yes no)]
          [else (%match-list (if (equal? (first pattern) 'list) (rest pattern) pattern) value bindings yes no)]))))

(define (%match-clauses value clauses)
  (if (null? clauses) `(error "match: no matching clause" ,value)
      (let* ([clause (first clauses)] [next (gensym "next-clause")])
        (unless (and (list? clause) (>= (length clause) 2)) (error "A match clause is [pattern body...]"))
        (if (equal? (first clause) 'else)
            (begin
              (unless (null? (rest clauses)) (error "match: else must be the last clause"))
              `(begin ,@(rest clause)))
            (let* ([forms (rest clause)]
                   [guard (and (pair? (first forms)) (equal? (first (first forms)) 'when))]
                   [body (if guard (rest forms) forms)])
              (when (and guard (not (= (length (first forms)) 2))) (error "A match guard is (when expression)"))
              `(let ([,next (lambda () ,(%match-clauses value (rest clauses)))])
                 ,(%match-pattern (first clause) value nil
                    (lambda (bindings)
                      (%match-bindings bindings
                        (if guard (list `(if ,(second (first forms)) (begin ,@body) (,next))) body)))
                    `(,next))))))))

~ Matches value once against clauses. Supports literals, bindings, _, lists, cons, vector, record, ? and and patterns, plus (when guard).
(define-macro (match expression . clauses)
  (let ([value (gensym "match")]) `(let ([,value ,expression]) ,(%match-clauses value clauses))))

~ Destructures parallel initializers and evaluates body with the matched bindings.
(define-macro (match-let bindings . body)
  (unless (and (list? bindings) (all? (lambda (b) (and (list? b) (= (length b) 2))) bindings))
    (error "match-let bindings are [pattern expression]"))
  `(match (list ,@(map second bindings)) [(list ,@(map first bindings)) ,@body]))

~ Returns the first matching element, or default (#f when omitted).
(define (find predicate xs . default)
  (when (> (length default) 1) (error "find accepts one optional default"))
  (cond [(null? xs) (if (null? default) #f (first default))]
        [(predicate (first xs)) (first xs)]
        [else (apply find predicate (rest xs) default)]))

~ Maps each element, retaining every result except #f; nil is retained.
(define (filter-map procedure xs)
  (let loop ([xs xs] [out nil])
    (if (null? xs) (reverse out)
        (let ([v (procedure (first xs))]) (loop (rest xs) (if (equal? v #f) out (cons v out)))))))

~ Maps elements to proper lists and concatenates the results.
(define (append-map procedure xs)
  (apply append (map procedure xs)))

~ Returns (matching nonmatching), preserving order in both lists.
(define (partition predicate xs)
  (let loop ([xs xs] [yes nil] [no nil])
    (cond [(null? xs) (list (reverse yes) (reverse no))]
          [(predicate (first xs)) (loop (rest xs) (cons (first xs) yes) no)]
          [else (loop (rest xs) yes (cons (first xs) no))])))

~ Keeps the first element for each structural key, in input order.
(define (unique-by key xs)
  (let ([seen (make-set)]) (filter (lambda (x) (set-add! seen (key x))) xs)))

~ Groups elements into an insertion-ordered table of key to list, preserving group order.
(define (group-by key xs)
  (let ([groups (make-table)])
    (for-each (lambda (x) (table-update! groups (key x) (lambda (old) (cons x old)) nil)) xs)
    (for-each (lambda (k) (table-update! groups k reverse)) (table-keys groups))
    groups))

~ Updates several existing record fields in one immutable copy. Field expressions evaluate left to right.
(define-macro (record-update record . fields)
  (unless (all? (lambda (f) (and (list? f) (= (length f) 2) (symbol? (first f)))) fields)
    (error "record-update fields are [field expression]"))
  `(%record-update ,record (list ,@(map (lambda (f) `(list ',(first f) ,(second f))) fields))))

~ Solves dataflow with a deterministic worklist. transfer(node,facts) computes a new fact; dependents(node) gives affected nodes. initial(node) seeds facts.
(define (worklist-fixpoint nodes dependents transfer initial . limit)
  (let ([facts (make-table :identity)] [queued (make-set :identity)]
        [budget (if (null? limit) 100000 (first limit))]
        [same? (if (= (length limit) 2) (second limit) equal?)])
    (when (> (length limit) 2) (error "worklist-fixpoint accepts an optional budget and fact comparator"))
    (unless (and (int? budget) (> budget 0)) (error "worklist-fixpoint requires a positive step budget"))
    (for-each (lambda (n) (table-set! facts n (initial n))) nodes)
    (let solve ([front (filter (lambda (n) (set-add! queued n)) nodes)] [back nil] [steps 0])
      (cond [(null? front) (if (null? back) facts (solve (reverse back) nil steps))]
            [(>= steps budget) (error "worklist-fixpoint exceeded its step budget" budget)]
            [else
             (let* ([node (first front)]
                    [_ (set-delete! queued node)]
                    [value (transfer node facts)])
               (if (same? value (table-ref facts node)) (solve (rest front) back (+ steps 1))
                   (begin
                     (table-set! facts node value)
                     (let ([added (filter (lambda (n)
                                           (unless (table-has? facts n) (error "Unknown worklist dependent" n))
                                           (set-add! queued n)) (dependents node))])
                       (solve (rest front) (append (reverse added) back) (+ steps 1))))))]))))

)luna";
}

} // namespace Luwu::Dsl
