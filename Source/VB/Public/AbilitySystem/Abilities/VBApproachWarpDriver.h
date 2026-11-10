// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class ACharacter;
class UAnimMontage;
class UMotionWarpingComponent;
class USceneComponent;

// 대상 때문에 공격 워프를 끊는 이유. 능력이 로그 문구로 바꾼다.
enum class EVBWarpDropReason : uint8
{
	TargetDied,
	TargetDestroyed,
	TargetUntargetable,
};

// 한 프레임 붙잡기의 결과 - 능력이 로그로 남길 전환만 None 이 아니다.
enum class EVBWarpHoldEvent : uint8
{
	None, // 할 일 없음, 또는 붙잡은 채 그대로
	HoldStarted, // 멈추는 거리에 닿아 제자리에서 대상을 보게 붙잡았다
	Restored, // 대상이 다시 멀어져 원래 목표로 되돌렸다
	Dropped, // 대상이 자격을 잃었다 - 부르는 쪽이 이유를 남기고 지운다
	Forgot, // 다른 쪽이 목표를 지웠다 - 되살리지 않고 잊었다
};

struct FVBWarpHoldResult
{
	EVBWarpHoldEvent Event = EVBWarpHoldEvent::None;
	float Distance = 0.0f; // 공격자-대상 2D 거리(cm), HoldStarted, Restored 일 때만
};

/**
 * 근접 공격 한 타의 접근 워프(AttackTarget): 목표 세우기, 멈추는 거리에서 붙잡기, 창 시작 감지, 지우기.
 * 대상 고르기, 거리 계산, 로그는 능력 몫이다. 대상 때문에 끊는 길은 모두 능력의 DropWarpTarget 을 지난다.
 */
struct VB_API FVBApproachWarpDriver
{
	// 이번 몽타주로 묶는다. bTrackWindows 면 AttackTarget 창 시작 시각을 모은다. 몽타주나 컴포넌트가 없으면 false.
	bool Bind(UMotionWarpingComponent* Warping, const UAnimMontage* Montage, bool bTrackWindows);
	// 묶은 컴포넌트를 돌려주고 잊는다 - 부르는 쪽이 자기 구독을 거기서 뗀다.
	UMotionWarpingComponent* Unbind();

	// 목표를 세운다. bFollowTarget 이면 대상을 매 프레임 따라가고, 아니면 지금 자리에서 대상 쪽 한 점에 고정한다.
	void SetGoal(UMotionWarpingComponent& Warping, const AActor& Avatar, const USceneComponent& Target, float StopDistance,
		bool bFollowTarget);
	// 엔진 워프 계산 직전(OnPreUpdate)에 부른다.
	FVBWarpHoldResult Hold(const AActor& Avatar, float MinLead);
	// 목표와 붙잡기를 잊고 AttackTarget 을 지운다.
	void Clear(UMotionWarpingComponent* Warping);

	// 재생 위치가 창 시작을 막 지났으면 true(OutStart). 묶은 직후, 리셋 뒤 첫 호출은 위치만 잡는다.
	bool PassedWindowStart(float MontagePosition, float& OutStart);
	// 섹션 점프로 재생 위치가 건너뛸 때 - 그 사이의 창 시작을 "지났다" 로 읽지 않는다.
	void ResetWindowCheck() { bResetWindowCheck = true; }

	float GetStopDistance() const { return StopDistance; }

	// 두 캡슐 반경의 합(cm). 이보다 안쪽 목적지는 CMC 가 밀어내 떨린다.
	static float MinSeparation(const ACharacter* Self, const USceneComponent* Target);

private:
	void WriteGoal(UMotionWarpingComponent& Warping, const USceneComponent& Target) const;

	TWeakObjectPtr<UMotionWarpingComponent> BoundWarping;
	TArray<float> WindowStarts;
	float LastCheckPosition = 0.0f;
	bool bResetWindowCheck = true;

	TWeakObjectPtr<const USceneComponent> GoalTarget;
	float StopDistance = 0.0f;
	bool bGoalFollows = true;
	FVector FixedGoal = FVector::ZeroVector;
	FVector FixedAim = FVector::ZeroVector;
	FRotator FixedFacing = FRotator::ZeroRotator;
	bool bHeld = false;
};
