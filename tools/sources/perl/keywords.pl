#!/usr/bin/perl
#
# Every word in perl/config/keywords.txt used in a naming position.
#
# Perl accepts all of them as scalar, array, hash, sub, hash-key and package
# names (verified with perl -c). Sigils mean $if and @for never collide with
# the keyword list, but bare sub names and hash keys do.
#
# Many entries in that list are builtin *functions*, not keywords -- push,
# shift, sort, join, print, length. The calls at the bottom exercise them.
#
# All identifiers are two characters or longer, so nothing here is dropped on
# minimum length rather than on keyword-ness.

use strict;
use warnings;

# Keywords as scalars.
my $if = 1;
my $elsif = 2;
my $else = 3;
my $unless = 4;
my $while = 5;
my $until = 6;
my $for = 7;
my $foreach = 8;
my $do = 9;
my $given = 10;
my $when = 11;
my $default = 12;
my $use = 13;
my $no = 14;
my $require = 15;
my $package = 16;
my $sub = 17;
my $my = 18;
my $our = 19;
my $local = 20;
my $return = 21;
my $last = 22;
my $next = 23;
my $redo = 24;
my $goto = 25;
my $and = 26;
my $or = 27;
my $not = 28;
my $eq = 29;
my $ne = 30;
my $lt = 31;
my $gt = 32;
my $le = 33;
my $ge = 34;
my $cmp = 35;
my $print = 36;
my $say = 37;
my $die = 38;
my $warn = 39;
my $undef = 40;
my $defined = 41;
my $scalar = 42;
my $push = 43;
my $pop = 44;
my $shift = 45;
my $unshift = 46;
my $splice = 47;
my $reverse = 48;
my $sort = 49;
my $map = 50;
my $grep = 51;
my $join = 52;
my $split = 53;
my $chomp = 54;
my $chop = 55;
my $length = 56;
my $substr = 57;
my $sprintf = 58;
my $printf = 59;
my $open = 60;
my $close = 61;
my $read = 62;
my $write = 63;
my $binmode = 64;
my $true = 65;
my $false = 66;

# Keywords as arrays.
my @if = (1, 2);
my @elsif = (1, 2);
my @else = (1, 2);
my @unless = (1, 2);
my @while = (1, 2);
my @until = (1, 2);
my @for = (1, 2);
my @foreach = (1, 2);
my @do = (1, 2);
my @given = (1, 2);
my @when = (1, 2);
my @default = (1, 2);
my @use = (1, 2);
my @no = (1, 2);
my @require = (1, 2);
my @package = (1, 2);
my @sub = (1, 2);
my @my = (1, 2);
my @our = (1, 2);
my @local = (1, 2);
my @return = (1, 2);
my @last = (1, 2);
my @next = (1, 2);
my @redo = (1, 2);
my @goto = (1, 2);
my @and = (1, 2);
my @or = (1, 2);
my @not = (1, 2);
my @eq = (1, 2);
my @ne = (1, 2);
my @lt = (1, 2);
my @gt = (1, 2);
my @le = (1, 2);
my @ge = (1, 2);
my @cmp = (1, 2);
my @print = (1, 2);
my @say = (1, 2);
my @die = (1, 2);
my @warn = (1, 2);
my @undef = (1, 2);
my @defined = (1, 2);
my @scalar = (1, 2);
my @push = (1, 2);
my @pop = (1, 2);
my @shift = (1, 2);
my @unshift = (1, 2);
my @splice = (1, 2);
my @reverse = (1, 2);
my @sort = (1, 2);
my @map = (1, 2);
my @grep = (1, 2);
my @join = (1, 2);
my @split = (1, 2);
my @chomp = (1, 2);
my @chop = (1, 2);
my @length = (1, 2);
my @substr = (1, 2);
my @sprintf = (1, 2);
my @printf = (1, 2);
my @open = (1, 2);
my @close = (1, 2);
my @read = (1, 2);
my @write = (1, 2);
my @binmode = (1, 2);
my @true = (1, 2);
my @false = (1, 2);

# Keywords as hashes.
my %if = (aa => 1);
my %elsif = (aa => 1);
my %else = (aa => 1);
my %unless = (aa => 1);
my %while = (aa => 1);
my %until = (aa => 1);
my %for = (aa => 1);
my %foreach = (aa => 1);
my %do = (aa => 1);
my %given = (aa => 1);
my %when = (aa => 1);
my %default = (aa => 1);
my %use = (aa => 1);
my %no = (aa => 1);
my %require = (aa => 1);
my %package = (aa => 1);
my %sub = (aa => 1);
my %my = (aa => 1);
my %our = (aa => 1);
my %local = (aa => 1);
my %return = (aa => 1);
my %last = (aa => 1);
my %next = (aa => 1);
my %redo = (aa => 1);
my %goto = (aa => 1);
my %and = (aa => 1);
my %or = (aa => 1);
my %not = (aa => 1);
my %eq = (aa => 1);
my %ne = (aa => 1);
my %lt = (aa => 1);
my %gt = (aa => 1);
my %le = (aa => 1);
my %ge = (aa => 1);
my %cmp = (aa => 1);
my %print = (aa => 1);
my %say = (aa => 1);
my %die = (aa => 1);
my %warn = (aa => 1);
my %undef = (aa => 1);
my %defined = (aa => 1);
my %scalar = (aa => 1);
my %push = (aa => 1);
my %pop = (aa => 1);
my %shift = (aa => 1);
my %unshift = (aa => 1);
my %splice = (aa => 1);
my %reverse = (aa => 1);
my %sort = (aa => 1);
my %map = (aa => 1);
my %grep = (aa => 1);
my %join = (aa => 1);
my %split = (aa => 1);
my %chomp = (aa => 1);
my %chop = (aa => 1);
my %length = (aa => 1);
my %substr = (aa => 1);
my %sprintf = (aa => 1);
my %printf = (aa => 1);
my %open = (aa => 1);
my %close = (aa => 1);
my %read = (aa => 1);
my %write = (aa => 1);
my %binmode = (aa => 1);
my %true = (aa => 1);
my %false = (aa => 1);

# Case variants: the filter lowercases before comparing, so $If folds onto if.
my $If = 'if';
my $Elsif = 'elsif';
my $Else = 'else';
my $Unless = 'unless';
my $While = 'while';
my $Until = 'until';
my $For = 'for';
my $Foreach = 'foreach';
my $Do = 'do';
my $Given = 'given';
my $When = 'when';
my $Default = 'default';
my $Use = 'use';
my $No = 'no';
my $Require = 'require';
my $Package = 'package';
my $Sub = 'sub';
my $My = 'my';
my $Our = 'our';
my $Local = 'local';
my $Return = 'return';
my $Last = 'last';
my $Next = 'next';
my $Redo = 'redo';
my $Goto = 'goto';
my $And = 'and';
my $Or = 'or';
my $Not = 'not';
my $Eq = 'eq';
my $Ne = 'ne';
my $Lt = 'lt';
my $Gt = 'gt';
my $Le = 'le';
my $Ge = 'ge';
my $Cmp = 'cmp';
my $Print = 'print';
my $Say = 'say';
my $Die = 'die';
my $Warn = 'warn';
my $Undef = 'undef';
my $Defined = 'defined';
my $Scalar = 'scalar';
my $Push = 'push';
my $Pop = 'pop';
my $Shift = 'shift';
my $Unshift = 'unshift';
my $Splice = 'splice';
my $Reverse = 'reverse';
my $Sort = 'sort';
my $Map = 'map';
my $Grep = 'grep';
my $Join = 'join';
my $Split = 'split';
my $Chomp = 'chomp';
my $Chop = 'chop';
my $Length = 'length';
my $Substr = 'substr';
my $Sprintf = 'sprintf';
my $Printf = 'printf';
my $Open = 'open';
my $Close = 'close';
my $Read = 'read';
my $Write = 'write';
my $Binmode = 'binmode';
my $True = 'true';
my $False = 'false';

# Keywords as hash keys (bare, no sigil to protect them).
my %settings = (
    if => 1,
    elsif => 1,
    else => 1,
    unless => 1,
    while => 1,
    until => 1,
    for => 1,
    foreach => 1,
    do => 1,
    given => 1,
    when => 1,
    default => 1,
    use => 1,
    no => 1,
    require => 1,
    package => 1,
    sub => 1,
    my => 1,
    our => 1,
    local => 1,
    return => 1,
    last => 1,
    next => 1,
    redo => 1,
    goto => 1,
    and => 1,
    or => 1,
    not => 1,
    eq => 1,
    ne => 1,
    lt => 1,
    gt => 1,
    le => 1,
    ge => 1,
    cmp => 1,
    print => 1,
    say => 1,
    die => 1,
    warn => 1,
    undef => 1,
    defined => 1,
    scalar => 1,
    push => 1,
    pop => 1,
    shift => 1,
    unshift => 1,
    splice => 1,
    reverse => 1,
    sort => 1,
    map => 1,
    grep => 1,
    join => 1,
    split => 1,
    chomp => 1,
    chop => 1,
    length => 1,
    substr => 1,
    sprintf => 1,
    printf => 1,
    open => 1,
    close => 1,
    read => 1,
    write => 1,
    binmode => 1,
    true => 1,
    false => 1,
);

# Keywords as sub names (bare, no sigil to protect them).
sub if { return 1; }
sub elsif { return 1; }
sub else { return 1; }
sub unless { return 1; }
sub while { return 1; }
sub until { return 1; }
sub for { return 1; }
sub foreach { return 1; }
sub do { return 1; }
sub given { return 1; }
sub when { return 1; }
sub default { return 1; }
sub use { return 1; }
sub no { return 1; }
sub require { return 1; }
sub package { return 1; }
sub sub { return 1; }
sub my { return 1; }
sub our { return 1; }
sub local { return 1; }
sub return { return 1; }
sub last { return 1; }
sub next { return 1; }
sub redo { return 1; }
sub goto { return 1; }
sub and { return 1; }
sub or { return 1; }
sub not { return 1; }
sub eq { return 1; }
sub ne { return 1; }
sub lt { return 1; }
sub gt { return 1; }
sub le { return 1; }
sub ge { return 1; }
sub cmp { return 1; }
sub print { return 1; }
sub say { return 1; }
sub die { return 1; }
sub warn { return 1; }
sub undef { return 1; }
sub defined { return 1; }
sub scalar { return 1; }
sub push { return 1; }
sub pop { return 1; }
sub shift { return 1; }
sub unshift { return 1; }
sub splice { return 1; }
sub reverse { return 1; }
sub sort { return 1; }
sub map { return 1; }
sub grep { return 1; }
sub join { return 1; }
sub split { return 1; }
sub chomp { return 1; }
sub chop { return 1; }
sub length { return 1; }
sub substr { return 1; }
sub sprintf { return 1; }
sub printf { return 1; }
sub open { return 1; }
sub close { return 1; }
sub read { return 1; }
sub write { return 1; }
sub binmode { return 1; }
sub true { return 1; }
sub false { return 1; }

# Builtin functions from the keyword list, called normally.
my @stack = (3, 1, 2);
push @stack, 4;
my $top = pop @stack;
my $head = shift @stack;
unshift @stack, 9;
my @sorted = sort @stack;
my @doubled = map { $_ * 2 } @stack;
my @kept = grep { $_ > 1 } @stack;
my $joined = join(',', @stack);
my @parts = split(/,/, $joined);
my $size = length($joined);
my $piece = substr($joined, 0, 2);
my $text = sprintf('%s', $joined);
print "$text $top $head $size $piece @sorted @doubled @kept @parts\n";

1;
