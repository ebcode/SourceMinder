#!/usr/bin/perl
use strict;
use warnings;

# Shared mutable state between closures
sub make_stack {
    my @stack;
    my $push = sub { push @stack, $_[0] };
    my $pop  = sub { pop @stack };
    my $size = sub { scalar @stack };
    return ($push, $pop, $size);
}

my ($push, $pop, $size) = make_stack();
$push->(1);
$push->(2);
my $top = $pop->();

# Generator-style closure
sub counter_from {
    my ($start, $step) = @_;
    $step //= 1;
    my $n = $start;
    return sub { my $val = $n; $n += $step; return $val };
}

my $evens = counter_from(0, 2);
my $a = $evens->();
my $b = $evens->();

# Memoization via closure
sub memoize {
    my ($func) = @_;
    my %cache;
    return sub {
        my $key = join(',', @_);
        $cache{$key} //= $func->(@_);
        return $cache{$key};
    };
}

my $slow_double = sub { return $_[0] * 2 };
my $fast_double = memoize($slow_double);

# Closure over loop variable (capture by value)
my @actions;
for my $i (1..3) {
    push @actions, sub { return $i * $i };
}
my @squares = map { $_->() } @actions;

# Named inner sub closing over lexical
sub make_validator {
    my ($min, $max) = @_;
    my $in_range = sub { $_[0] >= $min && $_[0] <= $max };
    sub validate {
        my ($val) = @_;
        return $in_range->($val) ? $val : undef;
    }
    return \&validate;
}

# Closure as object (poor man's OOP)
sub make_account {
    my ($balance) = @_;
    return {
        deposit  => sub { $balance += $_[0] },
        withdraw => sub { $balance -= $_[0] },
        balance  => sub { $balance },
    };
}

my $account = make_account(100);
$account->{deposit}->(50);
my $bal = $account->{balance}->();
