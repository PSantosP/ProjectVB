// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"

class AVBPowerPanel;
class AVBPowerSwitch;
class AVBPhysicalDoor;
class UVBInteractorComponent;
class UVBInteractableComponent;
class UVBStoryStateComponent;
class ULightComponent;
class FWorldSceneViewExtension;
struct FVBPowerPanelExposureState;

// fixture 가 준비한 화면 측정이다 - 읽힘, 서사 PASS 가 아니다
class FVBPlayBotScenario_PowerPanel : public FVBPlayBotScenarioBase
{
public:
	virtual ~FVBPlayBotScenario_PowerPanel() override;
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
	virtual void Teardown(FVBPlayBotContext& Ctx) override;

private:
	void AddWalk(FString Name, FVector Point);
	void AddAim(FString Name, AActor* Target, bool bFramePanel);
	void AddUse(FString Cue, AActor* Target);
	void AddHold(FString Name, double Seconds, int32 ExpectedIndex, FString FromCue = FString());
	void Mark(FVBPlayBotContext& Ctx, const FString& Name);
	bool Framed(FVBPlayBotContext& Ctx, bool bPanel) const;
	// 살펴보기 변형 - 보통 look 입력으로 InspectYawOffsetDegrees 만큼씩 판을 지나 돌고, 판이 화면에 들고
	//   판 중심, 모서리로 가는 카메라 ray 가 pawn capsule 에 안 걸릴 때만 멈춘다(pawn 이 판을 가리지 않게).
	void AddInspect(FString Name, bool bPanel, double Seconds, int32 ExpectedIndex);
	bool ClearOfPawn(FVBPlayBotContext& Ctx, bool bPanel) const;
	// 카메라에서 패널 판(bPanel) 또는 출구 판 중심까지의 pitch. 판이 없으면 false
	bool PitchDelta(FVBPlayBotContext& Ctx, bool bPanel, double& OutDelta) const;
	void Sample(FVBPlayBotContext& Ctx);
	void DrainExposure(FVBPlayBotContext& Ctx);
	void WriteExposure(const FString& Line);
	void StopObservation();
	EVBPlayBotState Finish(FVBPlayBotContext& Ctx, FString Reason);

	FVBPlayBotSequence Flow;
	TWeakObjectPtr<AVBPowerPanel> Panel;
	TWeakObjectPtr<AVBPowerSwitch> L, D;
	TWeakObjectPtr<AVBPhysicalDoor> Door;
	TWeakObjectPtr<UVBInteractorComponent> Interactor;
	TWeakObjectPtr<UVBStoryStateComponent> Story;
	TWeakObjectPtr<ULightComponent> WorkLight, ExitLight;
	TWeakObjectPtr<UVBInteractableComponent> UseTarget;
	TArray<TWeakObjectPtr<AActor>> Labels;
	TArray<TWeakObjectPtr<ULightComponent>> Lights;
	TMap<TWeakObjectPtr<ULightComponent>, bool> LightVisible;
	TSharedPtr<FVBPowerPanelExposureState, ESPMode::ThreadSafe> ExposureState;
	TSharedPtr<FWorldSceneViewExtension, ESPMode::ThreadSafe> ExposureObserver;
	TUniquePtr<FArchive> ExposureFile;
	FDelegateHandle DoorHandle, RefusedHandle;
	FGameplayTag FixtureFact, AllocationTag, OpenedTag;
	FName InteractAction, JumpAction;
	FString ViewVariant, Phase, ObservationError;
	TMap<FString, int64> InjectionFrames;
	TArray<FString> PendingDoorEvents;
	int64 FirstFrame = -1, HoldStart = 0;
	int64 ExposureRows = 0, ValidExposureRows = 0;
	int64 PanelHoldFrames = 0, ExitHoldFrames = 0;
	int64 InspectAimStart = 0;
	TMap<FString, int64> InspectFrames;
	TMap<FString, bool> InspectClear;
	TArray<double> InspectYawOffsets;
	bool bBeforeUse = false;
	uint32 ReachedSerial = 0;
	bool bPrepared = false, bRoute = false, bInputTargets = true;
	bool bP0 = false, bP1 = false, bP2 = false, bP3 = false;
	bool bPanelFramed = true, bExitFramed = false, bObliqueP2 = false, bFinished = false;
	double ApproachTolerance = 0, WalkTimeout = 0, StateTimeout = 0, NeutralSeconds = 0;
	double ViewSeconds = 0, PreSeconds = 0, PostSeconds = 0;
	double AlignedDegrees = 0, PitchTolerance = 0, PitchMagnitude = 0, PitchInputSign = 0;
	double FramingMargin = 0, FloorZ = 0, FloorTolerance = 0;
	FVector ObliquePoint = FVector::ZeroVector;
};
