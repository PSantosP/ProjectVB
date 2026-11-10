// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Combat/VBProjectileRules.h"

FVector VBProjectileRules::AimPoint(const FVector& TargetLocation, const FVector& TargetVelocity, const FVector& Origin,
	float Speed, float MaxLeadDistance)
{
	if (Speed <= 0.0f)
	{
		return TargetLocation;
	}

	const double FlightSeconds = FVector::Dist(Origin, TargetLocation) / Speed;
	const FVector Lead = (TargetVelocity * FlightSeconds).GetClampedToMaxSize(FMath::Max(MaxLeadDistance, 0.0f));
	return TargetLocation + Lead;
}

FVector VBProjectileRules::LaunchVelocity(const FVector& Origin, const FVector& AimPoint, float Speed, float GravityZ)
{
	const FVector ToAim = AimPoint - Origin;
	const double Distance = ToAim.Size();
	if (Speed <= 0.0f || Distance <= UE_KINDA_SMALL_NUMBER)
	{
		return FVector::ZeroVector;
	}

	const double FlightSeconds = Distance / Speed;
	// 직선 비행 시간 동안 떨어질 0.5 g t^2 를 위로 갚는다 - 그 시각에 정확히 겨눈 점에 닿는다.
	return ToAim / Distance * Speed + FVector::UpVector * (-0.5 * GravityZ * FlightSeconds);
}
