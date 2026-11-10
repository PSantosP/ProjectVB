// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimLocomotionRules.h"

#include "Curves/RichCurve.h"

namespace VBAnimLocomotionRules
{
	float ComputeLocomotionAngle(const FVBLocomotionAngleInputs& In)
	{
		float TargetAngle = 0.0f;

		if (In.bShouldMove && In.GroundSpeed > In.MinSpeed)
		{
			// 충분한 속도가 있을때만 Velocity 방향을 신뢰한다.
			const FRotator VelocityRotation = In.Velocity.ToOrientationRotator();
			const FRotator DeltaRot = (VelocityRotation - In.ActorRotation).GetNormalized();
			TargetAngle = DeltaRot.Yaw;
		}

		const float AngleDiff = FRotator::NormalizeAxis(TargetAngle - In.CurrentAngle);
		const float InterpAlpha = FMath::Min(In.InterpSpeed * In.DeltaSeconds, 1.0f);
		return FRotator::NormalizeAxis(In.CurrentAngle + AngleDiff * InterpAlpha);
	}

	float ComputeWantedPlayRate(const FVBPlayRateInputs& In)
	{
		if (In.bShouldMove && In.GroundSpeed > In.MoveThreshold)
		{
			// 곡선이 있으면 속도->PlayRate, 없으면 Gait 기준 속도 비율.
			if (In.SpeedCurve && In.SpeedCurve->GetNumKeys() > 0)
			{
				return FMath::Clamp(In.SpeedCurve->Eval(In.GroundSpeed), In.MinPlayRate, In.MaxPlayRate);
			}

			// Walk 는 Walk 기준 속도, Run, Sprint 는 Run 기준 속도.
			const float ReferenceSpeed = (In.Gait == EVBGait::Walk)
				? In.WalkRootMotionSpeed
				: In.RunRootMotionSpeed;

			if (ReferenceSpeed > KINDA_SMALL_NUMBER)
			{
				return FMath::Clamp(In.GroundSpeed / ReferenceSpeed, In.MinPlayRate, In.MaxPlayRate);
			}

			return 1.0f;
		}

		return 1.0f;
	}

	FVBMovementStateResult StepMovementStateAndGait(const FVBMovementStateInputs& In)
	{
		FVBMovementStateResult Result;
		Result.LastMovingGait = In.LastMovingGait;

		// GASP IsMoving() 그대로(허용 오차 0.1).
		const bool bIsMovingGASP =
			!In.Velocity.IsNearlyZero(0.1f) && !In.FutureVelocity.IsNearlyZero(0.1f);

		if (bIsMovingGASP)
		{
			Result.MMState = EVBMMState::Moving;
			// 질주 중엔 Gait 를 Run 으로 강제해 Run_Loops 풀을 타게 한다(Walk 토글 상태에서도).
			if (In.bHasMovementInput)
			{
				Result.Gait = In.bSprintActive ? EVBGait::Run : In.CharacterGait;
				Result.LastMovingGait = Result.Gait;
			}
			else
			{
				Result.Gait = Result.LastMovingGait;
			}
		}
		else
		{
			Result.MMState = EVBMMState::Idle;
			// 정지 시 마지막 Gait 를 유지해야 Walk_Stops 가 매칭된다.
			Result.Gait = Result.LastMovingGait;
		}

		return Result;
	}

	EVBRotationMode ResolveRotationMode(bool bSprinting, bool bLockedOn, bool bArmedCombat)
	{
		if (bSprinting)
		{
			return EVBRotationMode::OrientToMovement;
		}

		if (bLockedOn)
		{
			return EVBRotationMode::OrientToTarget;
		}

		if (bArmedCombat)
		{
			return EVBRotationMode::Strafe;
		}

		// 기본은 OrientToMovement(GASP 기본).
		return EVBRotationMode::OrientToMovement;
	}

	FTransform ApplyRootYawCorrection(FTransform Root, float YawCorrection)
	{
		if (!FMath::IsNearlyZero(YawCorrection))
		{
			FRotator Rot = Root.Rotator();
			Rot.Yaw = FRotator::NormalizeAxis(Rot.Yaw + YawCorrection);
			Root.SetRotation(Rot.Quaternion());
		}

		return Root;
	}

	FVector ComputeRelativeAcceleration(const FVBRelativeAccelerationInputs& In)
	{
		if (In.MaxAcceleration <= 0.f || In.MaxBrakingDeceleration <= 0.f)
		{
			return FVector::ZeroVector;
		}

		const bool bAccelerating = FVector::DotProduct(In.Acceleration, In.Velocity) > 0.f;
		const float MaxScale = bAccelerating ? In.MaxAcceleration : In.MaxBrakingDeceleration;
		if (MaxScale <= KINDA_SMALL_NUMBER)
		{
			return FVector::ZeroVector;
		}

		FVector Clamped = In.VelocityAcceleration;
		const float SizeSq = Clamped.SizeSquared();
		if (SizeSq > MaxScale * MaxScale)
		{
			Clamped = Clamped.GetSafeNormal() * MaxScale;
		}

		const FVector Normalized = Clamped / MaxScale;

		return In.CharacterRotation.UnrotateVector(Normalized);
	}

	FVector2D ComputeLeanAmount(const FVector& RelativeAcceleration, float Speed2D, float RunRootMotionSpeed)
	{
		// 좌우 기울기 = 로컬 가속 Y × 속도 비율. Y 는 안 쓴다. GASP 그대로.
		const float SpeedScale = FMath::GetMappedRangeValueClamped(
			FVector2D(0.f, RunRootMotionSpeed),
			FVector2D(0.f, 1.f),
			Speed2D);
		return FVector2D(RelativeAcceleration.Y * SpeedScale, 0.f);
	}

	float ComputeTimeToLand(EVBMovementMode MovementMode, const FVector& Velocity, float LandPredictionHeight)
	{
		// 착지까지 남은 시간 추정(InAir chooser 의 Jumps_Far 분기). 지면 0, 상승 중 999.
		if (MovementMode == EVBMovementMode::Grounded)
		{
			return 0.0f;
		}

		const float Vz = Velocity.Z;
		if (Vz >= 0.0f)
		{
			return 999.0f;
		}

		const float Result = LandPredictionHeight / -Vz;
		return FMath::Clamp(Result, 0.0f, 999.0f);
	}
}
