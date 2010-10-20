// Copyright (c) 1997-2009 Nokia Corporation and/or its subsidiary(-ies).
// All rights reserved.
// This component and the accompanying materials are made available
// under the terms of "Eclipse Public License v1.0"
// which accompanies this distribution, and is available
// at the URL "http://www.eclipse.org/legal/epl-v10.html".
//
// Initial Contributors:
// Nokia Corporation - initial contribution.
//
// Contributors:
//
// Description:
//

//************************************************************************************************************
//#include <bautils.h>
#include <profiler.h>
#include <e32debug.h>





//------------------------------------------------------------------------------------------------------------
void startProfilerServer()
   {
	RDebug::Print(_L("Startup Profiling - startProfilerServer()"));
	_LIT(KParam,"");
	RProcess p;
	TInt err;   
	
	// Run the Sampling Profiler
	err=p.Create(KProfilerName,KParam);
	if (err == KErrNone)
		{
		p.Resume();
		p.Close();
		User::After(1000000);
		}
	else
		{
		RDebug::Print(_L("Startup Profiling - ERROR %d: Unable to execute Sampling Profiler\n"), err);
		}
	}


	
//************************************************************************************************************
// PARSE PARAMETER
//************************************************************************************************************
TInt parseNumberL(TDes &aArgs, const TDesC &aParam)
   {
   TInt     pos, num, ret;
	TBuf<100> argsTmp;
   TLex     lexArgs;	   
   
   pos = aArgs.Find(aParam);
   if (pos == KErrNotFound)
      User::Leave(KErrArgument);

   argsTmp.Insert(0,aArgs);
   argsTmp.Delete(0,pos + aParam.Length());
   lexArgs=argsTmp;
   ret=lexArgs.Val(num);
   
   if (ret != KErrNone)
      User::Leave(KErrArgument);
   
   return num;
   }



   
//************************************************************************************************************
//************************************************************************************************************
//  MAIN
//************************************************************************************************************
//************************************************************************************************************
void doMainL()
	{
	RDebug::Print(_L("Startup Profiling - doMainL"));

	TBuf<100>       buf, args;
	TInt            waittime(300000000), err;

   _LIT(KParamTime,	"time=");
		
   // Get and prepare Command Line
#ifndef __SECURE_API__
   RProcess().CommandLine(args);
#else
   User::CommandLine(args);   
#endif
   args.LowerCase();
   

   // Parse param and decide what to do  
   if (args.Find(KParamTime) != KErrNotFound) 
		{
		waittime  = parseNumberL(args, KParamTime);  
		RDebug::Print(_L("Starting Profiler - Wait time requested: %d"), waittime);
		}
/*		
	// ECOM should be already running on a real phone. Does not in testshell mode
	REComSession ecom;
	ecom.OpenL();
	User::After(2000000);	      
*/
	// Start the profiler server
	startProfilerServer();
	
	
	err=Profiler::Start();
	if (err != KErrNone)
		{
		RDebug::Print(_L("Startup Profiling - ERROR %d : Unable to start Sampling Profiler.\n"),err);
		User::Leave(err);
		}
	else
		{
		RDebug::Print(_L("Startup Profiling - Profiler Started OK"));
		}
		
	// Now go to sleep until the alloted time (ie until we think boot-up should be complete)	
	User::After(waittime);
	RDebug::Print(_L("Startup Profiling - Time's Up, going to stop profiling now"));
	
	// Stop, close and unload the profiler properly
	err=Profiler::Stop();
	User::After(300000);
	err=err | Profiler::Close();
	User::After(300000);
	err=err | Profiler::Unload();
	User::After(300000);         
	if (err != KErrNone)
		{
		RDebug::Print(_L("Startup Profiling - ERROR %d : Unable to Stop/Close/Unload Sampling Profiler\n"),err);
		User::Leave(err);            
		}
	}



//************************************************************************************************************
//************************************************************************************************************
// Program Entry
//************************************************************************************************************
//************************************************************************************************************
GLDEF_C TInt E32Main()
	{	
	__UHEAP_MARK;
	CActiveScheduler* rootScheduler = new CActiveScheduler;
	CActiveScheduler::Install(rootScheduler);
	CTrapCleanup* theCleanup=CTrapCleanup::New();

	TRAPD(ret,doMainL());	
	
	delete theCleanup;	
	delete rootScheduler;
	__UHEAP_MARKEND;
	return(KErrNone);
	}
