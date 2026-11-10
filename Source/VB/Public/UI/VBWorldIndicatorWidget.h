// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/VBWorldIndicatorProjection.h"
#include "VBWorldIndicatorWidget.generated.h"

class UCanvasPanelSlot;

/**
 * 월드 표시기 한 갈래(상호작용 프롬프트, 화면 밖 위협, 목표 마커 층)의 공통 부모.
 * 무엇을 보일지는 갈래가, 언제, 어느 시야로 볼지는 표시기 층(UVBWorldIndicatorLayerWidget)이 정한다.
 */
UCLASS(Abstract)
class VB_API UVBWorldIndicatorWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 층이 매 틱 부른다(숨어 있어도). 갈래는 스스로 투영하지 않는다 - 한 프레임에 시야 하나.
	virtual void UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot)
		PURE_VIRTUAL(UVBWorldIndicatorWidget::UpdateIndicator, );
};
