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
# This script generates a list of static dependencies for files in a ROM

use strict;
use Getopt::Long;

my $inverted_table = 0;
my $existing_file;
GetOptions(
  "i|invert" => \$inverted_table,   # add the inverted table
  "u|update=s" => \$existing_file,  # update existing file
  );

my @existinglines;
if ($existing_file)
	{
	open EXISTING, "<$existing_file" or die("Cannot open $existing_file: $!\n");
	print STDERR "Reading existing dependencies from $existing_file\n";
	@existinglines = <EXISTING>;
	close EXISTING;
	}

my %romfiles;
my @contents;

my $line;
while ($line = <>)
	{
  my ($romfile,$hostfile,$ibyfile,$package,$cmd,@columns) = split /,/, $line;		# first 5 fields are guaranteed to be simple
	next if (!defined $hostfile);
	next if ($romfile eq "ROM file");		# skip header line
	
	if (lc $cmd eq "stem")
		{
		$hostfile =~ s/(\/|\\)([^\\\/]+)$/$1stem_$2/;			# use stem version instead
		}
	push @contents, "$romfile\t$hostfile";
	$romfiles{lc $romfile} = $romfile;
	}

sub canonical_romfile($)
	{
	my ($romfile) = @_;
	my $canonical = $romfiles{lc $romfile};
	return $canonical if (defined $canonical);

	# New romfile not seen before - add to table
	$romfiles{lc $romfile} = $romfile;	# set the standard for others!
	return $romfile;	
	}

my %outputlines;
foreach my $existingline (@existinglines)
	{
	chomp $existingline;
	my ($romfile, $hostfile, $deps) = split /\t/, $existingline;
	if (defined $deps)
		{
		$romfile = canonical_romfile($romfile);
		$outputlines{$romfile} = "$romfile\t$hostfile\t$deps";
		}
	}

my %dependents;

sub print_dependency($$@)
	{
	my ($romfile,$hostfile,@dependencies) = @_;
	$outputlines{$romfile} = "$romfile\t$hostfile\t". join(":",@dependencies);
	
	next unless $inverted_table;
	
	# Create inverted table 
	foreach my $dependent (@dependencies)
		{
		next if ($dependent =~ /^sid=/);
		$dependent = lc $dependent;
		
		$dependent =~ s/^sys\\bin\\//;	# no directory => sys\bin anyway
		$dependent =~ s/\[\S+\]//;	# ignore the UIDs for now
		
		if (!defined $dependents{$dependent})
			{
			$dependents{$dependent} = $romfile;
			}
		else
			{
			$dependents{$dependent} .= "\t$romfile";
			}
		}
	}

sub generate_elftran_dependencies($$)
	{
	my ($romfile,$hostfile) = @_;
	
	return if ($hostfile =~ /\/z\//); 	# data files in armv5\urel\z
	return if ($hostfile =~ /\.sis$/);	# not an e32 image file

	my @elftran = `elftran $hostfile`;
	
	my $sid;
	my @imports;
	foreach my $line (@elftran)
		{
		# 2 imports from backend{00010001}[102828d5].dll
		# 17 imports from dfpaeabi{000a0000}.dll
		if ($line =~ /imports from (\S+)\{.{8}\}(\S+)$/)
			{
			push @imports, $1.$2;
			next;
			}
		if ($line =~ /^Secure ID: (\S+)$/)
			{
			$sid = $1; 	# presumably owns private/$sid  and various $sid.etxn files
			next;
			}
		}
	print_dependency($romfile,$hostfile,"sid=$sid",@imports);
	}

sub find_exe_names_dependencies($$)
	{
	my ($romfile,$hostfile) = @_;
	
	my @strings = `$ENV{"SBS_HOME"}\\win32\\mingw\\bin\\strings $hostfile`;
	
	my %executables;
	foreach my $string (@strings)
		{
		if ($string =~ /^(.*\\)?([^-\\]+\.(exe|dll))$/i)
			{
			my $exename = $2;
			$exename =~ s/^\s+//;		# strip off leading whitespace (e.g.length byte before "clock.exe")
			$exename = canonical_romfile("sys\\bin\\$exename");	# get the exact capitalisation
			# print STDERR "Found $exename in $string";
			$executables{$exename} = 1;
			}
		}
	if (%executables)
		{
		print_dependency($romfile,$hostfile,sort keys %executables);
		}
	else
		{
		print STDERR "No executable names found in system statup resource $hostfile\n";
		}
	}

sub find_dependency_in_sys_bin($$$)
	{
	my ($romfile,$hostfile,$basename) = @_;
	$basename = lc $basename;
	foreach my $extension (".exe",".dll")
		{
		my $dependency = "sys\\bin\\$basename$extension";
		if (defined $romfiles{$dependency})
			{
			print_dependency($romfile,$hostfile,$romfiles{$dependency});
			return;
			}
		}
	# grep in the contents list?
	# print_dependency($romfile,$hostfile,"unmatched sys\\bin\\$basename");
	}

foreach $line (@contents)
	{
	my ($romfile,$hostfile) = split /\t/, $line;
	
	if ($hostfile =~ /epoc32.release.arm/i)
		{
		generate_elftran_dependencies($romfile,$hostfile);
		next;
		}
	
	# App registration files
	if ($romfile =~ /private.10003a3f.*apps\\(.*)_reg\.rsc$/i)
		{
		my $dependency = "sys\\bin\\$1.exe";
		print_dependency($romfile,$hostfile,$dependency);
		next;
		}
	# app resources
	if ($romfile =~ /resource.apps\\(.*)(\.mif|\.mbm|\.rsc)$/)
		{
		my $executable = $1;
		$executable =~ s/_aif$//; 	# xxx_aif.mif
		$executable =~ s/_loc$//; 	# xxx_loc.rsc
		
		find_dependency_in_sys_bin($romfile,$hostfile,$executable);
		next;
		}

	# System state manager resource files
	if ($romfile =~ /private.2000d75b\\.*\.rsc$/i)
		{
		find_exe_names_dependencies($romfile,$hostfile);
		next;
		}
	
	# Assume that the rest don't depend on anything, and leave them out.
	}

foreach my $romfile ( sort keys %outputlines)
	{
	print $outputlines{$romfile}, "\n";
	}

if ($inverted_table)
	{
	print "\n";
	foreach my $inverted (sort keys %dependents)
		{
		print "x\t$inverted\t$dependents{$inverted}\n";
		}
	}
