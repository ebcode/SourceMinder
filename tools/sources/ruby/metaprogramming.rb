# Metaprogramming: define_method, method_missing, Struct, and DSL-style calls.
class DynamicBuilder
  [:name, :email, :phone].each do |field|
    define_method(field) do
      instance_variable_get("@#{field}")
    end

    define_method("#{field}=") do |value|
      instance_variable_set("@#{field}", value)
    end
  end

  def method_missing(method_name, *args, &block)
    super unless method_name.to_s.start_with?("find_by_")
    "searching"
  end

  def respond_to_missing?(method_name, include_private = false)
    method_name.to_s.start_with?("find_by_") || super
  end
end

# Struct-based value object
Point = Struct.new(:xpos, :ypos) do
  def distance
    Math.sqrt(xpos**2 + ypos**2)
  end
end

# Data.define (Ruby 3.2+)
Coord = Data.define(:lat, :lng)

# Class with mixed visibility
class Service
  def call
    perform
  end

  private

  def perform
    "working"
  end

  protected

  def helper
    "assist"
  end
end
