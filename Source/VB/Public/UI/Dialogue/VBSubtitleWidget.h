// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Settings/VBOptionTypes.h"
#include "VBSubtitleWidget.generated.h"

class UTextBlock;
class UVBSpokenLineComponent;
struct FVBSpokenLine;

/**
 * 화면 아래 자막 한 줄. 그리기만 하고, 무엇을 언제 말할지는 UVBSpokenLineComponent 가 정한다.
 * 새 줄은 지금 줄을 바로 갈아 끼우고 시간이 지나면 접는다. 입력은 받지 않는다(HitTestInvisible).
 */
UCLASS(Abstract)
class VB_API UVBSubtitleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 이 줄 컴포넌트를 구독한다(기존 바인딩은 해제, nullptr 이면 해제만). 구성 때 소유 PC 의 컴포넌트에 스스로 바인딩한다.
	void BindTo(UVBSpokenLineComponent* InLines);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명). 화자 이름은 선택이다.
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtLine;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtSpeaker;

private:
	void HandleLine(const FVBSpokenLine& Line, float Seconds);
	void HideLine();
	void ApplyTextSize(EVBSubtitleSize Size);

	TWeakObjectPtr<UVBSpokenLineComponent> BoundLines;
	FDelegateHandle LineHandle;
	FTimerHandle HideTimer;
};
