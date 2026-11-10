// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBWorldIndicatorWidget.h"
#include "VBOffscreenThreatWidget.generated.h"

class UImage;

/**
 * 예고 중인 적이 화면 밖일 때 가장자리에 뜨는 화살표. 표시기 층의 한 갈래다.
 * 텍스처는 위를 가리키게 저작한다(각도 0 = 위).
 */
UCLASS()
class VB_API UVBOffscreenThreatWidget : public UVBWorldIndicatorWidget
{
	GENERATED_BODY()

public:
	virtual void UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot) override;

	// 답의 색. 큐가 추적기에 실어 보낸 값을 그대로 쓴다.
	void SetThreatColor(const FLinearColor& Color);

protected:
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UImage> Arrow;

	// 화살표 중심이 화면 가장자리에서 떨어지는 거리(위젯 단위).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(ClampMin="0"))
	float EdgeMargin = 80.0f;

private:
	void Hide();
};
