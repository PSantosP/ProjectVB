// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UI/VBScreenWidget.h"
#include "UI/VBUIMessagingSubsystem.h"
#include "VBConfirmationDialogWidget.generated.h"

class UTextBlock;

/** 재사용하는 로컬 FIFO 표시. WBP 에 전체 화면 Visible 포인터 가림막이 있어야 한다.
 * 배경 클릭은 무시한다. handle 이 낡은 답을 거르고 guard 가 연타를 막는다.
 */
UCLASS(Abstract)
class VB_API UVBConfirmationDialogWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBConfirmationDialogWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	// layout 교체로 정상 비활성화를 건너뛰어도 한 번은 알린다.
	virtual void NativeDestruct() override;
	virtual bool NativeOnHandleBackAction() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtHeader;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtBody;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnConfirm;

	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UButton> BtnDecline;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtConfirmLabel;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtDeclineLabel;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Confirmation")
	FText DefaultConfirmLabel;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Confirmation")
	FText DefaultDeclineLabel;

	// 새 handle 마다 거는 실시간 guard. 본문만 바뀌면 유지한다. 0 = 끔.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI|Confirmation", meta=(ClampMin="0.0", Units="s"))
	float NewRequestInputGuardSeconds = 0.2f;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBUIMessagingTestAccess;
#endif

	void Refresh();
	void ReleaseOwner();
	void SendAnswer(EVBConfirmationResult Result);

	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleDeclineClicked();

	// 소유 플레이어가 바뀌어도 활성화 때 구독한 Messaging 에서 해제한다.
	TWeakObjectPtr<UVBUIMessagingSubsystem> Messaging;
	FDelegateHandle PresentedChangedHandle;
	FVBConfirmationHandle ShownHandle;
	bool bFocusDeclineShown = false;
	double ShownAtSeconds = 0.0;
};
