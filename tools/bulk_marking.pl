# Copyright (c) 2010 Symbian Foundation Ltd.
# All rights reserved.
# This component and the accompanying materials are made available
# under the terms of the License "Eclipse Public License v1.0"
# which accompanies this distribution, and is available
# at the URL "http://www.eclipse.org/legal/epl-v10.html".
#
# Initial Contributors:
# Symbian Foundation - initial contribution.
#
# Contributors:
#
# Description: 
# Read a list of "static dependency" lines and tag them in rom_content.csv

use strict;
use Getopt::Long;

my $newcmd = "";
my $who;
my $marking;
my $other_exes = 0;
GetOptions(
  "c|command=s" => \$newcmd,    # value for the What colum (can be blank)
  "m|message=s" => \$marking,   # comment to add to the CSV lines
  "u|user=s" => \$who,          # value for the Who column
  "x" => \$other_exes,          # marking is applied to exes NOT listed
  );

die("must specify a value for the Why column with the -m option") if (!defined $marking);
die("must specify a value for the Who column with the -u option") if (!defined $who);

my %romfiles;
my $mark_count = 0;
my $line;
while ($line=<>)
	{
	# PRIVATE\10003A3F\IMPORT\APPS\musui_reg.rsc	/epoc32/data/z/private/10003a3f/apps/musui_reg.rsc	sys\bin\musui.exe
	if ($line =~ /^\S+\t\S+\t(\S+)/)
		{
		my $romfile = $1;
		$romfiles{lc $romfile} = 1;
		next;
		}

	my ($romfile,$hostfile,$ibyfile,$package,$cmd,@rest) = split /,/, $line;
	if (!defined $cmd)
		{
		if ($line =~ /^(\S+)$/)
			{
			# guess that this is a preserved executable
			my $exe = "sys\\bin\\". lc $1;
			$romfiles{$exe} = 1;
			# print STDERR "Preserving $exe\n";
			}
		next;
		}
	
	if ($cmd ne "" && $cmd !~ /_/)
		{
		print $line;	# already marked, so leave it alone
		next;
		}

	my $mark_me = 0;
	if ($other_exes)
		{
		if ($romfile =~ /^sys.bin.(.*)$/i)
			{
			# this is an exe - are we tagging it?
			if (!defined $romfiles{lc $romfile})
				{
				# print STDERR "Marking $romfile\n";
				$mark_me = 1;
				}
			}
		}
	elsif (defined $romfiles{lc $romfile})
		{
		$mark_me = 1;
		}

	if ($mark_me)
		{
		print STDERR "Overriding $cmd for $romfile\n" if ($cmd ne "" && lc $cmd ne lc $newcmd);
		$mark_count += 1;
		print join(",", $romfile,$hostfile,$ibyfile,$package,$newcmd,$who,$marking), "\n";
		}
	else
		{
		print $line;
		}
	}

print STDERR "Marked $mark_count lines\n";
