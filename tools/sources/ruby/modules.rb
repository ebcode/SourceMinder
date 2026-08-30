# Modules used as namespaces and as mixins.
module Greetable
  DEFAULT = "hello"

  def greet
    "#{DEFAULT}, #{name}"
  end
end

module Walkable
  def walk
    "walking"
  end
end

# Namespaced module with nested class and module_function.
module Zoo
  class Enclosure
    def initialize(species)
      @species = species
    end
  end

  module Utils
    module_function

    def sanitize(str)
      str.strip.downcase
    end
  end
end

# Class that mixes in modules.
class Person
  include Greetable
  include Walkable
  extend Greetable

  attr_reader :name

  def initialize(name)
    @name = name
  end
end
