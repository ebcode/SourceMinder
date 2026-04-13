#!/usr/bin/perl
use strict;
use warnings;

# --- Basic eval/die ---

sub divide {
    my ($a, $b) = @_;
    die "Division by zero\n" if $b == 0;
    return $a / $b;
}

eval {
    my $result = divide(10, 0);
};
if ($@) {
    warn "Caught: $@";
}

# --- die with a reference (object exception) ---

sub read_file {
    my ($path) = @_;
    open(my $fh, '<', $path) or die { code => 404, message => "File not found: $path" };
    return do { local $/; <$fh> };
}

eval {
    read_file('/no/such/file');
};
if (ref $@ eq 'HASH') {
    my $err = $@;
    warn "Error $err->{code}: $err->{message}\n";
}

# --- Exception class (classic OOP style) ---

package Exception;

sub new {
    my ($class, %args) = @_;
    return bless {
        message => $args{message} // 'Unknown error',
        code    => $args{code}    // 0,
    }, $class;
}

sub message { return $_[0]->{message} }
sub code    { return $_[0]->{code}    }
sub throw   {
    my ($class, %args) = @_;
    die $class->new(%args);
}

package IOException;
use parent -norequire, 'Exception';

sub new {
    my ($class, %args) = @_;
    $args{code} //= 500;
    return $class->SUPER::new(%args);
}

package NetworkException;
use parent -norequire, 'Exception';

sub new {
    my ($class, %args) = @_;
    $args{code} //= 503;
    return $class->SUPER::new(%args);
}

package main;

# --- Catching typed exceptions ---

eval {
    IOException->throw(message => 'Disk full', code => 507);
};
if (my $e = $@) {
    if (ref $e && $e->isa('IOException')) {
        warn "IO error " . $e->code . ": " . $e->message . "\n";
    } elsif (ref $e && $e->isa('Exception')) {
        warn "General error: " . $e->message . "\n";
    } else {
        die $e;  # rethrow unknown
    }
}

# --- Nested eval ---

sub safe_connect {
    my ($host, $port) = @_;
    my $result = eval {
        NetworkException->throw(message => "Cannot connect to $host:$port");
    };
    return $result;
}

# --- local $@ to preserve error across nested calls ---

sub wrap_eval {
    my ($code) = @_;
    eval { $code->() };
    my $err = $@;
    return $err ? "failed: $err" : "ok";
}
