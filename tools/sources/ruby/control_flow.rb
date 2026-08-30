# Control flow, exceptions, and case expressions.
def classify(value)
  if value < 0
    "negative"
  elsif value.zero?
    "zero"
  else
    "positive"
  end
end

def describe(shape)
  case shape
  when :circle then "round"
  when :square, :rectangle then "boxy"
  else "unknown"
  end
end

def fetch_data(source)
  attempts = 0
  begin
    attempts += 1
    raise IOError, "no source" if source.nil?
    source.read
  rescue IOError => error
    retry if attempts < 3
    raise
  rescue StandardError => error
    "failed: #{error.message}"
  ensure
    puts "done"
  end
end

def loop_examples(items)
  items.each { |item| next if item.nil? }
  count = 0
  while count < 10
    count += 1
    break if count > 5
  end
  count.times { |index| puts index }
  until count.zero?
    count -= 1
  end
end

# Guard clause with unless
def process(record)
  return unless record.valid?
  record.save
end
