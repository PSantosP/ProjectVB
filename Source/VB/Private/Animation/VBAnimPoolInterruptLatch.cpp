// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimPoolInterruptLatch.h"

#include "GameFramework/CharacterMovementComponent.h"

bool FVBAnimPoolInterruptLatch::Step(const FInputs& In)
{
	bool bFire = false;

	// A: MM 풀이 바뀌면 래치했다가 속도가 수렴하거나 타임아웃일 때 한 번 발화(즉시 발화하면 두 번 갈아탄다).
	if (In.Pool != In.PoolLastFrame)
	{
		bPending = true;
		PendingTime = 0.0f;
	}

	if (bPending)
	{
		PendingTime += In.DeltaSeconds;
		const float TargetMax = In.Movement ? In.Movement->GetMaxSpeed() : 0.0f;
		const bool bSpeedSettled = (FMath::Abs(In.Speed2D - TargetMax) < In.SpeedSettleTolerance) || (In.Speed2D < In.NearStopSpeed);
		if (bSpeedSettled || PendingTime > In.TimeoutSeconds)
		{
			bFire = true;
			bPending = false;
		}
	}

	// B: 같은 풀 안에서 발도<->납도가 바뀌면 즉시 발화. 풀이 바뀌는 경우는 A 가 맡아 이중 발화가 없다.
	const bool bSheathed = (In.Pool != EVBWeaponType::None
		&& In.LocomotionState == EVBLocomotionState::CombatSheathed);
	if (bSheathed != bSheathedLastFrame && In.Pool == In.PoolLastFrame)
	{
		bFire = true;
		bPending = false;
	}

	bSheathedLastFrame = bSheathed;
	return bFire;
}
