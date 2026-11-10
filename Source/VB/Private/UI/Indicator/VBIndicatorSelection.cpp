// ProjectVB Copyright (c) 2025. All Rights Reserved.
#include "UI/Indicator/VBIndicatorSelection.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"

namespace VBIndicatorSelection
{
	bool MatchesViewer(const FVBIndicatorRequest& Request, const APlayerController* Viewer)
	{
		if (!IsValid(Viewer) || Viewer->IsActorBeingDestroyed())
		{
			return false;
		}

		if (Request.ViewerScope == EVBIndicatorViewerScope::AllLocalViewers)
		{
			return true;
		}

		return Request.ViewerScope == EVBIndicatorViewerScope::OneViewer && Request.Viewer.Get() == Viewer;
	}

	FVBIndicatorViewerClaims BuildClaims(TConstArrayView<FVBIndicatorSelectionCandidate> Candidates,
		const FGameplayTagContainer& ClaimingGroups, const APlayerController* Viewer)
	{
		FVBIndicatorViewerClaims Result;
		Result.Viewer = const_cast<APlayerController*>(Viewer);
		for (const FVBIndicatorSelectionCandidate& Candidate : Candidates)
		{
			const FVBIndicatorRequest& Request = Candidate.Request;
			if (Candidate.Id != 0 && Request.AnchorComponent.IsValid() && Request.Subject.IsValid()
				&& MatchesViewer(Request, Viewer) && ClaimingGroups.HasTagExact(Request.Group))
			{
				Result.Claims.Add(FVBIndicatorSubjectClaim{Request.Group, Request.Subject});
			}
		}

		return Result;
	}

	bool IsClaimed(const FVBIndicatorViewerClaims& Claims, const APlayerController* Viewer,
		const AActor* Subject, const FGameplayTagContainer& ExcludedGroups)
	{
		if (!IsValid(Viewer) || Claims.Viewer.Get() != Viewer || !Subject)
		{
			return false;
		}

		return Claims.Claims.ContainsByPredicate([Subject, &ExcludedGroups](const FVBIndicatorSubjectClaim& Claim)
		{
			return Claim.Subject.Get() == Subject && ExcludedGroups.HasTagExact(Claim.Group);
		});
	}

	TArray<uint64> Select(TConstArrayView<FVBIndicatorSelectionCandidate> Candidates,
		const FVBIndicatorSelectionParams& Params, const APlayerController* Viewer,
		const FVBIndicatorViewerClaims& Claims, TFunctionRef<bool(const AActor*)> IsDead)
	{
		TArray<uint64> Result;
		if (!Params.Group.IsValid() || Params.MaxCount <= 0 || !IsValid(Viewer))
		{
			return Result;
		}

		const float Radius = FMath::IsFinite(Params.Radius) ? FMath::Max(Params.Radius, 0.0f) : 0.0f;
		// VBEnemyBars::PickNearby 의 float 경계와 같게 - 0.1f 같은 반경도.
		const double RadiusSquared = FMath::Square(Radius);
		TArray<const FVBIndicatorSelectionCandidate*> Eligible;
		Eligible.Reserve(Candidates.Num());
		for (const FVBIndicatorSelectionCandidate& Candidate : Candidates)
		{
			const FVBIndicatorRequest& Request = Candidate.Request;
			const AActor* Subject = Request.Subject.Get();
			if (Candidate.Id == 0 || Request.Group != Params.Group || Request.bSuppressed
				|| !Request.AnchorComponent.IsValid() || !MatchesViewer(Request, Viewer)
				|| (!Request.Subject.IsExplicitlyNull() && !Subject) || !Candidate.bPassesScreenPolicy
				|| !FMath::IsFinite(Candidate.DistanceSquared) || Candidate.DistanceSquared < 0.0
				|| Candidate.DistanceSquared > RadiusSquared
				|| (Params.bFilterDead && Subject && IsDead(Subject))
				|| IsClaimed(Claims, Viewer, Subject, Params.ExcludeClaimedBy))
			{
				continue;
			}

			Eligible.Add(&Candidate);
		}
		// TArray 의 포인터 정렬은 인자를 역참조한다. 번호가 목록의 안정된 순서를 되살린다.
		Eligible.StableSort([&Params](const FVBIndicatorSelectionCandidate& A, const FVBIndicatorSelectionCandidate& B)
		{
			if (Params.SortOrder == EVBIndicatorSortOrder::PriorityThenRegistration)
			{
				if (A.Request.Priority != B.Request.Priority)
				{
					return A.Request.Priority > B.Request.Priority;
				}
			}
			else if (A.DistanceSquared != B.DistanceSquared)
			{
				return A.DistanceSquared < B.DistanceSquared;
			}

			return A.Id < B.Id;
		});
		const int32 Count = FMath::Min(Params.MaxCount, Eligible.Num());
		Result.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Result.Add(Eligible[Index]->Id);
		}

		return Result;
	}
}
