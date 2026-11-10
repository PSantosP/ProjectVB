// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBConfirmationDialogWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"

#define LOCTEXT_NAMESPACE "VBConfirmationDialog"

UVBConfirmationDialogWidget::UVBConfirmationDialogWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 입력 동작은 CommonUI 가 막고, 포인터 입력은 WBP 배경이 막는다.
	bIsBackHandler = true;
	bIsModal = true;
	InputMode = EVBScreenInputMode::Menu;
	DefaultConfirmLabel = LOCTEXT("Confirm", "확인");
	DefaultDeclineLabel = LOCTEXT("Decline", "아니오");
}

void UVBConfirmationDialogWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 풀링된 인스턴스는 컨트롤을 유지한다. 새로 고칠 때마다 현재 핸들을 기억한다.
	BtnConfirm->OnClicked.AddDynamic(this, &UVBConfirmationDialogWidget::HandleConfirmClicked);
	BtnDecline->OnClicked.AddDynamic(this, &UVBConfirmationDialogWidget::HandleDeclineClicked);
}

void UVBConfirmationDialogWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	UVBUIMessagingSubsystem* Owner = UVBUIMessagingSubsystem::Get(this);
	Messaging = Owner;
	if (!Owner)
	{
		DeactivateWidget();
		return;
	}

	PresentedChangedHandle = Owner->OnPresentedChanged.AddUObject(this, &UVBConfirmationDialogWidget::Refresh);
	Owner->NotifyDialogActivated();
	Refresh();
}

void UVBConfirmationDialogWidget::NativeOnDeactivated()
{
	ReleaseOwner();
	Super::NativeOnDeactivated();
}

void UVBConfirmationDialogWidget::NativeDestruct()
{
	ReleaseOwner();
	Super::NativeDestruct();
}

void UVBConfirmationDialogWidget::ReleaseOwner()
{
	ShownHandle = FVBConfirmationHandle();
	// 알리기 전에 그 출처를 정확히 비운다. deactivate + destruct 는 여러 번 해도 같다.
	if (UVBUIMessagingSubsystem* Owner = Messaging.Get())
	{
		Owner->OnPresentedChanged.Remove(PresentedChangedHandle);
		PresentedChangedHandle.Reset();
		Messaging.Reset();
		Owner->NotifyDialogDeactivated();
	}
}

bool UVBConfirmationDialogWidget::NativeOnHandleBackAction()
{
	SendAnswer(EVBConfirmationResult::Cancelled);
	return true;
}

UWidget* UVBConfirmationDialogWidget::NativeGetDesiredFocusTarget() const
{
	return bFocusDeclineShown && BtnDecline->IsVisible() ? static_cast<UWidget*>(BtnDecline) : static_cast<UWidget*>(BtnConfirm);
}

void UVBConfirmationDialogWidget::Refresh()
{
	UVBUIMessagingSubsystem* Owner = Messaging.Get();
	FVBConfirmationHandle Handle;
	const FVBConfirmationRequest* Request = Owner ? Owner->GetPresented(Handle) : nullptr;
	if (!Request)
	{
		if (IsActivated())
		{
			DeactivateWidget();
		}

		return;
	}

	const bool bNewRequest = !(Handle == ShownHandle);
	ShownHandle = Handle;
	TxtHeader->SetText(Request->Header);
	TxtBody->SetText(Request->Body);
	if (TxtConfirmLabel)
	{
		TxtConfirmLabel->SetText(Request->ConfirmLabel.IsEmpty() ? DefaultConfirmLabel : Request->ConfirmLabel);
	}

	if (TxtDeclineLabel)
	{
		TxtDeclineLabel->SetText(Request->DeclineLabel.IsEmpty() ? DefaultDeclineLabel : Request->DeclineLabel);
	}

	BtnDecline->SetVisibility(Request->bOfferDecline ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	bFocusDeclineShown = Request->bFocusDecline;
	// 카운트다운, 본문 갱신은 패드 포커스와 클릭 가드를 유지해야 한다.
	if (bNewRequest)
	{
		ShownAtSeconds = FPlatformTime::Seconds();
		RequestRefreshFocus();
	}
}

void UVBConfirmationDialogWidget::SendAnswer(EVBConfirmationResult Result)
{
	if (FPlatformTime::Seconds() - ShownAtSeconds < NewRequestInputGuardSeconds)
	{
		return;
	}

	if (UVBUIMessagingSubsystem* Owner = Messaging.Get())
	{
		Owner->Answer(ShownHandle, Result);
	}
}

void UVBConfirmationDialogWidget::HandleConfirmClicked()
{
	SendAnswer(EVBConfirmationResult::Confirmed);
}

void UVBConfirmationDialogWidget::HandleDeclineClicked()
{
	SendAnswer(EVBConfirmationResult::Declined);
}

#undef LOCTEXT_NAMESPACE
