// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBOffscreenThreatWidget.h"

#include "AbilitySystem/VBThreatTrackerSubsystem.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Engine/World.h"

void UVBOffscreenThreatWidget::UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot)
{
	UWorld* World = GetWorld();
	UVBThreatTrackerSubsystem* Tracker = World ? World->GetSubsystem<UVBThreatTrackerSubsystem>() : nullptr;
	if (!View.PC || !Tracker)
	{
		Hide();
		return;
	}

	// 화면 밖인 첫 예고. 코디네이터 예고 게이트가 동시 예고를 막으므로 보통 하나뿐이다 - 화살표도 하나다.
	//   자리는 가장자리(EdgeOnly).
	const FVBThreatEntry* Pick = nullptr;
	FVBIndicatorPlacement Placement;
	for (const FVBThreatEntry& Entry : Tracker->GetThreats())
	{
		if (!Entry.Enemy.IsValid())
		{
			continue;
		}

		Placement = VBWorldIndicatorProjection::Place(View, Entry.Enemy->GetActorLocation(), EVBIndicatorScreenPolicy::EdgeOnly,
			EdgeMargin);
		if (Placement.bVisible)
		{
			Pick = &Entry;
			break;
		}
	}

	if (!Pick)
	{
		Hide();
		return;
	}

	IndicatorSlot.SetPosition(Placement.Position);
	SetRenderTransformAngle(Placement.ArrowAngle);
	SetThreatColor(Pick->Color);
	if (GetVisibility() == ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UVBOffscreenThreatWidget::SetThreatColor(const FLinearColor& Color)
{
	if (Arrow)
	{
		Arrow->SetColorAndOpacity(Color);
	}
}

void UVBOffscreenThreatWidget::Hide()
{
	if (GetVisibility() != ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}
