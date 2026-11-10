// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h"

struct FRichCurve;

// 이동 방향 각도 계산의 입력.
struct FVBLocomotionAngleInputs
{
	float CurrentAngle = 0.0f;
	bool bShouldMove = false;
	float GroundSpeed = 0.0f;
	// 이 속도 이하면 Velocity 방향을 믿지 않는다.
	float MinSpeed = 0.0f;
	FVector Velocity = FVector::ZeroVector;
	FRotator ActorRotation = FRotator::ZeroRotator;
	float InterpSpeed = 0.0f;
	float DeltaSeconds = 0.0f;
};

// 재생 속도 계산의 입력.
struct FVBPlayRateInputs
{
	bool bShouldMove = false;
	float GroundSpeed = 0.0f;
	float MoveThreshold = 0.0f;
	EVBGait Gait = EVBGait::Run;
	// 키가 하나라도 있으면 속도 -> 재생 속도 곡선을 쓴다. 없으면 Gait 기준 속도 비율.
	const FRichCurve* SpeedCurve = nullptr;
	float MinPlayRate = 0.0f;
	float MaxPlayRate = 0.0f;
	float WalkRootMotionSpeed = 0.0f;
	float RunRootMotionSpeed = 0.0f;
};

// 이동 상태와 Gait 한 걸음의 입력.
struct FVBMovementStateInputs
{
	FVector Velocity = FVector::ZeroVector;
	FVector FutureVelocity = FVector::ZeroVector;
	bool bHasMovementInput = false;
	bool bSprintActive = false;
	EVBGait CharacterGait = EVBGait::Run;
	EVBGait LastMovingGait = EVBGait::Run;
};

// 이동 상태와 Gait 한 걸음의 결과.
struct FVBMovementStateResult
{
	EVBMMState MMState = EVBMMState::Idle;
	EVBGait Gait = EVBGait::Run;
	EVBGait LastMovingGait = EVBGait::Run;
};

// 캐릭터 로컬 가속 계산의 입력.
struct FVBRelativeAccelerationInputs
{
	FVector Acceleration = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	// 프레임 사이 실제 속도 변화.
	FVector VelocityAcceleration = FVector::ZeroVector;
	FQuat CharacterRotation = FQuat::Identity;
	float MaxAcceleration = 0.0f;
	float MaxBrakingDeceleration = 0.0f;
};

// UVBAnimInstance 의 이동 값 계산. 상태 없이 입력만 읽으므로 어느 스레드에서 불러도 된다.
namespace VBAnimLocomotionRules
{
	// 이동 방향과 캐릭터 방향의 각도 차를 보간한다(Orientation Warping 입력).
	VB_API float ComputeLocomotionAngle(const FVBLocomotionAngleInputs& In);

	// 애니메이션 재생 속도를 정한다.
	VB_API float ComputeWantedPlayRate(const FVBPlayRateInputs& In);

	// 지금 속도와 미래 속도로 MMState 와 Gait 를 정한다.
	VB_API FVBMovementStateResult StepMovementStateAndGait(const FVBMovementStateInputs& In);

	// 그래프가 읽는 회전 모드를 정한다. WantsToStrafe 는 일부러 안 본다.
	VB_API EVBRotationMode ResolveRotationMode(bool bSprinting, bool bLockedOn, bool bArmedCombat);

	// 메시 전방 관례를 보정한 root 트랜스폼을 낸다.
	VB_API FTransform ApplyRootYawCorrection(FTransform Root, float YawCorrection);

	// 가속을 최대 가속 또는 감속으로 정규화한 캐릭터 로컬 값([-1..1]).
	VB_API FVector ComputeRelativeAcceleration(const FVBRelativeAccelerationInputs& In);

	// 기울기 블렌드스페이스 입력. 옆 가속에 속도 비율을 곱한다.
	VB_API FVector2D ComputeLeanAmount(const FVector& RelativeAcceleration, float Speed2D, float RunRootMotionSpeed);

	// 착지까지 남은 시간 추정. 지상 0, 상승 중 999.
	VB_API float ComputeTimeToLand(EVBMovementMode MovementMode, const FVector& Velocity, float LandPredictionHeight);
}
