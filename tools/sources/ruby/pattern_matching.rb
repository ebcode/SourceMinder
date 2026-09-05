# Ruby 3 pattern matching: case/in binds new local variables implicitly.
# The bound names (status, rest, lat, lng...) appear only on the `in` side.

def route(request)
  case request
  in { method: "GET", path: ["users", id] }
    "show user #{id}"
  in { method: "POST", path: ["users"], body: { name: String => name } }
    "create user #{name}"
  in { method: "DELETE", path: ["users", id], **rest }
    "delete #{id} (opts: #{rest})"
  else
    "not found"
  end
end

# Array patterns with splats and a find pattern (*, x, *).
def analyze(list)
  case list
  in []
    "empty"
  in [single]
    "one: #{single}"
  in [first, *middle, last]
    "ends #{first}..#{last}, #{middle.size} between"
  in [*, Integer => pivot, *] if pivot > 100
    "has a big number: #{pivot}"
  end
end

# deconstruct / deconstruct_keys make any object matchable.
class Point
  attr_reader :x, :y

  def initialize(x, y)
    @x = x
    @y = y
  end

  def deconstruct = [x, y]
  def deconstruct_keys(keys) = { x: x, y: y }
end

def quadrant(point)
  case point
  in Point[0, 0]
    :origin
  in Point[x, y] if x > 0 && y > 0
    :first
  in { x:, y: } if x < 0 && y < 0
    :third
  in Point => p
    "somewhere: #{p.x},#{p.y}"
  end
end

# One-line pattern match with => (rightward assignment / destructuring).
def unpack(config)
  config => { host:, port: Integer => port }
  "#{host}:#{port}"
end

# The boolean `in` form: match test that returns true/false.
def valid_pair?(data)
  data in [Symbol, Numeric]
end

# Alternative patterns with |, and pinning with ^ to match a known value.
def classify_token(token, expected)
  case token
  in :plus | :minus | :times
    :operator
  in ^expected
    :matched_expected
  in Integer | Float
    :number
  in String if token.empty?
    :blank
  end
end

# --- Exercise: feed inputs through each matcher. ---
routes = [
  route({ method: "GET", path: ["users", "42"] }),
  route({ method: "POST", path: ["users"], body: { name: "Ada" } }),
  route({ method: "PATCH", path: ["users"] })
]
shapes  = [analyze([]), analyze([1]), analyze([1, 2, 3, 4]), analyze([1, 200, 3])]
region  = quadrant(Point.new(3, 4))
addr    = unpack({ host: "localhost", port: 8080 })
pair_ok = valid_pair?([:age, 30])
tokens  = [classify_token(:plus, :eq), classify_token(42, :eq),
           classify_token("", :eq)]
p [routes, shapes, region, addr, pair_ok, tokens]
