/*
* Copyright (c) 2008 Nokia Corporation and/or its subsidiary(-ies).
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
* Description:  The API supports attributes not present in MCS from SAT Api
*
*/


#ifndef __MCSSATHANDLER_H__
#define __MCSSATHANDLER_H__

#include <AknIconUtils.h>

#include <rsatsession.h>
#include <tsaticoninfo.h>
#ifdef SIM_ATK_SERVICE_API_V1 
#include <rsatservice.h>// MCL 
#else 
#include <RSatIcon.h> // 5.0 
#endif 
/**
 *  SAT Handler.
 *  @lib mcssathandler.lib
 *  @since S60 v5.0
 */
NONSHARABLE_CLASS( CMcsSatHandler ): public CBase
    {
public:
    
    /**
    * Two-phased constructor. Leaves on failure.
    * @return The constructed object.
    */
    IMPORT_C static CMcsSatHandler* NewL();
    
    /**
    * Destructor.
    * @since S60 v5.0
    * @capability None.
    * @throws None.
    * @panic None.
    */
    virtual ~CMcsSatHandler();
    
    IMPORT_C CAknIcon* LoadIconL();
    
    IMPORT_C TInt GetName( TDes& aName );
    
    IMPORT_C static TBool CheckVisibility() ;
    
private:

    /**
    * Constructor.
    */
    CMcsSatHandler();

    /**
    * 2nd phase constructor.
    */
    void ConstructL();

#if 0    
    /**
    * Gets best icon from aIconEF.
    */
    CFbsBitmap* GetBitmapL( const RIconEf& aIconEF );
    
private:    // data
    RSatSession iSatSession;
    
//#ifdef SIM_ATK_SERVICE_API_V1
    RSatService iSatIcon;
//#else
    RSatIcon iSatIcon;
#endif 
    };

#endif // __MCSSATHANDLER_H__
