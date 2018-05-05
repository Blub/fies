package Sections;

use strict;
use warnings;

use Text::Wrap ();

our $makedepends;

sub openfile($$$) {
	my ($includes, $mode, $path) = @_;
	my $fh;
	if ($path =~ m@^/@) {
		open($fh, $mode, $path)
			or die "failed to open $path: $!\n";
		return ($fh, dirname($path), $path);
	}
	for my $dir (reverse @$includes) {
		next if !defined $dir;
		my $p = "$dir/$path";
		if (open(my $fh, $mode, $p)) {
			my $addinc = undef;
			if ($path =~ m@/@) {
				$addinc = "$p/".dirname($path);
			}
			return ($fh, $addinc, $p);
		}
	}
	die "failed to open $path: $!\n";
}

sub __wrap {
	local $Text::Wrap::huge = 'wrap';
	local $Text::Wrap::columns = shift;
	local $Text::Wrap::unexpand = 0;
	return Text::Wrap::wrap(@_);
}

sub __option_parse_opt($) {
	my ($opt) = @_;
	my ($opts, $arg) = ($opt =~ /^\s*(\S+(?:\s*,\s*\S+)*)\s*(\w+)?\s*$/);
	die "failed to parse option: $opt\n" if !$opts;
	return ([split(/\s*,\s*/, $opts)], $arg);
}

sub __option_parse($$) {
	my ($include_dirs, $fh) = @_;
	my $data = [];

	my @fhstack;

	my $cur = {};
	my $dest = 'long';

	my $reset = sub {
		$cur = {};
		$dest = 'long';
	};

	my $done = sub {
		return if !$cur->{opts};
		if (defined(my $only = $cur->{only})) {
			if ($only eq 'auto') {
				if (!exists($cur->{short})) {
					$cur->{only} = 'doc';
				} elsif (!exists($cur->{long})) {
					$cur->{only} = 'cli';
				} else {
					die "ambiguous automatic \\only flag\n";
				}
			}
		}
		chomp $cur->{short} if exists($cur->{short});
		chomp $cur->{long} if exists($cur->{long});
		push @$data, $cur;
		$reset->();
	};

	nextfh:
	while (defined(my $line = <$fh>)) {
		chomp $line;
		if ($line =~ /^\\opt\s+(.+)$/) {
			$done->();
			my ($opts, $arg) = __option_parse_opt($1);
			$cur->{opts} = $opts;
			$cur->{arg} = $arg if defined($arg);
		} elsif ($line =~ /^\\include\s+(\S+)\s*$/) {
			my $file = $1;
			my ($incfh, $addinc) =
			    openfile($include_dirs, '<', $file);
			push @fhstack, $fh;
			push @$include_dirs, $addinc;
			$fh = $incfh;
		} elsif ($line =~ /^\\only(?:\s+(\w+))?\s*$/) {
			$cur->{only} = $1 // 'auto';
		} elsif ($line =~ /^\\short\s+(.+)$/) {
			$cur->{short} = $1;
		} elsif ($line =~ /^\\(short|long)\s*$/) {
			$dest = $1;
		} elsif ($line =~ /^\\/) {
			die "unknown command: $line\n";
		} else {
			$cur->{$dest} //= '';
			if ($dest eq 'short') {
				$line =~ s/^\s+//;
				$line =~ s/\s+$//;
				$cur->{$dest} .= ' ' if length($cur->{$dest});
			}
			$cur->{$dest} .= $line;
			if ($dest ne 'short') {
				$cur->{$dest} .= "\n";
			}
		}
	}
	if (@fhstack) {
		close($fh);
		pop @$include_dirs;
		$fh = pop @fhstack;
		goto nextfh;
	}

	$done->();

	return $data;
}

sub option_display_rst_entry($) {
	my ($entry) = @_;
	return '' if ($entry->{only}//'doc') ne 'doc';

	my @opt = @{$entry->{opts}};
	my $arg = $entry->{arg};

	@opt = map{"``$_``"} @opt;

	my $text = join(', ', @opt);
	$text .= "\\ *$arg*" if defined($arg);
	$text .= "\n";

	if (defined(my $long = $entry->{long})) {
		$text .= $long."\n";
		#return(__wrap(80, '    ', '    ', $long), "\n");
	}

	return $text;
}

sub option_display_rst {
	my ($fd, $data) = @_;
	for my $entry (@$data) {
		print {$fd} option_display_rst_entry($entry);
	}
}

sub option_c_entry_optstring($) {
	my ($entry) = @_;
	my @opt = @{$entry->{opts}};
	my $arg = $entry->{arg};

	my $optstring = join(',', @opt);
	if (defined($arg)) {
		$optstring .= ' ' if substr($optstring, -1) !~ /^[=-]$/;
		$optstring .= $arg;
	}
	return $optstring;
}

sub option_display_c_entry($$) {
	my ($entry, $longest) = @_;
	my $optstring = option_c_entry_optstring($entry);
	my $firstindent = "  $optstring "
	                . (' ' x ($longest - length($optstring)));
	my $subindent = ' ' x length($firstindent);
	return __wrap(74, $firstindent, $subindent, $entry->{short})."\n";
}

sub option_display_c {
	my ($fd, $data) = @_;
	my $text = '';

	my $longest = 0;
	my @c_data;
	for my $entry (@$data) {
		die "missing option names in entry\n"
			if !@{$entry->{opts}};
		next if ($entry->{only}//'cli') ne 'cli';
		next if !exists($entry->{short});

		my $optlen = length(option_c_entry_optstring($entry));
		$longest = $optlen if $optlen > $longest;
		push @c_data, $entry;
	}
	print {$fd} "\"\\\n";
	for my $entry (@c_data) {
		my $text = option_display_c_entry($entry, $longest);
		$text =~ s/\n/\\n\\\n/g;
		print {$fd} $text;
	}
	print {$fd} "\"\n";
}

my %option_formatter = (
	rst => \&Sections::option_display_rst,
	c => \&Sections::option_display_c
);

sub option_format($$$$) {
	my ($include_dirs, $fmtname, $infd, $outfd) = @_;
	my $fmt = $option_formatter{$fmtname};
	die "unknown option formatter: $fmtname\n" if !$fmt;
	my $data = __option_parse($include_dirs, $infd);
	$fmt->($outfd, $data);
}

sub sections_pipe($$$) {
	my ($include_dirs, $infd, $outfd) = @_;
	while (defined(my $line = <$infd>)) {
		if ($line =~ /\\OPTIONS (\w+) ([a-zA-Z0-9_\-.]+)\s*$/) {
			my $incfile = $2;
			my ($ofd, $addinc, $realpath) =
			    openfile($include_dirs, '<', $incfile);
			print {$makedepends->[0]}
			    "$makedepends->[1]: $realpath\n"
			    if defined $makedepends;
			push @$include_dirs, $addinc;
			eval { option_format($include_dirs, $1, $ofd, $outfd) };
			my $err = $@;
			pop @$include_dirs;
			close($ofd);
			die $err if $err;
		} else {
			print {$outfd} $line;
		}
	}
}

1;
