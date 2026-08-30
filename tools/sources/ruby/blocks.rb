# Blocks, procs, and lambdas.
numbers = [1, 2, 3, 4, 5]

# Block with do..end
numbers.each do |num|
  puts num
end

# Block with braces
doubled = numbers.map { |num| num * 2 }

# Block with multiple params and index
numbers.each_with_index do |value, idx|
  puts "#{idx}: #{value}"
end

# Proc and lambda locals
adder = proc { |lhs, rhs| lhs + rhs }
multiplier = lambda { |lhs, rhs| lhs * rhs }
stabby = ->(val) { val * val }

# Method that takes a block and yields
def repeat(times)
  times.times { |idx| yield idx }
end

# Numbered block parameters (Ruby 2.7+)
tripled = numbers.map { _1 * 3 }
