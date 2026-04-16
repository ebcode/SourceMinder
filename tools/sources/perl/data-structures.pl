use strict;
use warnings;
use Scalar::Util qw(blessed reftype weaken);

# Building nested structures
sub make_user {
    my (%args) = @_;
    return {
        name    => $args{name},
        email   => $args{email},
        roles   => $args{roles} // [],
        address => {
            street => $args{street} // '',
            city   => $args{city}   // '',
            zip    => $args{zip}    // '',
        },
    };
}

# Deep access with arrow notation
sub get_city {
    my ($user) = @_;
    return $user->{address}{city};
}

sub get_first_role {
    my ($user) = @_;
    return $user->{roles}[0];
}

# Dereferencing
sub list_roles {
    my ($user) = @_;
    return @{ $user->{roles} };
}

sub merge_address {
    my ($user, $updates) = @_;
    my %addr = %{ $user->{address} };
    %addr = (%addr, %$updates);
    $user->{address} = \%addr;
}

# exists / delete
sub remove_field {
    my ($hash, $key) = @_;
    return unless exists $hash->{$key};
    delete $hash->{$key};
}

# keys / values / each
sub summarize {
    my ($hash) = @_;
    my @keys   = keys %$hash;
    my @vals   = values %$hash;
    return scalar(@keys);
}

sub find_by_value {
    my ($hash, $target) = @_;
    while (my ($k, $v) = each %$hash) {
        return $k if $v eq $target;
    }
    return undef;
}

# Array of hashrefs
sub sort_users {
    my ($users) = @_;
    return sort { $a->{name} cmp $b->{name} } @$users;
}

sub filter_by_role {
    my ($users, $role) = @_;
    return grep { exists $_->{roles} && grep { $_ eq $role } @{ $_->{roles} } } @$users;
}

# Nested array dereference
sub flatten {
    my ($nested) = @_;
    return map { ref $_ eq 'ARRAY' ? @$_ : $_ } @$nested;
}

# reftype / blessed
sub describe_ref {
    my ($ref) = @_;
    my $type  = reftype($ref) // 'not a ref';
    my $class = blessed($ref) // 'unblessed';
    return "$class ($type)";
}

# Weak references
sub make_linked_nodes {
    my ($val_a, $val_b) = @_;
    my $node_a = { value => $val_a };
    my $node_b = { value => $val_b, prev => $node_a };
    $node_a->{next} = $node_b;
    weaken($node_b->{prev});
    return ($node_a, $node_b);
}

# Hash slice
sub pick_fields {
    my ($hash, @fields) = @_;
    return map { $_ => $hash->{$_} } grep { exists $hash->{$_} } @fields;
}

# Array slice
sub first_n {
    my ($arr, $n) = @_;
    return @{$arr}[0 .. $n - 1];
}

# Scalar ref
sub make_counter_ref {
    my ($start) = @_;
    my $count = $start // 0;
    return \$count;
}

sub increment {
    my ($ref) = @_;
    $$ref++;
}

1;
