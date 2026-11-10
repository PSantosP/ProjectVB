// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Quest/VBObjectiveMarkerWidget.h"

#include "Components/TextBlock.h"

void UVBObjectiveMarkerWidget::SetMarkerState(bool bOffscreen, float ArrowAngleDegrees, int32 Meters)
{
	if (Arrow)
	{
		const ESlateVisibility Wanted = bOffscreen ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed;
		if (Arrow->GetVisibility() != Wanted)
		{
			Arrow->SetVisibility(Wanted);
		}

		if (bOffscreen)
		{
			Arrow->SetRenderTransformAngle(ArrowAngleDegrees);
		}
	}

	if (TxtDistance && Meters != ShownMeters)
	{
		ShownMeters = Meters;
		TxtDistance->SetVisibility(Meters < 0 ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		if (Meters >= 0)
		{
			TxtDistance->SetText(FText::Format(DistanceFormat, Meters));
		}
	}
}
