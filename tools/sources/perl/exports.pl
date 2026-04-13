#!/usr/bin/perl
package MyUtils;

use strict;
use warnings;
use Exporter qw(import);

# Default exports (imported automatically with 'use MyUtils')
our @EXPORT = qw(
    trim
    slugify
    is_empty
);

# Optional exports (available via 'use MyUtils qw(...)')
our @EXPORT_OK = qw(
    truncate_str
    pad_left
    pad_right
    capitalize
    count_words
);

# Export tags (groups of optional exports)
our %EXPORT_TAGS = (
    string  => [qw(trim slugify truncate_str capitalize)],
    padding => [qw(pad_left pad_right)],
    all     => [@EXPORT, @EXPORT_OK],
);

# --- Exported by default ---

sub trim {
    my ($str) = @_;
    $str =~ s/^\s+|\s+$//g;
    return $str;
}

sub slugify {
    my ($str) = @_;
    $str = lc($str);
    $str =~ s/[^a-z0-9]+/-/g;
    $str =~ s/^-|-$//g;
    return $str;
}

sub is_empty {
    my ($val) = @_;
    return !defined($val) || $val eq '';
}

# --- Optionally exported ---

sub truncate_str {
    my ($str, $max) = @_;
    return length($str) > $max ? substr($str, 0, $max) . '...' : $str;
}

sub pad_left {
    my ($str, $width, $char) = @_;
    $char //= ' ';
    return sprintf("%*s", $width, $str);
}

sub pad_right {
    my ($str, $width, $char) = @_;
    $char //= ' ';
    return sprintf("%-*s", $width, $str);
}

sub capitalize {
    my ($str) = @_;
    return ucfirst(lc($str));
}

sub count_words {
    my ($str) = @_;
    my @words = split /\s+/, trim($str);
    return scalar @words;
}

# --- Internal (not exported) ---

sub _normalize {
    my ($str) = @_;
    return trim(lc($str));
}

1;
