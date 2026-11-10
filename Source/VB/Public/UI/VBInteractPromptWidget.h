// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonInputTypeEnum.h"   // ECommonInputType - 장치 바뀜 인자
#include "Styling/SlateBrush.h"
#include "UI/VBWorldIndicatorWidget.h"
#include "VBInteractPromptWidget.generated.h"

class APawn;
class APlayerController;
class UImage;
class UProgressBar;
class UTextBlock;
class UVBInputDeviceManager;
class UVBInteractableComponent;
class UVBInteractorComponent;

/**
 * 상호작용 프롬프트 한 개. 표시기 층의 한 갈래로 대상 위치에 뜬다.
 * 폰의 상호작용 초점과 입력 장치를 구성 때 묶고 해체 때 푼다(폰이 바뀌면 옮긴다).
 */
UCLASS()
class VB_API UVBInteractPromptWidget : public UVBWorldIndicatorWidget
{
	GENERATED_BODY()

	friend class FVBHUDRowTestAccess;

public:
	virtual void UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot) override;

	// 행동 이름과 누를 키. 키 그림 > 키 이름 > 숨김 순으로 보인다.
	void SetPrompt(const FText& ActionText, const FText& KeyText, const TOptional<FSlateBrush>& KeyIcon);

	// 0~1. 음수면 진행 칸을 숨긴다(탭 대상이거나 아직 안 누름).
	void SetHoldProgress(float Progress);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> ActionLabel;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> KeyLabel;

	// 키 그림 칸. 없으면 키 이름 글자만 쓴다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgKey;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UProgressBar> HoldProgress;

private:
	UFUNCTION()
	void HandlePossessedPawnChanged(APawn* OldPawn, APawn* NewPawn);

	UFUNCTION()
	void HandleFocusChanged(UVBInteractableComponent* NewFocus);

	UFUNCTION()
	void HandleInputDeviceChanged(ECommonInputType NewDeviceType);

	// 옛 폰을 풀고 새 폰의 초점, 장치에 묶은 뒤 지금 값을 반영한다.
	void BindPawn(APawn* Pawn);
	// 묶음을 풀고 보여 주던 것(대상, 글, 홀드)을 비운 뒤 숨긴다.
	void Unbind();
	void RefreshKeyText();
	void Hide();

	// 구독한 소스. 해체 때 여기서 푼다.
	TWeakObjectPtr<APlayerController> PawnSource;
	TWeakObjectPtr<APawn> BoundPawn;
	TWeakObjectPtr<UVBInteractorComponent> Interactor;
	TWeakObjectPtr<UVBInputDeviceManager> DeviceManager;

	TWeakObjectPtr<UVBInteractableComponent> Target;
	FText ShownActionText;
	FText ShownKeyText;
	TOptional<FSlateBrush> ShownKeyIcon;
	ECommonInputType InputType = ECommonInputType::MouseAndKeyboard;
};
