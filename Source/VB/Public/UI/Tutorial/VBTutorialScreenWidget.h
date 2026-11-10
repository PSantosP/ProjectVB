// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "UI/VBScreenWidget.h"
#include "Narrative/Tutorial/VBTutorialDefinition.h"
#include "VBTutorialScreenWidget.generated.h"

class UVBTutorialComponent;
class UVBListRowWidget;
class UPanelWidget;
class UImage;
class UTextBlock;
class UScrollBox;
class APlayerController;
class APlayerState;
class UButton;

/**
 * 튜토리얼 다시 보기 화면(UI.Screen.Tutorial, Modal 층). 켜질 때 PC 의 대기 ID 를 한 번 가져간다.
 * 읽기만 한다: 서버가 허용한 단계(UnlockedSteps)만 보이고, 고르는 것은 진행을 바꾸지 않는다.
 */
UCLASS(Abstract)
class VB_API UVBTutorialScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBTutorialScreenWidget();
	void SelectTutorial(FGameplayTag TutorialId);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Tutorial") TSubclassOf<UVBListRowWidget> ListRowWidgetClass;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UPanelWidget> EntryBox;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> TxtTitle;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> TxtBody;
	UPROPERTY(meta=(BindWidget)) TObjectPtr<UTextBlock> TxtStatus;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> TxtKey;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UImage> ImgKey; // 키 그림 칸. 없으면 TxtKey 글자만
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UScrollBox> BodyScroll;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> BtnRetry;

private:
	struct FReviewRow
	{
		FGameplayTag TutorialId;
		FGameplayTag StepId;
	};
	void Unbind();
	void HandleSelectionPending();
	bool IsCurrent(uint32 ExpectedGeneration, TWeakObjectPtr<APlayerController> ExpectedOwner) const;
	// 지금 PlayerState 의 튜토리얼 부품과 다르면 갈아 끼운다. 켜질 때와 PlayerState 가 정해질 때 부른다.
	void RefreshBinding(APlayerState* PlayerState);
	// 키 글 폴링(0.25초). 세대, 소유자가 바뀌었으면 멈춘다.
	bool PollKey(float DeltaSeconds, uint32 ExpectedGeneration, TWeakObjectPtr<APlayerController> ExpectedOwner);
	void Rebuild();
	void RefreshDetail();
	void RefreshKey();
	void SetStatus(const FText& Text);
	void HandleRowClicked(int32 Index);
	UFUNCTION() void HandleRetryClick();
	TWeakObjectPtr<APlayerController> BoundOwner;
	TWeakObjectPtr<UVBTutorialComponent> Tutorial;
	FDelegateHandle ChangedHandle;
	FDelegateHandle SelectionPendingHandle;
	FDelegateHandle PlayerStateSetHandle;
	FTSTicker::FDelegateHandle KeyPollHandle;
	TArray<FReviewRow> Rows;
	FGameplayTag RequestedTutorialId;
	FGameplayTag SelectedTutorialId;
	FGameplayTag SelectedStepId;
	FGameplayTag ShownTutorialId;
	FGameplayTag ShownStepId;
	// 보이는 단계의 복사본. 키 폴링이 여기서 다시 읽는다(객체를 가리킬 수 있어 UPROPERTY).
	UPROPERTY(Transient)
	FVBTutorialStep ShownStep;
	uint32 Generation = 0;
};
