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
		# print STDERR "Deletion request for >$romfile<\n";
		$deletions{$romfile} = "out";
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
my %exe_to_romfile;
my %lc_romfiles;

my $line;
open STATIC_DEPENDENCIES, "<$static_dependencies_txt" or die ("Cannot read $static_dependencies_txt: $!\n");
while ($line = <STATIC_DEPENDENCIES>)
	{
	chomp $line;
	last if ($line eq "");	# blank line between the two sections
	my ($romfile, $hostfile, $stuff) = split /\t/, $line;

	$lc_romfiles{lc $romfile} = $romfile;
	
	if ($romfile =~ /^sys.bin.(.*)$/i)
		{
		my $exe = lc $1;
		$exe_to_romfile{$exe} = $romfile;
		}
	}
my %exe_dependencies;
while ($line = <STATIC_DEPENDENCIES>)
	{
	chomp $line;
	my ($x, $exename, @dependencies) = split /\t/,$line;
	$exe_dependencies{$exename} = \@dependencies;
	}
close STATIC_DEPENDENCIES;

# Create static dependencies for aliases

my @obylines = <>;	# read the oby file

foreach my $line (@obylines)
	{
	if ($line =~ /^\s*alias\s+(\S+)\s+(\S+)\s*$/)
		{
		my $romfile = $1;
		my $newname = $2;

		$romfile =~ s/^\\sys/sys/;	# remove leading \, to match $romfile convention
		next if (!defined $lc_romfiles{lc $romfile});		# aliases to non-executables
		$romfile = $lc_romfiles{lc $romfile};

		$newname =~ s/^\\sys/sys/;	# remove leading \, to match $romfile convention
		$lc_romfiles{lc $newname} = $newname;
		
		if ($romfile =~ /^sys.bin.(\S+)$/i)
			{
			my $realexe = lc $1;
			push @{$exe_dependencies{$realexe}}, $newname;		# the alias is a dependent of the real file
			# print STDERR "added $newname as a dependent of $realexe\n"
			}
		}
	}

# foreach my $exe ("libopenvg.dll", "libopenvg_sw.dll")
# 	{
# 	printf STDERR "Dependents of %s = %s\n", $exe, join(", ", @{$exe_dependencies{$exe}});
# 	}

# process the "out" commands to recursively expand the deletions

sub delete_dependents($$$)
	{
	my ($romtrail,$original_reason,$listref) = @_;
	my ($romfile,$trail) = split /\t/, $romtrail;
	
	if (defined $deletions{$romfile})
		{
		# already marked for deletion
		if ($original_reason eq $romfile && $deletions{$romfile} ne $original_reason)
			{
			print STDERR "$romfile already deleted by removing $deletions{$romfile}\n";
			}
		return;
		}
	
	# We should keep the following information, but it's rather verbose
	# printf STDERR "  %d - deleting %s (%s)\n", scalar @{$listref}, $romfile, $trail;
	
	$deletions{$romfile} = $original_reason;	# this ensures that it gets deleted
	if ($romfile =~ /^sys.bin.(.*)$/i)
		{
		my $exe = lc $1;
		if (!defined $exe_dependencies{$exe})
			{
			# print STDERR "No dependencies for $exe ($romfile)\n";
			return;
			}
		foreach my $dependent (@{$exe_dependencies{$exe}})
			{
			if (!defined $deletions{$dependent})
				{
		  	push @{$listref}, "$dependent\t$romfile $trail";
		  	}
			}
		}
	}

my @delete_cmds = sort keys %deletions;
foreach my $romfile (@delete_cmds)
	{
	delete $deletions{$romfile}; 	# so that delete_dependents will iterate properly
	my @delete_list = ("$romfile\tout");
	while (scalar @delete_list > 0)
		{
		my $next_victim = shift @delete_list;
		delete_dependents($next_victim, $romfile, \@delete_list);
		}
	}

# read the oby file and apply the commands

my $stem_count = 0;
my $deletion_count = 0;
foreach my $line (@obylines)
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

		$lc_romfiles{lc $romfile} = $romfile;
		
		if ($deletions{$romfile})
			{
			$deletion_count++;
			next;
			}
		
		if (defined $stem_substitutions{$romfile})
			{
			# print STDERR "Applying stem_ prefix to $hostfile in $line\n";
			$hostfile =~ s/(\/|\\)([^\\\/]+)$/$1stem_$2/;
			$stem_count++;
			}
		print $romcmd, $hostfile, $romfile, $rest, "\n";
		next;
		}
	
	# __ECOM_PLUGIN(emulator directory, file rom dir, dataz_, resource rom dir, filename, resource filename)
	if ($line =~ /__ECOM_PLUGIN\(([^)]+)\)/)
		{
		my ($emudir, $romdir, $dataz, $resourcedir, $exename, $rscname) = split /\s*,\s*/, $1;
		my $romfile = $romdir. "\\". $exename;

		if ($deletions{$romfile})
			{
			# print STDERR "Deleted __ECOM_PLUGIN for $romfile\n";
			$deletion_count++;
			next;
			}		
		}
	if ($line =~ /^\s*alias\s+(\S+)\s+(\S+)\s*$/)
		{
		my $romfile = $1;
		my $newname = $2;
		
		$romfile =~ s/^\\sys/sys/;	# remove leading \, to match $romfile convention
		$romfile = $lc_romfiles{lc $romfile};
		if ($deletions{$romfile})
			{
			# delete the alias if the real file is marked for deletion
			$deletion_count++;
			next;
			}
		else
			{
			# print STDERR "$romfile is not deleted - saving $line\n";
			}
		}
	
	# patchdata  sys\bin\eiksrv.dll addr 0x0000c944 4 5
	if ($line =~ /^\s*patchdata\s*(\S+)/)
		{
		my $romfile = $1;
		$romfile =~ s/^\\//;	# remove leading \, to match $romfile convention
		$romfile = $lc_romfiles{lc $romfile};
		
		# print STDERR "deleting patchdata line for $romfile\n";
		if ($deletions{$romfile})
			{
			# don't count these lines as deletions - they are just extra lines relating to deleted files.
			next;
			}
		}
	print $line,"\n";
	}

print STDERR "Applied $stem_count stem substitutions and deleted $deletion_count rom files\n";