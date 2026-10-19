// This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
#include "Core/Evaluator.h"

namespace Luwu::Dsl
{

// Contains the standard library prelude written in Luna, evaluated into every evaluator's global environment.
const char* lunaPrelude()
{
    return R"luna(
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
)luna";
}

} // namespace Luwu::Dsl
