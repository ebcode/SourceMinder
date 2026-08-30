# Method definition variety: default args, keyword args, splat, block args.
def simple
  42
end

def with_args(lhs, rhs)
  lhs + rhs
end

def with_defaults(base, step = 10, span = 20)
  base + step + span
end

def with_keywords(name:, greeting: "hi")
  "#{greeting}, #{name}"
end

def with_splat(first, *rest)
  [first, rest]
end

def with_double_splat(**opts)
  opts
end

def with_block(&blk)
  blk.call
end

def yields_values
  yield 1
  yield 2
end

# Endless method definition (Ruby 3.0+).
def square(num) = num * num

# Operator methods.
class Vector
  def +(other)
    Vector.new
  end

  def [](index)
    index
  end
end
