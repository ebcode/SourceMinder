use strict;
use warnings;
use Carp qw(croak confess carp cluck);
use Scalar::Util qw(blessed);

# Basic eval/die pattern
sub safe_divide {
    my ($a, $b) = @_;
    die "Division by zero\n" if $b == 0;
    return $a / $b;
}

sub try_divide {
    my ($a, $b) = @_;
    my $result = eval { safe_divide($a, $b) };
    if ($@) {
        warn "Caught: $@";
        return undef;
    }
    return $result;
}

# Local $@ to preserve error across nested evals
sub nested_eval {
    my ($code) = @_;
    local $@;
    my $result = eval { $code->() };
    return ($result, $@);
}

# Carp-based error reporting
sub parse_config {
    my ($file) = @_;
    croak "File not specified" unless defined $file;
    open(my $fh, '<', $file) or croak "Cannot open $file: $!";
    close($fh);
}

sub validate_age {
    my ($age) = @_;
    carp "Age is negative" if $age < 0;
    cluck "Age over 150 is suspicious" if $age > 150;
    confess "Age must be numeric" unless $age =~ /^\d+$/;
    return $age;
}

# Exception object pattern
package Exception;

sub new {
    my ($class, %args) = @_;
    return bless {
        message  => $args{message} // 'Unknown error',
        code     => $args{code}    // 0,
        trace    => $args{trace}   // '',
    }, $class;
}

sub message { return $_[0]->{message} }
sub code    { return $_[0]->{code}    }
sub trace   { return $_[0]->{trace}   }

sub throw {
    my ($class, %args) = @_;
    die $class->new(%args);
}

sub stringify {
    my ($self) = @_;
    return sprintf("Exception(%d): %s", $self->{code}, $self->{message});
}

package IOException;
use parent -norequire, 'Exception';

sub new {
    my ($class, %args) = @_;
    $args{code} //= 100;
    return $class->SUPER::new(%args);
}

package main;

# Catching typed exceptions
sub read_file {
    my ($path) = @_;
    open(my $fh, '<', $path)
        or IOException->throw(message => "Cannot read $path: $!", code => 101);
    local $/;
    my $content = <$fh>;
    close($fh);
    return $content;
}

sub handle_errors {
    my ($code) = @_;
    my $result = eval { $code->() };
    if (my $err = $@) {
        if (blessed($err) && $err->isa('IOException')) {
            warn "IO error: " . $err->message;
        } elsif (blessed($err) && $err->isa('Exception')) {
            warn "Error " . $err->code . ": " . $err->message;
        } else {
            die $err;  # rethrow unknown errors
        }
    }
    return $result;
}

# $! (errno) usage
sub check_file {
    my ($path) = @_;
    unless (-e $path) {
        croak "File '$path' not found: $!";
    }
    return 1;
}

1;
