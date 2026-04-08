#!/usr/bin/perl
use strict;
use warnings;

# Anonymous subroutines
my $double   = sub { return $_[0] * 2 };
my $square   = sub { return $_[0] ** 2 };
my $identity = sub { return $_[0] };

# Immediately invoked
my $result = (sub { 42 })->();

# Closure capturing outer variable
sub make_counter {
    my $count = 0;
    return sub { return ++$count };
}

my $counter = make_counter();
$counter->();
$counter->();

# Closure capturing multiple vars
sub make_adder {
    my ($n) = @_;
    return sub { return $_[0] + $n };
}

my $add5  = make_adder(5);
my $add10 = make_adder(10);

# Callbacks
sub process_each {
    my ($list_ref, $callback) = @_;
    my @results;
    for my $item (@$list_ref) {
        push @results, $callback->($item);
    }
    return @results;
}

my @evens = grep { $_ % 2 == 0 } 1..10;
my @mapped = process_each(\@evens, sub { $_[0] ** 2 });

# Method as coderef
my $method_ref = \&Animal::speak;
