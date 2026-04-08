#!/usr/bin/perl
use strict;
use warnings;

# Basic pattern matching
sub is_valid_email {
    my ($email) = @_;
    return $email =~ /^[^@]+@[^@]+\.[^@]+$/;
}

sub extract_digits {
    my ($str) = @_;
    my @digits = ($str =~ /(\d+)/g);
    return @digits;
}

# Named captures
sub parse_date {
    my ($date_str) = @_;
    if ($date_str =~ /(?<year>\d{4})-(?<month>\d{2})-(?<day>\d{2})/) {
        return ($+{year}, $+{month}, $+{day});
    }
    return ();
}

# Substitution
sub sanitize_input {
    my ($input) = @_;
    $input =~ s/[<>&"']//g;
    return $input;
}

sub normalize_newlines {
    my ($text) = @_;
    $text =~ s/\r\n|\r/\n/g;
    return $text;
}

# Compiled regex (qr//)
my $ip_pattern    = qr/\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}/;
my $email_pattern = qr/[\w.+-]+@[\w-]+\.[\w.]+/;
my $url_pattern   = qr|https?://[\w/:%#\$&\?\(\)~\.=\+\-]+|;

sub extract_ips {
    my ($text) = @_;
    my @ips = ($text =~ /($ip_pattern)/g);
    return @ips;
}

# Split with regex
sub tokenize {
    my ($str) = @_;
    return split /[\s,;]+/, $str;
}
