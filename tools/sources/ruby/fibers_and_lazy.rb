# Fibers, Enumerators, and lazy infinite streams: coroutines in Ruby.

# An infinite lazy stream of primes via trial division.
primes = Enumerator.new do |y|
  candidate = 2
  seen = []
  loop do
    y << candidate unless seen.any? { |p| (candidate % p).zero? }
    seen << candidate if seen.none? { |p| (candidate % p).zero? }
    candidate += 1
  end
end.lazy

first_ten_primes = primes.first(10)

# Lazy pipelines only compute what is pulled through them.
powers_of_two = (1..Float::INFINITY).lazy
  .map { |n| 2**n }
  .select { |n| n.to_s.include?("7") }
  .first(5)

# A Fiber used as an explicit coroutine that yields control back.
counter = Fiber.new do
  total = 0
  loop do
    increment = Fiber.yield(total)
    total += (increment || 1)
  end
end

# Producer/consumer: two fibers passing values back and forth.
def ping_pong(rounds)
  ping = Fiber.new do
    rounds.times do |i|
      Fiber.yield("ping #{i}")
    end
    "done"
  end

  results = []
  loop do
    value = ping.resume
    break unless ping.alive?
    results << value
  end
  results
end

# External iterator: pull items one at a time with .next / .peek.
def merge_sorted(left, right)
  a = left.each
  b = right.each
  merged = []

  loop do
    begin
      if a.peek <= b.peek
        merged << a.next
      else
        merged << b.next
      end
    rescue StopIteration
      break
    end
  end

  merged + a.to_a + b.to_a
end

# Custom class that is Enumerable via a yielding each.
class Tree
  include Enumerable

  def initialize(value, children = [])
    @value = value
    @children = children
  end

  def each(&block)
    yield @value
    @children.each { |child| child.each(&block) }
  end
end

# Lazy generator that memoizes as it goes (a Lisp-style stream).
def naturals
  Enumerator.new do |y|
    n = 0
    loop { y.yield(n += 1) }
  end
end

# --- Exercise: drive the coroutines and lazy definitions. ---
tick     = counter.resume       # start the fiber
tock     = counter.resume(5)    # feed a value back in
volley   = ping_pong(3)
merged   = merge_sorted([1, 4, 6], [2, 3, 5])
tree     = Tree.new(1, [Tree.new(2), Tree.new(3, [Tree.new(4)])])
preorder = tree.to_a            # Enumerable#to_a via each
counts   = naturals.first(5)
p [first_ten_primes, powers_of_two, tick, tock, volley, merged, preorder, counts]
