#!/usr/bin/perl
package MyApp::Component;

use strict;
use warnings;

# --- BEGIN: runs at compile time, before anything else ---
BEGIN {
    # Typically used for compile-time setup, loading optional modules
    $ENV{APP_ENV} //= 'development';
}

# --- END: runs at program exit (reverse order of declaration) ---
END {
    # Cleanup: close connections, flush logs, etc.
    # $? holds the exit code
    print "Shutting down (exit code: $?)\n";
}

# Multiple END blocks — run in LIFO order
END {
    print "Final cleanup\n";
}

# --- INIT: runs after all BEGIN, before main execution ---
# (less common than BEGIN/END)
# INIT { ... }

# --- CHECK: runs after compile phase, before run phase ---
# CHECK { ... }

# --- Instance lifecycle ---

sub new {
    my ($class, %args) = @_;
    return bless {
        name    => $args{name} // 'unnamed',
        _active => 1,
    }, $class;
}

# --- DESTROY: called when object is garbage collected ---
sub DESTROY {
    my ($self) = @_;
    $self->{_active} = 0;
    # Release resources, close handles, etc.
}

# --- AUTOLOAD: called for any undefined method ---
our $AUTOLOAD;
sub AUTOLOAD {
    my ($self) = @_;
    my $method = $AUTOLOAD;
    $method =~ s/.*:://;  # strip package name

    return if $method eq 'DESTROY';  # avoid AUTOLOAD catching DESTROY

    # Generate accessor for hash-based attributes
    if (exists $self->{$method}) {
        no strict 'refs';
        *{"MyApp::Component::$method"} = sub {
            my ($obj, $val) = @_;
            $obj->{$method} = $val if defined $val;
            return $obj->{$method};
        };
        goto &{"MyApp::Component::$method"};
    }

    die "Unknown method '$method' on " . ref($self) . "\n";
}

# --- Regular methods ---

sub name   { return $_[0]->{name}    }
sub active { return $_[0]->{_active} }

sub deactivate {
    my ($self) = @_;
    $self->{_active} = 0;
}

package main;

my $obj = MyApp::Component->new(name => 'widget');
print $obj->name . "\n";
$obj->deactivate;
