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
# This script fixes rombuild logfiles which elf4rom doesn't like

use strict;

my $line;
my $skipping = 0;
while ($line = <>)
	{
	if ($line =~ /^PageSize:/)
		{
		print $line;
		$skipping = 1;
		next;
		}
	if ($line =~ /^Variant /)
		{
		print $line;
		$skipping = 0;
		next;
		}
	
	next if ($skipping);
	print $line;
	}
