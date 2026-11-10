// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlayBot/VBPlayBotClock.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotRecorder.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotHospitalOpening.h"
#include "PlayBot/VBPlayBotHospitalCombat.h"
#include "VBPlayBotSessionSubsystem.generated.h"

class APawn;

// 별도 정상 시간 실행의 입력, 관측, 기록 수명을 하나로 관리한다.
UCLASS()
class UVBPlayBotSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	bool TickSession(float DeltaSeconds);
	void BeforeWorldTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void WorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void Finish(const FString& Reason, bool bPartialComplete, bool bRequestExit = true);
	bool Bind(UWorld* World, FString& Error);
	bool BeginStep(UWorld* World, FString& Error);
	FVBPlayBotClock Clock;
	FVBPlayBotInputDriver Input{EVBPlayBotClockMode::NativeSeconds};
	FVBPlayBotRecorder Recorder;
	FVBPlayBotRunConfig Config;
	FVBPlayBotHumanModel Human;
	FVBPlayBotHospitalObservation Observation;
	FVBPlayBotHospitalActions Actions;
	FVBPlayBotHospitalCombatObservation CombatObservation;
	FVBPlayBotHospitalCombatActions CombatActions;
	TMap<FName,FVBPlayBotHospitalFightSpec> FightSpecs;
	TWeakObjectPtr<UWorld> BoundWorld;
	FTSTicker::FDelegateHandle Ticker;
	FDelegateHandle WorldTickHandle;
	FDelegateHandle CleanupHandle;
	FString PendingFinish;
	bool bPendingPartialComplete = false;
	TArray<FVBPlayBotOpeningStep> Steps;
	int32 StepIndex = 0;
	double StepSeconds = 0.0;
	TWeakObjectPtr<APawn> BoundPawn;
	double ReadyTimeout = 0.0;
	double BoundSeconds = 0.0;
	bool bFinished = false;
	bool bBound = false;
	bool bPolicyCaptured = false;
	bool bOldInputWhenInactive = false;
	bool bWakeSent = false;
	int64 EventId = 0;
};
