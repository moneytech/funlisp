(assert (equal?  ((lambda () 1)) 1))
(assert (equal?  ((lambda (x) (+ 1 x)) 1) 2))
(assert-error 'LE_2FEW (lambda))
(assert-error 'LE_TYPE (lambda (x 2) 1))

(define when
  (macro (condition expr-if-true)
         `(if ,condition ,expr-if-true '())))

; OUTPUT(0)
