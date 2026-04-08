#!/usr/bin/perl
package MyApp::Config;

use strict;
use warnings;
use Exporter 'import';

our @EXPORT_OK   = qw(get_config load_config);
our %EXPORT_TAGS = (all => \@EXPORT_OK);
our $VERSION     = '2.1.0';

sub get_config {
    my ($key) = @_;
    return $defaults{$key};
}

sub load_config {
    my ($file) = @_;
    # load from file
}

package MyApp::Database;

use strict;
use warnings;

our $DSN      = 'dbi:Pg:dbname=myapp';
our $USERNAME = 'myapp_user';

sub connect {
    my ($class) = @_;
    # connection logic
}

sub disconnect {
    my ($self) = @_;
    # cleanup
}

package MyApp::Utils;

sub trim {
    my ($str) = @_;
    $str =~ s/^\s+|\s+$//g;
    return $str;
}

sub slugify {
    my ($str) = @_;
    $str = lc $str;
    $str =~ s/[^a-z0-9]+/-/g;
    $str =~ s/^-|-$//g;
    return $str;
}
