#! /usr/bin/env perl
#

use strict;

my $test_exec = './globus-gram-protocol-pack-test';

my $gpath = $ENV{GLOBUS_LOCATION};

if (!defined($gpath))
{
    die "GLOBUS_LOCATION needs to be set before running this script"
}

@INC = (@INC, "$gpath/lib/perl");

my @tests;

sub test
{
    my ($errors,$rc) = ("",0);
    my ($arg) = shift;
    my $output;
    my $valgrind = '';
    my @args = ($test_exec, $arg);
    my $testname = $test_exec;
    $testname =~ s|^\./||;

    if (exists $ENV{VALGRIND})
    {
        my @valgrind_args = ();
        push(@valgrind_args, "valgrind");
        push(@valgrind_args, "--log-file=VALGRIND-$testname.log");
        if (exists $ENV{VALGRIND_OPTIONS})
        {
            push(@valgrind_args, split(/\s+/, $ENV{VALGRIND_OPTIONS}));
        }
        unshift(@args, @valgrind_args);
    }

    system(@args);
    return $?>> 8;
}

push(@tests, "test(1)");
push(@tests, "test(2)");
push(@tests, "test(3)");
push(@tests, "test(4)");
push(@tests, "test(5)");
push(@tests, "test(6)");

printf "1..%d\n", scalar(@tests);

foreach (@tests)
{
    eval "&$_";
}