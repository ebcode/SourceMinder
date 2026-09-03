# Every word in ruby/config/keywords.txt used in a naming position.
#
# Ruby accepts all of them as method names, hash keys and symbols, and accepts
# nearly all as capitalized constants and instance variables. Only four are
# legal as bare local variables.
#
# Ruby already exempts naming positions from the keyword list (index_name), so
# this file is regression cover: it should stay fully indexed.
#
# All identifiers are two characters or longer, so nothing here is dropped on
# minimum length rather than on keyword-ness.

# Keywords legal as bare local variables.
__dir__ = 1
__encoding__ = 1
__file__ = 1
__line__ = 1

# Capitalized variants as constants.
__dir__ = 1
__encoding__ = 2
__file__ = 3
__line__ = 4
Begin = 5
End = 6
Alias = 7
And = 8
Begin = 9
Break = 10
Case = 11
Class = 12
Def = 13
Do = 14
Else = 15
Elsif = 16
End = 17
Ensure = 18
False = 19
For = 20
If = 21
In = 22
Module = 23
Next = 24
Nil = 25
Not = 26
Or = 27
Redo = 28
Rescue = 29
Retry = 30
Return = 31
Self = 32
Super = 33
Then = 34
True = 35
Undef = 36
Unless = 37
Until = 38
When = 39
While = 40
Yield = 41

# Keywords as symbols.
SYMBOLS = [
  :__dir__,
  :__encoding__,
  :__file__,
  :__line__,
  :BEGIN,
  :END,
  :alias,
  :and,
  :begin,
  :break,
  :case,
  :class,
  :def,
  :defined?,
  :do,
  :else,
  :elsif,
  :end,
  :ensure,
  :false,
  :for,
  :if,
  :in,
  :module,
  :next,
  :nil,
  :not,
  :or,
  :redo,
  :rescue,
  :retry,
  :return,
  :self,
  :super,
  :then,
  :true,
  :undef,
  :unless,
  :until,
  :when,
  :while,
  :yield,
].freeze

# Keywords as hash keys.
SETTINGS = {
  __dir__: 1,
  __encoding__: 1,
  __file__: 1,
  __line__: 1,
  BEGIN: 1,
  END: 1,
  alias: 1,
  and: 1,
  begin: 1,
  break: 1,
  case: 1,
  class: 1,
  def: 1,
  defined?: 1,
  do: 1,
  else: 1,
  elsif: 1,
  end: 1,
  ensure: 1,
  false: 1,
  for: 1,
  if: 1,
  in: 1,
  module: 1,
  next: 1,
  nil: 1,
  not: 1,
  or: 1,
  redo: 1,
  rescue: 1,
  retry: 1,
  return: 1,
  self: 1,
  super: 1,
  then: 1,
  true: 1,
  undef: 1,
  unless: 1,
  until: 1,
  when: 1,
  while: 1,
  yield: 1,
}.freeze

# Keywords as method names and instance variables.
class KeywordHolder
  def initialize
    @__dir__ = nil
    @__encoding__ = nil
    @__file__ = nil
    @__line__ = nil
    @BEGIN = nil
    @END = nil
    @alias = nil
    @and = nil
    @begin = nil
    @break = nil
    @case = nil
    @class = nil
    @def = nil
    @do = nil
    @else = nil
    @elsif = nil
    @end = nil
    @ensure = nil
    @false = nil
    @for = nil
    @if = nil
    @in = nil
    @module = nil
    @next = nil
    @nil = nil
    @not = nil
    @or = nil
    @redo = nil
    @rescue = nil
    @retry = nil
    @return = nil
    @self = nil
    @super = nil
    @then = nil
    @true = nil
    @undef = nil
    @unless = nil
    @until = nil
    @when = nil
    @while = nil
    @yield = nil
  end

  def __dir__
    1
  end

  def __encoding__
    1
  end

  def __file__
    1
  end

  def __line__
    1
  end

  def BEGIN
    1
  end

  def END
    1
  end

  def alias
    1
  end

  def and
    1
  end

  def begin
    1
  end

  def break
    1
  end

  def case
    1
  end

  def class
    1
  end

  def def
    1
  end

  def defined?
    1
  end

  def do
    1
  end

  def else
    1
  end

  def elsif
    1
  end

  def end
    1
  end

  def ensure
    1
  end

  def false
    1
  end

  def for
    1
  end

  def if
    1
  end

  def in
    1
  end

  def module
    1
  end

  def next
    1
  end

  def nil
    1
  end

  def not
    1
  end

  def or
    1
  end

  def redo
    1
  end

  def rescue
    1
  end

  def retry
    1
  end

  def return
    1
  end

  def self
    1
  end

  def super
    1
  end

  def then
    1
  end

  def true
    1
  end

  def undef
    1
  end

  def unless
    1
  end

  def until
    1
  end

  def when
    1
  end

  def while
    1
  end

  def yield
    1
  end

end

