// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Data/VBTargetLockConfig.h"

bool FVBTargetLockFraming::IsValid() const
{
	return FMath::IsFinite(MinPitch) && FMath::IsFinite(MaxPitch) && MinPitch >= -89.0f && MaxPitch <= 89.0f && MinPitch <= MaxPitch
		&& FMath::IsFinite(RearYawOffset) && FMath::IsFinite(InterpSpeed) && InterpSpeed > 0.0f
		&& FMath::IsFinite(MaxTrackingDegreesPerSecond) && MaxTrackingDegreesPerSecond > 0.0f
		&& FMath::IsFinite(DefeatFollowSeconds) && DefeatFollowSeconds >= 0.0f;
}

FRotator FVBTargetLockFraming::DesiredRotation(float OwnerYaw, const FVector& CameraLocation, const FVector& Aim) const
{
	return FRotator(FMath::ClampAngle((Aim - CameraLocation).Rotation().Pitch, MinPitch, MaxPitch), OwnerYaw + RearYawOffset, 0.0f);
}
