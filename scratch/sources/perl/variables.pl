#!/usr/bin/perl
use strict;
use warnings;

# Scalar variables
my $name     = "Alice";
my $age      = 30;
my $pi       = 3.14159;
my $is_valid = 1;

# Array variables
my @fruits   = ('apple', 'banana', 'cherry');
my @numbers  = (1, 2, 3, 4, 5);
my @empty    = ();

# Hash variables
my %config = (
    host => 'localhost',
    port => 5432,
    user => 'admin',
);
my %scores = (alice => 95, bob => 87, carol => 92);

# our variables (package globals)
our $VERSION = '1.0';
our @EXPORT  = qw(greet farewell);
our %defaults = (timeout => 30, retries => 3);

# local variables
sub example_with_local {
    local $/ = undef;  # slurp mode
    local $, = ", ";
    my $data = "test";
    return $data;
}

# Complex data structures
my @matrix = ([1, 2, 3], [4, 5, 6], [7, 8, 9]);
my %registry = (
    users    => [],
    admins   => {},
    settings => { debug => 0 },
);

# Multiple assignment
my ($x, $y, $z) = (10, 20, 30);
my ($first, @rest) = @fruits;
