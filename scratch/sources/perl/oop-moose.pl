#!/usr/bin/perl
package Person;

use Moose;
use namespace::autoclean;

has 'name' => (
    is       => 'ro',
    isa      => 'Str',
    required => 1,
);

has 'age' => (
    is      => 'rw',
    isa     => 'Int',
    default => 0,
);

has 'email' => (
    is        => 'rw',
    isa       => 'Str',
    predicate => 'has_email',
    clearer   => 'clear_email',
);

sub greet {
    my ($self) = @_;
    return "Hi, I'm " . $self->name;
}

sub birthday {
    my ($self) = @_;
    $self->age($self->age + 1);
}

__PACKAGE__->meta->make_immutable;

package Employee;

use Moose;
use namespace::autoclean;

extends 'Person';

has 'company' => (
    is       => 'ro',
    isa      => 'Str',
    required => 1,
);

has 'salary' => (
    is      => 'rw',
    isa     => 'Num',
    default => 0,
);

around 'greet' => sub {
    my ($orig, $self) = @_;
    return $self->$orig() . " from " . $self->company;
};

sub give_raise {
    my ($self, $amount) = @_;
    $self->salary($self->salary + $amount);
}

__PACKAGE__->meta->make_immutable;

package Role::Printable;

use Moose::Role;

requires 'to_string';

sub print_self {
    my ($self) = @_;
    print $self->to_string, "\n";
}
