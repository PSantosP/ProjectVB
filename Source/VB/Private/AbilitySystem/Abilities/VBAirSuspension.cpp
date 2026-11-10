// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBAirSuspension.h"

#include "Data/VBHitReactConfig.h"
#include "GameFramework/CharacterMovementComponent.h"

void FVBAirSuspension::Begin(float MaxAirSeconds)
{
	ElapsedSeconds = 0.0f;
	BudgetSeconds = MaxAirSeconds;
	bLeftGround = false;
}

bool FVBAirSuspension::TryHold(float AttackerMaxAirSeconds)
{
	if (bHoldRequested || !FMath::IsFinite(AttackerMaxAirSeconds) || AttackerMaxAirSeconds <= 0.0f
		|| !FMath::IsFinite(ElapsedSeconds + AttackerMaxAirSeconds))
	{
		return false;
	}

	bHoldRequested = true;
	BudgetSeconds = FMath::Max(BudgetSeconds, ElapsedSeconds + AttackerMaxAirSeconds);
	return true;
}

bool FVBAirSuspension::CanSlam(float FallSpeed, float SlamMaxSeconds) const
{
	return FMath::IsFinite(FallSpeed) && FallSpeed > 0.0f && FMath::IsFinite(ElapsedSeconds + SlamMaxSeconds);
}

void FVBAirSuspension::Slam(UCharacterMovementComponent& Move, float FallSpeed, float SlamMaxSeconds)
{
	Release();
	Move.Velocity = FVector(0.0f, 0.0f, -FallSpeed);
	BudgetSeconds = FMath::Max(BudgetSeconds, ElapsedSeconds + SlamMaxSeconds);
	Move.UpdateComponentVelocity();
}

bool FVBAirSuspension::HoldUpAfterHit(const FVBKnockdownSet& Set, UCharacterMovementComponent* Move)
{
	if (bHoldRequested)
	{
		return false;
	}

	BudgetSeconds = Set.ExtendAirBudget(BudgetSeconds);
	if (Move && Set.AirHitHoldUpSpeed > 0.0f && Move->IsFalling())
	{
		Move->Velocity.Z = FMath::Max(Move->Velocity.Z, Set.AirHitHoldUpSpeed);
	}

	return true;
}

void FVBAirSuspension::FreezeIfHeld(UCharacterMovementComponent& Move, bool bFalling)
{
	if (!bHoldRequested || !bFalling || RestoreGravity.IsSet() || Move.Velocity.Z > 0.0f)
	{
		return;
	}

	RestoreGravity = Move.GravityScale;
	GravityOwner = &Move;
	Move.GravityScale = 0.0f;
	Move.Velocity.Z = 0.0f;
	Move.UpdateComponentVelocity();
}

void FVBAirSuspension::Release()
{
	if (RestoreGravity.IsSet())
	{
		UCharacterMovementComponent* Move = GravityOwner.Get();
		// 그동안 다른 쪽이 중력을 바꿨으면 그 값을 덮지 않는다.
		if (Move && Move->GravityScale == 0.0f)
		{
			Move->GravityScale = RestoreGravity.GetValue();
		}
	}

	RestoreGravity.Reset();
	GravityOwner.Reset();
	bHoldRequested = false;
}

EVBAirWatchResult FVBAirSuspension::AdvanceWatch(float DeltaSeconds, bool bFalling, bool bSlamReactionPlaying, float TakeoffGraceSeconds)
{
	ElapsedSeconds += DeltaSeconds;
	bLeftGround |= bFalling;
	if (bLeftGround && !bFalling)
	{
		return bSlamReactionPlaying && ElapsedSeconds < BudgetSeconds ? EVBAirWatchResult::Stay : EVBAirWatchResult::Landed;
	}

	if (!bLeftGround && ElapsedSeconds >= TakeoffGraceSeconds)
	{
		return EVBAirWatchResult::NeverLeftGround;
	}

	return ElapsedSeconds >= BudgetSeconds ? EVBAirWatchResult::BudgetSpent : EVBAirWatchResult::Stay;
}
