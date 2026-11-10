// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBCinematicSkillDefinition.h"

#include "Animation/AnimMontage.h"
#include "Animation/VBANS_AttackTraceWindow.h"
#include "Data/VBAttackConfig.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBCinematicSkillDefinition"

#if WITH_EDITOR
EDataValidationResult UVBCinematicSkillDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (RequiredWeapon == EVBWeaponType::None)
	{
		Context.AddError(LOCTEXT("NoWeapon", "RequiredWeapon is None - no stance could cast this skill."));
		Result = EDataValidationResult::Invalid;
	}

	if (MaxTargets <= 0 || RosterRadiusCm <= 0.0f)
	{
		Context.AddWarning(LOCTEXT("EmptyRoster", "MaxTargets or RosterRadiusCm is 0 - the show plays but holds and hits nobody."));
	}

	return Result;
}
#endif

namespace VBCinematicSkillRules
{
	TArray<int32> SelectRoster(TConstArrayView<FVBCinematicRosterCandidate> Candidates, float RadiusCm, float HalfAngleDeg,
		int32 MaxTargets)
	{
		TArray<int32> Chosen;
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			const FVBCinematicRosterCandidate& C = Candidates[i];
			if (C.bAlive && C.bGrounded && !C.bExcludedByTag && !C.bBodyBusy
				&& C.DistanceCm <= RadiusCm && C.AngleFromFacingDeg <= HalfAngleDeg)
			{
				Chosen.Add(i);
			}
		}
		// 가까운 순 - 거리가 같으면 입력 순서를 지켜 같은 장면이 같은 명단을 낸다.
		Chosen.StableSort([&Candidates](int32 A, int32 B) { return Candidates[A].DistanceCm < Candidates[B].DistanceCm; });
		if (Chosen.Num() > FMath::Max(0, MaxTargets))
		{
			Chosen.SetNum(FMath::Max(0, MaxTargets));
		}

		return Chosen;
	}

	bool FindFinalWindow(const UAnimMontage& Montage, float& OutStart, float& OutEnd)
	{
		int32 Windows = 0;
		for (const FAnimNotifyEvent& Event : Montage.Notifies)
		{
			if (!Cast<UVBANS_AttackTraceWindow>(Event.NotifyStateClass))
			{
				continue;
			}

			++Windows;
			OutStart = Event.GetTriggerTime();
			OutEnd = Event.GetEndTriggerTime();
		}
		// 재생되는 시퀀스의 판정 창은 이 검사가 시각을 잡을 수 없으니, 하나라도 있으면 스킬을 거절한다.
		int32 SourceWindows = 0;
		for (const FSlotAnimationTrack& Track : Montage.SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Track.AnimTrack.AnimSegments)
			{
				const UAnimSequenceBase* Source = Segment.GetAnimReference();
				if (!Source || Source == &Montage)
				{
					continue;
				}

				for (const FAnimNotifyEvent& Event : Source->Notifies)
				{
					SourceWindows += Cast<UVBANS_AttackTraceWindow>(Event.NotifyStateClass) ? 1 : 0;
				}
			}
		}

		return Windows == 1 && SourceWindows == 0;
	}

	bool IsReady(const UVBAttackConfig* Leaf, FString* OutWhy)
	{
		const auto Fail = [OutWhy](const TCHAR* Why) { if (OutWhy) *OutWhy = Why; return false; };
		if (!Leaf)
		{
			return Fail(TEXT("no attack config"));
		}

		if (!Leaf->AttackMontage)
		{
			return Fail(TEXT("attack config has no montage loaded"));
		}
		// 스타일 타격(DeliverHit)은 창이 열리기 전에 돌아 창 필터를 건너뛴다.
		if (Leaf->StyleBehavior)
		{
			return Fail(TEXT("attack config has a StyleBehavior - its hit would bypass the final-window gate"));
		}

		float Start = 0.0f, End = 0.0f;
		if (!FindFinalWindow(*Leaf->AttackMontage, Start, End))
		{
			return Fail(TEXT("montage needs exactly one attack trace window (the final impact)"));
		}

		return true;
	}

	bool IsFinalPhase(float MontagePosition, float FinalWindowStart)
	{
		constexpr float BeginFrameSlack = 1.0f / 60.0f;
		return MontagePosition >= FinalWindowStart - BeginFrameSlack;
	}

	TArray<int32> AllowFinalHits(TConstArrayView<const AActor*> HitActors, bool bInFinalWindow,
		const TSet<TWeakObjectPtr<AActor>>& Roster, TSet<TWeakObjectPtr<AActor>>& Consumed)
	{
		TArray<int32> Allowed;
		if (!bInFinalWindow)
		{
			return Allowed;
		}

		for (int32 i = 0; i < HitActors.Num(); ++i)
		{
			const TWeakObjectPtr<AActor> Actor(const_cast<AActor*>(HitActors[i]));
			if (!Actor.IsValid() || !Roster.Contains(Actor) || Consumed.Contains(Actor))
			{
				continue;
			}

			Consumed.Add(Actor);
			Allowed.Add(i);
		}

		return Allowed;
	}
}

#undef LOCTEXT_NAMESPACE
