// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBHitReactConfig.h"

#include "Animation/AnimMontage.h"
#include "Character/VBWeaponStateComponent.h"
#include "GameFramework/Controller.h"  // VBComputeHitDirection - 공격자를 폰으로 푼다
#include "GameFramework/PlayerState.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBHitReactConfig"

UAnimMontage* UVBHitReactConfig::ResolveMontage(EVBWeaponType Stance) const
{
	// 등록된 스탠스의 몽타주 우선. 스탠스가 등록됐어도 몽타주가 비어 있으면 DefaultSet 으로 폴백한다
	// (부분 할당된 엔트리가 무동작을 유발하지 않도록).
	if (const FVBHitReactSet* Set = StanceOverrides.Find(Stance))
	{
		if (Set->Montage)
		{
			return Set->Montage;
		}
	}

	return DefaultSet.Montage;
}

bool FVBKnockdownClips::Contains(const UAnimMontage* Montage) const
{
	return Montage && (Montage == Fall || Montage == DownIdle || Montage == GetUp || Montage == Launch || Montage == AirLoop
		|| Montage == Land || Montage == LaunchGetUp);
}

FVBKnockdownClips FVBKnockdownSet::OwnClips() const
{
	FVBKnockdownClips Own;
	Own.Fall = FallMontage;
	Own.DownIdle = DownIdleMontage;
	Own.GetUp = GetUpMontage;
	Own.Launch = LaunchMontage;
	Own.AirLoop = AirLoopMontage;
	Own.Land = LandMontage;
	Own.LaunchGetUp = LaunchGetUpMontage;
	return Own;
}

TArray<FVBKnockdownClips> FVBKnockdownSet::ClipsFor(bool bLaunch) const
{
	TArray<FVBKnockdownClips> Result;
	const FVBKnockdownClips Own = OwnClips();
	if (Own.CoversPath(bLaunch))
	{
		Result.Add(Own);
	}

	for (const FVBKnockdownClips& Variant : ClipVariants)
	{
		if (Variant.CoversPath(bLaunch))
		{
			Result.Add(Variant);
		}
	}

	return Result;
}

bool FVBKnockdownSet::HasPath(bool bLaunch) const
{
	return OwnClips().CoversPath(bLaunch)
		|| ClipVariants.ContainsByPredicate([bLaunch](const FVBKnockdownClips& Variant) { return Variant.CoversPath(bLaunch); });
}

void FVBKnockdownSet::FindProblems(TArray<FText>& OutProblems) const
{
	auto Check = [&OutProblems](const FVBKnockdownClips& Clips, const FText& Name)
	{
		if (Clips.IsEmpty())
		{
			return;
		}

		if (!Clips.CoversGround() && !Clips.CoversLaunch())
		{
			OutProblems.Add(FText::Format(LOCTEXT("HalfBundle",
				"넘어짐 {0} 이 땅 다운(넘어짐 · 대기 · 기상)도 띄움 다운(띄움 · 체공 · 착지 · 대기 · 기상)도 다 채우지 못했다 - 고를 때 빠진다."),
				Name));
		}

		const int32 LaunchClips = (Clips.Launch ? 1 : 0) + (Clips.AirLoop ? 1 : 0) + (Clips.Land ? 1 : 0);
		if (LaunchClips > 0 && LaunchClips < 3)
		{
			OutProblems.Add(FText::Format(LOCTEXT("HalfLaunch", "넘어짐 {0} 의 띄움 클립(띄움 · 체공 · 착지)이 일부만 있다 - 띄움 다운에서 빠진다."), Name));
		}
	};
	Check(OwnClips(), LOCTEXT("OwnBundle", "기본 칸"));
	for (int32 Index = 0; Index < ClipVariants.Num(); ++Index)
	{
		Check(ClipVariants[Index], FText::Format(LOCTEXT("VariantBundle", "묶음 {0}번"), Index));
		if (ClipVariants[Index].IsEmpty())
		{
			OutProblems.Add(FText::Format(LOCTEXT("EmptyVariant", "넘어짐 묶음 {0}번이 비었다."), Index));
		}
	}

	if (HasPath(true) && !HasPath(false))
	{
		OutProblems.Add(LOCTEXT("LaunchNoGround", "띄움 다운 묶음만 있고 땅 다운 묶음이 없다 - 넘어짐 데이터로 인정되지 않아 띄움도 안 된다."));
	}

	if (EscapeGetUpMontage && !HasPath(false))
	{
		OutProblems.Add(LOCTEXT("EscapeNoGround", "빠져나오기 기상이 있는데 땅 다운 묶음이 없다 - 엎드릴 일이 없어 쓰이지 않는다."));
	}

	if (AirHitExtendSeconds > 0.0f && MaxAirTotalSeconds <= MaxAirSeconds)
	{
		OutProblems.Add(LOCTEXT("AirExtendCapped", "AirHitExtendSeconds 가 있는데 MaxAirTotalSeconds 가 MaxAirSeconds 이하다 - 체공이 늘지 않는다."));
	}

	// 0 이하 주기는 타이머가 걸리지 않아 착지 감시와 반격이 조용히 사라진다.
	if (DownAttackWatchSeconds <= 0.0f || AirWatchSeconds <= 0.0f)
	{
		OutProblems.Add(LOCTEXT("WatchPeriod", "DownAttackWatchSeconds 와 AirWatchSeconds 는 0 보다 커야 한다 - 감시 타이머가 걸리지 않는다."));
	}
}

bool UVBHitReactConfig::IsDownReaction(const FVBKnockdownSet& Set, const FVBKnockdownClips& Clips, const UAnimMontage* Montage) const
{
	return Montage && (Clips.Contains(Montage) || Montage == Set.AirSlamMontage || Montage == Set.EscapeGetUpMontage
		|| Set.DownHitSet.Contains(Montage) || FlinchSet.Contains(Montage));
}

TArray<FText> UVBHitReactConfig::FindProblems() const
{
	TArray<FText> Problems;
	HeavySet.FindProblems(Problems);
	KnockdownSet.FindProblems(Problems);
	for (const auto& Entry : HeavyStanceOverrides)
	{
		TArray<FText> EntryProblems;
		Entry.Value.FindProblems(EntryProblems);
		for (const FText& Problem : EntryProblems)
		{
			Problems.Add(FText::Format(LOCTEXT("HeavyStanceProblem", "Heavy stance {0}: {1}"),
				FText::FromString(UEnum::GetValueAsString(Entry.Key)), Problem));
		}
	}

	for (const auto& Entry : KnockdownStanceOverrides)
	{
		TArray<FText> EntryProblems;
		Entry.Value.FindProblems(EntryProblems);
		for (const FText& Problem : EntryProblems)
		{
			Problems.Add(FText::Format(LOCTEXT("KnockdownStanceProblem", "Knockdown stance {0}: {1}"),
				FText::FromString(UEnum::GetValueAsString(Entry.Key)), Problem));
		}
	}

	return Problems;
}

const FVBKnockdownSet* UVBHitReactConfig::FindKnockdownSet(EVBWeaponType Stance) const
{
	if (const FVBKnockdownSet* Set = KnockdownStanceOverrides.Find(Stance); Set && Set->IsAuthored())
	{
		return Set;
	}

	return &KnockdownSet;
}

#if WITH_EDITOR
EDataValidationResult UVBHitReactConfig::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif

int32 VBPickHitReactIndex(int32 NumOptions, int32 Sequence, int32 LastIndex)
{
	if (NumOptions <= 1)
	{
		return 0;
	}
	// 순번을 섞어 보폭을 정한다. 보폭이 1..N-1 이라 직전 번호로는 절대 돌아오지 않는다.
	uint32 Mixed = static_cast<uint32>(Sequence) * 2654435761u;
	Mixed ^= Mixed >> 16;
	if (LastIndex < 0 || LastIndex >= NumOptions)
	{
		return static_cast<int32>(Mixed % static_cast<uint32>(NumOptions));
	}

	const int32 Step = 1 + static_cast<int32>(Mixed % static_cast<uint32>(NumOptions - 1));
	return (LastIndex + Step) % NumOptions;
}

UAnimMontage* FVBHitReactSet::ResolveByDirection(EVBHitDirection Direction, FVBHitReactPick* Pick) const
{
	UAnimMontage* Directional = nullptr;
	const TArray<TObjectPtr<UAnimMontage>>* DirectionalAlternates = nullptr;
	switch (Direction)
	{
	case EVBHitDirection::Front: Directional = MontageFront; DirectionalAlternates = &FrontAlternates; break;
	case EVBHitDirection::Back: Directional = MontageBack; DirectionalAlternates = &BackAlternates; break;
	case EVBHitDirection::Left: Directional = MontageLeft; DirectionalAlternates = &LeftAlternates; break;
	case EVBHitDirection::Right: Directional = MontageRight; DirectionalAlternates = &RightAlternates; break;
	}

	UAnimMontage* Primary = Directional ? Directional : Montage.Get();
	if (!Pick || !Primary)
	{
		return Primary;
	}

	TArray<UAnimMontage*, TInlineAllocator<8>> Options;
	Options.Add(Primary);
	for (UAnimMontage* Alternate : (Directional && DirectionalAlternates) ? *DirectionalAlternates : Alternates)
	{
		// 같은 클립을 두 칸에 넣으면 직전과 다른 번호 규칙이 같은 클립을 다시 고른다.
		if (Alternate)
		{
			Options.AddUnique(Alternate);
		}
	}

	const int32 LastIndex = Options.IndexOfByPredicate(
		[Pick](const UAnimMontage* Option) { return TObjectKey<UAnimMontage>(Option) == Pick->LastMontage; });
	UAnimMontage* Chosen = Options[VBPickHitReactIndex(Options.Num(), Pick->Sequence, LastIndex)];
	Pick->LastMontage = Chosen;
	return Chosen;
}

bool FVBHitReactSet::Contains(const UAnimMontage* InMontage) const
{
	if (!InMontage)
	{
		return false;
	}

	if (InMontage == Montage || InMontage == MontageFront || InMontage == MontageBack || InMontage == MontageLeft
		|| InMontage == MontageRight)
	{
		return true;
	}

	for (const TArray<TObjectPtr<UAnimMontage>>* List : {&Alternates, &FrontAlternates, &BackAlternates, &LeftAlternates, &RightAlternates})
	{
		if (List->Contains(InMontage))
		{
			return true;
		}
	}

	return false;
}

void FVBHitReactSet::FindProblems(TArray<FText>& OutProblems) const
{
	auto Check = [&OutProblems](const UAnimMontage* Primary, const TArray<TObjectPtr<UAnimMontage>>& Alternatives, const TCHAR* Slot)
	{
		if (!Primary && !Alternatives.IsEmpty())
		{
			OutProblems.Add(FText::Format(LOCTEXT("ReactionNoPrimary", "Reaction slot {0} has alternatives without a primary montage"),
				FText::FromString(Slot)));
		}

		if (Alternatives.Contains(nullptr))
		{
			OutProblems.Add(FText::Format(LOCTEXT("ReactionNullAlternative", "Reaction slot {0} contains an empty alternative"),
				FText::FromString(Slot)));
		}
	};
	Check(Montage, Alternates, TEXT("Default"));
	Check(MontageFront, FrontAlternates, TEXT("Front"));
	Check(MontageBack, BackAlternates, TEXT("Back"));
	Check(MontageLeft, LeftAlternates, TEXT("Left"));
	Check(MontageRight, RightAlternates, TEXT("Right"));
}

EVBWeaponType VBResolveVictimStance(const AActor* Victim)
{
	const UVBWeaponStateComponent* WeaponState = UVBWeaponStateComponent::FindFor(Victim);
	return WeaponState ? WeaponState->GetCurrentWeaponType() : EVBWeaponType::None;
}

UAnimMontage* UVBHitReactConfig::ResolveMontage(EVBWeaponType Stance, EVBHitDirection Direction, FVBHitReactPick* Pick) const
{
	// 스탠스 세트가 있고 거기서 무엇이든 나오면 그것. 세트가 통째로 비었으면 DefaultSet 으로 - 부분 할당이 무동작을 만들지 않게.
	if (const FVBHitReactSet* Set = StanceOverrides.Find(Stance))
	{
		if (UAnimMontage* FromStance = Set->ResolveByDirection(Direction, Pick))
		{
			return FromStance;
		}
	}

	return DefaultSet.ResolveByDirection(Direction, Pick);
}

const AActor* VBResolveInstigatorBody(const AActor* Instigator)
{
	// 플레이어 ASC 소유자는 PlayerState 라 위치가 원점이다 - 폰으로 풀어야 방향이 맞는다. 컨트롤러도 같은 이유로 푼다.
	if (const APlayerState* PlayerState = Cast<APlayerState>(Instigator))
	{
		return PlayerState->GetPawn();
	}

	if (const AController* Controller = Cast<AController>(Instigator))
	{
		return Controller->GetPawn();
	}

	return Instigator;
}

EVBHitDirection VBComputeHitDirection(const AActor* Victim, const AActor* InInstigator)
{
	const AActor* Instigator = VBResolveInstigatorBody(InInstigator);
	if (!Victim || !Instigator)
	{
		return EVBHitDirection::Front;
	}

	const FVector Local = Victim->GetActorTransform().InverseTransformVectorNoScale(
		Instigator->GetActorLocation() - Victim->GetActorLocation());
	if (FMath::Abs(Local.X) >= FMath::Abs(Local.Y))
	{
		return Local.X >= 0.0f ? EVBHitDirection::Front : EVBHitDirection::Back;
	}

	return Local.Y >= 0.0f ? EVBHitDirection::Right : EVBHitDirection::Left;
}

UAnimMontage* UVBHitReactConfig::ResolveStaggerMontage(EVBHitDirection Direction) const
{
	// 방향 슬롯이 비면 무방향으로 떨어진다.
	switch (Direction)
	{
		case EVBHitDirection::Front: if (StaggerSet.MontageFront) return StaggerSet.MontageFront; break;
		case EVBHitDirection::Back: if (StaggerSet.MontageBack) return StaggerSet.MontageBack; break;
		case EVBHitDirection::Left: if (StaggerSet.MontageLeft) return StaggerSet.MontageLeft; break;
		case EVBHitDirection::Right: if (StaggerSet.MontageRight) return StaggerSet.MontageRight; break;
		default: break;
	}

	return StaggerSet.Montage;
}

UAnimMontage* UVBHitReactConfig::ResolveHeavyMontage(EVBWeaponType Stance, EVBHitDirection Direction, FVBHitReactPick* Pick) const
{
	if (const FVBHitReactSet* Set = HeavyStanceOverrides.Find(Stance))
	{
		if (UAnimMontage* FromStance = Set->ResolveByDirection(Direction, Pick))
		{
			return FromStance;
		}
	}

	return HeavySet.ResolveByDirection(Direction, Pick);
}

UAnimMontage* UVBHitReactConfig::ResolveStaggerMontage() const
{
	// 스탠스와 무관한 단일 세트. 히트리액트로 폴백하지 않는다 - 미저작이 가벼운 플린치로 숨는다.
	return StaggerSet.Montage;
}

#undef LOCTEXT_NAMESPACE
