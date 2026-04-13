#!/usr/bin/perl
use strict;
use warnings;

# --- shift: extracting a single parameter from @_ ---

# Classic OOP: $self via shift
sub new {
    my $class = shift;
    my $self  = { name => 'default' };
    return bless $self, $class;
}

sub get_name {
    my $self = shift;
    return $self->{name};
}

# Extracting multiple params with successive shifts
sub connect {
    my $host = shift;
    my $port = shift;
    my $user = shift;
    return "$user\@$host:$port";
}

# shift on an explicit array (not @_)
sub pop_queue {
    my ($queue) = @_;
    my $item = shift @$queue;
    return $item;
}

# shift inside a loop
sub drain {
    my ($items) = @_;
    my @results;
    while (my $val = shift @$items) {
        push @results, $val;
    }
    return @results;
}

# shift to peel off a flag argument
sub maybe_verbose {
    my $verbose = shift if @_ && $_[0] eq '--verbose';
    my ($message) = @_;
    print $message if $verbose;
}

# --- unshift: prepending to arrays ---

# unshift onto a local array
sub prepend_header {
    my ($lines, $header) = @_;
    unshift @$lines, $header;
    return $lines;
}

# unshift multiple values
sub prepend_defaults {
    my ($args) = @_;
    unshift @$args, '--verbose', '--strict';
    return $args;
}

# unshift in a loop (build list in reverse)
sub reverse_build {
    my @source = (1, 2, 3, 4, 5);
    my @result;
    for my $item (@source) {
        unshift @result, $item;
    }
    return @result;
}

# Combined: shift off a command, unshift a replacement
sub normalize_command {
    my ($argv) = @_;
    my $cmd = shift @$argv;
    $cmd = lc($cmd);
    unshift @$argv, $cmd;
    return $argv;
}
