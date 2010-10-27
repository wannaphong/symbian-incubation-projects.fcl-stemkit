/*
* Copyright (c) 2003 - 2007 Nokia Corporation and/or its subsidiary(-ies).
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
* Description:  DRM3 Engine manages all DRM related database operations.
*
*/


// INCLUDE FILES
#include <e32std.h>
#include <e32test.h>
#include <etelmm.h>
#include <DRMTypes.h>
#include <starterclient.h>
#include <featmgr.h>

#ifdef RD_MULTIPLE_DRIVE
#include <driveinfo.h>
#endif

#include "DRMRightsServer.h"
#include "drmrightsdb.h"
#include "DRMDbSession.h"
#include "DRMEngineClientServer.h"
#include "drmlog.h"
#include "DRMNotifier.h"
#include "DrmKeyStorage.h"
#include "DRMNotifierServer.h"
#include "RoapStorageServer.h"
#include "drmnotifierclientserver.h"
#include "drmroapclientserver.h"
#include "DRMXOma.h"
#include "DRMBackupObserver.h"
#include "DRMBackup.h"
#include "flogger.h"
#include "DrmRightsParser.h"
#include "DRMRights.h"
#include "DRMHelperCommon.h"

#include  "wmdrmclientwrapper.h"



#ifdef __DRM_CLOCK
#include "DRMClockServer.h"
#include "drmclockclientserver.h"
#endif

#include <utf.h>
#include <DRMIndividualConstraintExtension.h>

/*
_LIT( KLogDir, "drm");
_LIT( KLogName, "backup.log");
*/

// EXTERNAL DATA STRUCTURES
// EXTERNAL FUNCTION PROTOTYPES
// CONSTANTS
LOCAL_C const TUint KMaxHeapsize = 0x7A120;

_LIT8( KImsiId,"IMSI:");

// MACROS
#ifdef _DRM_TESTING
_LIT( KDateTimeFormat, "%F%Y%M%D%H%T%S%C" );
#endif

// LOCAL CONSTANTS AND MACROS
const TUint8 KMaxStartTries = 30;
const TInt KWaitingTime = 2000000; // 2 sec
_LIT( KRightsServerThread, "RightsServer" );

#ifdef RD_MULTIPLE_DRIVE

_LIT( KDbTempPath, "%c:\\system\\temp\\" );
_LIT( KIndividualConstraintExtensionDll, "%c:\\sys\\bin\\DRMIndividualConstraintExtension.dll" );
_LIT( KRightsDir, "%c:\\private\\101F51F2\\rdb\\" );
_LIT( KTimedReplayCacheFile, "%c:\\private\\101F51F2\\timererc.dat" );
_LIT( KPlainReplayCacheFile, "%c:\\private\\101F51F2\\plainrc.dat" );
#ifdef RD_DRM_METERING
_LIT( KMeteringDataBaseFile, "%c:\\private\\101F51F2\\meterdb.dat" );
#endif

#define USE_RO_IMPORT

#ifdef USE_RO_IMPORT
_LIT( KInternalImportDir, "%c:\\private\\101F51F2\\import\\" );
_LIT( KUserDiskImportDir, "%c:\\import\\" ); // usually embedded MMC
_LIT( KUserRemovableDiskImportDir, "%c:\\import\\" ); // usually external MMC
_LIT( KDrSuffix, ".dr" );
#endif

#else

_LIT( KRightsDir, "c:\\private\\101F51F2\\rdb\\" );
_LIT( KTimedReplayCacheFile, "c:\\private\\101F51F2\\timererc.dat" );
_LIT( KPlainReplayCacheFile, "c:\\private\\101F51F2\\plainrc.dat" );
#ifdef RD_DRM_METERING
_LIT( KMeteringDataBaseFile, "c:\\private\\101F51F2\\meterdb.dat" );
#endif

#define USE_RO_IMPORT

#ifdef USE_RO_IMPORT
_LIT( KInternalImportDir, "c:\\private\\101F51F2\\import\\" );
_LIT( KUserDiskImportDir, "e:\\import\\" );
_LIT( KDrSuffix, ".dr" );
#endif

#endif

_LIT(KWmDrmClientWrapperName, "wmdrmclientwrapper.dll");

// MODULE DATA STRUCTURES

NONSHARABLE_STRUCT( TUnloadModule )
    {
    RTelServer* iServer;
    const TDesC* iName;
    };

// LOCAL FUNCTION PROTOTYPES

LOCAL_C TInt Startup( void );
LOCAL_C void SignalClient();
LOCAL_C TInt StartDBServer( void );

#if defined( __WINS__ )
#else
#define DRM_USE_SERIALNUMBER_URI
#include <mmtsy_names.h>
#endif


#ifdef DRM_USE_SERIALNUMBER_URI
LOCAL_C void DoUnloadPhoneModule( TAny* aAny );
#endif

// #define USE_RO_IMPORT

// FORWARD DECLARATIONS

// ============================= LOCAL FUNCTIONS ===============================
// -----------------------------------------------------------------------------
// Function Startup().
// This function starts the actual DRM Rights server after initializing
// the cleanup stack and active scheduler.
// Returns: TInt: Symbian OS error code.
// -----------------------------------------------------------------------------
//
LOCAL_C TInt Startup( void )
    {
    TInt error = KErrNone;
    CTrapCleanup* trap = CTrapCleanup::New();
    CActiveScheduler* scheduler = new CActiveScheduler();

    if ( trap && scheduler )
        {
        CActiveScheduler::Install( scheduler );

        error = StartDBServer();
        }
    else
        {
        error = KErrNoMemory;
        }

    delete scheduler;
    scheduler = NULL;

    delete trap;
    trap = NULL;

    if ( error )
        {
        // Something went wrong. Release the client (if any).
        SignalClient();

        if ( error == KErrAlreadyExists )
            {
            error = KErrNone;
            }
        }

    return error;
    }

// -----------------------------------------------------------------------------
// Function SignalClient().
// Signal the waiting client (one of them if any exists).
// -----------------------------------------------------------------------------
//
void SignalClient( void )
    {
    RSemaphore semaphore;
    if ( !semaphore.OpenGlobal( DRMEngine::KDRMSemaphore ) )
        {
        semaphore.Signal();
        semaphore.Close();
        }
    }

// -----------------------------------------------------------------------------
// Function StartDBServer().
// This function starts the actual server under TRAP harness and starts
// waiting for connections. This function returns only if there has been
// errors during server startup or the server is stopped for some reason.
//
// Returns: TInt: Symbian OS error code.
// -----------------------------------------------------------------------------
TInt StartDBServer( void )
    {
    TInt error = KErrNone;
    CDRMRightsServer* server = NULL;
    TUint8 count = 0;

    do
        {
        DRMLOG2( _L( "RightsServer.exe: StartDBServer: %d" ), error );

        ++count;

        TRAP( error, ( server = CDRMRightsServer::NewL() ) );

        if ( error )
            {
            User::After( TTimeIntervalMicroSeconds32(KWaitingTime) );
            }

        } while( error && ( count <= KMaxStartTries ) );

    if( error )
        {
        DRMLOG2( _L( "RightsServer.exe: CDRMRightsServer::NewL failed: %d " ), error );
        // Failed
        return error;
        }

    // Release the semaphore if necessary.
    SignalClient();

    // Start waiting for connections
    CActiveScheduler::Start();

    // Dying...
    // Delete CDRMRigntsServer

    DRMLOG( _L( "RightsServer.exe: DB server dying..." ) );

    delete server;

    return KErrNone;
    }

#ifdef DRM_USE_SERIALNUMBER_URI
// -----------------------------------------------------------------------------
// Function DoUnloadPhoneModule
// Unload phone module
// -----------------------------------------------------------------------------
//
void DoUnloadPhoneModule( TAny* aAny )
    {
    __ASSERT_DEBUG( aAny, User::Invariant() );
    TUnloadModule* module = ( TUnloadModule* ) aAny;
    module->iServer->UnloadPhoneModule( *( module->iName ) );
    }
#endif

#ifdef USE_RO_IMPORT
// -----------------------------------------------------------------------------
// PointerArrayResetDestroyAndClose
// Template method used to push RPointerArrays to the cleanup stack. Takes
// care of deleting all pointers in the array.
// -----------------------------------------------------------------------------
//
template<class S>
void PointerArrayResetDestroyAndClose(TAny* aPtr)
    {
    (reinterpret_cast<RPointerArray<S>*>(aPtr))->ResetAndDestroy();
    (reinterpret_cast<RPointerArray<S>*>(aPtr))->Close();
    }
#endif

// ============================ MEMBER FUNCTIONS ===============================

// CUsageUrl:

//--------------------------------------------------------------------------
// CUsageUrl::CUsageUrl
// Storage class default constructor
//--------------------------------------------------------------------------
//
CUsageUrl::CUsageUrl()
    {
    }

//--------------------------------------------------------------------------
// CUsageUrl::~CUsageUrl
// Storage class destructor
//--------------------------------------------------------------------------
//
CUsageUrl::~CUsageUrl()
    {
    delete iUrl;    
    }

// CDRMRightsServer:

// -----------------------------------------------------------------------------
// CDRMRightsServer::NewLC
// Two-phased constructor.
// -----------------------------------------------------------------------------
//
CDRMRightsServer* CDRMRightsServer::NewL()
    {
    CDRMRightsServer* self = new( ELeave ) CDRMRightsServer();

    CleanupStack::PushL( self );

    self->ConstructL();

    CleanupStack::Pop( self );

    return self;
    }

// -----------------------------------------------------------------------------
// Destructor
// -----------------------------------------------------------------------------
CDRMRightsServer::~CDRMRightsServer()
    {
    DRMLOG( _L( "CDRMRightsServer::~" ) );

    delete iIMEI; iIMEI = NULL;

    delete iIMSI; iIMSI = NULL;

    delete iDb; iDb = NULL;

    iClock.Close();
    iCache.Close();

    iMeteringDb.Close();

    iFs.Close();
    iActiveCountConstraints.ResetAndDestroy();
    iActiveCountConstraints.Close();

    delete iBackupObserver;
    delete iBackupHandler;
    delete iActiveBackupClient;
    delete iDbWatcher;

#if 0
    // Close and delete the shared data client
    if( iSharedDataClient )
        {
        iSharedDataClient->Close();
        delete iSharedDataClient;
        iSharedDataClient = NULL;
        }
#endif

    if( iNotifier )
        {
        delete iNotifier; iNotifier = NULL;
        }

    iActiveUrls.ResetAndDestroy();

    //An empty semaphore
    RSemaphore semaphore;
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::GetSecureTime
// Fetch the time from (secure) source.
// -----------------------------------------------------------------------------
//
TBool CDRMRightsServer::GetSecureTime( TTime& aTime ) const
    {
    DRMClock::ESecurityLevel secLevel = DRMClock::KInsecure;

    TInt timezone( 0 );

    iClock.GetSecureTime( aTime, timezone, secLevel );

    if( secLevel == DRMClock::KSecure )
        {
        DRMLOG( _L( "CDRMRightsServer::GetSecureTime: Time is secure\r\n" ) );
        return ETrue;
        }

    DRMLOG( _L( "CDRMRightsServer::GetSecureTime: Time is not secure\r\n" ) );

    return EFalse;
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::Notifier
// Return a handle to DRM Notifier.
// -----------------------------------------------------------------------------
//
CDRMNotifier& CDRMRightsServer::Notifier()
    {
    return *iNotifier;
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::Notifier
// Return a handle to DRM Notifier.
// -----------------------------------------------------------------------------
//
CDRMRightsDB& CDRMRightsServer::Database()
    {
    return *iDb;
    }

RFs& CDRMRightsServer::FileServerSession()
    {
    return iFs;
    }


RDRMReplayCache& CDRMRightsServer::ReplayCache()
    {
    return iCache;
    }


RDrmMeteringDb& CDRMRightsServer::MeteringDatabase()
    {
    return iMeteringDb;
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::HandleNotifyL
// Forward the event to the database.
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::HandleNotifyL(const TUid /*aUid*/,
                                     const TDesC& /*aKey*/,
                                     const TDesC& /*aValue*/)
    {
    /* XXX Backup via Publish/Subscribe
    __ASSERT_DEBUG( iDb, User::Invariant() );
    TInt value = -1;
    TLex16 parser( aValue );

    if ( aUid == KSDUidSystem )
        {
        // Check if it's a backup / restore status event
        if( !aKey.Compare( KBackupRestoreStatus ) )
            {
            User::LeaveIfError( parser.Val( value ) );
            if( value == 3 ) // Complete
                {
                iDb->MergeDBL();
                }
            }
        // Check if it's a drm backup restore status event
        else if ( aUid == KSDUidSystem )
            {
            if( !aKey.Compare( KDRMBackupRestoreStatus ) )
                {
                User::LeaveIfError( parser.Val( value ) );

                if( value == 1 ) // PrepareForBackup
                    {
                    TRAPD( error, iDb->BackupDBL( KNullDesC,
                                                  KNullDesC8 ) );
                    // Notify that it's done
                    User::LeaveIfError( iSharedDataClient->AssignToTemporaryFile(
                                        KSDUidSystem ) );
                    User::LeaveIfError( iSharedDataClient->SetInt(
                                        KDRMBackupRestoreStatus, 0 ) );
                    iSharedDataClient->Flush();
                    }
                }
            }
        }
    */
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::RunErrorL
// From CActive. Complete the request and restart the scheduler.
// -----------------------------------------------------------------------------
//
TInt CDRMRightsServer::RunError( TInt aError )
    {
    DRMLOG2( _L( "CDRMRightsServer::RunError: %d" ), aError );

    // Inform the client.
    if ( !Message().IsNull() )
        {
        Message().Complete( aError );
        }

    // Restart the scheduler.
    ReStart();

    // Error handled.
    return KErrNone;
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::NewSessionL
// Called when a client requires a new instance.
// -----------------------------------------------------------------------------
CSession2* CDRMRightsServer::NewSessionL( const TVersion& aVersion,
                                          const RMessage2& /*aMessage*/ ) const
    {
    DRMLOG( _L( "CDRMRightsServer::NewSessionL" ) );

    if ( ! User::QueryVersionSupported( TVersion( DRMEngine::KServerMajorVersion,
        DRMEngine::KServerMinorVersion,
        DRMEngine::KServerBuildVersion ),
        aVersion ) )
        {
        // Sorry, no can do.
        User::Leave( KErrNotSupported );
        }

    DRMLOG( _L( "CDRMRightsServer::NewSessionL: Creating a new session" ) );

    return CDRMDbSession::NewL();
    }
// -----------------------------------------------------------------------------
// CDRMRightsServer::CDRMRightsServer
// C++ default constructor can NOT contain any code, that
// might leave.
// -----------------------------------------------------------------------------
//
CDRMRightsServer::CDRMRightsServer() :
    CServer2( EPriorityStandard ),
    iIMEI( NULL ),
    iArmed( EFalse ),
    iIMSI( NULL ),
    iGetImsi( ETrue )
    {
    // Nothing
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::ConstructL
// Symbian 2nd phase constructor can leave.
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::ConstructL()
    {
    DRMLOG( _L( "CDRMRightsServer::ConstructL" ) );

    TDRMKey key;
    RSemaphore semaphore;
    RProcess currentprocess;

    // Ignore errors
    User::RenameThread( KRightsServerThread );
    User::LeaveIfError( iFs.Connect() );

#ifndef RD_MULTIPLE_DRIVE

    // Ignore errors
    iFs.MkDirAll( KDRMDbTempPath );

#else //RD_MULTIPLE_DRIVE

    TFileName tempPath;
    TFileName tempPath2;
    TFileName tempRemovablePath;
    TInt driveNumber( -1 );
    TChar driveLetter;
    TChar driveLetterRemovable;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    tempPath.Format( KDbTempPath, (TUint)driveLetter );

    // Ignore errors
    iFs.MkDirAll( tempPath );

#endif

    DRMLOG( _L( "CDRMRightsServer::ConstructL: SharedDataClient" ) );

    // Create and instance of the shared data client
    // iSharedDataClient = new (ELeave) RSharedDataClient(this);

    // Connecting to the shared data server
    // User::LeaveIfError(iSharedDataClient->Connect());

    /* XXX Backup via Publish/Subscribe
    User::LeaveIfError(iSharedDataClient->NotifyChange(
        KSDUidSystem, &KBackupRestoreStatus ) );
    User::LeaveIfError(iSharedDataClient->NotifyChange(
        KSDUidSystem, &KDRMBackupRestoreStatus) );
    */


    GetDbKeyL( key );


    DRMLOG( _L( "CDRMRightsServer::ConstructL: database" ) );

    GetIMEIL();

    // Create the imsi pointer array:
    iIMSI = CDRMPointerArray<HBufC8>::NewL();
    iIMSI->SetAutoCleanup(ETrue);

    GetIMSIL();

#ifndef RD_MULTIPLE_DRIVE

    iDb = CDRMRightsDB::NewL( iFs, KRightsDir, key, *iIMEI, const_cast<CDRMRightsServer*>(this) );

#else //RD_MULTIPLE_DRIVE

    tempPath.Format( KRightsDir, (TUint)driveLetter );

    iDb = CDRMRightsDB::NewL( iFs, tempPath, key, *iIMEI, const_cast<CDRMRightsServer*>(this) );

#endif

    key.FillZ();

    DRMLOG( _L( "CDRMRightsServer::ConstructL: DB started." ) );

    DRMLOG( _L( "CDRMRightsServer::ConstructL: Starting Notifier ." ) );

    User::LeaveIfError( semaphore.CreateGlobal( KDRMEngCommonSemaphore, 0 ) );
    CleanupClosePushL( semaphore );

    StartThreadL( DRMNotifier::KServerName, StartupNotifier, semaphore );
    DRMLOG( _L( "CDRMRightsServer::ConstructL: Notifier thread created." ) );

    StartThreadL( Roap::KServerName, StartupRoapStorage, semaphore );
    DRMLOG( _L( "CDRMRightsServer::ConstructL: ROAP thread created." ) );

#ifdef __DRM_CLOCK
    StartThreadL( DRMClock::KServerName, StartupClock, semaphore );
    DRMLOG( _L( "CDRMRightsServer::ConstructL: clock thread created." ) );
#endif

    CleanupStack::PopAndDestroy(); // semaphore

    iNotifier = CDRMNotifier::NewL();

    iCache.Set( iFs );

#ifndef RD_MULTIPLE_DRIVE

    iCache.InitL( KTimedReplayCacheFile, KPlainReplayCacheFile );

#ifdef RD_DRM_METERING
    iMeteringDb.Set( iFs );
    iMeteringDb.InitL( KMeteringDataBaseFile );
#endif

#else //RD_MULTIPLE_DRIVE

    tempPath.Format( KTimedReplayCacheFile, (TUint)driveLetter );
    tempPath2.Format( KPlainReplayCacheFile, (TUint)driveLetter );

    iCache.InitL( tempPath, tempPath2 );

#ifdef RD_DRM_METERING

    tempPath.Format( KMeteringDataBaseFile, (TUint)driveLetter );

    iMeteringDb.Set( iFs );
    iMeteringDb.InitL( tempPath );

#endif

#endif

    User::LeaveIfError( iClock.Connect() );

    // xoma header list creation
    iXOmaHeaders = new (ELeave) RPointerArray< CDRMXOma >();

    // p/s
    iBackupObserver = CDRMBackupObserver::NewL( *(const_cast<CDRMRightsServer*>(this)));
    iBackupObserver->Start();

#ifdef USE_RO_IMPORT
    // Import any OMA DRM 1.0 RO in the import directory, ignore all errors (except
    // when checking the default removable mass storage)
    TInt r = KErrNone;

#ifndef RD_MULTIPLE_DRIVE

    TRAP( r, ImportRightsObjectsL( KInternalImportDir ) );
    TRAP( r, ImportRightsObjectsL( KUserDiskImportDir ) );

#else //RD_MULTIPLE_DRIVE

    tempPath.Format( KInternalImportDir, (TUint)driveLetter );

    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultMassStorage, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    // Default mass storage is usually eMMC
    tempPath2.Format( KUserDiskImportDir, (TUint)driveLetter );

    // Find out if a removable mass storage also exists
    r = DriveInfo::GetDefaultDrive( DriveInfo::EDefaultRemovableMassStorage, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetterRemovable );

    // Import is not needed from the default removable mass storage drive if the drive
    // letter of the default mass storage and the default removable mass storage are
    // the same or the removable mass storage is not supported
    if ( ( driveLetter != driveLetterRemovable ) && ( r == KErrNone ) )
        {
        tempRemovablePath.Format( KUserRemovableDiskImportDir, (TUint)driveLetterRemovable );
        TRAP( r, ImportRightsObjectsL( tempRemovablePath ) );
        }

    TRAP( r, ImportRightsObjectsL( tempPath ) );
    TRAP( r, ImportRightsObjectsL( tempPath2 ) );

#endif

#endif

    // Add the server to the scheduler.
    StartL( DRMEngine::KServerName );

    // Start watching our RDB
    iDbWatcher = CDbWatcher::NewL( *this );
    iDbWatcher->StartWatching();

    // Start watching the helper server
    iProcWatcher = CProcWatcher::NewL( *this, _L( "*DcfRepSrv*" ), _L( "DcfRepSrv" ) );
    iProcWatcher->StartWatching();

    // Ready to watch
    iArmed = ETrue;

    __UHEAP_MARK;
    TRAP( r, FeatureManager::InitializeLibL() );
    if( !r && FeatureManager::FeatureSupported( KFeatureIdWindowsMediaDrm ) )
        {
        static const TInt KGateOrdinal = 1;
        RLibrary library;
        r = library.Load( KWmDrmClientWrapperName );
        if( !r )
            {
            CWmDrmClientWrapper* wrapper = NULL;
            TLibraryFunction function = library.Lookup( KGateOrdinal );
            if( function != NULL )
                {
                __UHEAP_MARK;
                TRAP( r, wrapper = reinterpret_cast<CWmDrmClientWrapper*>( function() ) );
                if( !r )
                    {
                    r = wrapper->Connect();
                    }
                delete wrapper;
                __UHEAP_MARKEND;
                }
            }
        library.Close();
        }
    FeatureManager::UnInitializeLib();
    __UHEAP_MARKEND;
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::StartThreadL
// Start a new thread.
// -----------------------------------------------------------------------------
void CDRMRightsServer::StartThreadL( const TDesC& aThreadName,
                                     TThreadFunction aFunc,
                                     RSemaphore& aSemaphore )
    {
    RThread thread;

    User::LeaveIfError(
          thread.Create( aThreadName,
                         aFunc,
                         KDefaultStackSize,
                         KMinHeapSize,
                         KMaxHeapsize,
                         NULL ) );

    thread.Resume();

    aSemaphore.Wait();

    thread.Close();
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::GetDbKeyL
// Fetches the rights database key from Wallet or uses a constant
// key if Wallet is not supported.
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::GetDbKeyL( TDRMKey& aKey  )
    {
    TInt r = KErrNone;

    DRMLOG( _L( "CDRMRightsServer::GetDbKey" ) );
    MDrmKeyStorage* storage = DrmKeyStorageNewL();
    TRAP( r, storage->GetDeviceSpecificKeyL( aKey ) );
    delete storage;
    User::LeaveIfError( r );
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::GenerateKeyL
// Generates the actual key based on the given key seed.
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::GenerateKeyL( HBufC*& aKeySeed,
                                    TDRMKey& aKey ) const
    {
    __ASSERT_ALWAYS( aKeySeed->Size() >= KDRMKeyLength,
        User::Leave( KErrUnderflow ) );

    TPtrC8 key( reinterpret_cast< TUint8* >( const_cast< TUint16* >( aKeySeed->Ptr() ) ),
                KDRMKeyLength );

    aKey = key;
    }


// -----------------------------------------------------------------------------
// CDRMRightsServer::XOmaHeaders()
// return the pointer of the X-Oma headers list
// -----------------------------------------------------------------------------
//
RPointerArray< CDRMXOma >& CDRMRightsServer::XOmaHeaders( void )
    {
    return *iXOmaHeaders;
    }



// -----------------------------------------------------------------------------
// CDRMRightsServer::GetIMEIL
// -----------------------------------------------------------------------------
//
const TDesC& CDRMRightsServer::GetIMEIL()
    {
    if ( iIMEI )
        {
        return *iIMEI;
        }

#ifdef DRM_USE_SERIALNUMBER_URI
    TInt error( KErrNone );
    TInt count( 0 );
    TInt count2( 0 );
    TUint32 caps( 0 );
    TBool found (EFalse);

    RTelServer etelServer;
    RMobilePhone phone;

    TUint KMaxImeiTries = 5;

    for ( TUint8 i = 0; i < KMaxImeiTries; ++i )
        {
        error = etelServer.Connect();
        if ( error )
            {
            User::After( TTimeIntervalMicroSeconds32( KWaitingTime ) );
            }
        else
            {
            break;
            }
        }

    User::LeaveIfError( error );
    CleanupClosePushL( etelServer );

    User::LeaveIfError( etelServer.LoadPhoneModule( KMmTsyModuleName ) );

    TUnloadModule unload;
    unload.iServer = &etelServer;
    unload.iName = &KMmTsyModuleName;

    TCleanupItem item( DoUnloadPhoneModule, &unload );
    CleanupStack::PushL( item );
    User::LeaveIfError( etelServer.EnumeratePhones( count ) );

    for ( count2 = 0; count2 < count && !found; ++count2 )
        {
        RTelServer::TPhoneInfo phoneInfo;
        User::LeaveIfError( etelServer.GetTsyName( count2, phoneInfo.iName ) );

        if ( phoneInfo.iName.CompareF(KMmTsyModuleName()) == 0 )
           {
            User::LeaveIfError( etelServer.GetPhoneInfo( count2, phoneInfo ) );
            User::LeaveIfError( phone.Open( etelServer, phoneInfo.iName ) );
            CleanupClosePushL( phone );
            found = ETrue;
            }
        }

    if ( !found )
        {
        // Not found.
        User::Leave( KErrNotFound );
        }

    User::LeaveIfError( phone.GetIdentityCaps( caps ) );
    if ( caps & RMobilePhone::KCapsGetSerialNumber )
        {
        RMobilePhone::TMobilePhoneIdentityV1 id;
        TRequestStatus status;

        phone.GetPhoneId( status, id );

        User::WaitForRequest( status );

        User::LeaveIfError( status.Int() );

        iIMEI = id.iSerialNumber.AllocL();

        CleanupStack::PopAndDestroy( 3 ); // phone, item, etelServer

        HBufC8* buf = HBufC8::NewL( iIMEI->Size() );
        TPtr8 ptr( buf->Des() );
        ptr.Copy( *iIMEI );

        DRMLOG(_L("IMEI:"));
        DRMLOGHEX(ptr);
        delete buf;

        return *iIMEI;
        }

    User::Leave( KErrNotFound );

    // Never happens...
    return *iIMEI;

#else
    _LIT( KDefaultSerialNumber, "123456789123456789" );
    iIMEI = KDefaultSerialNumber().AllocL();

    return *iIMEI;
#endif
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::GetIMSIL
// -----------------------------------------------------------------------------
//
const CDRMPointerArray<HBufC8>& CDRMRightsServer::GetIMSIL()
    {

    if ( !iGetImsi )
        {
        return *iIMSI;
        }

#ifndef __WINS__
    TInt error( KErrNone );
    TInt count( 0 );
    TInt count2( 0 );
    TUint32 caps( 0 );
    TBool found (EFalse);
    HBufC8* imsi = NULL;
    HBufC8* imsiNumber = NULL;

    RTelServer etelServer;
    RMobilePhone phone;

    TUint KMaxImeiTries = 5;
    for ( TUint8 i = 0; i < KMaxImeiTries; ++i )
        {
        error = etelServer.Connect();
        if ( error )
            {
            User::After( TTimeIntervalMicroSeconds32( KWaitingTime ) );
            }
        else
            {
            break;
            }
        }

    User::LeaveIfError( error );
    CleanupClosePushL( etelServer );
    User::LeaveIfError( etelServer.LoadPhoneModule( KMmTsyModuleName ) );

    TUnloadModule unload;
    unload.iServer = &etelServer;
    unload.iName = &KMmTsyModuleName;

    TCleanupItem item( DoUnloadPhoneModule, &unload );
    CleanupStack::PushL( item );

    User::LeaveIfError( etelServer.EnumeratePhones( count ) );

    for ( count2 = 0; count2 < count && !found; ++count2 )
        {
        RTelServer::TPhoneInfo phoneInfo;
        User::LeaveIfError( etelServer.GetTsyName( count2, phoneInfo.iName ) );

        if ( phoneInfo.iName.CompareF(KMmTsyModuleName()) == 0 )
           {
            User::LeaveIfError( etelServer.GetPhoneInfo( count2, phoneInfo ) );
            User::LeaveIfError( phone.Open( etelServer, phoneInfo.iName ) );
            CleanupClosePushL( phone );
            found = ETrue;
            }
        }

    if ( !found )
        {
        // Not found.
        User::Leave( KErrNotFound );
        }


    User::LeaveIfError( phone.GetIdentityCaps( caps ) );

    if( caps & RMobilePhone::KCapsGetSubscriberId )
        {
        RMobilePhone::TMobilePhoneSubscriberId imsiId;
        TRequestStatus status;

        phone.GetSubscriberId( status, imsiId );

        User::WaitForRequest( status );

        if( ! status.Int() )
            {
            imsi = HBufC8::NewMaxLC( imsiId.Length() + KImsiId().Size() );
            TPtr8 imsiPtr(const_cast<TUint8*>(imsi->Ptr()), 0, imsi->Size());

            imsiNumber = CnvUtfConverter::ConvertFromUnicodeToUtf8L( imsiId );
            CleanupStack::PushL( imsiNumber );

            imsiPtr.Copy( KImsiId() );
            imsiPtr.Append( *imsiNumber );
            CleanupStack::PopAndDestroy(); // imsiNumber
            }
        else
            {
            imsi = NULL;
            }
        }
    else
        {
        imsi = NULL;
        }


    // Clean up whatever is in there
    iIMSI->ResetAndDestroy();

    if( imsi )
        {
        // if we got it we wont try again
        iIMSI->AppendL( imsi );
        CleanupStack::Pop(); // imsi
        iGetImsi = EFalse;
        }

    // Check for possible extra IMSI individual constraints
    AppendExtendedIndividualConstraintsL(&phone);

    CleanupStack::PopAndDestroy(); // phone
    CleanupStack::PopAndDestroy(); // cleanup item
    CleanupStack::PopAndDestroy(); // etel server

    return *iIMSI;

#else
    HBufC8* imsi = NULL;

    if( iGetImsi )
        {
        iGetImsi = EFalse;
        _LIT8( KDefaultSerialNumber, "IMSI:123456789123456789" );
        imsi = KDefaultSerialNumber().AllocLC();
        iIMSI->AppendL( imsi );
        CleanupStack::Pop();
        AppendExtendedIndividualConstraintsL();
        }


    return *iIMSI;
#endif // __WINS__
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::AppendExtendedIndividualConstraintsL
// If the extension DLL exists it is loaded and used to obtain additional
// valid individual constraints
// -----------------------------------------------------------------------------
void CDRMRightsServer::AppendExtendedIndividualConstraintsL(RMobilePhone* aMobilePhone)
    {
    // Load the externsion DLL
    RLibrary lib;

#ifndef RD_MULTIPLE_DRIVE

    if (lib.LoadRomLibrary(KDRMIndividualConstraintExtensionDll,KNullDesC)==KErrNone)

#else //RD_MULTIPLE_DRIVE

    TInt driveNumber( -1 );
    TChar driveLetter;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultRom, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    TFileName individualConstraindExtensionDll;
    individualConstraindExtensionDll.Format(
                        KIndividualConstraintExtensionDll, (TUint)driveLetter );

    if ( lib.LoadRomLibrary( individualConstraindExtensionDll, KNullDesC ) == KErrNone )

#endif

        {
        CleanupClosePushL(lib);

        // Get first exported ordinal - factory method returning
        // MDRMIndividualConstraintExtension*
        TLibraryFunction factory = lib.Lookup(1);

        if (factory)
            {
            // Instantiate object
            MDRMIndividualConstraintExtension* extendedConstraints =
                reinterpret_cast<MDRMIndividualConstraintExtension*>(factory());

            if (extendedConstraints)
                {
                CleanupStack::PushL(TCleanupItem(Release,extendedConstraints));
                extendedConstraints->AppendConstraintsL(*iIMSI,aMobilePhone);
                CleanupStack::PopAndDestroy(extendedConstraints); //calls Release
                }
            }

        // unload library
        CleanupStack::PopAndDestroy(&lib); //close
        }
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::Release
// -----------------------------------------------------------------------------
void CDRMRightsServer::Release(TAny* aIndividualConstraintExtension)
    {
    MDRMIndividualConstraintExtension* extendedConstraints =
        reinterpret_cast<MDRMIndividualConstraintExtension*>(aIndividualConstraintExtension);
    extendedConstraints->Release(); //free resources
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::HandleBackupEventL
// Handle Backup Events
// -----------------------------------------------------------------------------
//

void CDRMRightsServer::HandleBackupEventL( TInt aBackupEvent )
    {
    //RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::BackupCalled\n\r"));

    //conn::TBURPartType eventType;
    //conn::TBackupIncType incType;
    TDriveList aDriveList;

    //RFileLogger::WriteFormat(KLogDir, KLogName, EFileLoggingModeAppend, _L8("backupevent: %d"), aBackupEvent);

    // If there is no operation going or state is normal
    // Delete the client and handler

    if( aBackupEvent == conn::EBURUnset ||
        aBackupEvent & conn::EBURNormal )
        {
        /*
        if( aBackupEvent == conn::EBURUnset )
            {
                        RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::Unset\n\r"));
            }
        else
            {
            RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::Normal\n\r"));
            }
        */
        if( iActiveBackupClient )
            {
            delete iActiveBackupClient;
            iActiveBackupClient = NULL;
            }

        if( iBackupHandler )
            {
            delete iBackupHandler;
            iBackupHandler = NULL;
            }
        }
    else if( aBackupEvent & conn::EBURBackupFull ||
             aBackupEvent & conn::EBURRestoreFull )
        {
        //RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::Full\n\r"));
        // ab handler
        iBackupHandler = CDRMBackup::NewL( iDb, iFs );

        // ab client
        iActiveBackupClient = conn::CActiveBackupClient::NewL( iBackupHandler );

        // Confirm that we have done everything if there even was anything to do
        //RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::Confirm F \n\r"));
        iActiveBackupClient->ConfirmReadyForBURL( KErrNone );
        }
    else if( aBackupEvent & conn::EBURBackupPartial ||
             aBackupEvent & conn::EBURRestorePartial )
        {
        //RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::Partial\n\r"));
        // ab handler
        iBackupHandler = CDRMBackup::NewL( iDb, iFs );

        // ab client
        iActiveBackupClient = conn::CActiveBackupClient::NewL( iBackupHandler );

        if( !iActiveBackupClient->DoesPartialBURAffectMeL() )
            {
            //RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::NotMe\n\r"));
            delete iActiveBackupClient;
            iActiveBackupClient = NULL;

            delete iBackupHandler;
            iBackupHandler = NULL;
            }
        else
            {
            //RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::Confirm P \n\r"));
            // Confirm that we have done everything if there even was anything to do
            iActiveBackupClient->ConfirmReadyForBURL( KErrNone );
            //RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::Confirm P Done \n\r"));
            }
        }
    else
        {
        //RFileLogger::Write(KLogDir, KLogName, EFileLoggingModeAppend, _L8("Handle::Argument\n\r"));
        // Unknown operation
        User::Leave(KErrArgument);
        }
    };

// -----------------------------------------------------------------------------
// CDRMRightsServer::WatchedObjectChangedL
// Handle Backup Events
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::WatchedObjectChangedL( const TDesC& aObject )
    {
    DRMLOG( _L( "CDRMRightsServer::WatchedObjectChangedL ->" ) );
    DRMLOG( aObject );

    if ( aObject.Left( KDirIdentifier().Length() ) == KDirIdentifier &&
         !iDb->Updating() && iArmed )
        {
#ifdef _DEBUG
        DRMLOG( _L( "RDB modified by outside party (DEBUG mode, not deleting the DB)" ) );
#else
        DRMLOG( _L( "RDB modified by outside party, deleting the DB" ) );
        iDb->MarkAsCorrupted();
        RStarterSession starter;
        User::LeaveIfError( starter.Connect() );
        starter.Reset( RStarterSession::EDRMReset );
        starter.Close();
#endif
        }
    else if ( aObject.Left( KProcIdentifier().Length() ) == KProcIdentifier && iArmed )
        {
#ifdef _DEBUG
        DRMLOG( _L( "Peer process killed (DEBUG mode, not rebooting)" ) );
#else
        DRMLOG( _L( "Peer process killed, rebooting" ) );
        RStarterSession starter;
        User::LeaveIfError( starter.Connect() );
        starter.Reset( RStarterSession::EDRMReset );
        starter.Close();
#endif
        }

    DRMLOG( _L( "CDRMRightsServer::WatchedObjectChangedL <-" ) );
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::HasActiveCountConstraint
// Check ID for active count constraint
// -----------------------------------------------------------------------------
//
TBool CDRMRightsServer::HasActiveCountConstraint( const TDesC8& aContentId )
    {
    TInt i;
    TBool r = EFalse;

    for ( i = 0; r == EFalse && i < iActiveCountConstraints.Count(); i++ )
        {
        if ( iActiveCountConstraints[i]->CompareF( aContentId ) == 0 )
            {
            r = ETrue;
            }
        }
    return r;
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::RemoveActiveCountConstraint
// Remove ID from count constraint list
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::RemoveActiveCountConstraint( const TDesC8& aContentId )
    {
    TInt i;
    TInt r = KErrNotFound;
    HBufC8* id = NULL;

    for ( i = 0; r == KErrNotFound && i < iActiveCountConstraints.Count(); i++ )
        {
        if ( iActiveCountConstraints[i]->CompareF( aContentId ) == 0 )
            {
            r = i;
            }
        }
    if ( r != KErrNotFound )
        {
        id = iActiveCountConstraints[r];
        iActiveCountConstraints.Remove( r );
        delete id;
        id = NULL;
        }
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::AddActiveCountConstraint
// Add ID to count constraint list
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::AddActiveCountConstraintL( const TDesC8& aContentId )
    {
    if ( !HasActiveCountConstraint( aContentId ) )
        {
        iActiveCountConstraints.AppendL( aContentId.AllocL() );
        }
    }



// -----------------------------------------------------------------------------
// CDRMRightsServer::IsAccessingUrl
// Add ID to count constraint list
// -----------------------------------------------------------------------------
//
TInt CDRMRightsServer::IsAccessingUrl( const TDesC8& aContentId )
    {
    for( TInt i = 0; i < iActiveUrls.Count(); i++ )
        {
        if( !iActiveUrls[i]->iUrl->Compare( aContentId ) ) 
            {
            return i;
            }    
        }
    return KErrNotFound;        
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::RemoveAccessingUrl
// Add ID to count constraint list
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::RemoveAccessingUrl( const TDesC8& aContentId )
    {
    CUsageUrl* usage = NULL;    
    TInt index = KErrNotFound;
    
    index = IsAccessingUrl( aContentId );
    
    if( index != KErrNotFound )
        {
        // If there are negative or 0 values in the list for some reason
        // remove them    
        if( iActiveUrls[index]->iRefCounter <= 1 )
            {
            usage = iActiveUrls[index];    
            iActiveUrls.Remove( index );    
            delete usage;
            }
        else
            {
            iActiveUrls[index]->iRefCounter--;
            }           
        }
    }

// -----------------------------------------------------------------------------
// CDRMRightsServer::AddAccessingUrlL
// Add ID to count constraint list
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::AddAccessingUrlL( const TDesC8& aContentId )
    {
    CUsageUrl* usage = NULL;
    TInt index = KErrNotFound;
    
    index = IsAccessingUrl( aContentId );
    
    if( index == KErrNotFound )
        {
        usage = new ( ELeave ) CUsageUrl();
        CleanupStack::PushL( usage );
        usage->iUrl = aContentId.AllocL();
        usage->iRefCounter = 1;
        iActiveUrls.AppendL( usage );
        CleanupStack::Pop( usage );
        }
    else
        {
        usage = iActiveUrls[index];
        usage->iRefCounter++;    
        }           
    }




// -----------------------------------------------------------------------------
// CDRMRightsServer::StopWatchingL
// Delete the watchers
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::StopWatchingL()
    {
    iArmed = EFalse;
    }

#ifdef USE_RO_IMPORT
// -----------------------------------------------------------------------------
// CDRMRightsServer::ImportRightsObjectsL
// Open the import directory and add all ROs that can be found there. ROs file
// names must end with .dr. Only OMA DRM 1.0 ROs in XML format are supported for
// security reasons
// -----------------------------------------------------------------------------
//
void CDRMRightsServer::ImportRightsObjectsL( const TDesC& aImportDir )
    {
    CDrmRightsParser* p;
    HBufC8* d = NULL;
    HBufC8* k = NULL;
    RFs fs;
    RFile file;
    TInt size;
    RPointerArray<CDRMRights> rights;
    CDir* dir;
    TFileName name;
    TPtr8 ptr( NULL, 0 );
    TInt i;
    TInt r = KErrNone;
    TCleanupItem listCleanup(PointerArrayResetDestroyAndClose<CDRMRights>,
        &rights);
    TDRMUniqueID id;
    TTime time;

    DRMLOG( _L( "CDRMRightsServer::ImportRightsObjectsL" ) );
    DRMLOG( aImportDir );
    __UHEAP_MARK;
    GetSecureTime( time );
    p = CDrmRightsParser::NewL();
    CleanupStack::PushL( p );
    User::LeaveIfError( iFs.GetDir( aImportDir, KEntryAttNormal,
        ESortNone, dir ) );
    CleanupStack::PushL( dir );
    for (i = 0; i < dir->Count(); i++)
        {
        name.Copy( aImportDir );
        name.Append( (*dir)[i].iName );
        if ( ( name.Length() > 3 && name.Right(3).CompareF( KDrSuffix ) == 0 ) )
            {
            User::LeaveIfError( file.Open( iFs, name, EFileRead ) );
            CleanupClosePushL( file );
            User::LeaveIfError( file.Size( size ) );
            d = HBufC8::NewLC( size );
            ptr.Set( d->Des() );
            User::LeaveIfError( file.Read( ptr ) );
            p->ParseL( ptr, rights );
            if ( rights.Count() > 0 )
                {
                k = NULL;
                CleanupStack::PushL( listCleanup );
                CDRMPermission& permission = rights[0]->GetPermission();
                CDRMAsset& asset = rights[0]->GetAsset();

                // Add RO only if no rights are available at all for this content
                TRAP( r, k = iDb->GetDecryptionKeyL( *asset.iUid ) );
                if (k == NULL )
                    {
                    iDb->AddDBEntryL( *asset.iUid, permission, asset.iKey, id );
                    }
                else
                    {
                    delete k;
                    }
                CleanupStack::PopAndDestroy(); // listCleanup
                }
            CleanupStack::PopAndDestroy( 2 ); // d, file
            iFs.Delete( name );
            }
        }
    CleanupStack::PopAndDestroy( 2 ); // dir, p
    __UHEAP_MARKEND;
    DRMLOG( _L( "CDRMRightsServer::ImportRightsObjectsL done" ) );
    }
#endif



// ========================== OTHER EXPORTED FUNCTIONS =========================


TInt E32Main()
    {
    return Startup();
    }


//  End of File
