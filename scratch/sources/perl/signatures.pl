#!/usr/bin/perl
use strict;
use warnings;
use feature 'signatures';
no warnings 'experimental::signatures';

# Modern Perl signatures (5.20+)
sub greet ($name) {
    return "Hello, $name!";
}

sub add ($x, $y) {
    return $x + $y;
}

# With default values
sub connect ($host = 'localhost', $port = 5432) {
    return "$host:$port";
}

# Optional parameter
sub log_message ($msg, $level = 'info') {
    print "[$level] $msg\n";
}

# Slurpy parameter
sub sum_all ($first, @rest) {
    my $total = $first;
    $total += $_ for @rest;
    return $total;
}

# Named parameters via hash ref
sub create_user (%args) {
    my $name  = $args{name}  // die "name required";
    my $email = $args{email} // die "email required";
    my $role  = $args{role}  // 'user';
    return { name => $name, email => $email, role => $role };
}
