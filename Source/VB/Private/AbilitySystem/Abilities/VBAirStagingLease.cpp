// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBAirStagingLease.h"

#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Character/VBAirAfterimageComponent.h"
#include "Character/VBTargetLockComponent.h"

void FVBAirStagingLease::HoldAfterimage(UVBAirAfterimageComponent& InAfterimage, uint64 RequestId)
{
	if (RequestId != 0)
	{
		Afterimage = &InAfterimage;
		AfterimageRequestId = RequestId;
	}
}

void FVBAirStagingLease::HoldBodyFade(UVBCharacterAppearanceComponent& Appearance, uint64 RequestId)
{
	if (RequestId != 0)
	{
		FadeAppearance = &Appearance;
		BodyFadeRequestId = RequestId;
	}
}

void FVBAirStagingLease::HoldCameraFraming(UVBTargetLockComponent& Lock, uint64 RequestId)
{
	if (RequestId != 0)
	{
		CameraFramingOwner = &Lock;
		CameraFramingRequestId = RequestId;
	}
}

void FVBAirStagingLease::Release()
{
	if (UVBCharacterAppearanceComponent* Appearance = FadeAppearance.Get())
	{
		Appearance->EndBodyFade(BodyFadeRequestId);
	}

	FadeAppearance.Reset();
	BodyFadeRequestId = 0;
	if (UVBAirAfterimageComponent* ActiveAfterimage = Afterimage.Get())
	{
		ActiveAfterimage->StopSampling(AfterimageRequestId);
	}

	Afterimage.Reset();
	AfterimageRequestId = 0;
	if (UVBTargetLockComponent* Lock = CameraFramingOwner.Get())
	{
		Lock->EndCameraFraming(CameraFramingRequestId);
	}

	CameraFramingOwner.Reset();
	CameraFramingRequestId = 0;
}
