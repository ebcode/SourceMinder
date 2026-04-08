#!/usr/bin/perl
use strict;
use warnings;

# Basic use statements
use Scalar::Util qw(blessed looks_like_number weaken);
use List::Util qw(min max sum first reduce any all none);
use POSIX qw(floor ceil);
use Carp qw(croak confess carp cluck);

# Version requirements
use 5.010;
use 5.020;

# Pragma modules
use utf8;
use feature 'say';
use feature qw(say state unicode_strings);
use constant PI => 3.14159265358979;
use constant {
    MAX_RETRIES => 3,
    TIMEOUT     => 30,
    BASE_URL    => 'https://example.com',
};

# OOP imports
use parent 'Base::Class';
use base 'Another::Base';
use Moo;
use Moose;

# require (runtime loading)
require LWP::UserAgent;
require File::Path;

# Conditional loading
eval { require JSON::XS };
if ($@) {
    require JSON::PP;
}
