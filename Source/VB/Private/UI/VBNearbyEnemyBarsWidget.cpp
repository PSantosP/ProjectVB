// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBNearbyEnemyBarsWidget.h"

#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBTargetable.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Enemy/VBBossBase.h"
#include "Enemy/VBBossEncounterSubsystem.h"
#include "Enemy/VBEnemyBase.h"
#include "Enemy/VBEnemyRegistrySubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/VBEnemyBarRules.h"
#include "UI/VBEnemyHealthBarWidget.h"
#include "UI/VBWorldIndicatorProjection.h"

void UVBNearbyEnemyBarsWidget::UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& /*IndicatorSlot*/)
{
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const float DeltaSeconds = LastUpdateTime < 0.0 ? 0.0f : float(Now - LastUpdateTime);
	LastUpdateTime = Now;
	if (!View.PC || !BarClass || !BarCanvas)
	{
		for (FEntry& Entry : Entries)
		{
			Release(Entry);
		}

		Entries.Reset();
		return;
	}

	if (Now >= NextSelectTime)
	{
		NextSelectTime = Now + SelectionInterval;
		Select(View);
	}
	// 제외 대상은 매 프레임 본다 - 락온을 옮긴 순간 그 적의 근처 바가 서서히 사라지는 동안 락온 바와 둘이 겹쳐 보이지 않게 바로 뺀다.
	const AActor* Locked = nullptr;
	const AActor* Boss = nullptr;
	FindExclusions(View, Locked, Boss);
	for (int32 Index = Entries.Num() - 1; Index >= 0; --Index)
	{
		FEntry& Entry = Entries[Index];
		AActor* Target = Entry.Target.Get();
		UVBEnemyHealthBarWidget* Bar = Entry.Bar.Get();
		if (!Target || !Bar || Target == Locked || Target == Boss)
		{
			Release(Entry);
			Entries.RemoveAtSwap(Index);
			continue;
		}

		if (UVBAbilitySystemStatics::IsActorDead(Target))
		{
			Entry.bLeaving = true;
		}
		// 고르기 사이에 화면 밖으로 나가면 그 프레임엔 숨긴다. 빠지는 중이었으면 바로 푼다.
		const FVBIndicatorPlacement Placement = VBWorldIndicatorProjection::Place(View, AnchorOf(*Target),
			EVBIndicatorScreenPolicy::InsideOnly, 0.0f);
		if (!Placement.bVisible)
		{
			Bar->SetVisibility(ESlateVisibility::Collapsed);
			if (Entry.bLeaving)
			{
				Release(Entry);
				Entries.RemoveAtSwap(Index);
			}

			continue;
		}

		if (UCanvasPanelSlot* BarSlot = Cast<UCanvasPanelSlot>(Bar->Slot))
		{
			BarSlot->SetPosition(Placement.Position);
		}

		const float FadeSeconds = Entry.bLeaving ? FadeOutSeconds : FadeInSeconds;
		const float Step = FadeSeconds > KINDA_SMALL_NUMBER ? DeltaSeconds / FadeSeconds : 1.0f;
		Entry.Opacity = FMath::Clamp(Entry.Opacity + (Entry.bLeaving ? -Step : Step), 0.0f, 1.0f);
		Bar->SetRenderOpacity(Entry.Opacity);
		if (Entry.bLeaving && Entry.Opacity <= 0.0f)
		{
			Release(Entry);
			Entries.RemoveAtSwap(Index);
			continue;
		}

		if (Bar->GetVisibility() == ESlateVisibility::Collapsed)
		{
			Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void UVBNearbyEnemyBarsWidget::Select(const FVBIndicatorView& View)
{
	const APawn* Pawn = View.PC->GetPawn();
	UWorld* World = GetWorld();
	UVBEnemyRegistrySubsystem* Registry = World ? World->GetSubsystem<UVBEnemyRegistrySubsystem>() : nullptr;
	if (!Pawn || !Registry)
	{
		for (FEntry& Entry : Entries)
		{
			Entry.bLeaving = true;
		}

		return;
	}

	const AActor* Locked = nullptr;
	const AActor* Boss = nullptr;
	FindExclusions(View, Locked, Boss);

	// 반경 밖은 투영하기 전에 거른다(PickNearby 도 반경을 보지만, 먼 적까지 투영할 이유가 없다).
	const FVector Origin = Pawn->GetActorLocation();
	const float RadiusSquared = FMath::Square(ShowRadius);
	TArray<FVBNearbyBarCandidate> Candidates;
	for (const TWeakObjectPtr<AVBEnemyBase>& Weak : Registry->GetEnemies())
	{
		AVBEnemyBase* Enemy = Weak.Get();
		const float DistanceSquared = Enemy ? float(FVector::DistSquared(Origin, Enemy->GetActorLocation())) : 0.0f;
		if (!Enemy || DistanceSquared > RadiusSquared)
		{
			continue;
		}

		FVBNearbyBarCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Actor = Enemy;
		Candidate.DistanceSquared = DistanceSquared;
		Candidate.bDead = UVBAbilitySystemStatics::IsActorDead(Enemy);
		Candidate.bOnScreen = VBWorldIndicatorProjection::Place(View, AnchorOf(*Enemy), EVBIndicatorScreenPolicy::InsideOnly,
			0.0f).bVisible;
	}

	const TArray<AActor*> Picked = VBEnemyBars::PickNearby(Candidates, ShowRadius, MaxBars, Locked, Boss);

	for (FEntry& Entry : Entries)
	{
		// 다시 뽑힌 바는 빠지기를 멈춘다(지금 투명도에서 다시 나타난다).
		Entry.bLeaving = !Picked.Contains(Entry.Target.Get());
	}

	for (AActor* Target : Picked)
	{
		if (Entries.ContainsByPredicate([Target](const FEntry& Entry) { return Entry.Target.Get() == Target; }))
		{
			continue;
		}

		UVBEnemyHealthBarWidget* Bar = AcquireBar();
		if (!Bar)
		{
			break;
		}

		if (!Bar->ShowEnemy(Target))
		{
			// ASC 없는 적 - 바를 쉬게 둔다.
			Bar->SetVisibility(ESlateVisibility::Collapsed);
			continue;
		}

		Bar->SetRenderOpacity(0.0f);
		FEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.Target = Target;
		Entry.Bar = Bar;
	}
}

UVBEnemyHealthBarWidget* UVBNearbyEnemyBarsWidget::AcquireBar()
{
	for (UVBEnemyHealthBarWidget* Bar : Pool)
	{
		if (Bar && !Entries.ContainsByPredicate([Bar](const FEntry& Entry) { return Entry.Bar.Get() == Bar; }))
		{
			return Bar;
		}
	}

	UVBEnemyHealthBarWidget* Bar = CreateWidget<UVBEnemyHealthBarWidget>(this, BarClass);
	UCanvasPanelSlot* BarSlot = Bar ? BarCanvas->AddChildToCanvas(Bar) : nullptr;
	if (!BarSlot)
	{
		return nullptr;
	}

	Bar->SetHealthNumberFormat(NumberFormat);
	BarSlot->SetAutoSize(false);
	BarSlot->SetSize(BarSize);
	BarSlot->SetAlignment(FVector2D(0.5f, 1.0f));
	Bar->SetVisibility(ESlateVisibility::Collapsed);
	Pool.Add(Bar);
	return Bar;
}

void UVBNearbyEnemyBarsWidget::Release(FEntry& Entry)
{
	if (UVBEnemyHealthBarWidget* Bar = Entry.Bar.Get())
	{
		Bar->Clear();
		Bar->SetRenderOpacity(0.0f);
		Bar->SetVisibility(ESlateVisibility::Collapsed);
	}

	Entry.Target.Reset();
	Entry.Bar.Reset();
}

void UVBNearbyEnemyBarsWidget::FindExclusions(const FVBIndicatorView& View, const AActor*& OutLocked, const AActor*& OutBoss) const
{
	// 제 바가 따로 있는 둘 - 락온 대상(락온 대상 바)과 싸우는 보스(보스 바). 한 적에 바 하나.
	const UVBTargetLockComponent* Lock = View.PC ? UVBTargetLockComponent::FindFor(View.PC->GetPawn()) : nullptr;
	OutLocked = Lock ? Lock->GetLockedTarget() : nullptr;
	UVBBossEncounterSubsystem* Encounters = GetWorld() ? GetWorld()->GetSubsystem<UVBBossEncounterSubsystem>() : nullptr;
	OutBoss = Encounters ? Encounters->GetEngagedBoss() : nullptr;
}

FVector UVBNearbyEnemyBarsWidget::AnchorOf(const AActor& Target) const
{
	const IVBTargetable* Targetable = Cast<IVBTargetable>(&Target);
	const FVector Base = Targetable ? Targetable->GetTargetMarkerLocation() : Target.GetActorLocation();
	return Base + FVector(0.0f, 0.0f, HeightAboveMarker);
}

void UVBNearbyEnemyBarsWidget::NativeDestruct()
{
	// 바들은 자기 해체에서 ASC 를 놓는다. 여기는 '누구를 보이던 중' 기억을 지워 재구성 뒤 처음부터 고르게 한다.
	for (FEntry& Entry : Entries)
	{
		Release(Entry);
	}

	Entries.Reset();
	LastUpdateTime = -1.0;
	NextSelectTime = 0.0;
	Super::NativeDestruct();
}
