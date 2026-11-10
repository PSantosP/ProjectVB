// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h"
#include "PoseSearch/PoseSearchLibrary.h"

class UPoseSearchDatabase;
struct FTransformTrajectory;

// MotionMatching 노드의 Blend Time 계산 입력.
struct FVBMMBlendTimeInputs
{
	EVBMovementMode MovementMode = EVBMovementMode::Grounded;
	EVBMovementMode MovementModeLastFrame = EVBMovementMode::Grounded;
	FVector Velocity = FVector::ZeroVector;
	float DefaultBlendTime = 0.0f;
	float FastLandBlendTime = 0.0f;
	float VeryShortJumpBlendTime = 0.0f;
	float InAirDefaultBlendTime = 0.0f;
	// Velocity.Z 가 이 이상이면 점프 시작으로 본다.
	float JumpSpeedThreshold = 0.0f;
};

// MotionMatching 검색 InterruptMode 계산 입력.
struct FVBMMInterruptInputs
{
	EVBMovementMode MovementMode = EVBMovementMode::Grounded;
	EVBMovementMode MovementModeLastFrame = EVBMovementMode::Grounded;
	EVBMMState MMState = EVBMMState::Idle;
	EVBMMState MMStateLastFrame = EVBMMState::Idle;
	EVBGait Gait = EVBGait::Run;
	EVBGait GaitLastFrame = EVBGait::Run;
	EVBStance Stance = EVBStance::Stand;
	EVBStance StanceLastFrame = EVBStance::Stand;
	EVBRotationMode RotationMode = EVBRotationMode::Strafe;
	EVBRotationMode RotationModeLastFrame = EVBRotationMode::Strafe;
	// 이번 프레임 풀 교체 펄스.
	bool bPoolInterrupt = false;
	const UPoseSearchDatabase* SelectedDatabase = nullptr;
};

// chooser 와 그래프 핀이 묻는 MotionMatching 판정. 상태 없이 입력만 읽으므로 작업자 스레드에서 불러도 된다.
namespace VBAnimMotionMatchingRules
{
	// 이동을 막 시작했나. Pivot DB 중이면 Start 로 보지 않는다.
	VB_API bool IsStarting(bool bMoving, const TArray<FName>& DatabaseTags, const FVector& Velocity, const FVector& FutureVelocity,
		float VelocityBias);

	// 현재와 미래 속도 방향 차이가 임계 이상인가.
	VB_API bool IsPivoting(const FVector& Velocity, const FVector& FutureVelocity, EVBRotationMode RotationMode,
		float StrafeThreshold,
		float FreeThreshold);

	// 루트와 캡슐 yaw 격차가 크고, strafe 중이거나 방금 멈췄는가.
	VB_API bool ShouldTurnInPlace(const FTransform& Root, const FTransform& Capsule, float YawDeltaThreshold, bool bStrafe,
		EVBMMState MMState, EVBMMState MMStateLastFrame);

	// 루트와 캡슐 격차가 크고 충분히 빠르며 이미 SpinTransition 중이 아닌가.
	VB_API bool ShouldSpinTransition(const FTransform& Root, const FTransform& Capsule, float YawDeltaThreshold, float Speed2D,
		float MinSpeed, const TArray<FName>& DatabaseTags);

	// 상황별 MotionMatching Blend Time 을 고른다.
	VB_API float ResolveBlendTime(const FVBMMBlendTimeInputs& In);

	// 상태가 바뀐 프레임에만 끊는다. 정지 유지 중 Stop 클립은 끊지 않는다.
	VB_API EPoseSearchInterruptMode ResolveInterruptMode(const FVBMMInterruptInputs& In);

	// 궤적에서 LookaheadSeconds 이상인 첫 샘플의 트랜스폼. 샘플이 없으면 Fallback.
	VB_API FTransform FindDesiredFacing(const FTransformTrajectory& Trajectory, float LookaheadSeconds, const FTransform& Fallback);
}
