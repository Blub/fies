#!/usr/bin/env perl

use strict;
use warnings;

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
		open($infd, '<', $ARGV[0])
			or die "section: open($ARGV[0]) for reading: $!\n";
		shift @ARGV;
	}

	if (@ARGV) {
		my $outfile = $ARGV[0];
		$cleanup = sub { unlink($outfile) };
		open($outfd, '>', $outfile)
			or die "section: open($outfile) for writing: $!\n";
		shift @ARGV;
	}

	Sections::option_format($type, $infd, $outfd);
},
parse => sub {
	my $infd = \*STDIN;
	my $outfd = \*STDOUT;

	if (@ARGV > 2) {
		print STDERR "section: too many parameters\n";
		usage();
	}

	if (@ARGV) {
		open($infd, '<', $ARGV[0])
			or die "section: open($ARGV[0]) for reading: $!\n";
		shift @ARGV;
	}

	if (@ARGV) {
		my $outfile = $ARGV[0];
		$cleanup = sub { unlink($outfile) };
		open($outfd, '>', $outfile)
			or die "section: open($outfile) for writing: $!\n";
		shift @ARGV;
	}

	Sections::sections_pipe($infd, $outfd);
}
);

my $cmd = shift @ARGV;
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
