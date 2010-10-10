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

my %romfiles;
my @contents;

my $line;
while ($line = <>)
	{
  my ($romfile,$hostfile,@columns) = split /,/, $line;		# first two fields are guaranteed to be simple
	next if (!defined $hostfile);
	next if ($romfile eq "ROM file");		# skip header line
	
	push @contents, "$romfile\t$hostfile";		# for use with "grep" later
	$romfiles{lc $romfile} = $romfile;
	}

my %dependents;

sub print_dependency($$@)
	{
	my ($romfile,$hostfile,@dependencies) = @_;
	print "$romfile\t$hostfile\t", join(":",@dependencies), "\n";
	
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
		if ($line =~ /imports from (\S+)\{000a0000\}(\S+)$/)
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

	# Assume that the rest don't depend on anything, and leave them out.
	}

print "\n";
foreach my $inverted (sort keys %dependents)
	{
	print "x\t$inverted\t$dependents{$inverted}\n";
	}
