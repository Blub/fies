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

sub check_patch($) {
	my ($file) = @_;
	open(my $fh, '<', $file)
		or die "open($file): $!\n";
	my $lno = 0;
	my $state = 0;

	my $dstfile;
	my $hunk = 0;
	my $hunkat = 0;
	my $hunkadd = 0;
	my $hunksub = 0;
	my $hunkline = 0;

	my $subject;
	my $from;
	my %tags;

	my $malformed = sub {
		die "$file:$lno: malformatted unified diff\n"
		   . "[$state:$hunk:$hunkadd,$hunksub]\n";
	};
	my $checkstate = sub {
		$malformed->() if $state != $_[0];
		$state = $_[1];
	};
	eval {
	while (defined(my $line = <$fh>)) {
		chomp $line;
		++$lno;
		if ($line =~ /^---$/) {
			$checkstate->(0, 1);
		}
		elsif ($state == 0) {
			if ($line =~ /^From:\s*(.*)$/i) {
				$from = $1;
			}
			elsif ($line =~ /^Subject:\s*(.*)$/i) {
				$subject = $1;
			}
			elsif ($line =~ /^\s*([A-Z][\-A-Za-z]+):\s*(.*?)\s*$/) {
				push @{$tags{$1}}, $2;
			}
		}
		elsif ($line =~ /^diff/) {
			$malformed->() if $state != 1 && $state != 4;
			die "$file:$lno: unexpected end of hunk\n"
				if 0 != ($hunksub || $hunkadd);
			$state = 2;
		}
		elsif ($line =~ /^--- (\S.*?)\s*$/) {
			$checkstate->(2, 3);
			$dstfile = $1;
			$hunk = 0;
		}
		elsif ($line =~ /^\+\+\+ \S+/) {
			$checkstate->(3, 4);
		}
		elsif ($line =~ /^\@\@ -\d+,(\d+) \+(\d+),(\d+) \@\@/) {
			$checkstate->(4, 5);
			($hunksub, $hunkadd) = ($1, $3);
			$hunkline = $2;
			$hunkat = 0;
			++$hunk;
		}
		elsif ($state == 5) {
			if ($line =~ /^ /) {
				# OK
				++$hunkat;
				--$hunksub;
				--$hunkadd;
			}
			elsif ($line =~ /^-/) {
				++$hunkat;
				--$hunksub;
			}
			elsif ($line =~ /^\+(.*)$/) {
				++$hunkat;
				--$hunkadd;
				check_line("$file:$lno:$dstfile",
				           $hunkat+$hunkline,
				           $1);
			}
			else {
				die "$file:$lno: bad patch\n";
			}
			if ($hunksub == 0 && $hunkadd == 0) {
				$state = 4;
			}
		}
		else {
			# OK
		}
	}
	};
	if (my $err = $@) {
		err $err;
	}
	close($fh);

	err "$file: missing commit message (subject)\n"
		if !defined($subject);
	err "$file: commit header too long (> 70 characters)\n"
		if length($subject) > 70;
	err "$file: no sender found (From header)?\n"
		if !defined($from);
	if (my $signoffs = $tags{'Signed-off-by'}) {
		my $found = 0;
		for my $person (@$signoffs) {
			last if !defined($from);
			if ($person eq $from) {
				$found = 1;
				last;
			}
		}
		err "$file: commit is not signed off by the sender"
		  . " as indicated by the From header\n"
			if !$found;
	} else {
		err "$file: commit is not signed off\n"
		  . "There should be a 'Signed-off-by' line (case sensitive)\n";
	}
}

if (@ARGV && $ARGV[0] eq '--patch') {
	shift @ARGV;
	check_patch($_) for @ARGV;
	if (!$rc) {
		print "Patch has no obvious style problems\n" if @ARGV == 1;
		print "Patches have no obvious style problems\n" if @ARGV > 1;
	}
} else {
	check_file($_) for @ARGV;
}
exit($rc);
