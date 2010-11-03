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
    iSatIcon.Close(); 
    iSatSession.Close();
    }

// -----------------------------------------------------------------------------    
// Destructor
// -----------------------------------------------------------------------------
EXPORT_C CAknIcon* CMcsSatHandler::LoadIconL()
    {
    TInt iconId( KErrNone );
    User::LeaveIfError( RProperty::Get( KCRUidMenu, KMenuSatUIIconId, iconId ) );
    CAknIcon* icon = CAknIcon::NewL(); 
    CleanupStack::PushL(icon);
    if( iconId != KErrNone )
        {
        RIconEf iIconEf;
        iSatIcon.GetIconInfoL( TUint8( iconId ), iIconEf ); 
        CleanupClosePushL( iIconEf );
        CFbsBitmap* bitmap = GetBitmapL( iIconEf );
            if( !bitmap )
                {
                CFbsBitmap* mask( NULL );
                CleanupStack::PushL( mask );    
                
                icon->SetBitmap( bitmap );
                // create and set mask
                User::LeaveIfError( mask->Create( bitmap->SizeInPixels(), EGray256 ) );
                
                CFbsBitmapDevice* maskDevice = CFbsBitmapDevice::NewL( mask );
                CleanupStack::PushL( maskDevice ); 
                CFbsBitGc* maskGc;
                User::LeaveIfError( maskDevice->CreateContext( maskGc ) );
                CleanupStack::PushL( maskGc );
                maskGc->SetBrushStyle( CGraphicsContext::ESolidBrush );
                maskGc->SetDrawMode( CGraphicsContext::EDrawModePEN );
                maskGc->SetBrushColor( KRgbBlack );
                maskGc->Clear();            
                maskGc->SetBrushColor( KRgbWhite );             
                maskGc->DrawRect( TRect( TPoint( ), bitmap->SizeInPixels() ) );                
                icon->SetMask( mask );
                
                CleanupStack::PopAndDestroy( maskGc );
                CleanupStack::PopAndDestroy( maskDevice );
                CleanupStack::Pop( mask );
                }
        CleanupStack::PopAndDestroy( &iIconEf ); // iIconEf
        CleanupStack::Pop( icon );
        }
    else
        {
        CleanupStack::PopAndDestroy( icon );
        icon = NULL;
        }
    return icon;    
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
    TInt visibility( KErrNone );
    TInt err = RProperty::Get( KCRUidMenu, KMenuShowSatUI, visibility );
    if( err == KErrNone && visibility )
        return ETrue;
    else
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
    iSatSession.ConnectL();
    iSatIcon.OpenL( iSatSession ); 
    }

// ---------------------------------------------------------------------------
// CMenuSATHandler::GetVisibility
// ---------------------------------------------------------------------------
// 
CFbsBitmap* CMcsSatHandler::GetBitmapL( const RIconEf& aIconEF )
    {
    TInt selectedIconIndex( KErrNotFound );
    TSize selectedIconSize( 0, 0 );
    CFbsBitmap* bitmap( NULL );
    for ( TInt i = 0; i < aIconEF.Count(); ++i )
        {
        if( ( aIconEF[i].IconSize().iHeight * aIconEF[i].IconSize().iWidth ) >= 
            ( selectedIconSize.iHeight * selectedIconSize.iWidth ) )
            if( bitmap )
                {
                delete bitmap;
                bitmap = NULL;
                }
            // test and select index of iIcon which is not too big
            TRAPD( bitmapErr, bitmap = iSatIcon.GetIconL( aIconEF[ i ] ) );
            if( !bitmapErr && bitmap ) //!iBitmap if iIcon is too big
                {
                selectedIconSize = aIconEF[i].IconSize();
                selectedIconIndex = i;
                }
            else if( bitmapErr )
                {
                User::Leave( bitmapErr );
                }
            }
    if( selectedIconIndex != KErrNotFound )
        {
        if( bitmap )
            {
            delete bitmap;
            bitmap = NULL;
            }
        TRAPD( bitmapErr, bitmap = iSatIcon.GetIconL( aIconEF[ selectedIconIndex ] ) );
        User::LeaveIfError( bitmapErr );    
        return bitmap;
        }
    else 
        {
        return NULL;
        }    
    }    
    
//  End of File  
