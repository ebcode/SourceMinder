# String literals, interpolation, symbols, and heredocs.
name = "world"
greeting = "hello, #{name}"
single = 'no interpolation here'
template = "sum is #{1 + 2}"

symbol_key = :status
hash_literal = { name: "Ada", role: :admin }

# Percent-literal strings
words = %w[apple banana cherry]
symbols = %i[one two three]

# Heredoc (squiggly, indented)
sql_query = <<~SQL
  SELECT id, name
  FROM users
  WHERE active = true
SQL

# Regular heredoc
message = <<-TEXT
  Multi-line
  message body
TEXT

# Regexp literal
pattern = /\A[a-z]+\z/
