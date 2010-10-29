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
# This script filters kernel trace output to the bare essentials

use strict;

my %processes;
my %threadcount;
my %instancecount;
my %originalname;
my @deathlist;
my %loaded_exes;

my $line;
while ($line = <>)
	{
	# AddThread ekern.exe::NVMem-ecc10dce to ekern.exe
	# Process FLogSvr.exe Die: 0 0 Kill
	# DLibrary domainSrv.exe::domainpolicy2.dll Close m=-1
	# Thread MTMInit::Via Infrared Via Infrared   Panic MTMInit 5
	# DProcess::Rename MSexe.exe to !MsvServer
	# DCodeSeg::Create c809aac8 file Z:\SYS\BIN\BmpAnSrv.dll ver 000a0000 process EwSrv.exe
	# Thread sysstatemgr.exe::!CleSrv_22f5d001 Logon to process matrixmenu.exe, status at 00800a74 rdv=1
	
	if ( $line =~ /^(AddThread |Process \S+ Die: |DLibrary |Thread |DProcess::Rename |DCodeSeg::Create )/o)
		{
		
		if ($line =~ /^DCodeSeg::Create \S+ file z:\\sys\\bin\\(\S+) ver \S+ process (\S+)/io)
			{
			my $exe = $1;
			my $process = $2;
			my $truename = $originalname{$process};
			$truename = $process if (!defined $truename);
			$loaded_exes{"$exe\t$truename"} = 1;
			next;
			}
		if ($line =~ /^DProcess::Rename (\S+) to (\S+)/o)
			{
			my $oldname = $1;
			my $process = $2;
		printf "Renaming %s (%d,%d) to %s\n", $oldname, $processes{$oldname}, $threadcount{$oldname}, $process;
			$processes{$process} = $processes{$oldname};
			$threadcount{$process} = $threadcount{$oldname};
			$instancecount{$process} = $instancecount{$oldname};

			$originalname{$process} = $originalname{$oldname};
			delete $processes{$oldname};
			delete $threadcount{$oldname};
			}
		if ($line =~ /^AddThread (\S+)::(\S+) to (\S+)$/o)
			{
			my $process = $1;
			my $thread = $2;
			
			if ($thread eq "Main" || $thread eq "Null")
				{
				# New process created
				$processes{$process} = $.;
				$threadcount{$process} = 0;
				$originalname{$process} = $process;
				}
			$threadcount{$process} += 1;
			if (!defined $instancecount{$process})
				{
				$instancecount{$process} = 0;
				}
			$instancecount{$process} += 1;
			}
		print "$.: $line";

	# Thread sysstatemgr.exe::!CleSrv_22f5d001 Logon to process matrixmenu.exe, status at 00800a74 rdv=1
		if ($line =~ /^Thread (.*)::.* Logon to process ([^,]+),/o)
			{
			my $parentprocess = $originalname{$1};
			my $childprocess = $2;
			$loaded_exes{"$childprocess\t$parentprocess"} = 2;
			next;
			}
		if ($line =~ /^Process (\S+) Die: (.*)$/o)
			{
			my $process = $1;
			my $details = $2;
			my $summary = sprintf "#%d, %d threads, lifetime %d-%d",
				$instancecount{$process},$threadcount{$process},$processes{$process},$.;
			
			print "\t$process: $summary\n";
			delete $processes{$process};
			delete $threadcount{$process};

			chomp $line;	
			push @deathlist, sprintf "%7d\t%-20s %s, died %s", $., $process, $summary, $details;
			}
		next;
		}

	# Initiating transition to state 0000.ffff.
	# R:\sf\os\devicesrv\sysstatemgmt\systemstatemgr\cmd\src\ssmcommandbase.cpp 135: Completing command with errorcode 0
	# ***** Execute cmd no. 11 without a delay
	# Starting : Z:\sys\bin\splashscreen.exe with arguments :  and Execution behaviour : 1
	if ( $line =~ /^(Initiating transition |\*\*\*\*\* |Starting : )/o)
		{
		print "SSM: $line";
		next;
		}

	if ($line =~ /^(MODE_USR:)/o)
		{
		# it's crashed
		print $line, <>;
		last;
		}
	}

printf "\n\nActive processes (%d):\n", scalar keys %processes;
foreach my $process (sort keys %processes)
	{
	printf "%-30s\t%d threads, created at line %d\n", $process, $threadcount{$process}, $processes{$process};
	}

printf "\n\nDead processes (%d)\n", scalar @deathlist;
print join("\n", sort @deathlist, "");

printf "\n\nLoaded executables (%d)\n", scalar keys %loaded_exes;
foreach my $exepair (sort keys %loaded_exes)
	{
	my ($exe,$parent) = split /\t/, $exepair;
	printf "%-30s\t%s\n", $exe, $parent;
	}
