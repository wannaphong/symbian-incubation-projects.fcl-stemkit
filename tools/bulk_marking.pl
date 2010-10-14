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

my $marking;
my $who;
GetOptions(
  "m|message=s" => \$marking,   # comment to add to the CSV lines
  "u|user=s" => \$who,          # value for the Who column
  );

die("must specify a value for the Why column with the -m option") if (!defined $marking);
die("must specify a value for the Who column with the -u option") if (!defined $who);

my %romfiles;
my $line;
while ($line=<>)
	{
	# PRIVATE\10003A3F\IMPORT\APPS\musui_reg.rsc	/epoc32/data/z/private/10003a3f/apps/musui_reg.rsc	sys\bin\musui.exe
	if ($line =~ /^\S+\t\S+\t(\S+)/)
		{
		my $romfile = $1;
		$romfiles{$romfile} = 1;
		next;
		}

	my ($romfile,$hostfile,$ibyfile,$package,$cmd,@rest) = split /,/, $line;
	next if (!defined $cmd);
	
	if (defined $romfiles{$romfile})
		{
		if ($cmd eq "")
			{
			# mark this one
			print join(",", $romfile,$hostfile,$ibyfile,$package,"",$who,$marking), "\n";
			next;
			}
		else
			{
			print STDERR "Skipping $romfile line - already marked as $cmd,",join(",", @rest);
			}
		}
	print $line;
	}
