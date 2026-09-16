; ============================================================================
; ArcppJIT Comprehensive Test Suite
; ============================================================================

(= tests-passed 0)
(= tests-failed 0)

(def assert-equal (name actual expected)
  (if (iso actual expected)
    (do
      (++ tests-passed)
      (prn "  [PASS] " name))
    (do
      (++ tests-failed)
      (prn "  [FAIL] " name ": expected " expected ", got " actual))))

(prn "Running ArcppJIT test suite...")
(prn "------------------------------------------------------------")

; 1. Arithmetic & Inlined Operations
(assert-equal "addition 2-arg" (+ 10 20) 30)
(assert-equal "addition multi-arg" (+ 1 2 3 4 5) 15)
(assert-equal "addition 1-arg" (+ 42) 42)
(assert-equal "subtraction binary" (- 100 35) 65)
(assert-equal "subtraction unary" (- 42) -42)
(assert-equal "multiplication 2-arg" (* 6 7) 42)
(assert-equal "multiplication multi-arg" (* 2 3 4 5) 120)
(assert-equal "division" (/ 100 4) 25)
(assert-equal "modulo" (mod 29 5) 4)

; 2. Comparisons & Relational Predicates
(assert-equal "less than true" (< 3 5) t)
(assert-equal "less than false" (< 5 3) nil)
(assert-equal "greater than true" (> 10 2) t)
(assert-equal "greater than false" (> 2 10) nil)
(assert-equal "less-or-equal" (<= 5 5) t)
(assert-equal "greater-or-equal" (>= 5 5) t)
(assert-equal "identity is" (is 'foo 'foo) t)
(assert-equal "identity isnt true" (isnt 1 2) t)
(assert-equal "identity isnt false" (isnt 'a 'a) nil)
(assert-equal "boolean not on nil" (no nil) t)
(assert-equal "boolean not on t" (no t) nil)

; 3. Lists & Cons Operations
(assert-equal "cons" (cons 1 (cons 2 nil)) '(1 2))
(assert-equal "car" (car '(10 20 30)) 10)
(assert-equal "cdr" (cdr '(10 20 30)) '(20 30))
(assert-equal "caar" (caar '((1 2) 3)) 1)
(assert-equal "cadr" (cadr '(1 2 3)) 2)
(assert-equal "cddr" (cddr '(1 2 3)) '(3))
(assert-equal "list" (list 1 2 3) '(1 2 3))
(assert-equal "rev" (rev '(1 2 3 4)) '(4 3 2 1))

; 4. Conditionals & Flow Control
(assert-equal "if true branch" (if t 'yes 'no) 'yes)
(assert-equal "if false branch" (if nil 'yes 'no) 'no)
(assert-equal "when" (when t 99) 99)
(assert-equal "unless" (unless nil 88) 88)

; 5. Closures, Recursion & Higher-Order Functions
(def add1 (x) (+ x 1))
(assert-equal "simple function" (add1 41) 42)

(def fib (n)
  (if (< n 2)
    n
    (+ (fib (- n 1)) (fib (- n 2)))))
(assert-equal "recursive fib(10)" (fib 10) 55)

(def tail-sum (n acc)
  (if (<= n 0)
    acc
    (tail-sum (- n 1) (+ acc n))))
(assert-equal "tail recursive sum" (tail-sum 100 0) 5050)

; 6. Continuations (call/cc)
(def test-ccc ()
  (ccc (fn (k)
         (k 123)
         456)))
(assert-equal "call/cc early exit" (test-ccc) 123)

; 7. High-Resolution Timing Primitives
(= t-start (msec))
(assert-equal "msec returns positive number" (> t-start 0) t)
(= val (time (fib 15)))
(assert-equal "time macro preserves value" val 610)

; 8. Summary
(prn "------------------------------------------------------------")
(prn "Tests complete: " tests-passed " passed, " tests-failed " failed.")

(if (> tests-failed 0)
  (quit 1)
  (prn "All tests passed successfully!"))

