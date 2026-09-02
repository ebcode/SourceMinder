# Constants, instance/class variables, and globals.
PI = 3.14159
MAX_RETRIES = 3
COLORS = %w[red green blue].freeze

$global_config = { debug: false }   # global variable

module Config
  VERSION = "1.0.0"
  DEFAULTS = {
    timeout: 30,
    retries: MAX_RETRIES
  }.freeze
end

class Circle
  TAU = PI * 2
  BIGGEST = Float::INFINITY

  def initialize(radius)
    @radius = radius
    @area = nil
    @@instances ||= 0
    @@instances += 1
  end

  def area
    @area ||= PI * @radius * @radius
  end

  def circumference
    Math.sqrt(@area) * TAU
  end
end
