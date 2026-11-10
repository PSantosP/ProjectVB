// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimMotionMatchingRules.h"

#include "Animation/TrajectoryTypes.h"
#include "PoseSearch/PoseSearchDatabase.h"

namespace VBAnimMotionMatchingRules
{
	namespace
	{
		// 루트 본과 캡슐의 yaw 격차(도). ShouldTurnInPlace 와 ShouldSpinTransition 이 같은 식을 쓰도록 한 곳에 둔다.
		float RootVsCapsuleYawDelta(const FTransform& Root, const FTransform& Capsule)
		{
			return FMath::Abs(FRotator::NormalizeAxis(Root.Rotator().Yaw - Capsule.Rotator().Yaw));
		}
	}

	bool IsStarting(bool bMoving, const TArray<FName>& DatabaseTags, const FVector& Velocity, const FVector& FutureVelocity,
		float VelocityBias)
	{
		// GASP 그대로. Pivot DB 중이면 Start 로 보지 않는다(Pivot 중간 포즈가 Start 와 비슷하다).
		if (!bMoving)
		{
			return false;
		}

		for (const FName& Tag : DatabaseTags)
		{
			if (Tag.ToString().Contains(TEXT("Pivots")))
			{
				return false;
			}
		}

		// 미래 속도가 현재보다 VelocityBias 이상 크면 Start.
		return FutureVelocity.Size2D() >= (Velocity.Size2D() + VelocityBias);
	}

	bool IsPivoting(const FVector& Velocity, const FVector& FutureVelocity, EVBRotationMode RotationMode, float StrafeThreshold,
		float FreeThreshold)
	{
		// GASP 그대로. 현재와 미래 속도 방향 차이가 임계 이상이면 Pivot.
		if (Velocity.IsNearlyZero(1.0f))
		{
			return false;
		}

		if (FutureVelocity.IsNearlyZero(1.0f))
		{
			return false;
		}

		const FRotator RotCur = Velocity.ToOrientationRotator();
		const FRotator RotFut = FutureVelocity.ToOrientationRotator();
		const float Delta = FMath::Abs(FRotator::NormalizeAxis(RotFut.Yaw - RotCur.Yaw));

		const float Threshold = (RotationMode == EVBRotationMode::Strafe)
			? StrafeThreshold
			: FreeThreshold;
		return Delta >= Threshold;
	}

	bool ShouldTurnInPlace(const FTransform& Root, const FTransform& Capsule, float YawDeltaThreshold, bool bStrafe, EVBMMState MMState,
		EVBMMState MMStateLastFrame)
	{
		// GASP 그대로. 루트-캡슐 yaw 격차가 크고, strafe 중이거나 방금 멈췄으면 제자리 회전.
		const float Delta = RootVsCapsuleYawDelta(Root, Capsule);

		if (Delta < YawDeltaThreshold)
		{
			return false;
		}

		const bool bJustStopped = (MMState == EVBMMState::Idle) && (MMStateLastFrame == EVBMMState::Moving);
		return bStrafe || bJustStopped;
	}

	bool ShouldSpinTransition(const FTransform& Root, const FTransform& Capsule, float YawDeltaThreshold, float Speed2D, float MinSpeed,
		const TArray<FName>& DatabaseTags)
	{
		// GASP 그대로. 루트-캡슐 격차가 크고 충분히 빠르며 이미 SpinTransition 중이 아닐 때.
		const float Delta = RootVsCapsuleYawDelta(Root, Capsule);

		if (Delta < YawDeltaThreshold)
		{
			return false;
		}

		if (Speed2D < MinSpeed)
		{
			return false;
		}

		// 이미 SpinTransition DB 중이면 재진입 방지.
		for (const FName& Tag : DatabaseTags)
		{
			if (Tag.ToString().Contains(TEXT("SpinTransition")))
			{
				return false;
			}
		}

		return true;
	}

	float ResolveBlendTime(const FVBMMBlendTimeInputs& In)
	{
		// GASP 그대로. 기본 / 착지 / 점프 시작 / 공중 네 경우.
		switch (In.MovementMode)
		{
		case EVBMovementMode::Grounded:
			if (In.MovementModeLastFrame == EVBMovementMode::InAir)
			{
				return In.FastLandBlendTime;
			}

			return In.DefaultBlendTime;

		case EVBMovementMode::InAir:
			if (In.Velocity.Z > In.JumpSpeedThreshold)
			{
				return In.VeryShortJumpBlendTime;
			}

			return In.InAirDefaultBlendTime;

		default:
			return In.DefaultBlendTime;
		}
	}

	EPoseSearchInterruptMode ResolveInterruptMode(const FVBMMInterruptInputs& In)
	{
		// GASP 그대로. 상태가 바뀌면 InterruptOnDatabaseChange, Grounded 중에만 MMState/Gait/Stance 변화를 본다.
		const bool bMovementModeChanged = (In.MovementMode != In.MovementModeLastFrame);
		const bool bGroundedAndOtherChanged =
			(In.MovementMode == EVBMovementMode::Grounded) &&
			((In.MMState != In.MMStateLastFrame) || (In.Gait != In.GaitLastFrame) || (In.Stance != In.StanceLastFrame)
				|| (In.RotationMode != In.RotationModeLastFrame));
		// 무기 스왑(MM 풀 교체)도 interrupt 사유. 시점은 풀 교체 래치의 펄스가 정한다.
		const bool bWeaponPoolChanged = In.bPoolInterrupt;

		const bool bShouldInterrupt = bMovementModeChanged || bGroundedAndOtherChanged || bWeaponPoolChanged;
		if (!bShouldInterrupt)
		{
			return EPoseSearchInterruptMode::DoNotInterrupt;
		}

		// Stop 보호: 정지 유지 중 Stop 클립이면 interrupt 하지 않는다(아니면 Idle 로 즉시 끊긴다).
		//   점프, 재이동, 무기 스왑은 보호를 우회한다.
		if (!bMovementModeChanged && !bWeaponPoolChanged && In.MMState == EVBMMState::Idle && In.SelectedDatabase)
		{
			const FString DBName = In.SelectedDatabase->GetName();
			if (DBName.Contains(TEXT("Run_Stops")) || DBName.Contains(TEXT("Walk_Stops")))
			{
				return EPoseSearchInterruptMode::DoNotInterrupt;
			}
		}

		return EPoseSearchInterruptMode::InterruptOnDatabaseChange;
	}

	FTransform FindDesiredFacing(const FTransformTrajectory& Trajectory, float LookaheadSeconds, const FTransform& Fallback)
	{
		// 궤적에서 LookaheadSeconds 시점 샘플의 Transform. Steering 이 이 방향으로 root 를 끈다.
		const TArray<FTransformTrajectorySample>& Samples = Trajectory.Samples;
		if (Samples.Num() == 0)
		{
			// 샘플 없으면 현재 Character 방향 유지 (안전 default).
			return Fallback;
		}

		// 목표 시각 이상인 첫 샘플(없으면 마지막).
		const FTransformTrajectorySample* Best = &Samples.Last();
		for (const FTransformTrajectorySample& S : Samples)
		{
			if (S.TimeInSeconds >= LookaheadSeconds)
			{
				Best = &S;
				break;
			}
		}

		return FTransform(Best->Facing, Best->Position);
	}
}
