// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "VBDialogueScreenWidget.generated.h"

class UButton;
class UPanelWidget;
class UTextBlock;
class UVBListRowWidget;
class UVBDialogueComponent;

/**
 * 대화 화면. 여닫기는 대화 컴포넌트가 관리하고, 이 화면은 OnViewChanged 만 구독해 그린다(버튼은 요청만).
 * 뒤로 가기는 떠나기 요청이다. 서버가 끝을 보내면 컴포넌트가 내린다.
 */
UCLASS(Abstract)
class VB_API UVBDialogueScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBDialogueScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual bool NativeOnHandleBackAction() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	// 선택지 한 줄의 WBP. 비면 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBListRowWidget> ChoiceWidgetClass;

	// 선택지 없이 다음 대사가 있을 때 버튼 글.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText ContinueText;

	// 마지막 대사이거나 고를 수 있는 선택지가 없을 때 버튼 글.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText EndText;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명).
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtSpeaker;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtLine;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> ChoiceBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnContinue;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtContinue;

private:
	void Refresh();
	void HandleChoiceClicked(int32 ChoiceIndex);

	UFUNCTION()
	void HandleContinueClicked();

	TWeakObjectPtr<UVBDialogueComponent> DialogueComponent;
	FDelegateHandle ViewChangedHandle;
};
