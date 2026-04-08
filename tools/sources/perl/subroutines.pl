#!/usr/bin/perl
use strict;
use warnings;

# Simple named subroutine
sub greet {
    my ($name) = @_;
    return "Hello, $name!";
}

# Subroutine with multiple parameters
sub add_numbers {
    my ($a, $b) = @_;
    return $a + $b;
}

# Subroutine with no args
sub get_version {
    return "1.0.0";
}

# Subroutine with local variables
sub process_data {
    my ($input) = @_;
    my $result = 0;
    my $count  = 0;

    foreach my $item (@$input) {
        $result += $item;
        $count++;
    }

    return $count > 0 ? $result / $count : 0;
}

# Nested subroutine call
sub format_output {
    my ($value) = @_;
    my $formatted = sprintf("%.2f", $value);
    return $formatted;
}

# Subroutine returning multiple values
sub divmod {
    my ($a, $b) = @_;
    return (int($a / $b), $a % $b);
}
