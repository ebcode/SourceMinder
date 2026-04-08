#!/usr/bin/perl
package Animal;

use strict;
use warnings;

sub new {
    my ($class, %args) = @_;
    my $self = {
        name  => $args{name},
        sound => $args{sound} // 'generic noise',
    };
    return bless $self, $class;
}

sub name {
    my ($self) = @_;
    return $self->{name};
}

sub sound {
    my ($self) = @_;
    return $self->{sound};
}

sub speak {
    my ($self) = @_;
    printf "%s says %s\n", $self->name(), $self->sound();
}

sub describe {
    my ($self) = @_;
    return sprintf("I am %s", $self->{name});
}

package Dog;
use parent -norequire, 'Animal';

sub new {
    my ($class, %args) = @_;
    $args{sound} = 'woof';
    my $self = $class->SUPER::new(%args);
    $self->{tricks} = [];
    return $self;
}

sub learn_trick {
    my ($self, $trick) = @_;
    push @{$self->{tricks}}, $trick;
}

sub show_tricks {
    my ($self) = @_;
    return @{$self->{tricks}};
}

package main;

my $dog = Dog->new(name => 'Rex');
$dog->learn_trick('sit');
$dog->learn_trick('shake');
$dog->speak();
