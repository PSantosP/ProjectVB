// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBTargetHealthIndicatorWidget.h"

#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBTargetable.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Enemy/VBBossBase.h"
#include "Enemy/VBBossEncounterSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "UI/VBEnemyHealthBarWidget.h"
#include "UI/VBWorldIndicatorProjection.h"
#include "VB/VB.h"

void UVBTargetHealthIndicatorWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	TargetBar = BarClass ? CreateWidget<UVBEnemyHealthBarWidget>(this, BarClass) : nullptr;
	if (!TargetBar)
	{
		VB_LOG(Warning, "락온 대상 체력 바: BarClass 미지정(WBP_TargetHealthIndicator 클래스 기본값) - 바가 뜨지 않는다");
		return;
	}

	BarSlot->AddChild(TargetBar);
}

void UVBTargetHealthIndicatorWidget::UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot)
{
	AActor* Target = View.PC && TargetBar ? PickTarget(*View.PC) : nullptr;
	const IVBTargetable* Targetable = Cast<IVBTargetable>(Target);
	if (!Targetable)
	{
		Hide();
		return;
	}

	const FVector Anchor = Targetable->GetTargetMarkerLocation() + FVector(0.0f, 0.0f, HeightAboveMarker);
	const FVBIndicatorPlacement Placement = VBWorldIndicatorProjection::Place(View, Anchor, EVBIndicatorScreenPolicy::InsideOnly, 0.0f);
	if (!Placement.bVisible)
	{
		Hide();
		return;
	}
	// 대상이 바뀔 때만 묶어 본다. ASC 가 없는 대상(소품, 허수아비)은 그 결과를 기억해 매 프레임 다시 찾지 않는다.
	//   해체, 재구성으로 자식이 풀렸으면 같은 대상이라도 다시 묶는다.
	if (Target != ShownTarget.Get())
	{
		ShownTarget = Target;
		bShownTargetHasBar = TargetBar->ShowEnemy(Target);
	}
	else if (bShownTargetHasBar && !TargetBar->GetBoundASC())
	{
		bShownTargetHasBar = TargetBar->ShowEnemy(Target);
	}

	if (!bShownTargetHasBar)
	{
		if (GetVisibility() != ESlateVisibility::Collapsed)
		{
			SetVisibility(ESlateVisibility::Collapsed);
		}

		return;
	}

	IndicatorSlot.SetPosition(Placement.Position);
	if (GetVisibility() == ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

AActor* UVBTargetHealthIndicatorWidget::PickTarget(const APlayerController& PC) const
{
	const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(PC.GetPawn());
	AActor* Target = Lock ? Lock->GetLockedTarget() : nullptr;
	if (!Target || UVBAbilitySystemStatics::IsActorDead(Target))
	{
		return nullptr;
	}
	// 보스 바가 보여 주는 보스는 여기서 안 그린다 - 두 바가 같은 선택(UVBBossEncounterSubsystem::GetEngagedBoss)을 본다.
	UVBBossEncounterSubsystem* Encounters = GetWorld() ? GetWorld()->GetSubsystem<UVBBossEncounterSubsystem>() : nullptr;
	if (Encounters && Target == Encounters->GetEngagedBoss())
	{
		return nullptr;
	}

	return Target;
}

void UVBTargetHealthIndicatorWidget::Hide()
{
	if (TargetBar && (ShownTarget.IsValid() || TargetBar->GetBoundASC()))
	{
		TargetBar->Clear();
	}

	ShownTarget.Reset();
	bShownTargetHasBar = false;
	if (GetVisibility() != ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UVBTargetHealthIndicatorWidget::NativeDestruct()
{
	// 자식 바는 자기 해체에서 놓는다. 여기는 보여 준 대상 기억만 지워 재구성 뒤 다시 묶게 한다.
	ShownTarget.Reset();
	bShownTargetHasBar = false;
	Super::NativeDestruct();
}
