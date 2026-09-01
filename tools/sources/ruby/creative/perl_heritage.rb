# Perl heritage: special globals, regex captures, flip-flop, and DATA.
# Ruby inherited Perl's "line-noise" globals and implicit-variable style.

# BEGIN/END blocks run at load and exit time (straight from Perl/awk).
BEGIN { $processed = 0 }
END   { warn "processed #{$processed} lines" }

# Output/record separators, just like Perl's $\ and $/.
$\ = "\n"
$, = ", "
$; = "\t"

LOG_LINE = /
  \A
  (?<ts>\d{4}-\d{2}-\d{2})   # date
  \s+
  (?<level>INFO|WARN|ERROR)  # severity
  \s+
  (?<msg>.*)                 # message body
  \z
/x

# Parse using named captures that leak into local variables via =~.
def parse(line)
  if line =~ LOG_LINE
    # $~ is the MatchData; $1/$2 and named locals both work.
    level = $~[:level]
    stamp = $1
    { at: stamp, level: level, text: $~[:msg] }
  else
    { raw: $_ }   # $_ is the last line read by gets
  end
end

# gsub with a block sees $~, $1... for each match — very Perlish.
def redact(text)
  text.gsub(/(?<user>\w+)@(?<host>[\w.]+)/) do
    "#{$~[:user]}@[redacted]"
  end
end

# Flip-flop operator: true between a start line and an end line.
def extract_section(lines)
  lines.each do |line|
    $processed += 1
    puts line if (line =~ /^BEGIN/)..(line =~ /^END/)
  end
end

# The classic Perl one-liner style: chained implicit operations.
def word_freq
  freq = Hash.new(0)
  DATA.each_line do |line|
    line.scan(/[a-z]+/i) { |w| freq[w.downcase] += 1 }
  end
  freq.max_by(3) { |_word, n| n }
end

# Backtick subshell and %x — Ruby kept Perl's shell-out syntax.
def host_uptime
  `uptime`.strip
end

# --- Exercise: drive every definition so the parser sees real usages. ---
records = ["2026-08-31 INFO service started", "garbled line"].map { |line| parse(line) }
cleaned = redact("ping ada@example.com and grace@dev.local")
extract_section(["intro", "BEGIN", "captured", "END", "outro"])
top_words = word_freq
uptime_line = host_uptime
puts [records, cleaned, top_words, uptime_line].inspect

__END__
the quick brown fox
the lazy dog sleeps
the fox and the dog
