// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBApproachWarpDriver.h"

#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBMotionWarpingNames.h"
#include "AnimNotifyState_MotionWarping.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier.h"

bool FVBApproachWarpDriver::Bind(UMotionWarpingComponent* Warping, const UAnimMontage* Montage, bool bTrackWindows)
{
	BoundWarping.Reset();
	WindowStarts.Reset();
	bResetWindowCheck = true;
	if (!Montage || !Warping)
	{
		return false;
	}

	if (bTrackWindows)
	{
		// 창을 고르는 규칙은 VBMeleeAttackRules::FindApproachWarpModifier 와 같다(AttackTarget 을 겨눈 MotionWarping 노티).
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			const UAnimNotifyState_MotionWarping* WarpState = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
			const URootMotionModifier_Warp* Modifier = WarpState ? Cast<URootMotionModifier_Warp>(WarpState->RootMotionModifier) : nullptr;
			if (Modifier && Modifier->WarpTargetName == VBMotionWarpingNames::AttackTarget)
			{
				WindowStarts.Add(Event.GetTriggerTime());
			}
		}
	}

	BoundWarping = Warping;
	return true;
}

UMotionWarpingComponent* FVBApproachWarpDriver::Unbind()
{
	UMotionWarpingComponent* Warping = BoundWarping.Get();
	BoundWarping.Reset();
	return Warping;
}

void FVBApproachWarpDriver::SetGoal(UMotionWarpingComponent& Warping, const AActor& Avatar, const USceneComponent& Target,
	float InStopDistance, bool bFollowTarget)
{
	// 고정 목표: 지금 대상 위치에서 공격자 쪽으로 멈추는 거리인 점, 대상을 보는 회전. 휘두르는 도중 대상이 물러나도 목표는 안 움직인다.
	GoalTarget = &Target;
	StopDistance = InStopDistance;
	bGoalFollows = bFollowTarget;
	bHeld = false;
	if (!bFollowTarget)
	{
		const FVector TargetLocation = Target.GetComponentLocation();
		const FVector ToOwner = (Avatar.GetActorLocation() - TargetLocation).GetSafeNormal2D();
		FixedAim = FVector(TargetLocation.X, TargetLocation.Y, Avatar.GetActorLocation().Z);
		FixedGoal = FixedAim + ToOwner * InStopDistance;
		FixedFacing = ToOwner.IsNearlyZero() ? Avatar.GetActorRotation() : (-ToOwner).Rotation();
	}

	WriteGoal(Warping, Target);
}

void FVBApproachWarpDriver::WriteGoal(UMotionWarpingComponent& Warping, const USceneComponent& Target) const
{
	if (!bGoalFollows)
	{
		Warping.AddOrUpdateWarpTargetFromLocationAndRotation(VBMotionWarpingNames::AttackTarget, FixedGoal, FixedFacing);
		return;
	}
	// 대상 중심에서 공격자 쪽으로 멈추는 거리만큼 당긴 점을 매 프레임 따라간다(몽타주 SkewWarp 의 bWarpTranslation 이 켜져 있어야 위치가 반영된다).
	Warping.AddOrUpdateWarpTargetFromComponent(VBMotionWarpingNames::AttackTarget, &Target, FName(NAME_None), true,
		EWarpTargetLocationOffsetDirection::VectorFromTargetToOwner, FVector(StopDistance, 0.0f, 0.0f), FRotator::ZeroRotator);
}

FVBWarpHoldResult FVBApproachWarpDriver::Hold(const AActor& Avatar, float MinLead)
{
	// 따라가는 목표는 공격자 위치로 다시 계산되어, 멈추는 거리에 닿으면 Facing 이 0 벡터나 반대쪽을 본다 - 그 프레임의 목표를 여기서 정한다.
	FVBWarpHoldResult Result;
	const USceneComponent* Target = GoalTarget.Get();
	UMotionWarpingComponent* Warping = BoundWarping.Get();
	if (!Target || !Warping)
	{
		return Result;
	}

	if (!VBMeleeAttackRules::CanKeepAttackWarp(Target->GetOwner()))
	{
		Result.Event = EVBWarpHoldEvent::Dropped;
		return Result;
	}

	if (!Warping->FindWarpTarget(VBMotionWarpingNames::AttackTarget))
	{
		GoalTarget.Reset();
		bHeld = false;
		Result.Event = EVBWarpHoldEvent::Forgot;
		return Result;
	}

	const FVector OwnerLocation = Avatar.GetActorLocation();
	const FVector TargetLocation = Target->GetComponentLocation();
	// 붙잡은 동안은 엔진 목표가 붙잡은 점이라, 원래 목표를 기록에서 다시 계산해 "다시 앞섰나" 를 본다.
	FVector Goal, Aim;
	VBMeleeAttackRules::ResolveApproachGoal(bGoalFollows, OwnerLocation, TargetLocation, StopDistance, FixedGoal, FixedAim, Goal, Aim);
	bool bHold = false;
	const FVector Sync = VBMeleeAttackRules::ResolveApproachSyncPoint(OwnerLocation, Avatar.GetActorForwardVector(), Aim, Goal,
		MinLead, bHold);
	if (bHold)
	{
		if (!bHeld)
		{
			Result.Event = EVBWarpHoldEvent::HoldStarted;
			Result.Distance = FVector::Dist2D(OwnerLocation, TargetLocation);
		}

		bHeld = true;
		Warping->AddOrUpdateWarpTargetFromLocationAndRotation(VBMotionWarpingNames::AttackTarget, Sync,
			(Sync - OwnerLocation).GetSafeNormal2D().Rotation());
		return Result;
	}

	if (bHeld)
	{
		// 대상이 다시 멀어졌다(넉백 등). 고정 목표는 대상이 움직여도 안 움직이므로 다시 앞서는 일이 드물다.
		bHeld = false;
		Result.Event = EVBWarpHoldEvent::Restored;
		Result.Distance = FVector::Dist2D(OwnerLocation, TargetLocation);
		WriteGoal(*Warping, *Target);
	}

	return Result;
}

void FVBApproachWarpDriver::Clear(UMotionWarpingComponent* Warping)
{
	GoalTarget.Reset();
	bHeld = false;
	if (Warping)
	{
		Warping->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
	}
}

bool FVBApproachWarpDriver::PassedWindowStart(float MontagePosition, float& OutStart)
{
	// 창이 하나뿐인 몽타주는 시작, 전환 때 세운 것으로 충분하다.
	if (WindowStarts.Num() <= 1)
	{
		return false;
	}

	if (bResetWindowCheck)
	{
		// 묶은 직후, 타 전환 직후: 지금 위치까지의 창은 이미 세운 목표가 맡는다.
		LastCheckPosition = MontagePosition;
		bResetWindowCheck = false;
		return false;
	}

	bool bPassed = false;
	for (const float Start : WindowStarts)
	{
		if (LastCheckPosition < Start && Start <= MontagePosition)
		{
			OutStart = Start;
			bPassed = true;
			break;
		}
	}

	LastCheckPosition = MontagePosition;
	return bPassed;
}

float FVBApproachWarpDriver::MinSeparation(const ACharacter* Self, const USceneComponent* Target)
{
	if (!Self || !Target)
	{
		return 0.0f;
	}

	float Separation = 0.0f;
	if (const UCapsuleComponent* SelfCap = Self->GetCapsuleComponent())
	{
		Separation += SelfCap->GetScaledCapsuleRadius();
	}

	if (const ACharacter* TargetChar = Cast<ACharacter>(Target->GetOwner()))
	{
		if (const UCapsuleComponent* TargetCap = TargetChar->GetCapsuleComponent())
		{
			Separation += TargetCap->GetScaledCapsuleRadius();
		}
	}

	return Separation;
}
