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
# This package is to identify the packages which contributed to a ROM image
# dir /s/b build_info\logs\releaseables\*.tsv | perl packages_in_rom.pl romfile.dir -

use strict;

my %romfiles;
my %ibyfiles;

sub scan_rom_dir_file($)
	{
	my ($romdirfile) = @_;
	
  my $line;
  open FILE, "<$romdirfile" or print "Failed to read $romdirfile: $!\n" and return;
  while ($line = <FILE>)
  	{
  	next if ($line !~ /\t/);
  	
  	chomp $line;
  	my ($romdest,$srcfile,$ibyfile) = split /\t/,$line;
  	$romdest =~ s/\s*$//;		# strip off trailing spaces
  	$romdest =~ s/\//\\/g; 	# convert to EPOC directory separators
  	
    $srcfile =~ s/\\/\//g;	# Unix directory separators please
    $srcfile =~ s/^"(.*)"$/$1/; 	# Remove quotes
    $srcfile =~ s/\/\//\//g; 	# Convert // to /
    $srcfile =~ s/^\[0x[0-9a-fA-F]+\]//;	# remove HVID, if present
    $srcfile =~ s/mbm_rom$/mbm/i; 	# use original name of derived file
    $srcfile =~ s/(z\/)system\/..\//$1/i; 	# z/system/../private
    $srcfile = lc $srcfile; 	# sigh...
    
    $ibyfile =~ s/\\/\//g;	# Unix directory separators please
    $ibyfile =~ s/^.*\/rom\/include\///; 	# normalise path to rom\include
    
  	$romfiles{$srcfile} = $romdest;
  	$ibyfiles{$romdest} = $ibyfile;
		}
	close FILE;
	}

my %packages;
my %packagenames;
my %package_by_romfile;

sub scan_tsv($)
  {
  my ($tsvfile) = @_;
  return if ($tsvfile !~ /\/(([^\/]+)\/([^\/]+))\/info.tsv$/i);
  my $packagename = $1;
  $packagename =~ s/\\/\//g;
  $packagenames{$packagename} = 1;
  
  my $line;
  open FILE, "<$tsvfile" or print "Failed to read $tsvfile: $!\n" and return;
  while ($line = <FILE>)
    {
    chomp $line;
    my ($filename,$type,$config) = split /\t/,$line;
    
    $filename = lc $filename;		# sigh...
    if (defined $romfiles{$filename})
    	{
    	$package_by_romfile{$filename} = $packagename;
    	}
    }
  }

my $romlisting = shift @ARGV;
scan_rom_dir_file($romlisting);

my $tsvfile;
while ($tsvfile = <>)
  {
  chomp $tsvfile;
  if ($tsvfile =~ /info.tsv$/)
    {
    $tsvfile =~ s/\\/\//g;
    scan_tsv($tsvfile);
    }
  }

print "ROM file,Host file,Iby file,Package,In/Out,Who,Why\n";
my $unknowns = 0;
foreach my $file (sort keys %romfiles)
	{
	my $romfile = $romfiles{$file};
	my $ibyfile = $ibyfiles{$romfile};
	my $package = $package_by_romfile{$file};

	if (!defined $package)
		{
		$package = "(unknown)";
		$package_by_romfile{$file} = $package;
		$unknowns += 1;
		}
	print join(",", $romfile, $file, $ibyfile, $package, "","",""), "\n";
	}
printf STDERR "\n%d files in %s, %d unknowns\n", scalar keys %romfiles, $romlisting, $unknowns;
