// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UI/VBScreenWidget.h"
#include "VBDifficultyPickerWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UVBListRowWidget;

/**
 * 새 게임 난이도 고르기(UI.Screen.DifficultyPicker). 켜질 때 AVBMenuPlayerController 의 요청(슬롯)을 한 번 가져간다.
 * 확인 버튼만 새 게임을 시작한다. 화면이 재사용되므로 상태는 NativeOnActivated 에서 새로 정한다.
 */
UCLASS(Abstract)
class VB_API UVBDifficultyPickerWidget : public UVBScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	// 단계 줄 생김새. 비면 줄을 못 그린다(경고).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Difficulty")
	TSubclassOf<UVBListRowWidget> ChoiceWidgetClass;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> ChoiceBox;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnConfirm;

	// 고른 단계의 이름과 설명. 없어도 동작한다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtSelected;

private:
	void Rebuild();
	void Select(int32 Index);
	UFUNCTION() void HandleConfirm();

	TArray<FGameplayTag> Levels;
	TArray<FText> Names;
	TArray<FText> Descriptions;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBListRowWidget>> Rows;

	int32 Selected = INDEX_NONE;
	// 가져간 요청(슬롯, INDEX_NONE = 슬롯 없이). 비어 있으면 확인 버튼이 꺼진다.
	TOptional<int32> Request;
	// 두 번 누름 방지 - 시작이 거절되면 다시 풀린다.
	bool bSubmitted = false;
};
