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
use Getopt::Long;

my $deleted_lines_oby = "filtered.oby";
my $deletion_details_file = "filter.log";
GetOptions(
  "d|deleted=s" => \$deleted_lines_oby,   # file to hold the deleted lines
  "l|log=s" => \$deletion_details_file,   # log of whats deleted and why
  );

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
		$must_have{$romfile} = 1;
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

printf STDERR "%d in (including %d stem), %d out\n", 
	scalar keys %must_have,
	scalar keys %stem_substitutions, 
	scalar keys %deletions; 

# read static dependencies file
my %exe_to_romfile;     # exe -> original romfile
my %exe_dependencies;   # exe -> list of romfile
my %exe_prerequisites;  # exe -> list of exe
my %lc_romfiles;

my $line;
open STATIC_DEPENDENCIES, "<$static_dependencies_txt" or die ("Cannot read $static_dependencies_txt: $!\n");
while ($line = <STATIC_DEPENDENCIES>)
	{
	chomp $line;
	my ($romfile, $hostfile, $stuff) = split /\t/, $line;

	next if ($romfile eq "x");	# pre-inverted dependency information
	
	if (defined $stem_substitutions{$romfile})
		{
		if ($hostfile !~ /\/stem_/)
			{
			print STDERR "Ignoring dependencies of $hostfile because of stem substitution of $romfile\n";
			next;
			}
		}

	$lc_romfiles{lc $romfile} = $romfile;
	
	my $romexe = "";
	if ($romfile =~ /^sys.bin.(.*)$/i)
		{
		$romexe = lc $1;
		$exe_to_romfile{$romexe} = $romfile;
		}

	my @prerequisite_exes = ();
	foreach my $prerequisite (split /:/,$stuff)
		{
		next if ($prerequisite =~ /^sid=/);	# not a real file
		$prerequisite =~ s/^sys.bin.//;	# remove leading sys/bin, if present
		if ($prerequisite !~ /\\/)
			{
			my $exe = lc $prerequisite;
			$exe =~ s/\[\S+\]//;	# ignore the UIDs for now
		
			push @prerequisite_exes, $exe;
			if (!defined $exe_dependencies{$exe})
				{
				my @dependents = ($romfile);
				$exe_dependencies{$exe} = \@dependents;
				}
			else
				{
				push @{$exe_dependencies{$exe}}, $romfile;
				}
			}
		}
	if ($romexe ne "")
		{
		$exe_prerequisites{$romexe} = \@prerequisite_exes;
		}
	}
close STATIC_DEPENDENCIES;

# Add static dependencies for aliases

my @obylines = <>;	# read the oby file

foreach my $line (@obylines)
	{
	if ($line =~ /^\s*alias\s+(\S+)\s+(\S+)\s*$/)
		{
		my $romfile = $1;
		my $newname = $2;

		$romfile =~ s/^\\sys/sys/;	# remove leading \, to match $romfile convention
		next if (!defined $lc_romfiles{lc $romfile});		# ignore aliases to non-executables
		$romfile = $lc_romfiles{lc $romfile};

		$newname =~ s/^\\sys/sys/;	# remove leading \, to match $romfile convention
		$lc_romfiles{lc $newname} = $newname;
		
		if ($romfile =~ /^sys.bin.(\S+)$/i)
			{
			my $realexe = lc $1;
			push @{$exe_dependencies{$realexe}}, $newname;		# the alias is a dependent of the real file
			
			if ($newname =~ /^sys.bin.(\S+)$/i)
				{
				my $newexe = lc $1;
				my @prerequisite_exes = ($realexe);
				$exe_prerequisites{$newexe} = \@prerequisite_exes;
				}

			# print STDERR "added $newname as a dependent of $realexe\n"
			}
		}
	}

if (0)
	{
	foreach my $exe ("libopenvg.dll", "libopenvg_sw.dll", "backend.dll")
		{
		printf STDERR "Dependents of %s = %s\n", $exe, join(", ", @{$exe_dependencies{$exe}});
		}
	}

# process the "out" commands to recursively expand the deletions

my @details;
sub print_detail($)
	{
	my ($message) = @_;
	push @details, $message;
	print STDERR $message, "\n";
	}

my @problems;	# list of romtrails which will be a problem
sub delete_dependents($$$)
	{
	my ($romtrail,$original_reason,$listref) = @_;
	my ($romfile,$trail) = split /\t/, $romtrail;
	
	if (defined $deletions{$romfile})
		{
		# already marked for deletion
		return;
		}
	
	if (defined $must_have{$romfile})
		{
		# Problem! We won't be able to build this ROM
		print_detail("WARNING: $romfile is being kept, but will fail to link because of deletion trail $trail");
		push @problems, $romtrail;
		# keep that file and see what happens anyway
		return;
		}

	push @details, sprintf "deleting %s (%s)", scalar $romfile, $trail;
	
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
		  elsif ($deletions{$romfile} eq "out")
				{
				print_detail("NOTE: direct deletion of $romfile is not needed - it would be removed by $original_reason");
				}
			}
		}
	}

my @delete_cmds = sort keys %deletions;
foreach my $romfile (@delete_cmds)
	{
	push @details, "", "===Deleting $romfile", "";

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
my @deleted_lines;

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
			push @deleted_lines, $line;
			next;
			}
		
		if (defined $stem_substitutions{$romfile})
			{
			if ($hostfile =~ /^(.*(\/|\\))([^\\\/]+)$/)
				{
				my $path=$1;
				my $filename=$3;
				if ($filename !~ /^stem_/)
					{
					# print STDERR "Applying stem_ prefix to $hostfile in $line\n";
					$hostfile = "${path}stem_$filename";
					$stem_count++;
					}
				}
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
			push @deleted_lines, $line;
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
			push @deleted_lines, $line;
			next;
			}
		else
			{
			# print STDERR "$romfile is not deleted - saving $line\n";
			}
		}
	
	# patchdata  sys\bin\eiksrv.dll addr 0x0000c944 4 5
	# patchdata rawip.dll@KRMtuIPv6 0x578
	if ($line =~ /^\s*patchdata\s*(\S+)(\s*@|\s+addr)/i)
		{
		my $romfile = $1;
		$romfile = "sys\\bin\\$romfile" if ($romfile !~ /\\/);
		$romfile =~ s/^\\//;	# remove leading \, to match $romfile convention
		$romfile = $lc_romfiles{lc $romfile};
		
		# print STDERR "deleting patchdata line for $romfile\n";
		if ($deletions{$romfile})
			{
			# don't count these lines as deletions - they are just extra lines relating to deleted files.
			push @deleted_lines, $line;
			next;
			}
		}
	print $line,"\n";
	}

print_detail("Applied $stem_count stem substitutions and deleted $deletion_count rom files");

# Caculate what else could be removed

my %must_have_exes;
sub mark_prerequisites($);	# prototype for recursion
sub mark_prerequisites($)
	{
	my ($exe) = @_;
	return if (defined $must_have_exes{$exe});	# already marked

	$must_have_exes{$exe} = 1;
	if (!defined $exe_prerequisites{$exe})
		{
		# printf STDERR "$exe has no prerequisites to be marked!\n";
		return;
		}
	foreach my $prerequisite (@{$exe_prerequisites{$exe}})
		{
		mark_prerequisites($prerequisite);
		}
	}

foreach my $romfile (keys %must_have)
	{
	if ($romfile =~ /^sys.bin.(.*)$/i)
		{
		my $exe = lc $1;
		mark_prerequisites($exe);
		}
	}
printf STDERR "Minimum ROM now has %d exes\n", scalar keys %must_have_exes;

sub count_dependents($$);	# prototype for recursion
sub count_dependents($$)
	{
	my ($exe, $hashref) = @_;
	return if (defined $$hashref{$exe});

	$$hashref{$exe} = 1;
	foreach my $dependent (@{$exe_dependencies{$exe}})
		{
		next if ($dependent =~ /^sid=/);
		if ($dependent =~ /^sys.bin.(.*)$/i)
			{
			my $depexe = lc $1;
			count_dependents($depexe, $hashref);
			}
		}
	}

my @deletion_roots;
foreach my $exe (sort keys %exe_dependencies)
	{
	next if (defined $must_have_exes{$exe});
	my %dependents;
	my $deletion_root = "";
	foreach my $prerequisite (@{$exe_prerequisites{$exe}})
		{
		next if (defined $must_have_exes{$prerequisite});
		$deletion_root = $prerequisite;	# at least one prerequisite is not a must_have, so will delete this exe if removed
		last;
		}
	if (defined $deletions{$exe_to_romfile{$exe}})
		{
		if ($deletion_root ne "")
			{
			#print STDERR "Explicit deletion of $exe is not efficient - $deletion_root would remove it\n";
			}
			next;	# no need to report this one
		}
	next if ($deletion_root ne "");
	
	count_dependents($exe, \%dependents);
	my $count = scalar keys %dependents;
	push @deletion_roots, sprintf "%-4d\t%s", scalar keys %dependents, $exe;
	}

my $deleted_lines_oby = "filtered.oby";
my $deletion_details_file = "filter.log";

if ($deleted_lines_oby && scalar @deleted_lines)
	{
	print STDERR "Writing deleted lines to $deleted_lines_oby\n";
	open FILE, ">$deleted_lines_oby" or die("Unable to write to file $deleted_lines_oby: $!\n");
	foreach my $line (@deleted_lines)
		{
		print FILE $line, "\n";
		}
	close FILE;
	}

if ($deletion_details_file && scalar (@details, @problems, @deletion_roots))
	{
	print STDERR "Writing deletion details to $deletion_details_file\n";
	open FILE, ">$deletion_details_file" or die("Unable to write to file $deletion_details_file: $!\n");
	foreach my $line (@details)
		{
		print FILE $line, "\n";
		}
		
	print FILE "\n====\n";
	foreach my $deletion_root (sort {$b <=> $a} @deletion_roots)
		{
		my ($count,$exe) = split /\s+/, $deletion_root;
		printf FILE "Deleting %s would remove %d files\n", $exe, $count;
		}	

	print FILE "\n====\n";
	foreach my $problem (sort @problems)
		{
		my ($romfile, $trail) = split /\t/, $problem;
		print FILE "$romfile depends on removed files $trail\n"
		}
	close FILE;
	}

