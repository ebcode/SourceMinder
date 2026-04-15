#!/usr/bin/perl
use strict;
use warnings;

my @nums   = (1..10);
my @words  = qw(banana apple cherry date elderberry fig);

# --- map ---
my @doubled  = map { $_ * 2 }    @nums;
my @strings  = map { "item $_" } @nums;
my @lengths  = map { length($_) } @words;

# map returning multiple values (flattening)
my @pairs    = map { ($_, $_ * $_ ) } 1..4;

# map to build a hash
my %len_of   = map { $_ => length($_) } @words;
my %squared  = map { $_ => $_ ** 2   } 1..5;

# Chained map
my @result   = map { uc } map { reverse } @words;

# --- grep ---
my @evens    = grep { $_ % 2 == 0 }      @nums;
my @long     = grep { length($_) > 4 }   @words;
my @defined  = grep { defined }          (1, undef, 2, undef, 3);

# grep with regex
my @has_a    = grep { /a/ }     @words;
my @no_e     = grep { !/e/ }    @words;

# grep to count matches
my $even_count = grep { $_ % 2 == 0 } @nums;

# --- sort ---
my @alpha    = sort @words;
my @rev      = sort { $b cmp $a }     @words;
my @by_len   = sort { length($a) <=> length($b) || $a cmp $b } @words;
my @numeric  = sort { $a <=> $b }     (5, 3, 8, 1, 9, 2);
my @rev_num  = sort { $b <=> $a }     @nums;

# Schwartzian transform (sort by computed key, efficiently)
my @by_len_st = map  { $_->[0] }
                sort { $a->[1] <=> $b->[1] || $a->[0] cmp $b->[0] }
                map  { [$_, length($_)] }
                @words;

# Sort objects/hashes by field
my @people  = (
    { name => 'Charlie', age => 30 },
    { name => 'Alice',   age => 25 },
    { name => 'Bob',     age => 35 },
);
my @by_age  = sort { $a->{age} <=> $b->{age} } @people;
my @by_name = sort { $a->{name} cmp $b->{name} } @people;

# --- combinations ---
my @top3_even_squares =
    map  { $_ * $_ }
    grep { $_ % 2 == 0 }
    sort { $a <=> $b }
    @nums;

# reduce (from List::Util)
use List::Util qw(reduce sum min max first any all none);

my $product = reduce { $a * $b } @nums;
my $total   = sum   @nums;
my $minimum = min   @nums;
my $maximum = max   @nums;
my $first_e = first { /e/ } @words;

my $has_even = any  { $_ % 2 == 0 } @nums;
my $all_pos  = all  { $_ > 0      } @nums;
my $none_neg = none { $_ < 0      } @nums;
