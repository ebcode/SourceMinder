# Lisp/lambda-calculus heritage: everything is a function.
# Ruby's blocks, procs, and lambdas make combinator programming natural.

# The Y-combinator: recursion without naming yourself.
Y = ->(f) { ->(x) { x.(x) }.(->(x) { f.(->(*a) { x.(x).(*a) }) }) }

factorial = Y.(->(fact) {
  ->(n) { n.zero? ? 1 : n * fact.(n - 1) }
})

# Church numerals: numbers encoded as higher-order functions.
ZERO = ->(_f) { ->(x) { x } }
SUCC = ->(n) { ->(f) { ->(x) { f.(n.(f).(x)) } } }
ONE  = SUCC.(ZERO)
TWO  = SUCC.(ONE)

def church_to_i(numeral)
  numeral.(->(n) { n + 1 }).(0)
end

# Currying: turn a binary function into a chain of unary ones.
add = ->(a, b) { a + b }.curry
increment = add.(1)
add_ten    = add.(10)

# Proc composition with >> (forward) and << (reverse) — point-free style.
sanitize   = ->(s) { s.strip }
downcase   = :downcase.to_proc          # symbol-to-proc: a Lisp-y trick
normalize  = sanitize >> downcase >> ->(s) { s.gsub(/\s+/, "-") }

# method() reifies a named method into a first-class Method object.
def shout(text)
  text.upcase + "!"
end

announce = method(:shout).to_proc

# Cons cells and a linked list built from closures (pure Lisp).
def cons(head, tail)
  ->(selector) { selector.(head, tail) }
end

def car(pair) = pair.(->(h, _t) { h })
def cdr(pair) = pair.(->(_h, t) { t })

def from_array(items)
  items.reverse.reduce(nil) { |acc, item| cons(item, acc) }
end

# fold/reduce with a symbol — the functional bread and butter.
def sum_all(*numbers)
  numbers.reduce(0, :+)
end

# Lazy, self-referential Fibonacci via an Enumerator.
fibs = Enumerator.new do |yielder|
  a, b = 0, 1
  loop do
    yielder << a
    a, b = b, a + b
  end
end

# --- Exercise: force every definition to actually run. ---
fact5      = factorial.(5)
two_as_int = church_to_i(TWO)
stepped    = [increment.(0), add_ten.(5)]
slug       = normalize.("  Hello World  ")
shouted    = announce.("ada")
list       = from_array([10, 20, 30])
first_two  = [car(list), car(cdr(list))]
total      = sum_all(1, 2, 3, 4)
fib_five   = fibs.first(5)
p [fact5, two_as_int, stepped, slug, shouted, first_two, total, fib_five]
