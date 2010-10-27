/*
* Copyright (c) 2002-2010 Nokia Corporation and/or its subsidiary(-ies).
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
* Description:  OMA DRM 2.0 Key Storage
*
*/


// INCLUDE FILES
#include <e32std.h>
#include <asymmetric.h>
#include <symmetric.h>
#include <hash.h>
#include <asn1dec.h>
#include <x509cert.h>
#include <etelmm.h>
#include <mmtsy_names.h>
#include <featmgr.h>

#ifdef RD_MULTIPLE_DRIVE
#include <driveinfo.h>
#endif

#include "DrmKeyStorage.h"
#include "DrmStdKeyStorage.h"

#ifdef _DEBUG
#define LOGGING
#endif

#ifdef LOGGING
_LIT(KLogDir, "DRM");
_LIT(KLogName, "KeyStorage.log");
#include "flogger.h"
#define LOG(string) \
    RFileLogger::Write(KLogDir, KLogName, \
        EFileLoggingModeAppend, string);
#define LOGHEX(buffer) \
    RFileLogger::HexDump(KLogDir, KLogName, \
        EFileLoggingModeAppend, _S(""), _S(""), \
        buffer.Ptr(), buffer.Length());
#else
#define LOG(string)
#define LOGHEX(buffer)
#endif

#pragma message("Compiling DRM Std KeyStorage..")


// LOCAL CONSTANTS AND MACROS

const TInt KKeyLength = 128;
const TInt KWaitingTime = 2000000; // 2 sec

#ifdef RD_MULTIPLE_DRIVE
_LIT(KKeyStoragePath, "%c:\\private\\101F51F2\\PKI\\");
_LIT(KRomKeyStoragePath, "%c:\\private\\101F51F2\\PKI\\");

#else
_LIT(KKeyStoragePath, "c:\\private\\101F51F2\\PKI\\");
_LIT(KRomKeyStoragePath, "z:\\private\\101F51F2\\PKI\\");
#endif

_LIT(KDeviceKeyFileName, "DevicePrivateKey.der");
_LIT(KDeviceCertFileName, "DeviceCert.der");
_LIT(KSingingCertFmt, "SigningCert%02d.der");
_LIT(KSingingCertPattern, "SigningCert*");
_LIT8(KDefaultKey, "0000000000000000");

#ifdef RD_MULTIPLE_DRIVE
_LIT(KSerialNumberFile, "%c:\\private\\101F51F2\\rdbserial.dat");
_LIT(KUdtCertFileName, "%c:\\private\\101F51F2\\PKI\\UdtCertificate.der");
#else
_LIT(KSerialNumberFile, "c:\\private\\101F51F2\\rdbserial.dat");
_LIT(KUdtCertFileName, "z:\\private\\101F51F2\\PKI\\UdtCertificate.der");
#endif

_LIT(KCmla, "CMLA");
_LIT(KPadding, "\x0");

NONSHARABLE_STRUCT( TUnloadModule )
    {
    RTelServer* iServer;
    const TDesC* iName;
    };


// ============================ LOCAL FUNCTIONS ================================
LOCAL_C void DoUnloadPhoneModule( TAny* aAny );

LOCAL_C void WriteFileL(RFs& aFs, const TDesC& aName, const TDesC8& aData)
    {
    RFile file;

    User::LeaveIfError(file.Replace(aFs, aName, EFileWrite));
    User::LeaveIfError(file.Write(aData));
    file.Close();
    }

LOCAL_C void ReadFileL(RFs& aFs, const TDesC& aName, HBufC8*& aContent)
    {
    RFile file;
    TInt size = 0;

    User::LeaveIfError(file.Open(aFs, aName, EFileRead));
    CleanupClosePushL(file);
    User::LeaveIfError(file.Size(size));
    aContent = HBufC8::NewLC(size);
    TPtr8 ptr(aContent->Des());
    User::LeaveIfError(file.Read(ptr, size));
    CleanupStack::Pop(); //aContent
    CleanupStack::PopAndDestroy(); // file
    }

HBufC8* I2OSPL(
    RInteger& aInt, TInt& aKeySize )
    {
    HBufC8* integer = aInt.BufferLC();
    if (integer->Length() < aKeySize)
        {
        HBufC8* r = HBufC8::NewLC(aKeySize);
        TPtr8 ptr(r->Des());
        for(TInt i = integer->Length(); i < aKeySize; i++)
            {
            ptr.Append(KPadding());
            }
        ptr.Append(*integer);
        CleanupStack::Pop(r);
        CleanupStack::PopAndDestroy(integer);
        return r;
        }
    else
        {
        CleanupStack::Pop(integer);
        return integer;
        }
    }

RInteger OS2IPL(
    const TDesC8& aOctetStream)
    {
    RInteger r;
    TInt i;

    r = RInteger::NewL(0);
    for (i = 0; i < aOctetStream.Length(); i++)
        {
        r *= 256;
        r += aOctetStream[i];
        }
    return r;
    }

void DoUnloadPhoneModule( TAny* aAny )
    {
    __ASSERT_DEBUG( aAny, User::Invariant() );
    TUnloadModule* module = ( TUnloadModule* ) aAny;
    module->iServer->UnloadPhoneModule( *( module->iName ) );
    }


// ============================ MEMBER FUNCTIONS ===============================

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage* CDrmStdKeyStorage::NewL
//
// -----------------------------------------------------------------------------
//
EXPORT_C CDrmStdKeyStorage* CDrmStdKeyStorage::NewL(RLibrary aLibrary)
    {
    CDrmStdKeyStorage* self = new (ELeave) CDrmStdKeyStorage(aLibrary);
    CleanupStack::PushL(self);
    self->ConstructL();
    CleanupStack::Pop();
    return self;
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::CDrmStdKeyStorage
//
// -----------------------------------------------------------------------------
//
CDrmStdKeyStorage::CDrmStdKeyStorage(RLibrary aLibrary):
    iFileMan(NULL),
    iRootSelected(EFalse),
    iKey(NULL),
    iImei(NULL),
    iLibrary(aLibrary)
    {
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::ConstructL
//
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::ConstructL()
    {

    LOG(_L("CDrmStdKeyStorage::ConstructL ->"));
    User::LeaveIfError(iFs.Connect());
    iFileMan = CFileMan::NewL(iFs);

    FeatureManager::InitializeLibL();
    
#ifdef __DRM_OMA2 
    if ( FeatureManager::FeatureSupported( KFeatureIdFfOmadrm2Support ) )
        {
        TRAP_IGNORE( SelectDefaultRootL() );
        }
#endif
    
    FeatureManager::UnInitializeLib();
    
    iDeviceSpecificKey.Copy(KDefaultKey);

    LOG(_L("CDrmStdKeyStorage::ConstructL <-"));
    }

// -----------------------------------------------------------------------------
// MDrmKeyStorage::~MDrmKeyStorage
//
// -----------------------------------------------------------------------------
//

MDrmKeyStorage::~MDrmKeyStorage()
    {
    }
// -----------------------------------------------------------------------------
// DrmStdKeyStorage::~CDrmStdKeyStorage
//
// -----------------------------------------------------------------------------
//

CDrmStdKeyStorage::~CDrmStdKeyStorage()
    {
    LOG(_L("CDrmStdKeyStorage::~CDrmStdKeyStorage ->"));
    delete iFileMan;
    delete iKey;
    delete iImei; iImei = NULL;
    iFs.Close();
    //iLibrary.Close();
    LOG(_L("CDrmStdKeyStorage::~CDrmStdKeyStorage <-"));
    }

// -----------------------------------------------------------------------------
// DrmStdKeyStorage::ModulusSize
//
// -----------------------------------------------------------------------------
//

TInt CDrmStdKeyStorage::ModulusSize()
    {
    LOG(_L("CDrmStdKeyStorage::ModulusSize ->"));

    if(iKey == NULL)
        {
        return KErrGeneral;
        }
    LOG(_L("CDrmStdKeyStorage::ModulusSize <-"));
    return iKey->N().BitCount();
    }

// -----------------------------------------------------------------------------
// DrmStdKeyStorage::SelectTrustedRootL
//
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::SelectTrustedRootL(
    const TDesC8& aRootKeyHash)
    {
    TFileName fileName;
    TEntry entry;
    TInt i;

    LOG(_L("CDrmStdKeyStorage::SelectTrustedRootL ->"));
    LOG(aRootKeyHash);
    if (aRootKeyHash.Length() != 0)
        {

#ifndef RD_MULTIPLE_DRIVE

        fileName.Copy(KKeyStoragePath);

#else //RD_MULTIPLE_DRIVE

        TFileName tempPath;
        TInt driveNumber( -1 );
        TChar driveLetter;
        DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
        iFs.DriveToChar( driveNumber, driveLetter );

        tempPath.Format( KKeyStoragePath, (TUint)driveLetter );

        fileName.Copy(tempPath);

#endif

        for (i = 0; i < SHA1_HASH; i++)
            {
            fileName.AppendNumFixedWidth(aRootKeyHash[i], EHex, 2);
            }
        fileName.Append('\\');
        if (iFs.Entry(fileName, entry) != KErrNone)
            {

#ifndef RD_MULTIPLE_DRIVE

            fileName.Copy(KRomKeyStoragePath);

#else //RD_MULTIPLE_DRIVE

            DriveInfo::GetDefaultDrive( DriveInfo::EDefaultRom, driveNumber );
            iFs.DriveToChar( driveNumber, driveLetter );

            tempPath.Format( KRomKeyStoragePath, (TUint)driveLetter );

            fileName.Copy(tempPath);

#endif

            for (i = 0; i < SHA1_HASH; i++)
                {
                fileName.AppendNumFixedWidth(aRootKeyHash[i], EHex, 2);
                }
            fileName.Append('\\');
            // check that the path exists
            User::LeaveIfError(iFs.Entry(fileName, entry));
            }
        User::LeaveIfError(iFs.SetSessionPath(fileName));
        InitializeKeyL();
        CheckRootForCmlaL();
        iRootSelected = ETrue;
        }
    else
        {
        SelectDefaultRootL();
        }
    LOG(_L("CDrmStdKeyStorage::SelectTrustedRootL <-"));
    }

// -----------------------------------------------------------------------------
// DrmStdKeyStorage::SelectDefaultRootL
//
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::SelectDefaultRootL()
    {
    CDir* dir = NULL;
    TFileName dirName;
    TBool found = EFalse;

    LOG(_L("CDrmStdKeyStorage::SelectDefaultRootL ->"));

#ifndef RD_MULTIPLE_DRIVE

    if (iFs.GetDir(KKeyStoragePath, KEntryAttDir, ESortByName, dir) == KErrNone)

#else //RD_MULTIPLE_DRIVE

    TFileName tempPath;
    TInt driveNumber( -1 );
    TChar driveLetter;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    tempPath.Format( KKeyStoragePath, (TUint)driveLetter );

    if (iFs.GetDir(tempPath, KEntryAttDir, ESortByName, dir) == KErrNone)

#endif

        {
        __UHEAP_MARK;
        LOG(_L("  Checking keys on C:"));
        CleanupStack::PushL(dir);
        if (dir->Count() >= 1)
            {

#ifndef RD_MULTIPLE_DRIVE

            dirName.Copy(KKeyStoragePath);

#else //RD_MULTIPLE_DRIVE

            dirName.Copy(tempPath);

#endif

            dirName.Append((*dir)[0].iName);
            dirName.Append('\\');
            User::LeaveIfError(iFs.SetSessionPath(dirName));
            found = ETrue;
            }
        CleanupStack::PopAndDestroy(dir);
        __UHEAP_MARKEND;
        }

#ifndef RD_MULTIPLE_DRIVE

    if (!found && iFs.GetDir(KRomKeyStoragePath, KEntryAttDir, ESortByName, dir) == KErrNone)

#else //RD_MULTIPLE_DRIVE

    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultRom, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    tempPath.Format( KRomKeyStoragePath, (TUint)driveLetter );

    if (!found && iFs.GetDir(tempPath, KEntryAttDir, ESortByName, dir) == KErrNone)

#endif
        {
        LOG(_L("  Checking keys on Z:"));
        CleanupStack::PushL(dir);
        if (dir->Count() < 1)
            {
            User::Leave(KErrGeneral);
            }

#ifndef RD_MULTIPLE_DRIVE

        dirName.Copy(KRomKeyStoragePath);

#else //RD_MULTIPLE_DRIVE

        dirName.Copy(tempPath);

#endif

        dirName.Append((*dir)[0].iName);
        dirName.Append('\\');
        User::LeaveIfError(iFs.SetSessionPath(dirName));
        CleanupStack::PopAndDestroy(dir);
        found = ETrue;
        }
    if (!found)
        {
        User::Leave(KErrGeneral);
        }
    InitializeKeyL();
    CheckRootForCmlaL();
    iRootSelected = ETrue;
    LOG(_L("CDrmStdKeyStorage::SelectDefaultRootL <-"));
    }

TBool CDrmStdKeyStorage::SelectedRootIsCmla()
    {
    return iRootIsCmla;
    }

// -----------------------------------------------------------------------------
// DrmStdKeyStorage::GetTrustedRootsL
//
// -----------------------------------------------------------------------------
//

void CDrmStdKeyStorage::GetTrustedRootsL(
    RPointerArray<HBufC8>& aRootList)
    {
    CDir* dir = NULL;
    TInt i;
    TInt j;
    TBuf8<SHA1_HASH> hash;
    TEntry entry;
    TUint8 c;
    TInt r = KErrNone;

    LOG(_L("CDrmStdKeyStorage::GetTrustedRootsL ->"));
    aRootList.ResetAndDestroy();

#ifndef RD_MULTIPLE_DRIVE

    if (iFs.GetDir(KKeyStoragePath, KEntryAttDir, ESortByName, dir) == KErrNone)

#else //RD_MULTIPLE_DRIVE

    TFileName tempPath;
    TInt driveNumber( -1 );
    TChar driveLetter;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    tempPath.Format( KKeyStoragePath, (TUint)driveLetter );

    if (iFs.GetDir(tempPath, KEntryAttDir, ESortByName, dir) == KErrNone)

#endif
        {
        LOG(_L("  Getting roots on C:"));
        CleanupStack::PushL(dir);
        for (i = 0; i < dir->Count(); i++)
            {
            entry = (*dir)[i];
            hash.SetLength(0);
            LOG(entry.iName);
            r = KErrNone;
            for (j = 0; r == KErrNone && j < SHA1_HASH; j++)
                {
                TLex lex(entry.iName.Mid(j * 2, 2));
                r = lex.Val(c, EHex);
                hash.Append(c);
                }
            if (r == KErrNone)
                {
                aRootList.Append(hash.AllocL());
                }
            }
        CleanupStack::PopAndDestroy(dir);
        }

#ifndef RD_MULTIPLE_DRIVE

    if (iFs.GetDir(KRomKeyStoragePath, KEntryAttDir, ESortByName, dir) == KErrNone)

#else //RD_MULTIPLE_DRIVE

    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultRom, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    tempPath.Format( KRomKeyStoragePath, (TUint)driveLetter );

    if (iFs.GetDir(tempPath, KEntryAttDir, ESortByName, dir) == KErrNone)

#endif
        {
        LOG(_L("  Getting roots on Z:"));
        CleanupStack::PushL(dir);
        for (i = 0; i < dir->Count(); i++)
            {
            LOG(entry.iName);
            entry = (*dir)[i];
            hash.SetLength(0);
            r = KErrNone;
            for (j = 0; r == KErrNone && j < SHA1_HASH; j++)
                {
                TLex lex(entry.iName.Mid(j * 2, 2));
                r = lex.Val(c, EHex);
                hash.Append(c);
                }
            if (r == KErrNone)
                {
                aRootList.Append(hash.AllocL());
                }
            }
        CleanupStack::PopAndDestroy(dir);
        }
    LOG(_L("CDrmStdKeyStorage::GetTrustedRootsL <-"));
    }

// -----------------------------------------------------------------------------
// DrmStdKeyStorage::GetCertificateChainL
//
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::GetCertificateChainL(
    RPointerArray<HBufC8>& aCertChain)
    {
    TFileName fileName;
    TInt i;
    CDir* dir = NULL;
    HBufC8* cert = NULL;

    LOG(_L("CDrmStdKeyStorage::GetCertificateChainL ->"));
    if (!iRootSelected)
        {
        User::Leave(KErrGeneral);
        }
    aCertChain.ResetAndDestroy();
    ReadFileL(iFs, KDeviceCertFileName, cert);
    aCertChain.Append(cert);
    iFs.GetDir(KSingingCertPattern, KEntryAttNormal, ESortByName, dir);
    CleanupStack::PushL(dir);
    for (i = 0; i < dir->Count(); i++)
        {
        ReadFileL(iFs, (*dir)[i].iName, cert);
        aCertChain.AppendL(cert);
        }
    CleanupStack::PopAndDestroy(); // dir
    LOG(_L("CDrmStdKeyStorage::GetCertificateChainL <-"));
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::RsaSignL
// -----------------------------------------------------------------------------
//
HBufC8* CDrmStdKeyStorage::RsaSignL(
    const TDesC8& aInput)
    {
    return ModularExponentiateWithKeyL(aInput);
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::RsaDecryptL
// -----------------------------------------------------------------------------
//
HBufC8* CDrmStdKeyStorage::RsaDecryptL(
    const TDesC8& aInput)
    {
    return ModularExponentiateWithKeyL(aInput);
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::ImportDataL
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::ImportDataL(
    const TDesC8& aPrivateKey,
    const RArray<TPtrC8>& aCertificateChain)
    {
    TInt i;
    TInt n;
    HBufC8* publicKey = NULL;
    CX509Certificate* cert = NULL;
    CSHA1* hasher = NULL;
    TBuf8<SHA1_HASH> publicKeyHash;
    TFileName fileName;

    LOG(_L("CDrmStdKeyStorage::ImportDataL ->"));
    n = aCertificateChain.Count();
    cert = CX509Certificate::NewLC(aCertificateChain[n - 1]);
    publicKey = cert->DataElementEncoding(
        CX509Certificate::ESubjectPublicKeyInfo)->AllocL();
    CleanupStack::PushL(publicKey);
    hasher = CSHA1::NewL();
    CleanupStack::PushL(hasher);
    hasher->Update(*publicKey);
    publicKeyHash.Copy(hasher->Final());

#ifndef RD_MULTIPLE_DRIVE

    fileName.Copy(KKeyStoragePath);

#else //RD_MULTIPLE_DRIVE

    TInt driveNumber( -1 );
    TChar driveLetter;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    TFileName keyStorageDir;
    keyStorageDir.Format( KKeyStoragePath, (TUint)driveLetter );

    fileName.Copy(keyStorageDir);

#endif

    for (i = 0; i < SHA1_HASH; i++)
        {
        fileName.AppendNumFixedWidth(publicKeyHash[i], EHex, 2);
        }
    fileName.Append('\\');
    iFileMan->Delete(fileName, CFileMan::ERecurse);
    iFs.MkDirAll(fileName);
    iFs.SetSessionPath(fileName);
    WriteFileL(iFs, KDeviceKeyFileName, aPrivateKey);
    fileName.Copy(fileName);
    WriteFileL(iFs, KDeviceCertFileName, aCertificateChain[0]);
    for (i = 1; i < n; i++)
        {
        fileName.SetLength(0);
        fileName.AppendFormat(KSingingCertFmt, i - 1);
        WriteFileL(iFs, fileName, aCertificateChain[i]);
        }
    CleanupStack::PopAndDestroy(3); // hasher, publicKey, cert
    LOG(_L("CDrmStdKeyStorage::ImportDataL <-"));
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::GetDeviceSpecificKeyL
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::GetDeviceSpecificKeyL(
    TBuf8<KDeviceSpecificKeyLength>& aKey)
    {

    HBufC8* key = NULL;
    TInt n;
    CSHA1* hasher = NULL;
    TBuf8<SHA1_HASH> hash;

    if (iDeviceSpecificKey.Compare(KDefaultKey) == 0)
        {

        GetImeiL();

        HBufC8* buf = HBufC8::NewLC( iImei->Size() + sizeof(VID_DEFAULT) );
        TPtr8 ptr( buf->Des() );
        ptr.Copy( *iImei );
        ptr.Append(VID_DEFAULT);

        hasher = CSHA1::NewL();
        CleanupStack::PushL(hasher);
        hasher->Update(ptr);
        hash.Copy(hasher->Final());
        key=hash.AllocL();
        CleanupStack::PopAndDestroy(2,buf); // hasher,buf;

        n = Min(key->Length(), KDeviceSpecificKeyLength);
        iDeviceSpecificKey.Copy(key->Right(n));
        delete key;
        n = KDeviceSpecificKeyLength - n;
        while (n > 0)
            {
            iDeviceSpecificKey.Append(0);
            n--;
            }
        }

    aKey.Copy(iDeviceSpecificKey);
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::InitializeKeyL
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::InitializeKeyL()
    {
    HBufC8* key = NULL;
    TASN1DecInteger encInt;
    TInt pos = 0;

    LOG(_L("CDrmStdKeyStorage::InitializeKeyL ->"));
    delete iKey;
    iKey = NULL;
    ReadFileL(iFs, KDeviceKeyFileName, key);
    CleanupStack::PushL(key);
    TASN1DecGeneric gen(*key);
    gen.InitL();
    pos += gen.LengthDERHeader();
    if (gen.Tag() != EASN1Sequence)
        {
        User::Leave(KErrArgument);
        }
    encInt.DecodeDERShortL(*key, pos); // version
    RInteger modulus = encInt.DecodeDERLongL(*key, pos);
    CleanupStack::PushL(modulus);
    RInteger publicExponent = encInt.DecodeDERLongL(*key, pos);
    CleanupStack::PushL(publicExponent);
    RInteger privateExponent = encInt.DecodeDERLongL(*key, pos);
    CleanupStack::PushL(privateExponent);
    iKey = CRSAPrivateKeyStandard::NewL(modulus, privateExponent);
    CleanupStack::Pop(); // privateExponent
    CleanupStack::PopAndDestroy();// publicExponent
    CleanupStack::Pop(); // modulus
    CleanupStack::PopAndDestroy(); // key
    LOG(_L("CDrmStdKeyStorage::InitializeKeyL <-"));
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::ModularExponentiateWithKeyL
// -----------------------------------------------------------------------------
//
HBufC8* CDrmStdKeyStorage::ModularExponentiateWithKeyL(
    const TDesC8& aInput)
    {
    RInteger result;
    RInteger input;
    HBufC8* output;
    TInt keyLength = KKeyLength;

    LOG(_L("CDrmStdKeyStorage::ModularExponentiateWithKeyL ->"));
    input = OS2IPL(aInput);
    CleanupClosePushL(input);
    result = TInteger::ModularExponentiateL(input,iKey->D(), iKey->N());
    CleanupClosePushL(result);
    output = I2OSPL(result,  keyLength);
    CleanupStack::PopAndDestroy(2); // result, input
    LOG(_L("CDrmStdKeyStorage::ModularExponentiateWithKeyL <-"));
    return output;
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::CheckRootForCmlaL
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::CheckRootForCmlaL()
    {
    CDir* dir = NULL;
    HBufC8* buffer = NULL;
    HBufC* name = NULL;
    CX509Certificate* cert = NULL;

    LOG(_L("CDrmStdKeyStorage::CheckRootForCmlaL ->"));
    __UHEAP_MARK;
    iFs.GetDir(KSingingCertPattern, KEntryAttNormal, ESortByName, dir);
    CleanupStack::PushL(dir);
    ReadFileL(iFs, (*dir)[dir->Count() - 1].iName, buffer);
    CleanupStack::PushL(buffer);
    cert = CX509Certificate::NewL(*buffer);
    CleanupStack::PushL(cert);
    name = cert->SubjectName().DisplayNameL();
    CleanupStack::PushL(name);
    if (name->Find(KCmla) != KErrNotFound)
        {
        iRootIsCmla = ETrue;
        }
    else
        {
        iRootIsCmla = EFalse;
        }
    CleanupStack::PopAndDestroy(4); // name, cert, buffer, dir
    LOG(_L("CDrmStdKeyStorage::CheckRootForCmlaL <-"));
    __UHEAP_MARKEND;
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::GetRdbSerialNumberL
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::GetRdbSerialNumberL(
    TBuf8<KRdbSerialNumberLength>& aSerialNumber)
    {
    HBufC8* buffer = NULL;
    TUint att;

#ifndef RD_MULTIPLE_DRIVE

    if (iFs.Att(KSerialNumberFile, att) != KErrNone)

#else //RD_MULTIPLE_DRIVE

    TInt driveNumber( -1 );
    TChar driveLetter;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    TFileName serialNoFile;
    serialNoFile.Format( KSerialNumberFile, (TUint)driveLetter );

    if (iFs.Att(serialNoFile, att) != KErrNone)

#endif
        {
        GenerateNewRdbSerialNumberL();
        }

#ifndef RD_MULTIPLE_DRIVE

    ReadFileL(iFs, KSerialNumberFile, buffer);

#else //RD_MULTIPLE_DRIVE

    ReadFileL(iFs, serialNoFile, buffer);

#endif

    aSerialNumber.Copy(*buffer);
    delete buffer;
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::GenerateNewRdbSerialNumberL
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::GenerateNewRdbSerialNumberL()
    {
    TBuf8<KRdbSerialNumberLength> serialNumber;
    TPtr8 random( const_cast<TUint8*>(serialNumber.Ptr()),
                  KRdbSerialNumberLength,
                  KRdbSerialNumberLength );

    RandomDataGetL(random,KRdbSerialNumberLength);

#ifndef RD_MULTIPLE_DRIVE

    WriteFileL(iFs, KSerialNumberFile, random);

#else //RD_MULTIPLE_DRIVE

    TInt driveNumber( -1 );
    TChar driveLetter;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    TFileName serialNoFile;
    serialNoFile.Format( KSerialNumberFile, (TUint)driveLetter );

    WriteFileL(iFs, serialNoFile, random);

#endif

    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::UdtEncryptL
// -----------------------------------------------------------------------------
//
HBufC8* CDrmStdKeyStorage::UdtEncryptL(
    const TDesC8& aInput)
    {
    HBufC8* buffer = NULL;
    HBufC8* output = HBufC8::NewMaxLC( 256 );
    CX509Certificate* cert = NULL;
    CRSAPublicKey* key = NULL;
    TX509KeyFactory factory;
    CRSAPKCS1v15Encryptor* encryptor = NULL;
    TPtr8 result(const_cast<TUint8*>(output->Ptr()), 0, 256);

#ifndef RD_MULTIPLE_DRIVE

    ReadFileL(iFs, KUdtCertFileName, buffer);

#else //RD_MULTIPLE_DRIVE

    TInt driveNumber( -1 );
    TChar driveLetter;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultRom, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    TFileName udtCertFile;
    udtCertFile.Format( KUdtCertFileName, (TUint)driveLetter );

    ReadFileL(iFs, udtCertFile, buffer);

#endif

    CleanupStack::PushL(buffer);
    cert = CX509Certificate::NewL(*buffer);
    CleanupStack::PushL(cert);
    key = factory.RSAPublicKeyL(cert->PublicKey().KeyData());
    CleanupStack::PushL(key);

    encryptor = CRSAPKCS1v15Encryptor::NewLC(*key);
    encryptor->EncryptL(aInput, result);

    CleanupStack::PopAndDestroy(4); // encryptor, key, cert, buffer
    CleanupStack::Pop();// output
    return output;
    };

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::GetRootCertificatesL
// -----------------------------------------------------------------------------
//
void CDrmStdKeyStorage::GetRootCertificatesL(
          RPointerArray<HBufC8>& aRootCerts)
    {
    CDir* dir = NULL;
    CDir* rootCerts = NULL;
    TFileName dirName;
    HBufC8* cert = NULL;
    TInt i = 0;
    TBuf<256> path;

    iFs.SessionPath( path );

#ifndef RD_MULTIPLE_DRIVE

    if (iFs.GetDir(KKeyStoragePath, KEntryAttDir, ESortByName, dir) == KErrNone)

#else //RD_MULTIPLE_DRIVE

    TFileName tempPath;
    TInt driveNumber( -1 );
    TChar driveLetter;
    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultSystem, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    tempPath.Format( KKeyStoragePath, (TUint)driveLetter );

    if (iFs.GetDir(tempPath, KEntryAttDir, ESortByName, dir) == KErrNone)

#endif
        {
        CleanupStack::PushL(dir);
        for(i = 0; i < dir->Count(); i++)
            {
            if ((*dir)[i].IsDir())
                {

#ifndef RD_MULTIPLE_DRIVE

                dirName.Copy(KKeyStoragePath);

#else //RD_MULTIPLE_DRIVE

                dirName.Copy(tempPath);

#endif

                dirName.Append((*dir)[i].iName);
                dirName.Append('\\');
                User::LeaveIfError(iFs.SetSessionPath(dirName));
                User::LeaveIfError(iFs.GetDir(KSingingCertPattern, KEntryAttNormal, ESortByName, rootCerts));
                CleanupStack::PushL(rootCerts);
                ReadFileL(iFs, (*rootCerts)[rootCerts->Count() - 1].iName, cert);
                CleanupStack::PushL(cert);
                aRootCerts.AppendL(cert);
                CleanupStack::Pop(cert);
                CleanupStack::PopAndDestroy(); // rootCerts
                }
            }
        CleanupStack::PopAndDestroy(dir);
        }

#ifndef RD_MULTIPLE_DRIVE

    if (iFs.GetDir(KRomKeyStoragePath, KEntryAttDir, ESortByName, dir) == KErrNone)

#else //RD_MULTIPLE_DRIVE

    DriveInfo::GetDefaultDrive( DriveInfo::EDefaultRom, driveNumber );
    iFs.DriveToChar( driveNumber, driveLetter );

    tempPath.Format( KRomKeyStoragePath, (TUint)driveLetter );

    if (iFs.GetDir(tempPath, KEntryAttDir, ESortByName, dir) == KErrNone)

#endif
        {
        CleanupStack::PushL(dir);
        for(i = 0; i < dir->Count(); i++)
            {
            if ((*dir)[i].IsDir())
                {

#ifndef RD_MULTIPLE_DRIVE

                dirName.Copy(KRomKeyStoragePath);

#else //RD_MULTIPLE_DRIVE

                dirName.Copy(tempPath);

#endif

                dirName.Append((*dir)[i].iName);
                dirName.Append('\\');
                User::LeaveIfError(iFs.SetSessionPath(dirName));
                User::LeaveIfError(iFs.GetDir(KSingingCertPattern, KEntryAttNormal, ESortByName, rootCerts));
                CleanupStack::PushL(rootCerts);
                ReadFileL(iFs, (*rootCerts)[rootCerts->Count() - 1].iName, cert);
                CleanupStack::PushL(cert);
                aRootCerts.AppendL(cert);
                CleanupStack::Pop(cert);
                CleanupStack::PopAndDestroy(); // rootCerts
                }
            }
        CleanupStack::PopAndDestroy(dir);
        }
    iFs.SetSessionPath( path );
    }

// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::GetIMEIL
// -----------------------------------------------------------------------------
//
const TDesC& CDrmStdKeyStorage::GetImeiL()
    {
    if ( iImei )
        {
        return *iImei;
        }

#if (defined __WINS__ || defined WINSCW)
    // Default IMEI used for emulator
    _LIT( KDefaultSerialNumber, "123456789123456789" );
    iImei = KDefaultSerialNumber().AllocL();

    return *iImei;
#else

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
    if (!( caps & RMobilePhone::KCapsGetSerialNumber ))
        {
         User::Leave( KErrNotFound );
        }

    RMobilePhone::TMobilePhoneIdentityV1 id;
    TRequestStatus status;

    phone.GetPhoneId( status, id );

    User::WaitForRequest( status );

    User::LeaveIfError( status.Int() );

    iImei = id.iSerialNumber.AllocL();

    CleanupStack::PopAndDestroy( 3 ); // phone, item, etelServer

    HBufC8* buf = HBufC8::NewL( iImei->Size() );
    TPtr8 ptr( buf->Des() );
    ptr.Copy( *iImei );

    LOG(_L("IMEI:"));
    LOGHEX(ptr);
    delete buf;

    return *iImei;
#endif /* __WINS__ , WINSCW */

    }


// -----------------------------------------------------------------------------
// CDrmStdKeyStorage::RandomDataGetL
// -----------------------------------------------------------------------------
//

void CDrmStdKeyStorage::RandomDataGetL( TDes8& aData, const TInt aLength )
    {
    if ( aLength <= 0 )
        {
         User::Leave(KErrArgument);
        }

    TInt size = aData.MaxSize();

    if( size < aLength )
        {
        User::Leave(KErrOverflow);
        }

    TRandom::Random( aData );
    }

// end of file
