// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHitPhysicsConfig.h"

float FVBHitPhysicsTier::EvaluateBlend(float Elapsed) const
{
	if (Elapsed < 0.0f || Elapsed >= TotalSeconds())
	{
		return 0.0f;
	}

	if (Elapsed < RiseSeconds)
	{
		return MaxBlendWeight * (Elapsed / RiseSeconds);
	}

	return MaxBlendWeight * (1.0f - (Elapsed - RiseSeconds) / FallSeconds);
}

const FVBHitPhysicsTier* UVBHitPhysicsConfig::FindActiveTier(EVBHitImpact Impact) const
{
	const FVBHitPhysicsTier* Tier = Tiers.Find(Impact);
	return Tier && Tier->IsActive() ? Tier : nullptr;
}
