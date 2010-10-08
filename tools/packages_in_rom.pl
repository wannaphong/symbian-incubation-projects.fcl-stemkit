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

sub scan_rom_dir_file($)
	{
	my ($romdirfile) = @_;
	
  my $line;
  open FILE, "<$romdirfile" or print "Failed to read $romdirfile: $!\n" and return;
  while ($line = <FILE>)
  	{
  	next if ($line !~ /\t/);
  	
  	chomp $line;
  	my ($romdest,$srcfile) = split /\t/,$line;
  	$romdest =~ s/\s*$//;		# strip off trailing spaces
    $srcfile =~ s/\\/\//g;	# Unix directory separators please
    
    $srcfile =~ s/^\[0x[0-9a-fA-F]+\]//;	# remove HVID, if present
    $srcfile = lc $srcfile; 	# sigh...
    
  	$romfiles{$srcfile} = $romdest;
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

print "File\tPackage\n";
my $unknowns = 0;
foreach my $file (sort keys %romfiles)
	{
	my $package = $package_by_romfile{$file};
	if (!defined $package)
		{
		$package = "(unknown)";
		$package_by_romfile{$file} = $package;
		$unknowns += 1;
		}
	printf "%s\t%s\n", $file, $package;
	}
printf "\n%d files in %s, %d unknowns\n", scalar keys %romfiles, $romlisting, $unknowns;
