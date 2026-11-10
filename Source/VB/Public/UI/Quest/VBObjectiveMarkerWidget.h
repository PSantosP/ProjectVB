// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VBObjectiveMarkerWidget.generated.h"

class UTextBlock;
class UWidget;

/** 목표 마커 한 칸(그림, 거리 글, 화면 밖 화살표). 자리와 상태는 UVBObjectiveMarkerLayerWidget 이 정한다. */
UCLASS()
class VB_API UVBObjectiveMarkerWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 화면 밖이면 화살표를 보이고 돌린다. Meters < 0 이면 거리 글을 숨긴다.
	void SetMarkerState(bool bOffscreen, float ArrowAngleDegrees, int32 Meters);

protected:
	// 화면 밖일 때만 보이는 방향 화살표.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UWidget> Arrow;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtDistance;

	// {0} = 미터(정수).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText DistanceFormat = NSLOCTEXT("VBObjectiveMarker", "Distance", "{0}m");

private:
	// 글은 바뀔 때만 다시 만든다.
	int32 ShownMeters = TNumericLimits<int32>::Min();
};
