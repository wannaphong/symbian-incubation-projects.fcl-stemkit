/*
* Copyright (c) 2002-2005 Nokia Corporation and/or its subsidiary(-ies).
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
* Description:  Used for receive SIM Application name, icon or 
*                visibility information.
*
*/



// INCLUDE FILES

#include <e32property.h>
// From SatClient
#include "mcsdef.h"
#include <bitdev.h>

#include "mcssathandler.h"


// ============================ MEMBER FUNCTIONS ===============================
// -----------------------------------------------------------------------------
// CMcsSatHandler::NewL
// Two-phased constructor.
// -----------------------------------------------------------------------------
//
EXPORT_C CMcsSatHandler* CMcsSatHandler::NewL()
    {
    CMcsSatHandler* self = new( ELeave ) CMcsSatHandler( );
    CleanupStack::PushL( self );
    self->ConstructL();
    CleanupStack::Pop( self );
    return self;
    }

// -----------------------------------------------------------------------------    
// Destructor
// -----------------------------------------------------------------------------
CMcsSatHandler::~CMcsSatHandler()
    {
    }

// -----------------------------------------------------------------------------    
// Destructor
// -----------------------------------------------------------------------------
EXPORT_C CAknIcon* CMcsSatHandler::LoadIconL()
    {
    return NULL;    
    }

// ---------------------------------------------------------------------------
// CMenuSATHandler::GetName
// ---------------------------------------------------------------------------
//    
EXPORT_C TInt CMcsSatHandler::GetName( TDes& aName )
    {
    return RProperty::Get( KCRUidMenu, KMenuSatUIName, aName );
    }

// ---------------------------------------------------------------------------
// CMenuSATHandler::GetVisibility
// ---------------------------------------------------------------------------
//        
EXPORT_C TBool CMcsSatHandler::CheckVisibility() 
    {
    return EFalse;
    }


// -----------------------------------------------------------------------------
// CMcsSatHandler::CMcsSatHandler
// C++ default constructor can NOT contain any code, that
// might leave.
// -----------------------------------------------------------------------------
//
CMcsSatHandler::CMcsSatHandler()
    {
    }

// -----------------------------------------------------------------------------
// CMcsSatHandler::ConstructL
// Symbian 2nd phase constructor can leave.
// -----------------------------------------------------------------------------
//
void CMcsSatHandler::ConstructL()
    {
    }

    
//  End of File  
