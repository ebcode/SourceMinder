# Single-character names, with and without sigils (N2/N3 probes).
module T
  module Helpers
  end
end

class C
  K = 5
  V = 6

  def f(*args)
    args
  end

  def n
    @x = 1        # 1-char ivar: @x is 2 chars as written
    @@c ||= 0     # 1-char cvar: @@c is 3 chars as written
    $g = 2        # 1-char global: $g is 2 chars as written
    i = 3         # bare 1-char local, expected filtered
    x = T.let(i, Integer)
    y = T::Helpers
    [$g, @x, x, y]
  end

  def d
    $o            # bare global read
  end
end

q = C.new
q.f(K)
