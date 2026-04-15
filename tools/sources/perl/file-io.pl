#!/usr/bin/perl
use strict;
use warnings;

# Lexical filehandle (preferred modern style)
open(my $fh, '<', 'input.txt') or die "Cannot open: $!";
while (my $line = <$fh>) {
    chomp $line;
    print "$line\n";
}
close($fh);

# Writing to a file
open(my $out, '>', 'output.txt') or die "Cannot open: $!";
print $out "Hello, file!\n";
close($out);

# Append mode
open(my $log, '>>', 'app.log') or die "Cannot open: $!";
print $log "Log entry\n";
close($log);

# Reading all lines at once
open(my $in, '<', 'data.txt') or die $!;
my @lines = <$in>;
close($in);
chomp @lines;

# Slurp entire file
open(my $slurp_fh, '<', 'config.txt') or die $!;
my $contents;
{ local $/; $contents = <$slurp_fh> }
close($slurp_fh);

# Bareword filehandles (legacy style)
open(LOGFILE, '>>', 'legacy.log') or die $!;
print LOGFILE "legacy entry\n";
close(LOGFILE);

# Standard handles
print STDOUT "to stdout\n";
print STDERR "to stderr\n";
my $input = <STDIN>;

# In-memory filehandle
use File::Temp qw(tempfile);
my ($tmp_fh, $tmp_name) = tempfile();

# String filehandle (open on scalar)
my $buffer = '';
open(my $str_fh, '>', \$buffer) or die $!;
print $str_fh "written to string\n";
close($str_fh);

# Read/write mode
open(my $rw, '+<', 'existing.txt') or die $!;
seek($rw, 0, 0);
my $first = <$rw>;
close($rw);

# -e, -f, -d file tests
if (-e 'input.txt') {
    my $size    = -s 'input.txt';
    my $is_file = -f 'input.txt';
    my $is_dir  = -d 'input.txt';
}

# Reading binary
open(my $bin, '<:raw', 'image.png') or die $!;
my $header;
read($bin, $header, 8);
close($bin);

# Encoding layer
open(my $utf8_fh, '<:encoding(UTF-8)', 'unicode.txt') or die $!;
while (<$utf8_fh>) { chomp; }
close($utf8_fh);
