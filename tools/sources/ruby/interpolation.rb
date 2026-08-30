# String interpolation: single, multiple, and inside declarative calls.
first = "Ada"
last = "Lovelace"
count = 3

# Single interpolation
greeting = "Hello #{first}"

# Multiple interpolations in one string
full_name = "#{first} #{last}"
summary = "#{first} #{last} has #{count} messages"

# Interpolation wrapping a method call and an expression
loud = "NAME: #{first.upcase}"
math = "total = #{count * 2}"

# Adjacent interpolations with no literal between them
joined = "#{first}#{last}"

# Interpolation inside a heredoc
report = <<~TEXT
  User #{first} #{last}
  Messages: #{count}
TEXT

# Dynamic method definition via interpolated names (single + setter form)
class Record
  [:title, :author].each do |field|
    define_method("#{field}") do
      instance_variable_get("@#{field}")
    end

    define_method("#{field}=") do |value|
      instance_variable_set("@#{field}", value)
    end
  end
end
