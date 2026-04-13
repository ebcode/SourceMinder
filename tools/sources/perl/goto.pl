#!/usr/bin/perl
use strict;
use warnings;

# --- Labels with last/next/redo ---

# Simple labeled loop
OUTER: for my $i (1..5) {
    INNER: for my $j (1..5) {
        next OUTER if $j == 3;
        last OUTER if $i == 4;
    }
}

# Label with while loop
RETRY: while (1) {
    my $ok = 1;
    last RETRY if $ok;
}

# redo — restart current iteration without re-evaluating condition
my $count = 0;
LOOP: while ($count < 3) {
    $count++;
    redo LOOP if $count == 1 && $count < 2;
    last LOOP;
}

# --- goto LABEL ---
sub check_value {
    my ($val) = @_;
    goto INVALID if $val < 0;
    return $val * 2;
    INVALID: return -1;
}

# --- goto &func (tail-call) ---
sub log_and_dispatch {
    my ($type) = @_;
    goto &handle_request if $type eq 'request';
    goto &handle_event   if $type eq 'event';
}

sub handle_request {
    my ($type) = @_;
    return "request: $type";
}

sub handle_event {
    my ($type) = @_;
    return "event: $type";
}

# --- goto EXPR (computed) ---
sub dispatch_by_name {
    my ($name) = @_;
    my %table = (
        foo => \&handle_request,
        bar => \&handle_event,
    );
    my $target = $table{$name};
    goto $target if defined $target;
    return undef;
}
