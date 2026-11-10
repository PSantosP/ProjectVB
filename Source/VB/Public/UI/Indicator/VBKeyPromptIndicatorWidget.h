// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonInputTypeEnum.h"
#include "UI/Indicator/VBRegisteredIndicatorsWidget.h"
#include "VBKeyPromptIndicatorWidget.generated.h"

class UCommonInputSubsystem;
class UEnhancedInputLocalPlayerSubsystem;
class UImage;
class UInputAction;
class UTextBlock;

// 「행동 이름 + 지금 누를 키」 표시 하나(예: WBP_ExecutePrompt - 처형). 키는 지금 입력 장치와 지금 걸린 키 묶음에서 읽어, 장치가 바뀌거나 키를 다시 묶으면 고친다.
UCLASS()
class VB_API UVBKeyPromptIndicatorWidget : public UVBRegisteredIndicatorWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// 보일 행동 이름.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText ActionText;

	// 키를 읽을 입력 행동(예: IA_Execute).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TObjectPtr<const UInputAction> Action;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> ActionLabel;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyLabel;

	// 키 그림 칸. 없으면 키 이름 글자만 쓴다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgKey;

private:
	UFUNCTION()
	void RefreshKey();
	void HandleInputMethodChanged(ECommonInputType Type);

	// 구독한 소스를 기억했다가 그 소스에서 푼다 - 해체 때 다시 찾지 않는다.
	TWeakObjectPtr<UCommonInputSubsystem> InputSource;
	TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> MappingSource;
	FDelegateHandle InputMethodHandle;
};
