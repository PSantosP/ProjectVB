// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "Settings/VBOptionCatalog.h"
#include "Settings/VBOptionsDraft.h"
#include "UI/VBUIMessagingSubsystem.h"
#include "VBOptionsScreenWidget.generated.h"

class UButton;
class UInputAction;
class UPanelWidget;
class UTextBlock;
class UVBListRowWidget;
class UVBOptionRowWidget;

/**
 * 옵션 화면(UI.Screen.Options). 고른 탭의 항목 목록(VBOptionCatalog)을 줄 위젯으로 그린다.
 * 줄은 초안만 고치고, 적용이 UVBOptionsSubsystem::Commit 에 전달한다.
 */
UCLASS(Abstract)
class VB_API UVBOptionsScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBOptionsScreenWidget();

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	// 비활성화 때 거둔다. Cancelled 는 영상 미리보기를 바로 되돌린다.
	virtual void NativeOnDeactivated() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual bool NativeOnHandleBackAction() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBListRowWidget> TabWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBOptionRowWidget> RowWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText DiscardPrompt;

	// {Seconds} = 되돌리기까지 남은 초.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText KeepVideoFormat;

	// 패드로 탭을 넘기는 입력(LB/RB).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TObjectPtr<UInputAction> PreviousTabInputAction;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TObjectPtr<UInputAction> NextTabInputAction;

	// UMG 이름 바인딩(WBP 위젯 이름 == 필드명).
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> TabBox;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> EntryBox;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnApply;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnReset;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBConfirmationScreensTestAccess;
#endif
	// 묻기는 공용 대화 상자가 맡고, 이 화면은 질문 하나의 종류만 든다.
	enum class EConfirm : uint8 { None, Discard, KeepVideo };

	void SelectTab(int32 TabIndex);
	// 보이는 탭을 넘기고 새 첫 줄에 초점을 준다. 묻는 동안 입력은 무시한다.
	void StepTab(int32 Step);
	// 목록 함수에 줄 바깥 사실. 켤 때, 탭을 고를 때마다 새로 만든다.
	FVBOptionCatalogContext MakeCatalogContext() const;
	// 줄이 없는 탭은 숨긴다. 숨은 탭을 보고 있었으면 첫 탭으로.
	void RefreshTabVisibility();
	void RebuildRows();
	void RefreshRows();
	void RefreshButtons();
	void HandleRowValueChanged(int32 RowIndex, float Value);
	void HandleRowAction(int32 RowIndex);
	// 탭, 줄, 적용, 초기화 입구를 모두 막는다(모달 아래 코드 경로 포함).
	bool IsAsking() const;
	void AskDiscard();
	void AskKeepVideo();
	void HandleDiscardAnswer(EVBConfirmationResult Result);
	void HandleKeepVideoAnswer(EVBConfirmationResult Result);
	// 미리보기는 늘 되돌리고, 초안, 줄 갱신은 이 화면이 활성일 때만 한다.
	void RevertVideoPreview();
	void Leave();
	class UVBOptionsSubsystem* GetOptions() const;

	UFUNCTION() void HandleApply();
	UFUNCTION() void HandleReset();
	void HandleBack();

	EVBOptionTab CurrentTab = EVBOptionTab::Display;
	TArray<FVBOptionEntry> Entries;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBOptionRowWidget>> Rows;

	FVBOptionsDraft Draft;
	FVBOptionsDraft Committed;
	EConfirm Confirm = EConfirm::None;
	FVBConfirmationRequester ConfirmAsk;
};
