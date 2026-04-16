use strict;
use warnings;

# Basic case conversion
sub normalize_name {
    my ($name) = @_;
    return ucfirst(lc($name));
}

# Title case
sub title_case {
    my ($str) = @_;
    $str =~ s/(\w+)/ucfirst(lc($1))/ge;
    return $str;
}

# Index and rindex
sub find_extension {
    my ($filename) = @_;
    my $dot = rindex($filename, '.');
    return $dot >= 0 ? substr($filename, $dot + 1) : '';
}

# String repetition and padding
sub pad_left {
    my ($str, $width, $char) = @_;
    $char //= ' ';
    my $padding = $char x ($width - length($str));
    return $padding . $str;
}

# Heredoc forms
sub build_report {
    my ($title, $body) = @_;
    my $header = <<END;
Report: $title
END
    my $footer = <<'END';
-- end of report --
END
    return $header . $body . $footer;
}

# tr/// transliteration
sub count_vowels {
    my ($str) = @_;
    (my $copy = $str) =~ tr/aeiouAEIOU//;
    return $copy;
}

sub rot13 {
    my ($str) = @_;
    $str =~ tr/A-Za-z/N-ZA-Mn-za-m/;
    return $str;
}

# pos() and \G
sub extract_words {
    my ($text) = @_;
    my @words;
    while ($text =~ /\G\s*(\w+)/gc) {
        push @words, $1;
    }
    return @words;
}

# quotemeta / \Q...\E
sub literal_search {
    my ($text, $pattern) = @_;
    my $quoted = quotemeta($pattern);
    return $text =~ /$quoted/;
}

# String multiplication for formatting
sub make_ruler {
    my ($width) = @_;
    return '-' x $width;
}

# index for finding substrings
sub count_occurrences {
    my ($haystack, $needle) = @_;
    my $count = 0;
    my $pos = 0;
    while (($pos = index($haystack, $needle, $pos)) != -1) {
        $count++;
        $pos += length($needle);
    }
    return $count;
}

# sprintf for formatting (keyword, but variables around it are interesting)
sub format_currency {
    my ($amount, $symbol) = @_;
    $symbol //= '$';
    return sprintf('%s%.2f', $symbol, $amount);
}

# lc / uc for normalization
sub canonicalize_key {
    my ($key) = @_;
    $key =~ s/\s+/_/g;
    return lc($key);
}

1;
