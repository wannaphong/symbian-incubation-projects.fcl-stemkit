// Copyright (c) 2002-2009 Nokia Corporation and/or its subsidiary(-ies).
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


#ifndef __MMFCLIENTTONEPLAY_H__
#define __MMFCLIENTTONEPLAY_H__


#include <e32std.h>
#include <e32base.h>
#include <mdaaudiotoneplayer.h>
#include <mmf/server/sounddevice.h>
//Panic category and codes
_LIT(KMMFMdaAudioToneUtilityPanicCategory, "MMFMdaAudioToneUtility");
enum TMMFMdaAudioToneUtilityPanicCodes
	{
	EMMFMdaAudioToneUtilityAlreadyPrepared,
	EMMFMdaAudioToneUtilityBadToneConfig,
	EMMFMdaAudioToneUtilityBadMixinCall
	};

/**
Active object utility class to allow the callback to be called asynchronously.
This should help prevent re-entrant code in clients of the mediaframework.
*/
class CMMFMdaAudioToneObserverCallback : public CActive, public MMdaAudioToneObserver, public MMdaAudioTonePlayStartObserver
	{
public:
	static CMMFMdaAudioToneObserverCallback* NewL(MMdaAudioToneObserver& aCallback, MMdaAudioTonePlayStartObserver& aPlayStartCallback);
	~CMMFMdaAudioToneObserverCallback();
// From MMdaAudioToneObserver
	virtual void MatoPrepareComplete(TInt aError);
	virtual void MatoPlayComplete(TInt aError);
// From MMdaAudioTonePlayStartObserver
	virtual void MatoPlayStarted(TInt aError);
private:
	CMMFMdaAudioToneObserverCallback(MMdaAudioToneObserver& aCallback, MMdaAudioTonePlayStartObserver& aPlayStartCallback);
	void RunL();
	void DoCancel();
private:
	enum TMMFAudioToneObserverCallbackAction {EPrepareComplete, EPlayComplete, EPlayStarted};
	MMdaAudioToneObserver& iCallback;
	MMdaAudioTonePlayStartObserver& iPlayStartCallback; 
	TMMFAudioToneObserverCallbackAction iAction;
	TInt iErrorCode;
	};

class CMMFToneConfig;

/**
Timer class used instead of actual CMMFDevSound - the interface isn't the same either!
*/
NONSHARABLE_CLASS( CDummyDevSound ) : public CTimer
	{
	public:
	static CDummyDevSound* NewL();
	
	void InitializeL(MDevSoundObserver& aDevSoundObserver);
	void RunL();
	void Play(const TTimeIntervalMicroSeconds& aDuration);
	
	private:
	CDummyDevSound();

	private:
	MDevSoundObserver* iObserver;
	};

/**
Concrete implementation of the CMdaAudioToneUtility API.
@see CMdaAudioToneUtility
*/
class CMMFMdaAudioToneUtility;
NONSHARABLE_CLASS( CMMFMdaAudioToneUtility ): public CBase,
											  public MMdaAudioToneObserver,
											  public MDevSoundObserver, 
											  public MMdaAudioTonePlayStartObserver
	{
friend class CMdaAudioToneUtility;
// only for testing purposes
friend class CTestStepUnitMMFAudClient;

public:
	static CMMFMdaAudioToneUtility* NewL(MMdaAudioToneObserver& aObserver, CMdaServer* aServer = NULL,
											   TInt aPriority = EMdaPriorityNormal, 
											   TInt aPref = EMdaPriorityPreferenceTimeAndQuality);

	~CMMFMdaAudioToneUtility();
	
	TMdaAudioToneUtilityState State();
	TInt MaxVolume();
	TInt Volume();
	void SetVolume(TInt aVolume); 
	void SetPriority(TInt aPriority, TInt aPref);
	void SetDTMFLengths(TTimeIntervalMicroSeconds32 aToneLength, 
										 TTimeIntervalMicroSeconds32 aToneOffLength,
										 TTimeIntervalMicroSeconds32 aPauseLength);
	void SetRepeats(TInt aRepeatNumberOfTimes, const TTimeIntervalMicroSeconds& aTrailingSilence);
	void SetVolumeRamp(const TTimeIntervalMicroSeconds& aRampDuration);
	TInt FixedSequenceCount();
	const TDesC& FixedSequenceName(TInt aSequenceNumber);
	void PrepareToPlayTone(TInt aFrequency, const TTimeIntervalMicroSeconds& aDuration);
	void PrepareToPlayDualTone(TInt aFrequencyOne, TInt aFrequencyTwo, const TTimeIntervalMicroSeconds& aDuration);
	void PrepareToPlayDTMFString(const TDesC& aDTMF);
	void PrepareToPlayDesSequence(const TDesC8& aSequence);
	void PrepareToPlayFileSequence(const TDesC& aFileName);
	void PrepareToPlayFileSequence(RFile& aFile);
	void PrepareToPlayFixedSequence(TInt aSequenceNumber);
	void CancelPrepare();
	void Play();
	void CancelPlay();
	TInt Pause();
	TInt Resume();

	void SetBalanceL(TInt aBalance=KMMFBalanceCenter);
	TInt GetBalanceL();
// From MMdaAudioToneObserver
	void MatoPrepareComplete(TInt aError);
	void MatoPlayComplete(TInt aError);

// From DevSoundObserver
	void InitializeComplete(TInt aError);
	void ToneFinished(TInt aError); 
	void BufferToBeFilled(CMMFBuffer* /*aBuffer*/) {User::Panic(KMMFMdaAudioToneUtilityPanicCategory,EMMFMdaAudioToneUtilityBadMixinCall);}
	void PlayError(TInt /*aError*/)	{User::Panic(KMMFMdaAudioToneUtilityPanicCategory,EMMFMdaAudioToneUtilityBadMixinCall);}
	void BufferToBeEmptied(CMMFBuffer* /*aBuffer*/)	{User::Panic(KMMFMdaAudioToneUtilityPanicCategory,EMMFMdaAudioToneUtilityBadMixinCall);} 
	void RecordError(TInt /*aError*/)	{User::Panic(KMMFMdaAudioToneUtilityPanicCategory,EMMFMdaAudioToneUtilityBadMixinCall);}
	void ConvertError(TInt /*aError*/)  {User::Panic(KMMFMdaAudioToneUtilityPanicCategory,EMMFMdaAudioToneUtilityBadMixinCall);}
	void DeviceMessage(TUid /*aMessageId*/, const TDesC8& /*aMsg*/) {User::Panic(KMMFMdaAudioToneUtilityPanicCategory,EMMFMdaAudioToneUtilityBadMixinCall);}	
	void SendEventToClient(const TMMFEvent& /*aEvent*/);
	
	TAny* CustomInterface(TUid aInterfaceId);
	
	void PlayAfterInitialized();

	void RegisterPlayStartCallback(MMdaAudioTonePlayStartObserver& aObserver);

// From MMdaAudioTonePlayStartObserver
	void MatoPlayStarted(TInt aError);
	
protected:
	CMMFMdaAudioToneUtility(MMdaAudioToneObserver& aCallback, TInt aPriority, TInt aPref);
	void ConstructL();
private:
	// functions to convert between MediaServer Balance and SoundDev balance
	void CalculateBalance( TInt& aBalance, TInt aLeft, TInt aRight ) const;
	void CalculateLeftRightBalance( TInt& aLeft, TInt& aRight, TInt aBalance ) const;
	
private:
	CDummyDevSound* iTimer;
	MMdaAudioToneObserver& iCallback;
	CMMFMdaAudioToneObserverCallback* iAsyncCallback;

	TMdaAudioToneUtilityState iState;

	TMMFPrioritySettings iPrioritySettings;
	
	TInt iInitializeState;   
	
	TInt iSequenceNumber;
	TBool iPlayCalled;
	
	TBool iInitialized;
	
	MMdaAudioTonePlayStartObserver* iPlayStartObserver;
	
#ifdef _DEBUG
	TBool iPlayCalledBeforeInitialized;
#endif

  TInt iDevSoundVolume;
  TInt iDevSoundBalance;
	TTimeIntervalMicroSeconds iDuration;
	};


#endif
