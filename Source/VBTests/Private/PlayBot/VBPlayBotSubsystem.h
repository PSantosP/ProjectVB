// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "PlayBot/VBPlayBotChecks.h"
#include "PlayBot/VBPlayBotHumanModel.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "PlayBot/VBPlayBotRecorder.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "VBPlayBotSubsystem.generated.h"

class ACharacter;
class UAbilitySystemComponent;
class UFXSystemComponent;
struct FGameplayEventData;

// UVBPlayBotSubsystem - 사람처럼 노는 play bot 실행. -VBPlayBot=<Scenario> -VBPlayBotOut=<dir> -VBPlayBotFFmpeg=<exe> 로 연 게임 월드에만 있다
// 에디터나 PIE 에는 생기지 않는다 ([-VBPlayBotForceWrong=<check,...>] 는 선택)
// 처리하지 않은 상태는 모두 "abort" 기록과 종료 코드 2 로 실행을 끝낸다
UCLASS()
class UVBPlayBotSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	// 메뉴가 게임을 멈춰도 bot 은 계속 읽고 눌러야 한다 (안 그러면 일시정지 메뉴에서 멈춘다)
	virtual bool IsTickableWhenPaused() const override { return true; }

private:
	enum class EStage : uint8 { Boot, Ground, Wake, Run, Draining, Finished };

	void Abort(const FString& Reason);
	// 이 프레임의 telemetry 줄 뒤에 프레임을 먼저 찍고 다음 tick 머리에서 중단한다 (f 와 cap 을 1:1 로 유지)
	void AbortAfterCapture(const FString& Reason);
	void FinishRun();
	void Emit(const FString& Json);
	bool StepBoot();
	bool StepGround();
	bool StepWake();
	void RefreshContext();

	void ClearHostiles();
	void Watch(AActor* Actor);
	void UnbindEvents();
	void HandleWindowEvent(const FGameplayEventData* Payload, bool bOpen);
	void HandleHitEvent(const FGameplayEventData* Payload, TWeakObjectPtr<AActor> Victim);
	void HandleTag(const FGameplayTag Tag, int32 Count, TWeakObjectPtr<AActor> Owner);
	void HandleButton(const FVBPlayBotPress& Press, bool bDown);

	void SampleFrame();
	// +ImpactFx 규칙 - 준비 때 읽고, 표본 프레임마다 대기 중인 타격을 정산하고, 끝에 규칙마다 검사 하나를 낸다
	bool ParseImpactFxRules(FString& OutError);
	void SettleImpactFx(bool bRunEnded);
	void ReportImpactFx();
	FString ActorJson(ACharacter* Character, bool bPlayer) const;
	int32 CurrentHitIndex() const;

	FVBPlayBotRunConfig Config;
	FVBPlayBotHumanModel Human;
	FVBPlayBotInputDriver Input;
	FVBPlayBotRecorder Recorder;
	FVBPlayBotOverrides Overrides;
	FVBPlayBotChecks Checks;
	FVBPlayBotContext Ctx;
	TUniquePtr<FVBPlayBotScenarioBase> Scenario;

	TWeakObjectPtr<ACharacter> Player;
	TWeakObjectPtr<UAbilitySystemComponent> PlayerASC;
	struct FWatched
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UAbilitySystemComponent> ASC;
		FDelegateHandle Hit;
		TMap<FGameplayTag, FDelegateHandle> Tags;
	};
	TArray<FWatched> Watched;
	FDelegateHandle WindowOpenHandle;
	FDelegateHandle WindowCloseHandle;
	TSet<TWeakObjectPtr<UFXSystemComponent>> KnownFX;
	// +ImpactFx=<impact tag>|<effect asset name> - 그 impact 의 플레이어 타격은 모두 그 이펙트를 내야 한다
	struct FImpactFxRule
	{
		FString Impact;
		FString Fx;
		int32 Hits = 0;
		int32 Found = 0;
		int64 FirstMissFrame = -1;
	};
	TArray<FImpactFxRule> ImpactFxRules;
	// 아직 규칙의 이펙트를 기다리는 플레이어 타격 - 규칙 index, 타격 프레임
	TArray<TPair<int32, int64>> PendingImpactFx;
	int32 ImpactFxGraceFrames = 1;

	EStage Stage = EStage::Boot;
	int64 Frame = 0;
	int64 StageFrame = 0;
	int64 PadReadyFrame = -1;
	int64 RunStartFrame = 0;
	int64 RequestedFrames = 0;
	bool bSampledThisFrame = false;
	FString PendingAbort;
	bool bCsvCapturing = false;
	int32 NonTargetHits = 0;
	FString FirstNonTarget;
	TSharedFuture<FString> CsvFile;
	bool bActive = false;
	bool bInputPolicyCaptured = false;
	bool bPreviousInputWhenInactive = false;
	bool bEnded = false;
	FString ScenarioName;
};
