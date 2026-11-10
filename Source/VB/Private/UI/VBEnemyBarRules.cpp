// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBEnemyBarRules.h"

namespace VBEnemyBars
{
	TArray<AActor*> PickNearby(TArrayView<const FVBNearbyBarCandidate> Candidates, float Radius, int32 Max,
		const AActor* ExcludeA, const AActor* ExcludeB)
	{
		TArray<const FVBNearbyBarCandidate*> Eligible;
		const float RadiusSquared = FMath::Square(FMath::Max(Radius, 0.0f));
		for (const FVBNearbyBarCandidate& Candidate : Candidates)
		{
			if (Candidate.Actor && Candidate.bOnScreen && !Candidate.bDead && Candidate.DistanceSquared <= RadiusSquared
				&& Candidate.Actor != ExcludeA && Candidate.Actor != ExcludeB)
			{
				Eligible.Add(&Candidate);
			}
		}

		Eligible.StableSort(
			[](const FVBNearbyBarCandidate& A, const FVBNearbyBarCandidate& B) { return A.DistanceSquared < B.DistanceSquared; });
		TArray<AActor*> Picked;
		for (int32 Index = 0; Index < FMath::Min(Eligible.Num(), FMath::Max(Max, 0)); ++Index)
		{
			Picked.Add(Eligible[Index]->Actor);
		}

		return Picked;
	}
}
