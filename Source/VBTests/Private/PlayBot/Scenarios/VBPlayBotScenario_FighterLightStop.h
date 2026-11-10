// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlayBot/VBPlayBotBrain.h"
#include "PlayBot/VBPlayBotScenario.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "PlayBot/VBPlayBotSkills.h"

// 조작감 측정: Fighter 약공격 콤보를 1..4 타 뒤에 멈추고 회복 끊기 끔, 켬을 비교한다.
// PASS/FAIL 이 아니라 측정값을 낸다. 실제로 안 맞은 타격은 건너뛰고 기록한다.
class FVBPlayBotScenario_FighterLightStop : public FVBPlayBotScenarioBase
{
public:
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
	virtual void Teardown(FVBPlayBotContext& Ctx) override;

private:
	enum class EPhase : uint8 { Prepare, Settle, Trial };

	bool BeginNextTrial(FVBPlayBotContext& Ctx);
	void ResetPositions(FVBPlayBotContext& Ctx);
	FVBPlayBotPercept Perceive(const FVBPlayBotContext& Ctx) const;

	FVBPlayBotComboParams Params;
	FString Weapon;
	FName WeaponAction;
	FString EnemyClass;
	double EnemyDistance = 400.0;
	// 선택: 초기화마다 "플레이어를 봄" 에 더할 대상 yaw. 0 = 정면 타격, +90 = 공격자가 대상 왼쪽, -90 = 오른쪽, 180 = 뒤.
	// 사분면은 VBComputeHitDirection 기준이다.
	double TargetYawOffsetDeg = 0.0;
	double SettleSeconds = 1.5;
	TArray<int32> StopAfter;
	int32 Repetitions = 5;
	int32 PracticeTrials = 3;
	TArray<FVBPlayBotVariant> Variants;

	FVBPlayBotBrain Brain;
	TWeakObjectPtr<UAbilitySystemComponent> WindowASC;
	FDelegateHandle WindowOpenHandle;
	FDelegateHandle WindowCloseHandle;
	// 다른 delegate 가 Montage 섹션을 건너뛴 뒤의 섹션이 아니라, 고른 저작 창을 알아본다.
	TWeakObjectPtr<const UObject> RepushWindowNotify;
	// 창 이벤트는 runner 가 Ctx.Frame 을 갱신하기 전, 월드 시뮬레이션 도중에 온다.
	TArray<TPair<bool, int32>> PendingWindows;
	FVBPlayBotSequence Prepare;
	TWeakObjectPtr<ACharacter> Enemy;
	FVector PlayerStart = FVector::ZeroVector;
	FVector EnemyStart = FVector::ZeroVector;
	TArray<FVBPlayBotTrialPlan> Plan;
	int32 PlanIndex = INDEX_NONE;
	FString ActiveVariant;
	bool bModelReady = false;
	TSet<int32> TrialContacts;
	EPhase Phase = EPhase::Prepare;
	int64 PhaseFrame = 0;
	bool bMoveRouteChecked = false;
	int64 StickActiveFrames = 0;
	bool bFinished = false;
	int32 CompletedMeasured = 0;
	// StopAfter 값마다, 멈출 섹션에 실제로 닿은 측정 시행 수.
	TMap<int32, int32> ReachedByStop;
};
