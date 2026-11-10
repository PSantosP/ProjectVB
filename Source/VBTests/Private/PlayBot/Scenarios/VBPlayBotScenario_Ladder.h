// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"

class AVBCharacter;
class AVBLadder;
class UVBTraversalComponent;
class USkeletalMeshComponent;
class UAnimMontage;
class IConsoleVariable;

// 일반 입력으로 도는 진단 코스. 접촉 품질은 여전히 눈으로 재는 값이다.
class FVBPlayBotScenario_Ladder : public FVBPlayBotScenarioBase
{
public:
	virtual ~FVBPlayBotScenario_Ladder() override;
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
	virtual void Teardown(FVBPlayBotContext& Ctx) override;

private:
	enum class EPhase : uint8 { Walk, Face, Enter, Idle, Sprint, Up, Stop, Resume, Down, LowStop, Top, Land, Done };
	void SetPhase(FVBPlayBotContext& Ctx, EPhase Next);
	const TCHAR* PhaseName() const;
	void Sample(FVBPlayBotContext& Ctx);
	void OnFinalized();
	void StopObserver();
	void WritePose(const FString& Line);
	EVBPlayBotState Finish(FVBPlayBotContext& Ctx, const FString& Reason);
	bool OnExpectedLadder() const;
	double Height() const;
	bool CheckLanding() const;
	// CheckLanding 조건마다 이름을 붙인다 - 시간 초과 이유(어느 것이 false 로 남았나)용.
	FString DescribeLanding() const;

	TWeakObjectPtr<AVBCharacter> Player;
	TWeakObjectPtr<AVBLadder> Ladder;
	TWeakObjectPtr<AActor> Landing;
	TWeakObjectPtr<UVBTraversalComponent> Traversal;
	TWeakObjectPtr<USkeletalMeshComponent> Mesh;
	FDelegateHandle FinalizedHandle;
	TUniquePtr<FArchive> PoseFile;
	IConsoleVariable* Telemetry = nullptr;
	int32 OldTelemetry = 0;
	bool bChangedTelemetry = false;
	// 프레임마다의 사다리 배치 B 단계(소비한 Montage 구간 + 최종 배치). IK 프레임의 전이 근거다.
	IConsoleVariable* PlacementTelemetry = nullptr;
	int32 OldPlacementTelemetry = 0;
	bool bChangedPlacementTelemetry = false;
	bool bObserve = false;
	bool bFinished = false;
	FString ObservationError;
	FString ActiveVariant;
	FName JumpAction;
	FName SprintAction;
	FVBPlayBotSequence Walk;
	FVBPlayBotSequence Enter;
	EPhase Phase = EPhase::Walk;
	int64 PhaseFrame = 0;
	int64 ActionReadyFrame = 0;
	int64 FirstRunFrame = -1;
	int64 FinalizeSerial = 0;
	int64 PoseRows = 0;
	int64 ClimbingPoses = 0;
	int32 SprintPressId = INDEX_NONE;
	int32 StableLandingFrames = 0;
	bool bJumpInjected = false;
	bool bSprintInjected = false;
	// -VBLadderNoSprint: 질주 없이 보통 속도로 오른다(패드 L3 는 앉기도 해서 사다리를 놓는다).
	bool bUseSprint = true;
	// -VBLadderDirectReverse: Resume 끝에서 스틱을 한 프레임에 위에서 아래로 꺾어(중립, 반응 지연 없음)
	//   사다리가 사이의 Idle Montage 없이 Up -> Down 으로 바로 바뀌게 한다.
	bool bDirectReverse = false;
	bool bSawDirectReverse = false;
	bool bEntered = false;
	bool bAscended = false;
	bool bStopped = false;
	bool bResumed = false;
	bool bDescended = false;
	bool bSawTopOut = false;
	bool bLanded = false;
	TWeakObjectPtr<UAnimMontage> PreviousMontage;
	double PreviousPosition = 0;
	double PreviousTime = -1;
	TArray<double> UpRates;
	TArray<double> DownRates;
	double StopMin = TNumericLimits<double>::Max();
	double StopMax = TNumericLimits<double>::Lowest();
	double Rate = 0;
	double RateTolerance = 0;
	double RateMaxSampleSeconds = 0;
	double RateMaxPhaseDelta = 0;
	double ApproachDistance = 0;
	double ApproachTimeout = 0;
	double StateTimeout = 0;
	double MoveTimeout = 0;
	double FaceDot = 0;
	double FaceMagnitude = 0;
	double FaceAngle = 0;
	double IdleSeconds = 0;
	double StopSeconds = 0;
	double StopMeasureSeconds = 0;
	double StopTolerance = 0;
	double FirstHeight = 0;
	double ResumeHeight = 0;
	double LowHeight = 0;
	double LandingZ = 0;
	double FloorTolerance = 0;
	int32 RequiredLandingFrames = 0;
	int32 RequiredRateSamples = 0;
};
