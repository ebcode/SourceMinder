#!/usr/bin/perl
use 5.020;
use strict;
use warnings;
use feature qw(say state unicode_strings);

# say instead of print
sub greet_modern {
    my ($name) = @_;
    say "Hello, $name!";
}

# state variables (like static locals in C)
sub call_count {
    state $count = 0;
    return ++$count;
}

sub fibonacci {
    my ($n) = @_;
    state %cache;
    return $n if $n <= 1;
    return $cache{$n} //= fibonacci($n-1) + fibonacci($n-2);
}

# Given/when (switch-like)
use feature 'switch';
no warnings 'experimental';

sub classify_number {
    my ($n) = @_;
    my $result;
    if    ($n < 0)  { $result = 'negative' }
    elsif ($n == 0) { $result = 'zero' }
    elsif ($n < 10) { $result = 'small' }
    else            { $result = 'large' }
    return $result;
}

# Postfix conditionals and loops
sub filter_positives {
    my (@nums) = @_;
    my @pos = grep { $_ > 0 } @nums;
    return @pos;
}

# String operations
sub normalize_whitespace {
    my ($str) = @_;
    $str =~ s/\s+/ /g;
    $str =~ s/^\s+|\s+$//g;
    return $str;
}

# Error handling with eval/die
sub safe_divide {
    my ($a, $b) = @_;
    my $result = eval { $a / $b };
    if ($@) {
        warn "Division failed: $@";
        return undef;
    }
    return $result;
}
