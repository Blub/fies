#!/usr/bin/env perl

use strict;
use warnings;

use File::Basename qw(dirname);
use Sections;

sub usage() {
	die <<'EOF';
usage: section <command>
commands:
  parse [inputfile [outputfile]]
  format-options {rst | c} [inputfile [outputfile]]
EOF
}

my $cleanup;

my @include_dirs = ('.');

my %cmds = (
'format-options' => sub {
	my $infd = \*STDIN;
	my $outfd = \*STDOUT;

	if (!@ARGV) {
		usage();
	} elsif (@ARGV > 3) {
		print STDERR "section: too many parameters\n";
		usage();
	}

	my $type = shift @ARGV;

	if (@ARGV) {
		my $file = shift @ARGV;
		open($infd, '<', $file)
			or die "section: open($file) for reading: $!\n";
		push @include_dirs, dirname($file);
	}

	if (@ARGV) {
		my $outfile = $ARGV[0];
		$cleanup = sub { unlink($outfile) };
		open($outfd, '>', $outfile)
			or die "section: open($outfile) for writing: $!\n";
		shift @ARGV;
	}

	Sections::option_format(\@include_dirs, $type, $infd, $outfd);
},
parse => sub {
	my $infd = \*STDIN;
	my $outfd = \*STDOUT;

	my $makedep;

	if (@ARGV && $ARGV[0] =~ /^-M(.+)$/) {
		open(my $makefd, '>', $1)
			or die "open($1): $!\n";
		$makefd->autoflush;
		$makedep = [$makefd, 'stdin'];
		shift @ARGV;
	}

	if (@ARGV > 2) {
		print STDERR "section: too many parameters\n";
		usage();
	}

	if (@ARGV) {
		my $file = shift @ARGV;
		open($infd, '<', $file)
			or die "section: open($file) for reading: $!\n";
		push @include_dirs, dirname($file);
	}

	if (@ARGV) {
		$makedep->[1] = $ARGV[0] if defined($makedep);
		my $outfile = $ARGV[0];
		$cleanup = sub { unlink($outfile) };
		open($outfd, '>', $outfile)
			or die "section: open($outfile) for writing: $!\n";
		shift @ARGV;
	}

	local $Sections::makedepends = $makedep;
	Sections::sections_pipe(\@include_dirs, $infd, $outfd);
}
);

my $cmd = shift @ARGV;
while ($cmd =~ /^-I(.*)$/) {
	if (length($1)) {
		push @include_dirs, $1;
	} else {
		my $dir = shift(@ARGV) // die "missing argument for -I\n";
		push @include_dirs, $dir;
	}
	$cmd = shift @ARGV;
}
usage() if !$cmd;
if (!exists($cmds{$cmd})) {
	warn "unknown command: $cmd\n";
	usage();
}
eval {
	$cmds{$cmd}->();
	exit(0);
};
$cleanup->() if $cleanup;
die $@;
