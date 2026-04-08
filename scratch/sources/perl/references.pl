#!/usr/bin/perl
use strict;
use warnings;

# Scalar references
my $value    = 42;
my $ref      = \$value;
my $deref    = $$ref;

# Array references
my @data     = (1, 2, 3);
my $aref     = \@data;
my $aref2    = [10, 20, 30];

# Hash references
my %opts     = (key => 'val');
my $href     = \%opts;
my $href2    = { host => 'localhost', port => 80 };

# Code references (coderefs)
my $greet    = sub { return "Hello, $_[0]!" };
my $add      = sub { return $_[0] + $_[1] };
my $result   = $greet->("World");

# Named coderef stored in hash (dispatch table)
my %dispatch = (
    add      => sub { $_[0] + $_[1] },
    subtract => sub { $_[0] - $_[1] },
    multiply => sub { $_[0] * $_[1] },
);

# Passing coderefs as callbacks
sub apply_to_list {
    my ($func, @list) = @_;
    return map { $func->($_) } @list;
}

my @doubled = apply_to_list(sub { $_[0] * 2 }, 1..5);

# Weak references
use Scalar::Util qw(weaken);
my $parent = { name => 'parent' };
my $child  = { name => 'child', parent => $parent };
weaken($child->{parent});
