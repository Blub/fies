#!/usr/bin/env perl

use strict;
use warnings;

my $rc = 0;

sub err($) {
	my ($msg) = @_;
	$rc = 1;
	warn $msg;
}

sub check_file($) {
	my ($file) = @_;
	open(my $fh, '<', $file)
		or die "open($file): $!\n";
	my $lno = 1;
	while (defined(my $line = <$fh>)) {
		check_line($file, $lno, $line);
		++$lno;
	}
	close($fh);
}

sub check_line($$$) {
	my ($file, $lno, $line) = @_;
	chomp($line);
	if ($line =~ /[^\t]\t/) {
		err "$file:$lno: illegal tab following a non-tab\n"
			unless $line =~ m@^\s*//@;
	}
	$line =~ s/\t/        /g;
	err "$file:$lno: line longer than 80 columns: ".length($line)."\n"
		if length($line) > 80;
}

check_file($_) for @ARGV;
exit($rc);
