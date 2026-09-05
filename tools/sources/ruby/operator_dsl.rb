# Operator overloading: a Money value type that behaves like a number.
# Almost every method here is named with punctuation, not letters.

class Money
  include Comparable

  attr_reader :cents, :currency

  def initialize(cents, currency = :usd)
    @cents = cents
    @currency = currency
  end

  # Arithmetic operators.
  def +(other)  = Money.new(@cents + other.cents, @currency)
  def -(other)  = Money.new(@cents - other.cents, @currency)
  def *(factor) = Money.new((@cents * factor).round, @currency)
  def /(divisor) = Money.new((@cents / divisor).round, @currency)

  # Unary minus and plus.
  def -@ = Money.new(-@cents, @currency)
  def +@ = self

  # Comparable drives <, <=, ==, >=, >, between? from this one method.
  def <=>(other)
    return nil unless currency == other.currency
    cents <=> other.cents
  end

  # Case-equality so Money works as a `when` / `case/in` guard.
  def ===(other)
    other.is_a?(Money) && other.currency == currency
  end

  # Index read and write on a virtual field.
  def [](field) = to_h[field]

  def []=(field, value)
    to_h[field] = value
  end

  # coerce lets `5 * money` work, not just `money * 5`.
  def coerce(numeric)
    [Money.new(numeric * 100, @currency), self]
  end

  # Bitwise-shift repurposed as an append DSL (like << on arrays/strings).
  def <<(other)
    self + other
  end

  # to_proc so `[a, b].map(&Money.method(:from_dollars))` reads cleanly.
  def to_proc
    ->(n) { self * n }
  end

  def to_h = { cents: @cents, currency: @currency }

  def self.from_dollars(dollars) = new((dollars * 100).round)
end

# A tiny query DSL built entirely from operator methods.
class Predicate
  def initialize(&test) = @test = test

  def &(other) = Predicate.new { |x| @test.(x) && other.match?(x) }
  def |(other) = Predicate.new { |x| @test.(x) || other.match?(x) }
  def !@       = Predicate.new { |x| !@test.(x) }
  def match?(x) = @test.(x)
end

positive = Predicate.new { |n| n > 0 }
even     = Predicate.new { |n| n.even? }
wanted   = positive & even

# --- Exercise: put the operators through their paces. ---
price    = Money.from_dollars(20)
tax      = Money.new(150)
total    = price + tax
diff     = tax - price
tripled  = price * 3
halved   = price / 2
negated  = -price
same     = +price
cheaper  = tax < price          # Comparable, via <=>
combined = price << tax         # << append DSL
cents    = total[:cents]        # []
total[:cents] = 999             # []=
coerced  = price.coerce(5)      # coerce
doubled  = price.to_proc.(2)    # to_proc
either   = (positive | even).match?(-2)   # | then match?
neither  = (!positive).match?(-5)         # !@ (unary not) then match?
matches  = wanted.match?(4)               # & chain built above
p [total.cents, diff.cents, tripled.cents, halved.cents, negated.cents,
   same.cents, cheaper, combined.cents, cents, coerced, doubled.cents,
   either, neither, matches]
