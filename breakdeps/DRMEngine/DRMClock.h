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
* Description:  Interface for the DRMClock
*
*/


#ifndef DRMCLOCK_H
#define DRMCLOCK_H

// INCLUDES

#include <e32base.h>	// CBase
#include <e32std.h>
#include <e32def.h>		// Type definitions
#include <bacntf.h>
#include <etelmm.h>
#include <DRMTypes.h>
#include <e32property.h>

// CONSTANTS

// MACROS

// DATA TYPES

// FUNCTION PROTOTYPES

// FORWARD DECLARATIONS
class CDRMNitzObserver;
class CDRMNotifier;
class CGPSWatcher;

// CLASS DECLARATION

/**
*  CDRMClock implements the drm clock required by DRM Engine
*
*  @lib unipertar.exe
*  @since 2.8
*/
NONSHARABLE_CLASS( CDRMClock )
    {
    public:
        // Notifications:        
        enum 
            {
            ENotifyNone = 0,    
            ENotifyGPSTimeReceived = 1    
            };    
    
    public:

        /**
        * NewLC
        *
        * Creates an instance of the CDRMClock class and returns a pointer to it
        * The function leaves the object into the cleanup stack
        *
        * @since  2.8
        * @return Functional CDRMClock object, Function leaves if an error occurs.
        */
        static CDRMClock* NewLC();

        /**
        * NewL
        *
        * Creates an instance of the CDRMClock class and returns a pointer to it
        *
        * @since  2.8
        * @return Functional CDRMClock object, Function leaves if an error occurs.
        */
        static CDRMClock* NewL();
  
        /**
        * Destructor
        */
        virtual ~CDRMClock();

        /**
        * GetSecureTime
        * 
        * Return the current time and the security of the current time
        *
        * @since 2.8
        * @param aTime : return parameter for time in UTC
        * @param aTimeZone : return parameter for the timezone in +/-15 minutes
        * @param aSecurityLevel : return parameter for security level
        * @return none
        */
        void GetSecureTime(TTime& aTime, TInt& aTimeZone, 
                           DRMClock::ESecurityLevel& aSecurityLevel);

        /**
        * ResetSecureTimeL
        *
        * Resets the secure time source and recalculates the offsets
        *
        * @since 2.8 
        * @param aSecureTime, the new secure time in UTC
        * @param aTimeZone, the time zone of the new secure time in +/- 15 minutes 
        * @return none, Function leaves with Symbian OS error code if an
        *         error occurs
        */
        void ResetSecureTimeL( const TTime& aSecureTime, const TInt& aTimeZone );
        
        
        /**
        * Notify
        *
        * Notifies about an event to the DRM Clock
        *
        * @since 9.2
        * @param Message The notification event   
        *
        */
        void Notify( TInt aMessage );
        
    protected:    
    private:
        /**
        * Default Constructor - First phase
        */
        CDRMClock();

         /**
        * ConstructL
        *
        * Second phase constructor
        *
        * @since  2.8
        * @return Leaves if an error occurs
        */	   
        void ConstructL();
 
        /**
        * ConnectToPhoneL
        *
        * Connects to the phone services
        *
        * @since 2.8
        * @return Leaves with symbian os error codes if an error occurs
        */
        void ConnectToPhoneL();
        
        // Variables
        CDRMNotifier* iNotifier;   
        
        // Nitz information handles      
        RTelServer iEtelServer;
        RMobilePhone iPhone;
        CDRMNitzObserver* iObserver;
        
        // GPS watcher component, updates DRM time from GPS if available
        CGPSWatcher* iGpsWatcher;
    };
#endif      // DRMCLOCK_H   
            
// End of File
