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
# Check in Mercurial for updates to files branched to make stem versions

use strict;
use Getopt::Long;

my $old_rev = "PDK_3.0.2";
my $new_rev = "PDK_3.0.4";
my $mercurial_tree = "Q:";
GetOptions(
  "old=s" => \$old_rev,             # revision which is the current basis for modified files
  "new=s" => \$new_rev,             # new revision we need to catch up to
  "hgtree=s" => \$mercurial_tree,   # location of a tree of mercurial repositories
  );

sub do_system(@)
	{
	my @args = @_;
	my $cmd = join(" ", @args);
	# print STDERR "* $cmd\n";
	my @lines = `$cmd`;
	# print STDERR join("    ", "", @lines);
	return @lines;
	}

my $line;
while ($line = <>)
	{
	chomp $line;
	my ($localdir,$repo,$path,$file) = split /\t/, $line;
	next if (!defined $file);
	
	my @output = do_system ("hg", "--cwd", "$mercurial_tree$repo", "status", "--rev", $old_rev, "--rev", $new_rev, "$path/$file");
	next if (!@output);	# must be unchanged
	
	# what's the difference?
	my @diff_output = do_system ("hg", "--cwd", "$mercurial_tree$repo", "diff", "--rev", $old_rev, "--rev", $new_rev, "$path/$file");
	next if (!@diff_output); # none
	
	print "\n$localdir/$file might need updating\n", @diff_output;
	}

