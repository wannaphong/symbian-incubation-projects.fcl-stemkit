/*
* Copyright (c) 2002-2008 Nokia Corporation and/or its subsidiary(-ies).
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
* Description:  Roap engine
 *
*/


// INCLUDE FILES

#include <random.h>

#include <DocumentHandler.h>

#ifdef RD_MULTIPLE_DRIVE
#include  <pathinfo.h>
#include  <driveinfo.h>
#else
#include  <pathinfo.h>
#endif

#ifndef __WINS__
#include <etelmm.h>
#include <mmtsy_names.h>
#include <SysUtil.h>
#endif

#include <flogger.h>
#include <x509cert.h>
#include <x509certext.h>
#include <hash.h>
#include <utf.h>
#include <asn1dec.h>
#include <centralrepository.h>
#include <e32base.h>  // CleanupResetAndDestroyPushL dependencies

#include "cleanupresetanddestroy.h" // CleanupResetAndDestroyPushL
#include "DRMRights.h"
#include "RoapEng.h"
#include "RoapTrigger.h"
#include "wbxmlroaptriggerparser.h"
#include "RoapResponse.h"
#include "RoapMessage.h"
#include "RoapParser.h"
#include "RoapSigner.h"
#include "DeviceHello.h"
#include "RIHello.h"
#include "RegistrationReq.h"
#include "RegistrationResp.h"
#include "RightsReq.h"
#include "RightsResp.h"
#include "JoinDomainReq.h"
#include "JoinDomainResp.h"
#include "LeaveDomainReq.h"
#include "LeaveDomainResp.h"
#ifdef RD_DRM_METERING
#include "MeteringReportReq.h"
#include "MeteringReportResp.h"
#endif
#include "RoapStorageClient.h"
#include "RoapDef.h"
#include "RoapLog.h"
#include "RoapObserver.h"
#include "CmlaCrypto.h"
#include "DRMRIContext.h"
#include "DRMDomainContext.h"
#include "DRMProtectedRoParser.h"
#include "DRMClockClient.h"
#include "DcfRep.h"
#include "DcfEntry.h"
#include "Base64.h"
#include "drmsettingsplugininternalcrkeys.h"


#define STUB_C_CLASS_IN_NAMESPACE( n, c ) namespace n { class c: public CBase { private: c(); public: virtual ~c(); }; } n::c::c() {} n::c::~c() {}
#define STUB_C_CLASS( c ) class c : public CBase { private: c(); public: virtual ~c(); }; c::c() {} c::~c() {}
// This class does not do anything.
// It is defined here only to keep binary compatibility,
// because of unintentional class name leak in
// armv5 export history.
// Don't ever use this class for anything.
STUB_C_CLASS_IN_NAMESPACE( Roap , CWbxmlRoapTriggerToXmlParser )

// Yet another stub classes because of moved classes
// which have leaked virtual table entries
STUB_C_CLASS( COCSPResponse )
STUB_C_CLASS( COCSPResponseCertInfo )


using namespace Roap;
// ================= CONSTANTS =======================
// For parsing multipart content
_LIT8(KCmlaIp1, "http://www.cm-la.com/tech/cmlaip/cmlaip#cmlaip-1");
_LIT8(KLeaveDomainElement, "leaveDomain");
_LIT8(KSignedInfoElement, "SignedInfo");
_LIT(KBOM1, "\xFFFE");
_LIT(KBOM2, "\xFEFF");
#ifdef RD_DRM_METERING
_LIT8( KRoapVersion11, "1.1" );
#endif

static const TInt KDomainGenerationLength( 3 );
static const TInt KMinCertChainLength( 3 );
// ================= LOCAL FUNCTIONS =======================

LOCAL_C TBool SortArrays(
    RPointerArray<HBufC8>& aKeys,
    RPointerArray<HBufC8>& aMacs,
    RPointerArray<HBufC8>& aElements,
    RArray<TInt>& aOrder )
    {
    TInt i;
    TInt j;
    TInt index;
    HBufC8* temp1 = NULL;
    HBufC8* temp2 = NULL;
    HBufC8* temp3 = NULL;
    TBool isInOrder = ETrue;

    if ( aOrder.Count() != aKeys.Count() || aKeys.Count() != aMacs.Count()
        || aMacs.Count() != aElements.Count() )
        {
        return EFalse;
        }

    for ( i = 0; i < aKeys.Count(); i++ )
        {
        index = aOrder[i];
        temp1 = aKeys[i];
        temp2 = aMacs[i];
        temp3 = aElements[i];
        j = i;
        while ( ( j > 0 ) && ( aOrder[j - 1] > index ) )
            {
            isInOrder = EFalse;
            aOrder[j] = aOrder[j - 1];
            aKeys[j] = aKeys[j - 1];
            aMacs[j] = aMacs[j - 1];
            aElements[j] = aElements[j - 1];
            j = j - 1;
            }
        aOrder[j] = index;
        aKeys[j] = temp1;
        aMacs[j] = temp2;
        aElements[j] = temp3;
        }
    return isInOrder;
    }

// ================= MEMBER FUNCTIONS =======================

// ---------------------------------------------------------
// CRoapEng::NewL()
// ---------------------------------------------------------
//
EXPORT_C CRoapEng* CRoapEng::NewL()
    {
    CRoapEng* engine = new ( ELeave ) CRoapEng();
    CleanupStack::PushL( engine );
    engine->ConstructL();
    CleanupStack::Pop( engine );
    return engine;
    }

// ---------------------------------------------------------
// CRoapEng::~CRoapEng()
// ---------------------------------------------------------
//
EXPORT_C CRoapEng::~CRoapEng()
    {
    if ( iStorageClient )
        {
        iStorageClient->Close();
        }
    delete iStorageClient;
    if ( iClockClient )
        {
        iClockClient->Close();
        }
    delete iClockClient;
    delete iParser;
    delete iSigner;
    delete iDeviceId;
    delete iRoParser;
    delete iDcfRep;
    iRiAlgorithms.ResetAndDestroy();
    }

// ---------------------------------------------------------
// CRoapEng::~CRoapEng()
// ---------------------------------------------------------
//
void CRoapEng::ConstructL()
    {
    LOGLIT( "CRoapEng::ConstructL" )

    CRoapEngBase::ConstructL();
    iParser = CRoapParser::NewL();
    iStorageClient = new ( ELeave ) RRoapStorageClient;
    User::LeaveIfError( iStorageClient->Connect() );
    iClockClient = new ( ELeave ) RDRMClockClient;
    User::LeaveIfError( iClockClient->Connect() );
    TBuf8<SHA1_HASH> deviceId;
    iStorageClient->GetDevicePublicKeyHashL( deviceId );
    iDeviceId = deviceId.AllocL();
    iSigner = CRoapSigner::NewL( *iStorageClient );
    iRoParser = CDrmProtectedRoParser::NewL();
    iDcfRep = CDcfRep::NewL();
    iCertNeeded = ETrue;
    iRiSupportsCertCaching = EFalse;
    iTransStatus = ENotAsked;
    iSelectedAlgorithms = EOma;
    iSelectedRoot = KNullDesC8;
    iStorageClient->SelectTrustedRootL( KNullDesC8 );
    iDeviceTimeError = EFalse;
    iDomainId.SetLength( 0 );
    iSecureTime = ETrue;
    iZone = 0;
    }

// ---------------------------------------------------------
// CRoapEng::CRoapEng()
// ---------------------------------------------------------
//
CRoapEng::CRoapEng() :
    CRoapEngBase()
    {
    }

// ---------------------------------------------------------
// CRoapEng::ParseTriggerL()
// ---------------------------------------------------------
//
CRoapTrigger* CRoapEng::ParseTriggerL( const TDesC8& aTrigger )
    {
    LOGLIT( "CRoapEng::ParseTriggerL" )

    CRoapTrigger* trigger( NULL );
    RBuf8 xmlTrigger;
    CleanupClosePushL( xmlTrigger );
    _LIT8( KRoap, "<roap:roapTrigger" );
    if ( aTrigger.FindF( KRoap ) == KErrNotFound )
        {
        DRM::CWbxmlRoapTriggerParser* wbParser(
            DRM::CWbxmlRoapTriggerParser::NewLC() );
        HBufC8* b( NULL );
        TRAPD( parseError, b = wbParser->ParseL( aTrigger ) );
        if ( parseError == KErrNone )
            {
            xmlTrigger.Assign( b );
            b = NULL;
            LOGLIT( "  We have a WBXML trigger" )
            }
        else
            { // OMA BCAST: Check if this is an XML trigger after all..
            LOGLIT( "  We have an XML trigger after all" )
            xmlTrigger.CreateL( aTrigger );
            }
        CleanupStack::PopAndDestroy( wbParser );
        }
    else
        {
        xmlTrigger.CreateL( aTrigger );
        }
    trigger = iParser->ParseRoapTriggerL( xmlTrigger );

    CleanupStack::PushL( trigger );
    if ( !trigger || !trigger->ValidTrigger() )
        {
        User::Leave( KErrRoapGeneral );
        }

    // check that SilentRightsUrl is on the white list
    // URL is searched from pre-configured white list
    TBool fromPreConfiguredWhiteList( ETrue );
    if ( iStorageClient->WhiteListURLExistsL( *trigger->iRoapUrl, fromPreConfiguredWhiteList ) )
        {
        iAllowedToContactRi = ETrue;
        }

    if ( trigger->iTriggerType == ELeaveDomainTrigger && trigger->iSignature )
        {
        if ( !VerifyTriggerSignatureL( xmlTrigger, *trigger ) )
            {
            User::Leave( KErrRoapServerFatal );
            }
        }

    CleanupStack::Pop( trigger );
    CleanupStack::PopAndDestroy( &xmlTrigger );

    return trigger;
    }

// ---------------------------------------------------------
// CRoapEng::GetRIContextL()
// ---------------------------------------------------------
//
void CRoapEng::GetRIContextL( TBool& aRegistered, const TDesC8& aRiId )
    {
    LOGLIT( "CRoapEng::GetRIContextL" )

    CDRMRIContext* context = NULL;

    aRegistered = EFalse;

    // delete old RI context and obtain a new one
    delete iStoredRiContext;
    iStoredRiContext = NULL;
    context = iStorageClient->GetRIContextL( aRiId );
    if ( !context )
        {
        return;
        }

    iStoredRiContext = context;
    iRiSupportsCertCaching = iStoredRiContext->DeviceCertCached();
    iSelectedRoot = iStoredRiContext->SelectedDeviceRoot();
    iStorageClient->SelectTrustedRootL( iSelectedRoot );

    if ( context->CertificateChain().Count() && context->ExpiryTime()
        > GetDrmTimeL() )
        {
        aRegistered = ETrue;
        iUseRiContextUrl = EFalse;
        }
    else
        {
        // Received Context was invalid or expired
        iUseRiContextUrl = EFalse;
        delete iStoredRiContext;
        iStoredRiContext = NULL;
        }
    }

// ---------------------------------------------------------
// CRoapEng::GetDomainContextL()
// ---------------------------------------------------------
//
void CRoapEng::GetDomainContextL(
    TBool& aIsJoined,
    TBool& aIsValidGeneration,
    const TDesC8& aDomainId )
    {
    LOGLIT( "CRoapEng::GetDomainContextL" )

    TInt generation = 0;
    CDRMDomainContext* context = NULL;

    aIsJoined = EFalse;
    aIsValidGeneration = EFalse;

    // last 3 digits are for Domain generation
    context = iStorageClient->GetDomainContextL( aDomainId );

    if ( !context )
        {
        return;
        }

    if ( context->ExpiryTime() > GetDrmTimeL() || context->ExpiryTime()
        == Time::NullTTime() )
        {
        aIsJoined = ETrue;
        }

    TLex8 lex( aDomainId.Right( KDomainGenerationLength ) );
    lex.Val( generation );

    if ( context->DomainGeneration() >= generation )
        {
        aIsValidGeneration = ETrue;
        }

    delete context;
    }

// ---------------------------------------------------------
// CRoapEng::CreateReqMessageL()
// ---------------------------------------------------------
//
void CRoapEng::CreateReqMessageL()
    {
    LOGLIT( "CRoapEng::CreateReqMessageL" )

    __ASSERT_ALWAYS( iTrigger, User::Invariant() );
    __ASSERT_ALWAYS( !iRequest, User::Invariant() );

    switch ( iReqMessage )
        {
        case EDeviceHello:
            {
            iRequest = CreateDeviceHelloL();
            break;
            }
        case ERegistration:
            {
            iRequest = CreateRegistrationRequestL();
            break;
            }
        case EROAcquisition:
            {
            iRequest = CreateRightsRequestL();
            break;
            }
        case EJoinDomain:
            {
            iRequest = CreateJoinDomainRequestL();
            break;
            }
        case ELeaveDomain:
            {
            iRequest = CreateLeaveDomainRequestL();
            break;
            }
#ifdef RD_DRM_METERING
        case EMeteringRequest:
            {
            iRequest = CreateMeteringReportRequestL();
            break;
            }
#endif
        default:
            {
            User::Leave( KErrArgument );
            }
        }
    }

// ---------------------------------------------------------
// CRoapEng::CreateDeviceHelloL()
// ---------------------------------------------------------
//
CRoapMessage* CRoapEng::CreateDeviceHelloL()
    {
    LOGLIT( "CRoapEng::CreateDeviceHelloL" )
    PERFORMANCE_LOGLIT( "Registration protocol started" )

    RPointerArray<TDesC8> idArray;
    CDeviceHello* req = CDeviceHello::NewL();
    CleanupStack::PushL( req );

    // Multi-PKI addition
    CleanupResetAndDestroyPushL( idArray );
    CreateDeviceIdHashArrayL( idArray );
    for ( TInt i = 0; i < idArray.Count(); i++ )
        {
        req->iDeviceIdArray.AppendL( *idArray[i] );
        }
    CleanupStack::PopAndDestroy( &idArray );
    // Multi-PKI

#ifndef RD_DRM_METERING
    req->iVersion.Copy( KRoapVersion ); // Version 1.0
#else
    req->iVersion.Copy( KRoapVersion11 );
#endif
    if ( iTrigger->iNonce )
        {
        req->iTriggerNonce = iTrigger->iNonce->AllocL();
        }
    CmlaCrypto::SupportedAlgorithmsL( req->iAlgorithms );
    iSelectedAlgorithms = EOma;

    CleanupStack::Pop( req );
    return req;
    }

// ---------------------------------------------------------
// CRoapEng::CreateRegistrationRequestL()
// ---------------------------------------------------------
//
CRoapMessage* CRoapEng::CreateRegistrationRequestL()
    {
    LOGLIT( "CRoapEng::CreateRegistrationRequestL ->" )

    __ASSERT_ALWAYS( iResponse, User::Invariant() );

    CRegistrationReq* req = NULL;
    CRIHello* resp = NULL;
    RPointerArray<HBufC8> trustedRootArray;
    HBufC8* temp = NULL;

    resp = STATIC_CAST( CRIHello*, iResponse );
    req = CRegistrationReq::NewL();
    CleanupStack::PushL( req );
    if ( resp->iSession )
        {
        req->iSession = resp->iSession->AllocL();
        }
    else
        {
        User::Leave( KErrRoapServerFatal );
        }

    req->iNonce.SetLength( KDeviceNonceLength );
    TRandom::Random( req->iNonce );

    req->iTime = GetDrmTimeL();

    // store the nonce for DRM Time sync
    iRegReqNonce = req->iNonce;

    if ( iCertNeeded )
        {
        req->iCertificateChain = GetCertificateChainL();
        if ( resp->iCertificateCaching )
            {
            iCertNeeded = EFalse;
            }
        }

    // Send all our trusted roots to the RI
    CleanupResetAndDestroyPushL( trustedRootArray );

    LOGLIT( "  Getting trusted roots" )

    iStorageClient->GetTrustedRootsL( trustedRootArray );

    if ( !trustedRootArray.Count() )
        {
        // No trusted roots found!
        LOGLIT( "  No trusted roots found!" )
        User::Leave( KErrRoapDevice );
        }
    for ( TInt i = 0; i < trustedRootArray.Count(); i++ )
        {
        temp = trustedRootArray[i]->AllocLC();
        req->iTrustedAuthorities.AppendL( temp );
        CleanupStack::Pop( temp );
        }

    LOGLIT( "  Setting server info" )
    if ( resp->iServerInfo && resp->iServerInfo->Size() )
        {
        req->iServerInfo = resp->iServerInfo->AllocL();
        }

    if ( iStoredRiContext )
        {
        LOGLIT( "  RI context available" )
        req->iPeerKeyIdentifier = iStoredRiContext->RIID();

        if ( iStoredRiContext->OCSPResponse().Count() && !iDeviceTimeError )
            {
            req->iOcspInfoStored = ETrue;
            req->iOcspResponderKeyId = GetOCSPResponderKeyHashL();
            }
        }
    if ( resp->iNeedDeviceDetails )
        {
        LOGLIT( "  Getting device details" )
        GetDeviceDetailsL( req->iDeviceDetailsManufacturer,
            req->iDeviceDetailsModel, req->iDeviceDetailsVersion );
        }

    if ( iTrigger->iNonce )
        {
        req->iTriggerNonce = iTrigger->iNonce->AllocL();
        }

    CleanupStack::PopAndDestroy( &trustedRootArray );
    CleanupStack::Pop( req );

    LOGLIT( "CRoapEng::CreateRegistrationRequestL <-" )

    return req;
    }

// ---------------------------------------------------------
// CRoapEng::CreateRightsRequestL()
// ---------------------------------------------------------
//
CRoapMessage* CRoapEng::CreateRightsRequestL()
    {
    LOGLIT( "CRoapEng::CreateRightsRequestL" )
    PERFORMANCE_LOGLIT( "RO acquisition protocol started" )

    __ASSERT_ALWAYS( iStoredRiContext, User::Invariant() );

    CRightsReq* req = NULL;
    RPointerArray<HBufC8> ttIDs;
    RPointerArray<HBufC8> cids;
    HBufC8* temp = NULL;
    TBuf8<SHA1_HASH> deviceId;

    req = CRightsReq::NewL();
    CleanupStack::PushL( req );

    req->iNonce.SetLength( KDeviceNonceLength );
    TRandom::Random( req->iNonce );

    req->iTime = GetDrmTimeL();

    iStorageClient->GetDevicePublicKeyHashL( deviceId );
    delete iDeviceId;
    iDeviceId = NULL;
    iDeviceId = deviceId.AllocL();
    req->iDeviceId = *iDeviceId;

    req->iRiId.Copy( iTrigger->iRiId );

    if ( !iRiSupportsCertCaching )
        {
        req->iCertificateChain = GetCertificateChainL();
        }
    if ( iTrigger->iDomainId )
        {
        req->iDomainId = iTrigger->iDomainId->AllocL();
        }

    for ( TInt i = 0; i < iTrigger->iRoIdList.Count(); i++ )
        {
        temp = iTrigger->iRoIdList[i]->AllocLC();
        req->iRoIdList.AppendL( temp );
        CleanupStack::Pop( temp );
        }

    if ( iStoredRiContext )
        {
        req->iPeerKeyIdentifier = iStoredRiContext->RIID();

        if ( iStoredRiContext->OCSPResponse().Count() )
            {
            req->iOcspInfoStored = ETrue;
            req->iOcspResponderKeyId = GetOCSPResponderKeyHashL();
            }
        }

    CleanupResetAndDestroyPushL( cids );

    CleanupResetAndDestroyPushL( ttIDs );

    FetchTransactionIDL( ttIDs, cids );

    for ( TInt i = 0; i < ttIDs.Count() && i < cids.Count(); i++ )
        {
        temp = ttIDs[i]->AllocLC();
        req->iTransTrackIDs.AppendL( temp );
        CleanupStack::Pop( temp );
        temp = cids[i]->AllocLC();
        req->iContentIDs.AppendL( temp );
        CleanupStack::Pop( temp );
        }

    CleanupStack::PopAndDestroy( &ttIDs );
    CleanupStack::PopAndDestroy( &cids );

    if ( iTrigger->iNonce )
        {
        req->iTriggerNonce = iTrigger->iNonce->AllocL();
        }

    CleanupStack::Pop( req );
    return req;
    }

// ---------------------------------------------------------
// CRoapEng::CreateJoinDomainRequestL()
// ---------------------------------------------------------
//
CRoapMessage* CRoapEng::CreateJoinDomainRequestL()
    {
    LOGLIT( "CRoapEng::CreateJoinDomainRequestL" )
    PERFORMANCE_LOGLIT( "Join domain protocol started" )

    __ASSERT_ALWAYS( iStoredRiContext, User::Invariant() );

    CJoinDomainReq* req = NULL;
    TBuf8<SHA1_HASH> deviceId;

    req = CJoinDomainReq::NewL();
    CleanupStack::PushL( req );

    req->iNonce.SetLength( KDeviceNonceLength );
    TRandom::Random( req->iNonce );

    req->iTime = GetDrmTimeL();

    iStorageClient->GetDevicePublicKeyHashL( deviceId );
    delete iDeviceId;
    iDeviceId = NULL;
    iDeviceId = deviceId.AllocL();
    req->iDeviceId = *iDeviceId;

    req->iRiId.Copy( iTrigger->iRiId );

    if ( !iRiSupportsCertCaching )
        {
        req->iCertificateChain = GetCertificateChainL();
        }
    if ( iTrigger->iDomainId )
        {
        req->iDomainId = iTrigger->iDomainId->AllocL();
        iDomainId.Copy( *req->iDomainId );
        }
    else if ( iDomainId.Length() && iTrigger->iTriggerType
        == ERoAcquisitionTrigger )
        {
        req->iDomainId = iDomainId.AllocL();
        }
    else
        {
        User::Leave( KErrRoapServerFatal );
        }

    if ( iStoredRiContext )
        {
        req->iPeerKeyIdentifier = iStoredRiContext->RIID();

        if ( iStoredRiContext->OCSPResponse().Count() )
            {
            req->iOcspInfoStored = ETrue;
            req->iOcspResponderKeyId = GetOCSPResponderKeyHashL();
            }
        }

#ifdef _DISABLE_HASH_CHAIN_GENERATION
    req->iHashChainSupport = EFalse;
#endif

    if ( iTrigger->iNonce )
        {
        req->iTriggerNonce = iTrigger->iNonce->AllocL();
        }

    CleanupStack::Pop( req );
    return req;
    }

// ---------------------------------------------------------
// CRoapEng::CreateLeaveDomainRequestL()
// ---------------------------------------------------------
//
CRoapMessage* CRoapEng::CreateLeaveDomainRequestL()
    {
    LOGLIT( "CRoapEng::CreateLeaveDomainRequestL" )
    PERFORMANCE_LOGLIT( "Leave domain protocol started" )

    __ASSERT_ALWAYS( iStoredRiContext, User::Invariant() );

    if ( !iTrigger->iDomainId )
        {
        User::Leave( KErrRoapServerFatal );
        }
    // delete Domain context before sending LeaveDomain req
    TRAPD( ret, iStorageClient->DeleteDomainContextL( *iTrigger->iDomainId ));

    CLeaveDomainReq* req = NULL;
    TBuf8<SHA1_HASH> deviceId;

    req = CLeaveDomainReq::NewL();
    CleanupStack::PushL( req );

    if ( ret == KErrNotFound )
        {
        req->iNotMember = ETrue;
        }
    else
        {
        req->iNotMember = EFalse;
        User::LeaveIfError( ret );
        }

    req->iNonce.SetLength( KDeviceNonceLength );
    TRandom::Random( req->iNonce );

    req->iTime = GetDrmTimeL();

    iStorageClient->GetDevicePublicKeyHashL( deviceId );
    delete iDeviceId;
    iDeviceId = NULL;
    iDeviceId = deviceId.AllocL();
    req->iDeviceId = *iDeviceId;

    req->iRiId.Copy( iTrigger->iRiId );

    if ( !iRiSupportsCertCaching )
        {
        req->iCertificateChain = GetCertificateChainL();
        }
    if ( iTrigger->iDomainId )
        {
        req->iDomainId = iTrigger->iDomainId->AllocL();
        }

    if ( iTrigger->iNonce )
        {
        req->iTriggerNonce = iTrigger->iNonce->AllocL();
        }

    CleanupStack::Pop( req );
    return req;
    }
// ---------------------------------------------------------
// CRoapEng::CreateMeteringReportRequestL()
// ---------------------------------------------------------
//
CRoapMessage* CRoapEng::CreateMeteringReportRequestL()
    {
#ifndef RD_DRM_METERING
    return NULL;
#else

    LOGLIT( "CRoapEng::CreateMeteringReportRequestL" )
    PERFORMANCE_LOGLIT( "Metering report creation started" )

    CMeteringReportReq* req = NULL;
    TBuf8<SHA1_HASH> deviceId;
    TBuf8<OmaCrypto::KMacSize> macKey;
    TBool registered( EFalse );

    req = CMeteringReportReq::NewL();
    CleanupStack::PushL( req );
    req->iAlgorithmInUse = iSelectedAlgorithms;
    // check if we are not using OMA algorithms
    // and update selected algorithm accordingly
    GetRIContextL( registered, iTrigger->iRiId );
    if ( registered && iStoredRiContext )
        {
        for ( TInt i = 0; i < iStoredRiContext->Algorithms().Count(); i++ )
            {
            if ( iStoredRiContext->Algorithms()[i]->CompareF( KCmlaIp1() )
                == KErrNone )
                {
                // note currently assumed that only
                // 1 of 7 ppossible algorithms used
                req->iAlgorithmInUse = ECmlaIp1;
                break;
                }
            }
        }

    // generate DeviceNonce
    req->iNonce.SetLength( KDeviceNonceLength );
    TRandom::Random( req->iNonce );

    // generate MeteringNonce
    req->iReportNonce.SetLength( KDeviceNonceLength );
    TRandom::Random( req->iReportNonce );

    // fetch secure time for request
    req->iTime = GetDrmTimeL();

    // insert DeviceId
    iStorageClient->GetDevicePublicKeyHashL( deviceId );
    delete iDeviceId;
    iDeviceId = NULL;
    iDeviceId = deviceId.AllocL();
    req->iDeviceId = *iDeviceId;

    // insert RiId
    req->iRiId.Copy( iTrigger->iRiId );

    // insert Certificate chain if needed
    if ( !iRiSupportsCertCaching )
        {
        req->iCertificateChain = GetCertificateChainL();
        }

    // add trigger Nonce
    if ( iTrigger->iNonce )
        {
        req->iTriggerNonce = iTrigger->iNonce->AllocL();
        }

    // Get from server encrypted metering report mac key as plain,
    // MEK and MAC key as encypted, and hash of
    // PKI public key used in encryition
    req->iCipherValue = iStorageClient->GetMeteringDataL( req->iRiId, macKey,
        req->iEncKeyHash, req->iEncryptedMekAndMak );

    // calculate mac over <encryptedMeteringReport>
    req->InsertMacL( macKey );

    CleanupStack::Pop( req );
    return req;

#endif //RD_DRM_METERING
    }

// ---------------------------------------------------------
// CRoapEng::HandleRoapResponseL()
// ---------------------------------------------------------
//
void CRoapEng::HandleRoapResponseL( const TDesC8& aXmlResponse )
    {
    LOGLIT( "CRoapEng::HandleRoapMessageL" )

    delete iResponse;
    iResponse = NULL;

    switch ( iReqMessage )
        {
        case EDeviceHello:
            {
            HandleRIHelloPduL( aXmlResponse );
            break;
            }
        case ERegistration:
            {
            HandleReqResponsePduL( aXmlResponse );
            break;
            }
        case EROAcquisition:
            {
            HandleRightsResponsePduL( aXmlResponse, EFalse );
            break;
            }
        case EJoinDomain:
            {
            HandleJoinDomainResponsePduL( aXmlResponse );
            break;
            }
        case ELeaveDomain:
            {
            HandleLeaveDomainResponsePduL( aXmlResponse );
            break;
            }
#ifdef RD_DRM_METERING
        case EMeteringRequest:
            {
            HandleMeteringReportResponsePduL( aXmlResponse );
            break;
            }
#endif
        default:
            {
            User::Leave( KErrArgument );
            }
        }
    }

// ---------------------------------------------------------
// CRoapEng::HandleRIHelloPduL()
// ---------------------------------------------------------
//
void CRoapEng::HandleRIHelloPduL( const TDesC8& aRiHello )
    {
    LOGLIT( "CRoapEng::HandleRIHelloPduL" )

    CRIHello* resp = NULL;
    HBufC8* temp = NULL;

    resp = iParser->ParseRIHelloL( aRiHello );
    iRoapStatus = resp->iStatus;
    iResponse = resp;
    if ( iRoapStatus == ESuccess )
        {
        iCertNeeded = ETrue;
        iRiSupportsCertCaching = EFalse;

        if ( resp->iPeerKeyIdentifier )
            {
            iRiSupportsCertCaching = ETrue;
            if ( resp->iPeerKeyId.Length() )
                {
                if ( resp->iPeerKeyId.CompareF( *iDeviceId ) == KErrNone )
                    {
                    iCertNeeded = EFalse;
                    }
                }
            else
                {
                iCertNeeded = EFalse;
                }
            }
        else if ( resp->iCertificateCaching )
            {
            iRiSupportsCertCaching = ETrue;
            }

        if ( resp->iAlgorithms.Count() )
            {
            iRiAlgorithms.ResetAndDestroy();
            for ( TInt i = 0; i < resp->iAlgorithms.Count(); i++ )
                {
                if ( resp->iAlgorithms[i]->CompareF( KCmlaIp1() ) == KErrNone )
                    {
                    iSelectedAlgorithms = ECmlaIp1;
                    }
                temp = resp->iAlgorithms[i]->AllocLC();
                iRiAlgorithms.AppendL( temp );
                CleanupStack::Pop( temp );
                }
            }
        iRiId.Copy( resp->iRiId );
        iRiVersion.Copy( resp->iSelectedVersion );

        /***
         This is needed when the multiple PKIs are supported.
         ***/
        if ( resp->iTrustedAuthorities.Count() )
            {
            // select the first matching root from the list
            LOGLIT( "Choose the first matching trust anchor" )
            iStorageClient->SelectTrustedRootL( resp->iTrustedAuthorities,
                iSelectedRoot );
            LOGLIT( "The trust anchor selected" )
            DETAILLOGHEX( iSelectedRoot.Ptr(), iSelectedRoot.Length() )
            }
        else
            {
            if ( iStoredRiContext && iStoredRiContext->RIID() == iRiId )
                {
                if ( iSelectedRoot != iStoredRiContext->SelectedDeviceRoot() )
                    {
                    DETAILLOGLIT( "Changing trusted root to that of saved RI context" )
                    DETAILLOGLIT( "old root" )
                    DETAILLOGHEX( iSelectedRoot.Ptr(), iSelectedRoot.Length() )

                    iSelectedRoot = iStoredRiContext->SelectedDeviceRoot();
                    iStorageClient->SelectTrustedRootL( iSelectedRoot );
                    }
                DETAILLOGLIT( "Using trusted root of saved RI context" )
                DETAILLOGHEX( iSelectedRoot.Ptr(), iSelectedRoot.Length() )
                }
            else
                {
                DETAILLOGLIT( "Using default trusted root" )
                iSelectedRoot = KNullDesC8;
                iStorageClient->SelectTrustedRootL( iSelectedRoot );
                }
            }

        iSigner->AddRequestL( aRiHello );
        }
    else if ( resp->iErrorUrl )
        {
        if ( iObserver )
            {
            iObserver->ErrorUrlL( *resp->iErrorUrl );
            }
        }
    }

// ---------------------------------------------------------
// CRoapEng::HandleReqResponsePduL()
// ---------------------------------------------------------
//
void CRoapEng::HandleReqResponsePduL( const TDesC8& aRegResp )
    {
    LOGLIT( "CRoapEng::HandleReqResponsePduL" )

    CRegistrationResp* resp = NULL;
    CDRMRIContext* context = NULL;
    CX509Certificate* cert = NULL;
    TTime riExpiry;
    TBool status = EFalse;
    TUint8 riCertCaching = EFalse;

    resp = iParser->ParseRegistrationRespL( aRegResp );
    iRoapStatus = resp->iStatus;
    iResponse = resp;
    if ( iRoapStatus == ESuccess )
        {
        if ( resp->iOcspResponse.Count() > 0 )
            {
            // adjust DRM Time according to OCSP response
            // All needed verifications done in server side
            TBool deviceTimeUpdated( EFalse );
            if ( resp->iCertificateChain.Count() > 0 )
                {
                deviceTimeUpdated = iStorageClient->UpdateDrmTimeL(
                    resp->iCertificateChain, resp->iOcspResponse,
                    iRegReqNonce );
                }
            else if ( iStoredRiContext )
                {
                deviceTimeUpdated = iStorageClient->UpdateDrmTimeL(
                    iStoredRiContext->CertificateChain(),
                    resp->iOcspResponse, iRegReqNonce );
                }
            if ( deviceTimeUpdated )
                {
                LOGLIT( "drm time updated" )
                iDeviceTimeError = EFalse;
                }
            }

        if ( !iStoredRiContext || ( resp->iCertificateChain.Count()
            && resp->iOcspResponse.Count() ) )
            {
            status = VerifyCertificateChainL( resp->iCertificateChain,
                resp->iOcspResponse );
            if ( !status )
                {
                LOGLIT( "Certificate chain validation failed" )
                User::Leave( KErrRoapServerFatal );
                }
            status = ValidateRiIdL( iRiId, *resp->iCertificateChain[0] );
            if ( !status )
                {
                LOGLIT( "RI ID validation failed" )
                User::Leave( KErrRoapServerFatal );
                }
            }

        if ( iStoredRiContext )
            {
            // if we have already stored certificates -> use those.
            status = VerifySignatureL( aRegResp, *resp->iSignature,
                iStoredRiContext->CertificateChain() );
            }
        else
            {
            // otherwise use the received certificates
            status = VerifySignatureL( aRegResp, *resp->iSignature,
                resp->iCertificateChain );
            }

        if ( !status )
            {
            LOGLIT( "Signature verification failed" )
            User::Leave( KErrRoapServerFatal );
            }

        if ( resp->iCertificateChain.Count() )
            {
            // Validate RI certificate
            cert = CX509Certificate::NewLC( *resp->iCertificateChain[0] );

            status = ValidateRiCertificateL( cert );
            if ( !status )
                {
                User::LeaveIfError( KErrRoapServerFatal );
                }

            riExpiry = cert->ValidityPeriod().Finish();

            iRiSupportsCertCaching ? riCertCaching = ETrue : riCertCaching
                = EFalse;

            context = CDRMRIContext::NewLC( iRiId, *iRiAlias, iRiVersion,
                iRiAlgorithms, resp->iWhiteList, *resp->iRiUrl, riExpiry,
                resp->iCertificateChain, resp->iOcspResponse, riCertCaching,
                iSelectedRoot, ETrue );

            iStorageClient->AddRIContextL( *context );
            delete iStoredRiContext;
            iStoredRiContext = context;
            CleanupStack::Pop( context );
            CleanupStack::PopAndDestroy( cert );
            }
        }
    else
        {
        if ( resp->iErrorUrl )
            {
            if ( iObserver )
                {
                iObserver->ErrorUrlL( *resp->iErrorUrl );
                }
            }
        iSigner->ResetResponses();
        }

    PERFORMANCE_LOGLIT( "Registration protocol completed" )
    }

// ---------------------------------------------------------
// CRoapEng::HandleRightsResponseL()
// ---------------------------------------------------------
//
void CRoapEng::HandleRightsResponsePduL(
    const TDesC8& aRightsResp,
    TBool aOnePass )
    {
    LOGLIT( "CRoapEng::HandleRightsResponsePduL" )

    CRightsResp* resp = NULL;
    TBool status = EFalse;

    resp = iParser->ParseRightsRespL( aRightsResp );

    CleanupStack::PushL( resp );

    if ( resp->iStatus == ESuccess )
        {
        if ( !aOnePass )
            {
            // 2-pass protocol
            __ASSERT_ALWAYS( iStoredRiContext, User::Invariant() );

            CRightsReq* request = NULL;
            request = STATIC_CAST( CRightsReq*, iRequest );
            if ( resp->iDeviceId.CompareF( request->iDeviceId ) != KErrNone
                || resp->iRiId.CompareF( request->iRiId ) != KErrNone
                || resp->iNonce->CompareF( request->iNonce ) != KErrNone )
                {
                User::Leave( KErrRoapServerFatal );
                }
            }
        else
            {
            LOGLIT( "1-pass ROAP" )
            // 1-pass protocol
            TBool registered = EFalse;
            GetRIContextL( registered, resp->iRiId );
            if ( !registered )
                {
                // Recoverable error by re-registering the device
                // (after receiving user consent or iv device belongs to whiteliust)
                LOGLIT( "Device not registered to RI" )
                User::Leave( KErrRoapNotRegistered );
                }
            if ( resp->iDeviceId.CompareF( *iDeviceId ) != KErrNone )
                {
                // Unrecoverable error
                LOGLIT( "Device ID mismatch!" )
                User::Leave( KErrRoapServerFatal );
                }
            }

        if ( !iStoredRiContext || ( resp->iCertificateChain.Count()
            && resp->iOcspResponse.Count() ) )
            {
            status = VerifyCertificateChainL( resp->iCertificateChain,
                resp->iOcspResponse );
            if ( !status )
                {
                LOGLIT( "Certificate chain validation failed" )
                User::Leave( KErrRoapServerFatal );
                }
            status = ValidateRiIdL( resp->iRiId, *resp->iCertificateChain[0] );
            if ( !status )
                {
                LOGLIT( "RI ID validation failed" )
                User::Leave( KErrRoapServerFatal );
                }
            }

        status = VerifySignatureL( aRightsResp, *resp->iSignature,
            iStoredRiContext->CertificateChain() );
        if ( !status )
            {
            LOGLIT( "Signature verification failed" )
            User::Leave( KErrRoapServerFatal );
            }

        iReturnedROs.ResetAndDestroy();
        TRAPD( r, iRoParser->ParseAndStoreL( aRightsResp, iReturnedROs ));

        if ( r == KErrRightsServerDomainNotRegistered )
            {
            // perform implicit Join Domain
            LOGLIT( "Domain RO received - Not joined" )
            LOGLIT( "Perform impicit Join Domain before storing the RO" )

            HBufC8* domainID = NULL;

            domainID = iRoParser->GetDomainIdL( aRightsResp );

            if ( domainID && domainID->Length() <= KDomainIdLength )
                {
                iDomainId.Copy( *domainID );
                delete domainID;
                domainID = NULL;
                }
            else
                {
                LOGLIT( "No Domain ID available!" )
                User::Leave( KErrRoapServerFatal );
                }

            delete iDomainRightsResp;
            iDomainRightsResp = NULL;
            iDomainRightsResp = aRightsResp.AllocL();
            iImplicitJoinDomain = ETrue;
            }
        else
            {
            User::LeaveIfError( r );

            if ( !aOnePass )
                {
                if ( iObserver )
                    {
                    iObserver->RightsObjectDetailsL( iReturnedROs ); // pass RO details to UI
                    }
                }
            }

            TRAP( r, InsertTransactionIDL( resp->iTransTrackIDs, resp->iContentIDs ) );
            TRAP( r, InsertDomainRosL() );

        // Device DRM Time is insecure, but server thinks that the time is correct
        // -> Set DRM Time as secure
        if ( !iSecureTime )
            {
            SetDrmTimeSecureL();
            }
        }
    else
        {
        if ( resp->iErrorUrl )
            {
            if ( iObserver )
                {
                iObserver->ErrorUrlL( *resp->iErrorUrl );
                }
            }
        iSigner->ResetResponses();
        }

    CleanupStack::Pop( resp );

    if ( !aOnePass )
        {
        iRoapStatus = resp->iStatus;
        iResponse = resp;
        }

    PERFORMANCE_LOGLIT( "RO acquisition protocol completed" )
    }

// ---------------------------------------------------------
// CRoapEng::HandleJoinDomainResponseL()
// ---------------------------------------------------------
//
void CRoapEng::HandleJoinDomainResponsePduL( const TDesC8& aJoinResp )
    {
    LOGLIT( "CRoapEng::HandleJoinDomainResponsePduL" )

    __ASSERT_ALWAYS( iStoredRiContext, User::Invariant() );

    CJoinDomainResp* resp = NULL;
    CDRMDomainContext* context = NULL;
    RPointerArray<HBufC8> domainKeyElements;
    TBool status = EFalse;

    CleanupResetAndDestroyPushL( domainKeyElements );

    resp = iParser->ParseJoinDomainRespL( aJoinResp, domainKeyElements );

    iResponse = resp;
    iRoapStatus = resp->iStatus;

    if ( iRoapStatus == ESuccess )
        {
        if ( resp->iDomainKeyRiId != resp->iRiId )
            {
            LOGLIT( "resp->iDomainKeyRiId != resp->iRiId" )
            User::Leave( KErrRoapServerFatal );
            }

        if ( !iStoredRiContext || ( resp->iCertificateChain.Count()
            && resp->iOcspResponse.Count() ) )
            {
            status = VerifyCertificateChainL( resp->iCertificateChain,
                resp->iOcspResponse );
            if ( !status )
                {
                LOGLIT( "Certificate chain validation failed" )
                User::Leave( KErrRoapServerFatal );
                }
            status = ValidateRiIdL( resp->iRiId, *resp->iCertificateChain[0] );
            if ( !status )
                {
                LOGLIT( "RI ID validation failed" )
                User::Leave( KErrRoapServerFatal );
                }
            }

        status = VerifySignatureL( aJoinResp, *resp->iSignature,
            iStoredRiContext->CertificateChain() );
        if ( !status )
            {
            LOGLIT( "Signature verification failed" )
            User::Leave( KErrRoapServerFatal );
            }

        if ( resp->iDomainKeys.Count() > 1 && resp->iDomainKeyIDs.Count() > 1
            && resp->iDomainKeys.Count() == resp->iDomainKeyIDs.Count() )
            {
            // Sort domain keys by generation (000 generation is first)
            TLex8 lex;
            TInt generation = 0;
            RArray<TInt> generations;
            CleanupClosePushL( generations );

            for ( TInt i = 0; i < resp->iDomainKeyIDs.Count(); i++ )
                {
                lex = resp->iDomainKeyIDs[i]->Right( KDomainGenerationLength );
                lex.Val( generation );
                generations.AppendL( generation );
                }

            SortArrays( resp->iDomainKeys, resp->iMacs, domainKeyElements,
                generations );

            CleanupStack::PopAndDestroy( &generations );
            }

        if ( !resp->iDomainKeys.Count() )
            {
            LOGLIT( "No valid domain keys present!" )
            User::Leave( KErrRoapServerFatal );
            }

#ifdef _DISABLE_HASH_CHAIN_GENERATION
        resp->iHashChainSupport = EFalse;
#endif

        if ( resp->iHashChainSupport )
            {
            if ( resp->iDomainKeys.Count() > 1 )
                {
                LOGLIT( "More than one Domain key present, hash chain key generation is supported!" )
                // Might be KErrRoapServerFatal server error
                }
            }

        context = CDRMDomainContext::NewLC( iDomainId,
            resp->iDomainExpiration, resp->iHashChainSupport,
            resp->iDomainKeys, resp->iRiId,
            iStoredRiContext->RightsIssuerURL() );
        iStorageClient->AddDomainContextL( *context, resp->iMacs,
            domainKeyElements, resp->iTransportScheme );
        iDomainId.SetLength( 0 );
        CleanupStack::PopAndDestroy( context );

        if ( iDomainRightsResp )
            {
            // It's a implicit Join Domain case
            // We still need to store the domain RO
            StoreDomainRightsL();
            }

        // Device DRM Time is insecure, but server thinks that the time is correct
        // -> Set DRM Time as secure
        if ( !iSecureTime )
            {
            SetDrmTimeSecureL();
            }
        }
    else
        {
        if ( resp->iErrorUrl )
            {
            if ( iObserver )
                {
                iObserver->ErrorUrlL( *resp->iErrorUrl );
                }
            }
        iSigner->ResetResponses();
        }
    CleanupStack::PopAndDestroy( &domainKeyElements );
    }

// ---------------------------------------------------------
// CRoapEng::HandleLeaveDomainResponseL()
// ---------------------------------------------------------
//
void CRoapEng::HandleLeaveDomainResponsePduL( const TDesC8& aLeaveResp )
    {
    LOGLIT( "CRoapEng::HandleLeaveDomainResponsePduL" )

    __ASSERT_ALWAYS( iStoredRiContext, User::Invariant() );

    CLeaveDomainResp* resp = NULL;
    resp = iParser->ParseLeaveDomainRespL( aLeaveResp );
    iRoapStatus = resp->iStatus;
    iResponse = resp;
    if ( iRoapStatus == ESuccess )
        {

        }
    else if ( resp->iErrorUrl )
        {
        if ( iObserver )
            {
            iObserver->ErrorUrlL( *resp->iErrorUrl );
            }
        }

    PERFORMANCE_LOGLIT( "Leave domain protocol completed" )
    }

// ---------------------------------------------------------
// CRoapEng::HandleMeteringReportResponsePduL()
// ---------------------------------------------------------
//
#ifndef RD_DRM_METERING
void CRoapEng::HandleMeteringReportResponsePduL( const TDesC8& /*aMeteringResp*/)
    {
    }
#else
void CRoapEng::HandleMeteringReportResponsePduL( const TDesC8& aMeteringResp )
    {
    LOGLIT( "CRoapEng::HandleMeteringReportResponsePduL" )
    __ASSERT_ALWAYS( iStoredRiContext, User::Invariant() );

    CMeteringResp* resp = NULL;
    CMeteringReportReq* request = NULL;

    resp = iParser->ParseMeteringRespL( aMeteringResp );

    request = static_cast<CMeteringReportReq*> ( iRequest );

    iRoapStatus = resp->iStatus;
    iResponse = resp;
    if ( iRoapStatus == ESuccess )
        {
        if ( resp->iDeviceId.CompareF( *iDeviceId ) != KErrNone
            || resp->iDeviceNonce->CompareF( request->iNonce ) != KErrNone )
            {
            LOGLIT( "Mismatch in deviceId or in nonce" )
            LOGLIT( "Observed DeviceId" )
            LOGHEX( resp->iDeviceId.Ptr(), resp->iDeviceId.Length() )
            LOGLIT( "Expected DeviceId" )
            LOGHEX( request->iDeviceId.Ptr(), request->iDeviceId.Length() )
            LOGLIT( "Observed nonce" )
            LOGHEX( resp->iDeviceNonce->Ptr(), resp->iDeviceNonce->Length() )
            LOGLIT( "Expected nonce" )
            User::Leave( KErrRoapServerFatal );
            }

        if ( !iStoredRiContext || ( resp->iCertificateChain.Count()
            && resp->iOcspResponse.Count() ) )
            {
            if ( !VerifyCertificateChainL( resp->iCertificateChain,
                resp->iOcspResponse ) )
                {
                LOGLIT( "Certificate chain validation failed" )
                User::Leave( KErrRoapServerFatal );
                }
            if ( !ValidateRiIdL( resp->iRiId, *resp->iCertificateChain[0] ) )
                {
                LOGLIT( "RI ID validation failed" )
                User::Leave( KErrRoapServerFatal );
                }
            }

        if ( !VerifySignatureL( aMeteringResp, *resp->iSignature,
            iStoredRiContext->CertificateChain() ) )
            {
            LOGLIT( "Signature verification failed" )
            User::Leave( KErrRoapServerFatal );
            }

        // Everything is fine, we can delete metering data
        iStorageClient->DeleteMeteringDataL( resp->iRiId );

        // notify PostResponseUrl for iObserver
        if ( resp->iPrUrl )
            {
            HBufC8* prUrl( resp->iPrUrl );
            LOGLIT( "PrUrl" )
            LOGHEX( prUrl->Ptr(), prUrl->Length() )
            if ( iObserver )
                {
                iObserver->PostResponseUrlL( *prUrl );
                LOGLIT( "Notified observer with PostResponseUrl" )
                }
            else
                {
                LOGLIT( "Warning no observer for PostResponseUrl" )
                }
            }
        }
    return;
    }
#endif //RD_DRM_METERING
// ---------------------------------------------------------
// CRoapEng::HandleMultipartL()
// ---------------------------------------------------------
//
void CRoapEng::HandleMultipartL()
    {
    LOGLIT( "CRoapEng::HandleMultipartL" )

    TInt rightsErr( KErrNone );
    TInt err( KErrNone );
    TInt docErr( KErrNone );
    TDataType type = TDataType();
    TBool mmcAllowed( EFalse );
    HBufC* contentName( NULL );
    RBuf newPath;
    TUid app_uid;
    RBuf rootPath;

    TRAP( rightsErr, HandleRoapResponseL( iRoapResp->ProtocolUnit() ) );

    newPath.CreateL( KMaxFileName );
    CleanupClosePushL( newPath  );

    CDocumentHandler* docHandler( CDocumentHandler::NewLC() );

    if ( iRoapResp->DcfFileName().Left( 1 ).CompareF( _L ("e") ) == 0 )
        {
        mmcAllowed = ETrue;
        }

    RFs fs;
    User::LeaveIfError( fs.Connect() );
    CleanupClosePushL( fs );

#ifndef RD_MULTIPLE_DRIVE
    rootPath.CreateL( mmcAllowed ?
        PathInfo::MemoryCardRootPath() :
        PathInfo::PhoneMemoryRootPath() );

#else //RD_MULTIPLE_DRIVE
    _LIT( KSysDriveRoot, "_:\\Data\\");
    _LIT( KMassDriveRoot, "_:\\" );
    TInt driveNumber( -1 );
    TChar driveLetter;

    if ( mmcAllowed )
        {
        // Set root path to memory card root
        rootPath.CreateL( KMassDriveRoot() );
        DriveInfo::GetDefaultDrive( DriveInfo::EDefaultMassStorage, driveNumber );
        }
    else
        {
        // Set root path to system root
        rootPath.CreateL( KSysDriveRoot() );
        DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
        }
    fs.DriveToChar( driveNumber, driveLetter );
    __ASSERT_ALWAYS( rootPath.Length()>0, User::Invariant() );
    rootPath[0] = (TUint)driveLetter;


#endif
    CleanupClosePushL( rootPath );

    iRoapResp->GetContentNameLC( contentName );

    if ( contentName && contentName->Length()
        && fs.IsValidName( *contentName ) )
        {
        if ( !rightsErr )
            {
            TRAP( err, docErr = docHandler->SilentMoveL( iRoapResp->DcfFileName(),
                    *contentName, rootPath, type, KEntryAttNormal ) );
            }
        else
            {
            // when an error occured during RO storing -> show "saved to" note
            TRAP( err, docErr = docHandler->MoveL( iRoapResp->DcfFileName(),
                    *contentName, type, KEntryAttNormal ) );
            }

        }
    else
        {
        if ( !rightsErr )
            {
            // use the default name
            User::LeaveIfError( docHandler->SilentMoveL(
                iRoapResp->DcfFileName(), KNullDesC(), rootPath, type,
                KEntryAttNormal ) );
            }
        else
            {
            // when an error occured during RO storing -> show "saved to" note
            docHandler->MoveL( iRoapResp->DcfFileName(), KNullDesC(), type,
                KEntryAttNormal );
            }
        }

    if ( err || docErr )
        {
        if ( !rightsErr )
            {
            // use the default name
            User::LeaveIfError( docHandler->SilentMoveL(
                iRoapResp->DcfFileName(), KNullDesC(), rootPath, type,
                KEntryAttNormal ) );
            }
        else
            {
            // when an error occured during RO storing -> show "saved to" note
            docHandler->MoveL( iRoapResp->DcfFileName(), KNullDesC(), type,
                KEntryAttNormal );
            }
        }
    User::LeaveIfError( rightsErr );

    User::LeaveIfError( docHandler->GetPath( newPath ) );
    User::LeaveIfError( docHandler->HandlerAppUid( app_uid ) );

    if ( iObserver )
        {
        iObserver->ContentDetailsL( newPath, type.Des8(), app_uid );
        }

    CleanupStack::PopAndDestroy( contentName );
    CleanupStack::PopAndDestroy( &rootPath );

    CleanupStack::PopAndDestroy( &fs );
    CleanupStack::PopAndDestroy( docHandler );
    CleanupStack::PopAndDestroy( &newPath );
    }

// ---------------------------------------------------------
// CRoapEng::SignMessageL()
// ---------------------------------------------------------
//
HBufC8* CRoapEng::SignMessageL( const TDesC8& aMessage ) const
    {
    LOGLIT( "CRoapEng::SignMessageL" )
    HBufC8* r = NULL;

    if ( iReqMessage == EDeviceHello )
        {
        // Device Hello always resets signing chain!!
        iSigner->ResetRequests();
        iSigner->ResetResponses();
        // Device Hello or RI Hello is not signed
        iSigner->AddRequestL( aMessage );
        r = aMessage.AllocL();
        }
    else
        {
        r = iSigner->SignAndAddRequestL( aMessage );
        iSigner->ResetRequests();

        if ( iReqMessage == ERegistration )
            {
            // Add signed request to the signer for verifying signature on
            // response (for Registration protocol only).
            iSigner->AddResponseL( *r );
            }
        }
    return r;
    }

// ---------------------------------------------------------
// CRoapEng::VerifySignatureL()
// ---------------------------------------------------------
//
TBool CRoapEng::VerifySignatureL(
    const TDesC8& aMessage,
    const TDesC8& aSignature,
    const RPointerArray<HBufC8>& aCertificateChain ) const
    {
    LOGLIT( "CRoapEng::VerifySignatureL" )

    TBool isValid = ETrue;

    if ( iReqMessage != EDeviceHello && iReqMessage != ELeaveDomain )
        {
        // RI Hello and Leave Domain resp are not signed
        isValid = iSigner->VerifyAndAddResponseL( aMessage, aSignature,
            aCertificateChain );
        iSigner->ResetResponses();
        }

#ifdef _DISABLE_SIGNATURE_CHECK
    isValid = ETrue;
#endif
    return isValid;
    }

// ---------------------------------------------------------
// CRoapEng::VerifyTriggerSignatureL()
// ---------------------------------------------------------
//
TBool CRoapEng::VerifyTriggerSignatureL(
    const TDesC8& aXmlTrigger,
    const CRoapTrigger& aTrigger ) const
    {
    LOGLIT( "CRoapEng::ValidateTriggerSignatureL" )

    TPtrC8 element( KNullDesC8 );
    TPtrC8 signedInfo( KNullDesC8 );
    CDRMDomainContext* context( NULL );
    HBufC8* domainKey( NULL );
    HBufC8* unwrappedMacKey( NULL );
    CSHA1* digest( NULL );
    CMessageDigest* hMac( NULL );
    TBool result( ETrue );
    TInt pos( 0 );
    TInt generation( 0 );

    element.Set( iParser->ExtractElement( aXmlTrigger, KLeaveDomainElement(),
        pos ) );
    pos = 0;
    signedInfo.Set( iParser->ExtractElement( aXmlTrigger,
        KSignedInfoElement(), pos ) );

    if ( !element.Length() || !signedInfo.Length() || !aTrigger.iEncKey
        || !aTrigger.iSignature )
        {
        User::Leave( KErrRoapServerFatal );
        }

    context = iStorageClient->GetDomainContextL( *aTrigger.iDomainId );

    if ( !context )
        {
        // we are not member of the domain
        LOGLIT( "No DomainContext for the domain -> Cannot verify Trigger signature" )
        return ETrue;
        }
    CleanupStack::PushL( context );

    TLex8 lex( aTrigger.iDomainId->Right( KDomainGenerationLength ) );
    lex.Val( generation );
    domainKey = context->DomainKeyL( generation );
    User::LeaveIfNull( domainKey );
    CleanupStack::PushL( domainKey );

    unwrappedMacKey = OmaCrypto::AesUnwrapL( *domainKey, *aTrigger.iEncKey );
    CleanupStack::PopAndDestroy( domainKey );
    CleanupStack::PushL( unwrappedMacKey );

    // hash the leaveDomain element
    digest = CSHA1::NewL();
    CleanupStack::PushL( digest );
    digest->Update( element );

    if ( digest->Final().CompareF( *aTrigger.iDigestValue ) )
        {
        LOGLIT( "Reference Validation failed!" )
        result = EFalse;
        }

    if ( result )
        {
        // calculate HMAC signature
        hMac = CMessageDigestFactory::NewHMACLC( CMessageDigest::ESHA1,
            *unwrappedMacKey );
        hMac->Update( signedInfo );

        if ( hMac->Final().CompareF( *aTrigger.iSignature ) != 0 )
            {
            LOGLIT( "Signature Validation failed!" )
            result = EFalse;
            }
        CleanupStack::PopAndDestroy( hMac );
        }

    CleanupStack::PopAndDestroy( digest );
    CleanupStack::PopAndDestroy( unwrappedMacKey );
    CleanupStack::PopAndDestroy( context );

    if ( !result )
        {
        LOGLIT( "Trigger signature check failed!" )
        }

#ifdef _DISABLE_SIGNATURE_CHECK
    result = ETrue;
#endif
    return result;
    }

// ---------------------------------------------------------
// CRoapEng::VerifyCertificateChainL()
// ---------------------------------------------------------
//
TBool CRoapEng::VerifyCertificateChainL(
    const RPointerArray<HBufC8>& aCertificateChain,
    const RPointerArray<HBufC8>& aOcspResponses ) const
    {
    LOGLIT( "CRoapEng::VerifyCertificateChainL" )

    CX509Certificate* cert = NULL;
    CX509Certificate* signingCert = NULL;
    CX509Certificate* riCA = NULL;
    TBool result = EFalse;
    RPointerArray<HBufC8> serialNums;
    CX500DistinguishedName* rootDistName = NULL;
    HBufC* rootName = NULL;
    HBufC8* rootCert = NULL;
    HBufC8* temp = NULL;

    if ( !aCertificateChain.Count() || !aOcspResponses.Count() )
        {
        User::Leave( KErrRoapServerFatal );
        }

    // Get the last cert from the chain. It is signed by some of our trusted anchor
    riCA = CX509Certificate::NewLC(
        *( aCertificateChain[aCertificateChain.Count() - 1] ) );
    rootDistName = CX500DistinguishedName::NewLC( riCA->IssuerName() );
    rootName = rootDistName->DisplayNameL();
    CleanupStack::PushL( rootName );
    // Get the correct root cert for validating the whole chain
    rootCert = iStorageClient->GetRootCertificateL( *rootName );
    CleanupStack::PopAndDestroy( rootName );
    CleanupStack::PopAndDestroy( rootDistName );
    CleanupStack::PopAndDestroy( riCA );

    if ( !rootCert )
        {
        LOGLIT( "No root certificate present!" )
        LOGLIT( "Certificate chain verification failed." )
        return EFalse;
        }
    CleanupStack::PushL( rootCert );

    CleanupResetAndDestroyPushL( serialNums );

    for ( TInt i = 0; i < aCertificateChain.Count(); i++ )
        {
        cert = CX509Certificate::NewLC( *aCertificateChain[i] );
        temp = cert->SerialNumber().AllocLC();
        serialNums.AppendL( temp );
        CleanupStack::Pop( temp );
        if ( aCertificateChain.Count() - 1 == i )
            {
            // signingCert = Trusted root cert
            signingCert = CX509Certificate::NewLC( *rootCert );
            }
        else
            {
            signingCert = CX509Certificate::NewLC( *aCertificateChain[i + 1] );
            }
        result = cert->VerifySignatureL( signingCert->PublicKey().KeyData() );

#ifdef _DISABLE_CERT_CHECK
        result = ETrue;
#endif
        CleanupStack::PopAndDestroy( signingCert );
        CleanupStack::PopAndDestroy( cert );
        if ( !result )
            {
            LOGLIT( "Certificate chain verification failed." )
            CleanupStack::PopAndDestroy( 2, rootCert );
            return result;
            }
        }

    if ( aCertificateChain.Count() >= 2 )
        {
        result = VerifyOcspResponsesL( aOcspResponses, *aCertificateChain[1],
            serialNums );

        if ( !result )
            {
            // CoreMedia's OCSP responder cert is signed by the root -> against CMLA spec
            LOGLIT( "Try to verify OCSP response cert using root cert" )
            result = VerifyOcspResponsesL( aOcspResponses, *rootCert,
                serialNums );
            }
        }
    else if ( aCertificateChain.Count() == 1 )
        {
        // There is only one cert in the cert chain -> the OCSP response cert is verified with root cert
        result = VerifyOcspResponsesL( aOcspResponses, *rootCert, serialNums );
        }
    else
        {
        result = EFalse;
        }

    if ( iStoredRiContext && aCertificateChain.Count() && result )
        {
        cert = CX509Certificate::NewLC( *aCertificateChain[0] );
        if ( iStoredRiContext->ExpiryTime() < cert->ValidityPeriod().Finish() )
            {
            iStoredRiContext->SetCertificateChainL( aCertificateChain );
            iStoredRiContext->SetOCSPResponseL( aOcspResponses );

            // update RI Context
            iStorageClient->AddRIContextL( *iStoredRiContext );
            }
        CleanupStack::PopAndDestroy( cert );
        }

    CleanupStack::PopAndDestroy( 2, rootCert ); // serialNums, rootCert

#ifdef _ROAP_TESTING
    if ( result )
        {
        LOGLIT( "Certificate chain verification ok." )
        }
    else
        {
        LOGLIT( "Certificate chain verification failed." )
        }
#endif
#ifdef _DISABLE_CERT_CHECK
    result = ETrue;
#endif

    return result;
    }

// ---------------------------------------------------------
// CRoapEng::VerifyOcspResponsesL()
// ---------------------------------------------------------
//
TBool CRoapEng::VerifyOcspResponsesL(
    const RPointerArray<HBufC8>& aOcspResponses,
    const TDesC8& aRiCaCert,
    const RPointerArray<HBufC8>& aCertSerialNums ) const
    {
    LOGLIT( "CRoapEng::VerifyOcspResponsesL" )

#ifdef _DISABLE_OCSP_CHECK
    TBool result( ETrue );
#else
    // Get verification result from the server
    TBool result( iStorageClient->VerifyOcspResponsesL( aOcspResponses,
        aRiCaCert, aCertSerialNums ) );
#endif

#ifdef _ROAP_TESTING
    if ( result )
        {
        LOGLIT( "OCSP response verification ok." )
        }
    else
        {
        LOGLIT( "OCSP response verification failed." )
        }
#endif

    return result;
    }

// ---------------------------------------------------------
// CRoapEng::ValidateRiCertificateL()
// ---------------------------------------------------------
//
TBool CRoapEng::ValidateRiCertificateL( const CX509Certificate* aCert )
    {
    LOGLIT( "CRoapEng::ValidateRiCertificateL" )

    TBool ret = ETrue;
    const CX509CertExtension* ext = NULL;
    CX509KeyUsageExt* keyUsageExt = NULL;
    CX509ExtendedKeyUsageExt* extendedKeyUsage = NULL;
    TTime riExpiry;
    TInt count = 0;

    if ( iSelectedAlgorithms == ECmlaIp1 )
        {
        // Check RI certificate extensions only in CMLA case
        ext = aCert->Extension( KKeyUsage() );
        if ( !ext || !( ext->Critical() ) )
            {
            LOGLIT( "RI cert KeyUsage extension missing or not critical!" )
            ret = EFalse;
            }

        if ( ext && ret )
            {
            keyUsageExt = CX509KeyUsageExt::NewLC( ext->Data() );
            if ( !keyUsageExt->IsSet( EX509DigitalSignature ) )
                {
                LOGLIT( "DigitalSignature bit is not set in KeyUsageExt of RI cert!" )
                ret = EFalse;
                }
            CleanupStack::PopAndDestroy( keyUsageExt );
            }

        ext = aCert->Extension( KExtendedKeyUsage() );
        if ( !ext || !( ext->Critical() ) )
            {
            LOGLIT( "RI cert ExtendedKeyUsage extension missing or not critical!" )
            ret = EFalse;
            }

        if ( ext && ret )
            {
            ret = EFalse;
            extendedKeyUsage = CX509ExtendedKeyUsageExt::NewLC( ext->Data() );
            count = extendedKeyUsage->KeyUsages().Count();
            for ( TInt i = 0; i < count && !ret; i++ )
                {
                if ( extendedKeyUsage->KeyUsages().At( i )->CompareF(
                    KOmaKpRightsIssuerOid() ) == 0 )
                    {
                    ret = ETrue;
                    }
                }
            if ( !ret )
                {
                LOGLIT( "OmaKpRightsIssuer OID is not set in ExtendedKeyUsageExt of RI cert!" )
                }
            CleanupStack::PopAndDestroy( extendedKeyUsage );
            }
        }

    riExpiry = aCert->ValidityPeriod().Finish();

    if ( riExpiry < GetDrmTimeL() )
        {
        LOGLIT( "RI Context certificate is expired!" )
        ret = EFalse;
        }
#ifdef _DISABLE_CERT_CHECK
    ret = ETrue;
#endif
    return ret;
    }

// ---------------------------------------------------------
// CRoapEng::GetCertificateChainL()
// ---------------------------------------------------------
//
RPointerArray<HBufC8> CRoapEng::GetCertificateChainL() const
    {
    LOGLIT( "CRoapEng::GetCertificateChainL ->" )

    TInt err = KErrNone;
    RPointerArray<HBufC8> certificateChain;
    HBufC8* root = NULL;

    CleanupResetAndDestroyPushL( certificateChain );

    err = iStorageClient->GetDeviceCertificateChainL( certificateChain );

    if ( err )
        {
        User::Leave( err );
        }

    if ( certificateChain.Count() < KMinCertChainLength )
        {
        // the CMLA chain must always contain:
        // the device certificate,
        // at lest one signing certificate (device CA),
        // and the root certificate
        DETAILLOGLIT( "Got improper certificate chain!!" )
        // Leaving in production devices.
        User::Leave( KErrRoapDevice );
        __ASSERT_DEBUG( ETrue, User::Invariant() );
        }

    // delete and remove the root certificate (it's always the last one in the list)
    root = certificateChain[certificateChain.Count() - 1];
    delete root;
    certificateChain.Remove( certificateChain.Count() - 1 );

    CleanupStack::Pop( &certificateChain );

    LOGLIT( "CRoapEng::GetCertificateChainL <-" )

    return certificateChain;
    }

// ---------------------------------------------------------
// CRoapEng::GetDeviceDetailsL()
// ---------------------------------------------------------
//
void CRoapEng::GetDeviceDetailsL(
    HBufC8*& aManufacturer,
    HBufC8*& aModel,
    HBufC8*& aVersion )
    {
    LOGLIT( "-> CRoapEng::GetDeviceDetailsL" )

#if 0
    TInt numPhone = 0;
    TUint32 caps = 0;
    TName tsyName;
    RMobilePhone phone;
    RTelServer etelServer;
    RTelServer::TPhoneInfo phoneInfo;
    HBufC* version = NULL;

    User::LeaveIfError( etelServer.Connect() );

    CleanupClosePushL( etelServer );

    User::LeaveIfError( etelServer.LoadPhoneModule( KMmTsyModuleName ) );
    User::LeaveIfError( etelServer.EnumeratePhones( numPhone) );

    for (TInt i(0); i < numPhone; i++)
        {
        User::LeaveIfError( etelServer.GetPhoneInfo( i, phoneInfo ) );
        User::LeaveIfError( etelServer.GetTsyName( i,tsyName ) );

        if ( tsyName.CompareF( KMmTsyModuleName ) == 0)
            {
            break;
            }
        }

    User::LeaveIfError( phone.Open( etelServer, phoneInfo.iName ) );
    CleanupClosePushL( phone );

    phone.GetIdentityCaps( caps );
    if ( !( caps & RMobilePhone::KCapsGetManufacturer ) &&
        !( caps & RMobilePhone::KCapsGetModel ) )
        {
        User::Leave( KErrRoapGeneral );
        }

    RMobilePhone::TMobilePhoneIdentityV1 details;
    TRequestStatus status;

    phone.GetPhoneId( status, details );
    User::WaitForRequest( status );

    User::LeaveIfError( status.Int() );

    HBufC8* manufacturer( HBufC8::NewLC( details.iManufacturer.Length() ) );
    manufacturer->Des().Copy( details.iManufacturer );
    HBufC8* model( HBufC8::NewLC( details.iModel.Length() ) );
    model->Des().Copy( details.iModel );

    version = HBufC::NewLC( KSysUtilVersionTextLength );
    TPtr ptr( version->Des() );
    User::LeaveIfError( SysUtil::GetSWVersion( ptr ) );

    // remove possible BOM from the end
    if ( ptr.Right( KBOM1().Length() ).CompareF( KBOM1 ) == KErrNone )
        {
        ptr.Delete( ptr.Length() - KBOM1().Length(), KBOM1().Length() );
        }
    if ( ptr.Right( KBOM2().Length() ).CompareF( KBOM2 ) == KErrNone )
        {
        ptr.Delete( ptr.Length() - KBOM2().Length(), KBOM2().Length() );
        }

    aVersion = CnvUtfConverter::ConvertFromUnicodeToUtf8L( ptr );

    CleanupStack::PopAndDestroy( version );
    CleanupStack::Pop( model );
    CleanupStack::Pop( manufacturer );
    aManufacturer = manufacturer;
    aModel = model;
    CleanupStack::PopAndDestroy( &phone );
    CleanupStack::PopAndDestroy( &etelServer );
#else
    aManufacturer = _L8("Nokia").AllocL();
    aModel = _L8("Emulator").AllocL();
    aVersion = _L8("9.0").AllocL();
#endif

    LOGLIT( "Device details:" )
    LOGLIT( "   Manufacturer: " )
    LOG( aManufacturer->Des() )
    LOGLIT( "   Model: " )
    LOG( aModel->Des() )
    LOGLIT( "   Revision: " )
    LOG( aVersion->Des() )

    LOGLIT( "<- CRoapEng::GetDeviceDetailsL" )
    }

// ---------------------------------------------------------
// CRoapEng::FetchTransactionIDL()
// ---------------------------------------------------------
//
void CRoapEng::FetchTransactionIDL(
    RPointerArray<HBufC8>& aTransIDs,
    RPointerArray<HBufC8>& aContentIDs )
    {
    LOGLIT( "CRoapEng::FetchTransactionIDL" )

    __ASSERT_ALWAYS( iTrigger, User::Invariant() );

    RArray<TPair> array;
    TInt err = KErrNone;

    CleanupClosePushL( array );

    if ( !iTrigger->iContentIdList.Count() )
        {
        CleanupStack::PopAndDestroy( &array );
        return;
        }

    if ( iTransStatus == ENotAsked && iObserver )
        {
        UpdateTransactionTrackingStatusL();
        }
    if ( iTransStatus == EAllowed )
        {
        for ( TInt i = 0; i < iTrigger->iContentIdList.Count(); i++ )
            {
            TPair pair;
            pair.iCid = iTrigger->iContentIdList[i]->Alloc(); // duplicate contentID,
            pair.iTtid = NULL; // pair.iCid is deleted by iRequest
            err = array.Append( pair );
            if ( err )
                {
                delete pair.iCid;
                pair.iCid = NULL;
                }
            }

            TRAP_IGNORE(iDcfRep->GetTtidL( array ) );

        for ( TInt i = 0; i < array.Count(); i++ )
            {
            if ( array[i].iTtid && array[i].iCid && array[i].iTtid->Length()
                && array[i].iCid->Length() )
                {
                err = aContentIDs.Append( array[i].iCid );
                if ( !err )
                    {
                    aTransIDs.Append( array[i].iTtid );
                    }
                else
                    {
                    delete array[i].iCid;
                    array[i].iCid = NULL;
                    delete array[i].iTtid;
                    array[i].iTtid = NULL;
                    }
                }
            else if ( array[i].iTtid || array[i].iCid )
                {
                delete array[i].iTtid;
                array[i].iTtid = NULL;
                delete array[i].iCid;
                array[i].iCid = NULL;
                }
            }
        }
    CleanupStack::PopAndDestroy( &array );
    }

// ---------------------------------------------------------
// CRoapEng::InsertTransactionIDL()
// ---------------------------------------------------------
//
void CRoapEng::InsertTransactionIDL(
    RPointerArray<HBufC8>& aTransIDs,
    RPointerArray<HBufC8>& aContentIDs )
    {
    LOGLIT( "CRoapEng::InsertTransactionIDL" )

    RArray<TPair> array;
    TRequestStatus status;

    CleanupClosePushL( array );

    if ( !aTransIDs.Count() || !aContentIDs.Count() )
        {
        LOGLIT( "Insert ttID: Wrong input data" )
        CleanupStack::PopAndDestroy( &array );
        return;
        }

    if ( aTransIDs.Count() != aContentIDs.Count() )
        {
        LOGLIT( "Insert ttID: ttID.Count != cid.Count" )
        CleanupStack::PopAndDestroy( &array );
        return;
        }

    if ( iTransStatus == ENotAsked && iObserver )
        {
        UpdateTransactionTrackingStatusL();
        }
    if ( iTransStatus == EAllowed )
        {
        for ( TInt i = 0; i < aContentIDs.Count() && i < aTransIDs.Count(); i++ )
            {
            TPair pair;
            pair.iCid = aContentIDs[i];
            pair.iTtid = aTransIDs[i];
            array.Append( pair );
            }

        iDcfRep->SetTtid( array, status );
        User::WaitForRequest( status );
        }

    CleanupStack::PopAndDestroy( &array );
    }

// ---------------------------------------------------------
// CRoapEng::GetOCSPResponderKeyHashL()
// ---------------------------------------------------------
//
HBufC8* CRoapEng::GetOCSPResponderKeyHashL() const
    {
    LOGLIT( "CRoapEng::GetOCSPResponderKeyHashL" )

    if ( !iStoredRiContext )
        {
        User::Leave( KErrRoapNotRegistered );
        }
    return iStorageClient->GetOcspResponderIdL( iStoredRiContext->RIID() );
    }

// ---------------------------------------------------------
// CRoapEng::GetDrmTimeL()
// ---------------------------------------------------------
//
TTime CRoapEng::GetDrmTimeL()
    {
    LOGLIT( "CRoapEng::GetDrmTimeL" )

    TTime drmTime;
    DRMClock::ESecurityLevel secureTime;
    TInt zone( 0 );

    User::LeaveIfError( iClockClient->GetSecureTime( drmTime, zone,
        secureTime ) );

    if ( secureTime == DRMClock::KInsecure )
        {
        iSecureTime = EFalse;
        }
    else
        {
        iSecureTime = ETrue;
        }

    return drmTime;
    }

// ---------------------------------------------------------
// CRoapEng::SetDrmTimeSecureL()
// ---------------------------------------------------------
//
void CRoapEng::SetDrmTimeSecureL()
    {
    LOGLIT( "CRoapEng::SetDrmTimeSecureL" )

    TTime drmTime;
    DRMClock::ESecurityLevel secureTime;
    TInt zone( 0 );

    User::LeaveIfError( iClockClient->GetSecureTime( drmTime, zone,
        secureTime ) );
    User::LeaveIfError( iClockClient->UpdateSecureTime( drmTime, zone ) );

    iSecureTime = ETrue;
    }

// ---------------------------------------------------------
// CRoapEng::AdjustDrmTime()
// ---------------------------------------------------------
//
void CRoapEng::AdjustDrmTimeL(
    const RPointerArray<HBufC8>& aOcspResponses,
    TDesC8& aRegReqNonce ) const
    {
    // To be removed on next API change.
    // Replace calls with direct call to RRoapStorageClient.
    LOGLIT( "CRoapEng::AdjustDrmTime calling server" )
    if ( aOcspResponses.Count() > 0 )
        {
        iStorageClient->UpdateDrmTimeL( iStoredRiContext->CertificateChain(),
            aOcspResponses, aRegReqNonce );
        }
    else
        {
        LOGLIT( "No OCSP responses present." )
        }
    }

// ---------------------------------------------------------
// CRoapEng::StoreDomainRightsL()
// ---------------------------------------------------------
//
void CRoapEng::StoreDomainRightsL()
    {
    LOGLIT( "CRoapEng::StoreDomainRightsL" )

    RPointerArray<CDRMRights> returnedROs;

    CleanupResetAndDestroyPushL( returnedROs );

    iRoParser->ParseAndStoreL( *iDomainRightsResp, returnedROs );

    if ( iObserver )
        {
        iObserver->RightsObjectDetailsL( returnedROs ); // pass RO details to UI
        }

    delete iDomainRightsResp;
    iDomainRightsResp = NULL;

    iImplicitJoinDomain = EFalse;

    CleanupStack::PopAndDestroy( &returnedROs );
    }

// ---------------------------------------------------------
// CRoapEng::InsertDomainRosL()
// ---------------------------------------------------------
//
void CRoapEng::InsertDomainRosL()
    {
    CDcfRep* rep = NULL;
    CDcfEntry* entry = NULL;
    CContent* content = NULL;
    TPtr8 ptr( NULL, 0 );
    TInt i;
    RFile file;
    RFs fs;
    TInt error( 0 );

    User::LeaveIfError( fs.Connect() );
    CleanupClosePushL( fs );
    rep = CDcfRep::NewL();
    CleanupStack::PushL( rep );
    for ( i = 0; i < iReturnedROs.Count(); i++ )
        {
        if ( iReturnedROs[i]->GetPermission().iDomainID )
            {
            rep->OrderListL( *iReturnedROs[i]->GetAsset().iUid );
            entry = rep->NextL();
            while ( entry )
                {
                CleanupStack::PushL( entry );
                error = file.Open( fs, entry->FileName(), EFileWrite
                    | EFileShareReadersOrWriters );
                if ( !error )
                    {
                    CleanupClosePushL( file );
                    content = CContent::NewLC( file );
                    content->AgentSpecificCommand( EEmbedDomainRo,
                        KNullDesC8, ptr );
                    CleanupStack::PopAndDestroy( 2, &file ); // content, file
                    }
                CleanupStack::PopAndDestroy( entry );
                entry = rep->NextL();
                }
            }
        }
    CleanupStack::PopAndDestroy( 2, &fs ); // rep, fs
    }

// ---------------------------------------------------------
// CRoapEng::MapStatusL()
// ---------------------------------------------------------
//
TInt CRoapEng::MapStatusL()
    {
    LOGLIT( "CRoapEng::MapStatusL" )

    if ( iRoapStatus == ESuccess )
        {
        LOGLIT( "ROAP Status: success " )
        return KErrNone;
        }

    if ( iRoapStatus == ENotRegistered || iRoapStatus == EDeviceTimeError )
        {
        // Initiate registration protocol
        LOG2( _L ( "Not Registered! Status: %d" ), iRoapStatus )

        if ( iRoapStatus == EDeviceTimeError )
            {
            iDeviceTimeError = ETrue;
            }

        return KErrRoapNotRegistered;
        }

    LOG2( _L ( "ROAP Error! Status: %d" ), iRoapStatus )

    switch ( iRoapStatus )
        {
        case EUnknownError:
        case EAbort:
            {
            User::Leave( KErrRoapServer );
            }
        case ENotSupported:
        case EAccessDenied:
        case ENotFound:
        case EMalformedRequest:
        case EUnknownRequest:
        case EUnknownCriticalExtension:
        case EUnsupportedVersion:
        case EUnsupportedAlgorithm:
        case ESignatureError:
        case EInvalidDCFHash:
            {
            User::Leave( KErrRoapServerFatal );
            }
        case ENoCertificateChain:
        case EInvalidCertificateChain:
        case ETrustedRootCertificateNotPresent:
            {
            User::Leave( KErrRoapDevice );
            }
        case EInvalidDomain:
            {
            User::Leave( KErrRoapInvalidDomain );
            }
        case EDomainFull:
            {
            User::Leave( KErrRoapDomainFull );
            }
        default:
            {
            User::Leave( KErrRoapUnsupported );
            }
        }
    return KErrNone;
    }

// ---------------------------------------------------------
// CRoapEng::ValidateRiIdL()
//
// Validates that RI ID equals to public key hash of RI certificate
// ---------------------------------------------------------
//
TBool CRoapEng::ValidateRiIdL( TDesC8& aRiId, TDesC8& aCertBuf )
    {
    TBool valid = EFalse;
    CX509Certificate* riCert = NULL;
    CSHA1* hash = NULL;
    HBufC8* publicKeyHash = NULL;

    riCert = CX509Certificate::NewLC( aCertBuf );

    // hash the SubjectPublicKeyInfo element
    hash = CSHA1::NewL();
    CleanupStack::PushL( hash );
    hash->Hash( *riCert->DataElementEncoding(
        CX509Certificate::ESubjectPublicKeyInfo ) );
    publicKeyHash = hash->Final().AllocLC();

    if ( aRiId.Compare( *publicKeyHash ) == KErrNone )
        {
        valid = ETrue;
        }

    CleanupStack::PopAndDestroy( publicKeyHash );
    CleanupStack::PopAndDestroy( hash );
    CleanupStack::PopAndDestroy( riCert );

    return valid;
    }

// ---------------------------------------------------------
// CRoapEng::UpdateTransactionTrackingStatusL()
//
// Update the status of transaction tracking variable
// ---------------------------------------------------------
//
void CRoapEng::UpdateTransactionTrackingStatusL()
    {
    TInt value = KErrNone;
    CRepository* repository = CRepository::NewL( KCRUidDRMSettings );
    repository->Get( KDRMSettingsTransactionTracking, value );
    delete repository;
    iTransStatus = value ? EAllowed : EForbidden;
    }

// ---------------------------------------------------------
// CRoapEng::CreateDeviceIdHashArrayL()
// ---------------------------------------------------------
//
TInt CRoapEng::CreateDeviceIdHashArrayL( RPointerArray<TDesC8>& aIdArray )
    {
    TInt err( KErrNone );
    RPointerArray<HBufC8> certChain;
    CSHA1* hasher = NULL;
    HBufC8* publicKey = NULL;
    CX509Certificate* cert = NULL;

    err = iStorageClient->GetDeviceCertificateChainL( certChain );

    CleanupResetAndDestroyPushL( certChain );

    hasher = CSHA1::NewL();
    CleanupStack::PushL( hasher );
    // take the hash of device certificate
    if (certChain.Count()<=0)
        {
        LOGLIT( "Could get Device id Hash!!!" )
        User::Leave( KErrGeneral );
        }
    cert = CX509Certificate::NewL( *certChain[0] );
    CleanupStack::PushL( cert );
    publicKey = cert->DataElementEncoding(
        CX509Certificate::ESubjectPublicKeyInfo )->AllocLC();

    hasher->Hash( *publicKey );

    HBufC8 *elem( hasher->Final().AllocLC() );
    aIdArray.AppendL( elem );
    CleanupStack::Pop( elem );

    CleanupStack::PopAndDestroy( publicKey );
    CleanupStack::PopAndDestroy( cert );

    CleanupStack::PopAndDestroy( hasher );
    CleanupStack::PopAndDestroy( &certChain );
    return err;
    }

// End of file
