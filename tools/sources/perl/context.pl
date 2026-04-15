#!/usr/bin/perl
use strict;
use warnings;

# wantarray: list vs scalar vs void context
sub context_aware {
    if (wantarray) {
        return (1, 2, 3);
    } elsif (defined wantarray) {
        return 42;
    } else {
        return;    # void context — side effects only
    }
}

my @list   = context_aware();    # list context
my $scalar = context_aware();    # scalar context
context_aware();                 # void context

# Array in scalar context yields count
my @items  = (10, 20, 30);
my $count  = @items;
my $last   = $#items;

# Hash in scalar context (bucket usage)
my %h      = (a => 1, b => 2);
my $hinfo  = %h;

# Forcing scalar context
my $num_items = scalar @items;
my $str       = scalar reverse "hello";

# localtime context sensitivity
my @time_parts = localtime(time);
my $time_str   = localtime(time);

# String vs numeric context
my $val   = "42abc";
my $num   = $val + 0;
my $str2  = $val . '';

# Boolean context
my @empty;
if (@empty) { }           # false — array in bool context
if (%h)     { }           # true  — hash in bool context
my $x = undef;
my $y = $x || 'default';  # short-circuit in boolean context
my $z = $x // 'defined';  # defined-or

# Context propagation through sub calls
sub wrap_context_aware {
    return context_aware();    # propagates caller's context
}

my @r1 = wrap_context_aware();
my $r2 = wrap_context_aware();

# grep/map impose list context on block
my @nums    = (1..10);
my @odds    = grep { $_ % 2 } @nums;
my @doubled = map  { $_ * 2  } @nums;

# Subroutine that returns differently by context
sub fetch_config {
    my %cfg = (host => 'localhost', port => 5432);
    return wantarray ? %cfg : \%cfg;
}

my %flat_cfg  = fetch_config();
my $cfg_ref   = fetch_config();

# String repetition vs list repetition by context
my $str_rep  = 'ab' x 3;       # 'ababab'
my @list_rep = (0) x 4;        # (0,0,0,0)
