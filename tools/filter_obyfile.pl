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
# This script filters an OBY file given a rom_content.csv and a static_dependencies.txt

use strict;

if (scalar @ARGV < 2)
	{
	die ("Must specify at least two arguments: rom_content.csv and static_dependencies.txt");
	}

my $rom_content_csv = shift @ARGV;
my $static_dependencies_txt = shift @ARGV;

open ROM_CONTENT, "<$rom_content_csv" or die("Cannot read $rom_content_csv: $!\n");
my @rom_content = <ROM_CONTENT>;
close ROM_CONTENT;

my $rom_content_header = shift @rom_content;
die("Not a valid rom_content.csv file") if ($rom_content_header !~ /^ROM file,/);

# read through the rom_content_csv looking for direct instructions
my %stem_substitutions;
my %deletions;
my %must_have;
foreach my $line (@rom_content)
	{
	my ($romfile,$hostfile,$ibyfile,$package,$cmd,@rest) = split /,/, $line;
	
	next if ($cmd eq "");

	$cmd = lc $cmd;
	if ($cmd eq "stem")
		{
		$stem_substitutions{$romfile} = $hostfile;
		next;
		}
	
	if ($cmd eq "out")
		{
		$deletions{$romfile} = "";
		next;
		}
	
	if ($cmd eq "in")
		{
		$must_have{$romfile} = 1;
		next;
		}
	}

printf STDERR "%d stem, %d out, %d in\n", 
	scalar keys %stem_substitutions, 
	scalar keys %deletions, 
	scalar keys %must_have;

# read static dependencies file
# process the "out" commands to recursively expand the deletions

# read the oby file and apply the commands

my $line;
while ($line = <>)
	{
	chomp $line;
	if ($line =~ /^(.*=)(\S+\s+)(\S+)(\s.*)?$/)
		{
		my $romcmd = $1;
		my $hostfile = $2;
		my $romfile = $3;
		my $rest = $4;
		$rest = "" if (!defined $rest);
		
		if ($romfile =~ /^"(.*)"$/)
			{
			$romfile = $1;
			$hostfile .= '"';
			$rest = '"'. $rest;
			}
		
		next if ($deletions{$romfile});
		
		if (defined $stem_substitutions{$romfile})
			{
			# print STDERR "Applying stem_ prefix to $hostfile in $line\n";
			$hostfile =~ s/(\/|\\)([^\\\/]+)$/$1stem_$2/;
			}
		print $romcmd, $hostfile, $romfile, $rest, "\n";
		next;
		}
	
	print $line,"\n";
	}