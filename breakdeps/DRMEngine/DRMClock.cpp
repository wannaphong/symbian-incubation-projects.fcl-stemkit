/*
* Copyright (c) 2003-2009 Nokia Corporation and/or its subsidiary(-ies).
* All rights reserved.
* This component and the accompanying materials are made available
* under the terms of "Eclipse Public License v1.0"
* which accompanies this distribution, and is available
* at the URL "http://www.eclipse.org/legal/epl-v10.html".
*
* Initial Contributors:
* Nokia Corporation - initial contribution.
*
* Contributors:
*
* Description:  Implementation of the DRM Clock
*
*/


// INCLUDE FILES

#include <mmtsy_names.h>

#include "DRMClock.h"
#include "drmlog.h"
#include "DRMEventTimeChange.h"
#include "wmdrmfileserverclient.h"

#include <DRMNotifier.h>
#include <s32strm.h>
#include <s32file.h>
#include <e32property.h>
#include <e32keys.h>

#ifdef RD_MULTIPLE_DRIVE
#include <driveinfo.h>
#endif

#include "DRMNitzObserver.h"
#include "GPSWatcher.h"

// EXTERNAL DATA STRUCTURES

// EXTERNAL FUNCTION PROTOTYPES  

// CONSTANTS
const TInt KMinuteInMicroseconds = 60000000;
const TInt KTimeZoneIncrement = 15;
 
// The time zones sanity check values, not sure if -13 hours exists
// But atleast +13 does in: Nuku'Alofa
const TInt KTimeZoneLow = -52; // -13 hours
const TInt KTimeZoneHigh = 55; // +13 hours 45 minutes Some NZ owned island


// MACROS

// LOCAL CONSTANTS AND MACROS

// MODULE DATA STRUCTURES

// LOCAL FUNCTION PROTOTYPES

// FORWARD DECLARATIONS

// ============================= LOCAL FUNCTIONS ===============================


// ============================ MEMBER FUNCTIONS ===============================


// -----------------------------------------------------------------------------
// CDRMRightsDB::CDRMRightsDB
// C++ default constructor can NOT contain any code, that
// might leave.
// -----------------------------------------------------------------------------
//    
CDRMClock::CDRMClock()
    {      
    };

// -----------------------------------------------------------------------------
// CDRMRightsDB::ConstructL
// Symbian 2nd phase constructor can leave.
// -----------------------------------------------------------------------------
//
void CDRMClock::ConstructL()
    {
    DRMLOG( _L( "DRM Clock Starting: " ) );
    TInt error = KErrNone;

    // Create a notifier instance
    iNotifier = CDRMNotifier::NewL();
        
#if 0 //ndef __WINS__
    ConnectToPhoneL();            
            
    iObserver = CDRMNitzObserver::NewL( iPhone, const_cast<CDRMClock*>(this));

    iObserver->Start();
    
    TRAP( error, iGpsWatcher = CGPSWatcher::NewL( const_cast<CDRMClock*>(this) ));
    DRMLOG2( _L("DRMClock: GPS watcher started: %d"), error );    
#endif

    DRMLOG( _L( "DRM Clock started" ) );		    
    };

// -----------------------------------------------------------------------------
// CDRMClock::NewLC
// Two-phased constructor
// -----------------------------------------------------------------------------
//
CDRMClock* CDRMClock::NewLC()
    {
    DRMLOG( _L( "CDRMClock::NewLC" ) );
    
    CDRMClock* self = new(ELeave) CDRMClock;
    CleanupStack::PushL( self );
    self->ConstructL();
    
    DRMLOG( _L( "CDRMClock::NewLC ok" ) );
    
    return self;
    };

// -----------------------------------------------------------------------------
// CDRMClock::NewL
// Two-phased constructor
// -----------------------------------------------------------------------------
//
CDRMClock* CDRMClock::NewL()
    {
    DRMLOG( _L( "CDRMClock::NewL" ) );
    
    CDRMClock* self = NewLC();
    CleanupStack::Pop();

    DRMLOG( _L( "CDRMClock::NewL ok" ) );
    
    return self;
    };
  
// ---------------------------------------------------------
// CDRMClock::~CDRMClock
// Destructor
// ---------------------------------------------------------
//
CDRMClock::~CDRMClock()
    { 
    DRMLOG( _L( "CDRMClock::~CDRMClock" ) );
       
    if( iNotifier )
        {
        delete iNotifier;
        iNotifier = 0;
        }
        
#if 0 //ndef __WINS__
    if(iObserver)            
        {
        iObserver->Cancel();
        delete iObserver;
        iObserver = 0;
        }  
        
    if( iGpsWatcher )
        {
        iGpsWatcher->Cancel();
        delete iGpsWatcher;
        iGpsWatcher = 0;
        }   
#endif // __WINS__        
    };

// -----------------------------------------------------------------------------
// CDRMClock::GetSecureTime
// returns time and security level
// -----------------------------------------------------------------------------
//
void CDRMClock::GetSecureTime(TTime& aTime, TInt& aTimeZone, 
                              DRMClock::ESecurityLevel& aSecurityLevel)
    {
    DRMLOG( _L( "CDRMClock::GetSecureTime" ) );
    
    TTime currentUniversal;
    TTime currentHome;
    TInt error = KErrNone;
    
    // if there is an error it's not initialized
    error = aTime.UniversalTimeSecure();
    
    if( error == KErrNoSecureTime )
        {
        currentHome.HomeTime();               
        currentUniversal.UniversalTime();         
        
        aTimeZone = ( currentHome.Int64() - currentUniversal.Int64() ) / 
                   ( KMinuteInMicroseconds* KTimeZoneIncrement );
        
        
        aTime.UniversalTime();

        aSecurityLevel = DRMClock::KInsecure; 
       
        DRMLOG( _L( "CDRMClock::GetSecureTime: DRMClock is Insecure" ) );        
        }
    else 
        {
        currentHome.HomeTimeSecure();        
        currentUniversal.UniversalTimeSecure();
        
        aTimeZone = ( currentHome.Int64() - currentUniversal.Int64() ) / 
                   ( KMinuteInMicroseconds* KTimeZoneIncrement );
        
        aSecurityLevel = DRMClock::KSecure;     
        DRMLOG( _L( "CDRMClock::GetSecureTime: DRMClock is Secure" ) );  
        }    

    DRMLOG( _L( "CDRMClock::GetSecureTime ok" ) );
    };


// -----------------------------------------------------------------------------
// CDRMClock::ResetSecureTimeL
// resets the secure time and recalculates the offsets
// should not reset to 0
// -----------------------------------------------------------------------------
//
// Do not reset the timezone, use whatever has been set or retrieved from the UI time
// However check that the timezone is a valid one just in case
void CDRMClock::ResetSecureTimeL( const TTime& aTime, const TInt& aTimeZone )
    {
    DRMLOG( _L( "CDRMClock::ResetSecureTimeL" ) );
   
    TRequestStatus status;  
    TInt error = KErrNone;  
    CDRMEventTimeChange* change = CDRMEventTimeChange::NewLC();
	TTime previousTime;
	TTime previousTimeLocal;
	TTime newTime;
	TInt timezone = 0;
	TDateTime temppi; // Only for logging

    // check for param that the time is even reasonably valid:
    if( aTime.Int64() == 0 )
        {
        DRMLOG( _L("Trying to reset to zero time") );             
    	User::Leave( KErrArgument );
        }
    
    // Sanity check: Time zone has to be +/- certail hours
    // for this check -13h to +13.75h
    if( aTimeZone < KTimeZoneLow || aTimeZone > KTimeZoneHigh )
        {
        DRMLOG2( _L("TimeZone invalid, time may be as well, aborting change: %d"), aTimeZone  );
        User::Leave( KErrArgument );
        }
    
    
    // Get the current secure time with timezone
    // Ask the hometime first so that rounding of any divider goes correctly
    error = previousTimeLocal.HomeTimeSecure(); 
    
    // If there is an error, the secure hometime has not been set
    // Which means that the UI clock has the valid data
    if( error )
        {
        previousTimeLocal.HomeTime();
        previousTime.UniversalTime();
        timezone = ( previousTimeLocal.Int64() - previousTime.Int64() ) / 
                   ( KMinuteInMicroseconds* KTimeZoneIncrement );
		change->SetOldSecurityLevel( DRMClock::KInsecure );
		}
	else
		{
		previousTime.UniversalTimeSecure();		                   
        
        timezone = ( previousTimeLocal.Int64() - previousTime.Int64() ) / 
                   ( KMinuteInMicroseconds* KTimeZoneIncrement );

		change->SetOldSecurityLevel( DRMClock::KSecure );
		change->SetNewSecurityLevel( DRMClock::KSecure );
		}
    
    // Since it's not important to get the timezone we keep what is set or reset it:
    previousTimeLocal = aTime.Int64() + ( timezone * ( KMinuteInMicroseconds* KTimeZoneIncrement ) );

    // Do the actual updating:
    // Update using the wmdrm fileserver since it has TCB capability
    RWmDrmFileServerClient resetclient;
    User::LeaveIfError( resetclient.Connect() );
    CleanupClosePushL( resetclient );
    
    newTime = aTime;
    User::LeaveIfError( resetclient.UpdateSecureTime( previousTimeLocal, newTime ) );
       
    CleanupStack::PopAndDestroy();


    DRMLOG( _L( "CDRMClock::ResetSecureTimeL: AFTER RESET." ));	

    // DRM Notifier data:
    // send info about the change:

    change->SetNewSecurityLevel( DRMClock::KSecure );
    
    change->SetOldTime( previousTime );
    change->SetOldTimeZone( timezone );

    change->SetNewTime( aTime );
    change->SetNewTimeZone( timezone );
    
    // Notify clients

    iNotifier->SendEventL(*change,status);
    User::WaitForRequest(status);
    CleanupStack::PopAndDestroy();    
    
    DRMLOG( _L( "CDRMClock::ResetSecureTimeL ok" ) );
    };

// ---------------------------------------------------------
// CDRMClock::Notify
// Notify DRM clock about an event
// ---------------------------------------------------------
//
void CDRMClock::Notify( TInt aNotify )
    {
    switch( aNotify )
        {
        case ENotifyGPSTimeReceived:
            // GPS time received, listen again after the next boot, destroy GPS watcher:
            DRMLOG(_L("Notify: ENotifyGPSTimeReceived, Deleting GPS watcher"));
            delete iGpsWatcher;
            iGpsWatcher = NULL;
            DRMLOG(_L("Notify: GPS Watcher deleted"));
            break;    
        case ENotifyNone:
        default:
            break;  // Do nothing    
        }    
    }





// ---------------------------------------------------------
// CDRMClock::ConnectToPhoneL(const TDateTime& aDateTime)
// Gets the nitz time from iNitzInfo
// ---------------------------------------------------------
//
void CDRMClock::ConnectToPhoneL()
    {
    DRMLOG( _L( "CDRMClock::ConnectToPhoneL" ) );
#if 0    
    const TInt KTriesToConnectServer(10);
    const TInt KTimeBeforeRetryingServerConnection(100000);
    TInt thisTry(0);
    TInt err(KErrNone);
    TInt numPhone;
    TName tsyName;
    RTelServer::TPhoneInfo phoneInfo;
    RMobilePhone::TMobilePhoneSubscriberId imsi;
    TRequestStatus status;
    

    while ((err = iEtelServer.Connect()) != KErrNone &&
                            (thisTry++) <= KTriesToConnectServer)
        {
        User::After(KTimeBeforeRetryingServerConnection);
        }
    User::LeaveIfError(err);

    User::LeaveIfError(iEtelServer.LoadPhoneModule(KMmTsyModuleName));
    User::LeaveIfError(iEtelServer.EnumeratePhones(numPhone));

    for (TInt i(0); i < numPhone; i++)
        {
        User::LeaveIfError(iEtelServer.GetPhoneInfo(i, phoneInfo));
        User::LeaveIfError(iEtelServer.GetTsyName(i,tsyName));

        if (tsyName.CompareF(KMmTsyModuleName) == 0)
            {
            break;
            }
        }

    User::LeaveIfError(iPhone.Open(iEtelServer, phoneInfo.iName));   
    
    iPhone.GetSubscriberId( status, imsi );
    User::WaitForRequest( status );
	
    DRMLOG( imsi );    
 #endif   
    DRMLOG( _L( "CDRMClock::ConnectToPhoneL ok" ) );
    };


// ========================== OTHER EXPORTED FUNCTIONS =========================


//  End of File
