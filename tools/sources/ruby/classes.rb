# Basic class definitions, inheritance, and methods.
require "set"
require_relative "modules"

# A plain class with an initializer and instance methods.
class Animal
  LEGS = 4                      # class-level constant
  @@count = 0                   # class variable

  attr_reader :name
  attr_accessor :age
  attr_writer :owner

  def initialize(name, age = 0)
    @name = name                # instance variable
    @age = age
    @@count += 1
  end

  def speak
    "..."
  end

  # Class method via self.
  def self.count
    @@count
  end

  # Predicate and bang method names.
  def adult?
    @age >= 3
  end

  def rename!(new_name)
    @name = new_name
  end
end

# Inheritance with super.
class Dog < Animal
  def initialize(name, breed)
    super(name)
    @breed = breed
  end

  def speak
    "Woof"
  end
end

# Qualified superclass (namespaced base class).
class Puppy < Animals::Base
  def speak
    "Yip"
  end
end

# Singleton method on a specific object.
rex = Dog.new("Rex", "Lab")
def rex.trick
  "rolls over"
end
