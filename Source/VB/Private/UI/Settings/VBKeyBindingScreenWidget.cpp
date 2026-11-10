// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Settings/VBKeyBindingScreenWidget.h"

#include "CommonInputSubsystem.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Input/VBInputUserSettings.h"
#include "UI/VBInputKeyText.h"
#include "UI/VBListRowWidget.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBKeyBindingScreen"

namespace
{
	/**
	 * 키를 기다리는 동안 Slate 입력을 먼저 보고 모두 삼킨다. 키는 뗄 때 전달한다.
	 * 누름에서 정하면 그 키의 뗌이 화면 뒤로 흘러 버튼을 누른다.
	 */
	class FVBKeyCaptureProcessor : public IInputProcessor
	{
	public:
		explicit FVBKeyCaptureProcessor(UVBKeyBindingScreenWidget& InScreen)
			: Screen(&InScreen)
		{
		}

		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
		{
		}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			return true;
		}

		virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			Capture(InKeyEvent.GetKey());
			return true;
		}

		virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override
		{
			return true;
		}

		virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			return true;
		}

		virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			Capture(MouseEvent.GetEffectingButton());
			return true;
		}

		virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
		{
			return true;
		}

		// 휠은 굴림으로 온다. 대상 바꾸기가 휠이라 키로 받는다.
		virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent,
			const FPointerEvent* InGestureEvent) override
		{
			if (InWheelEvent.GetWheelDelta() != 0.f)
			{
				Capture(InWheelEvent.GetWheelDelta() > 0.f ? EKeys::MouseScrollUp : EKeys::MouseScrollDown);
			}

			return true;
		}

		virtual const TCHAR* GetDebugName() const override
		{
			return TEXT("VBKeyCapture");
		}

	private:
		void Capture(const FKey& Key)
		{
			if (UVBKeyBindingScreenWidget* Target = Screen.Get())
			{
				Target->HandleCapturedKey(Key);
			}
		}

		TWeakObjectPtr<UVBKeyBindingScreenWidget> Screen;
	};
}

UVBKeyBindingScreenWidget::UVBKeyBindingScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 뒤로 가기를 받아 기다림, 확인을 먼저 거두고, 고르는 중이면 화면을 닫는다(NativeOnHandleBackAction).
	bIsBackHandler = true;
	bRequireCloseButton = true;
	InputMode = EVBScreenInputMode::Menu;
	WaitingKeyboardText = LOCTEXT("WaitingKeyboard", "새 키를 누른다. Esc 는 그대로 둔다.");
	WaitingGamepadText = LOCTEXT("WaitingGamepad", "새 버튼을 누른다. 지금 버튼을 누르면 그대로 둔다.");
	CannotBindFormat = LOCTEXT("CannotBind", "{Key} 는 여기에 쓸 수 없다.");
	ConflictFormat = LOCTEXT("Conflict", "{Key} 는 «{Actions}» 에 쓰는 키다. 바꾸면 그쪽은 키가 없어진다. 바꿀까?");
	ReboundFormat = LOCTEXT("Rebound", "{Action} = {Key}");
	RebindFailedText = LOCTEXT("RebindFailed", "바꾸지 못했다.");
	ResetDoneText = LOCTEXT("ResetDone", "기본값으로 되돌렸다.");
	UnboundKeyText = LOCTEXT("Unbound", "없음");
	NoSettingsText = LOCTEXT("NoSettings", "키 설정을 읽지 못했다.");
}

void UVBKeyBindingScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// 필수 묶음들은 WBP 가 컴파일됐으면 비어 있지 않다.
	BtnKeyboardTab->OnClicked.AddUniqueDynamic(this, &UVBKeyBindingScreenWidget::HandleKeyboardTabClicked);
	BtnGamepadTab->OnClicked.AddUniqueDynamic(this, &UVBKeyBindingScreenWidget::HandleGamepadTabClicked);
	if (BtnReset)
	{
		BtnReset->OnClicked.AddUniqueDynamic(this, &UVBKeyBindingScreenWidget::HandleResetClicked);
	}
}

void UVBKeyBindingScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	const UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(GetOwningLocalPlayer());
	const bool bUsingGamepad = CommonInput && CommonInput->GetCurrentInputType() == ECommonInputType::Gamepad;
	ShowDevice(bUsingGamepad ? EVBKeyDevice::Gamepad : EVBKeyDevice::KeyboardMouse);
}

void UVBKeyBindingScreenWidget::NativeOnDeactivated()
{
	WithdrawConflict();
	StopCapture();
	State = EState::Browsing;
	Super::NativeOnDeactivated();
}

void UVBKeyBindingScreenWidget::NativeDestruct()
{
	// 전처리기가 남으면 모든 입력을 삼키므로 파괴 때도 뗀다.
	StopCapture();
	Super::NativeDestruct();
}

bool UVBKeyBindingScreenWidget::NativeOnHandleBackAction()
{
	if (State != EState::Browsing)
	{
		WithdrawConflict();
		StopCapture();
		EnterBrowsing(FText::GetEmpty());
		return true;
	}

	return Super::NativeOnHandleBackAction();
}

UWidget* UVBKeyBindingScreenWidget::NativeGetDesiredFocusTarget() const
{
	// 대화 상자가 닫히면 고치던 줄로 초점을 돌린다. 대화 상자 초점은 대화 상자가 맡는다.
	if (EditingRow != INDEX_NONE && EntryBox->GetChildrenCount() > EditingRow)
	{
		if (const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(EditingRow)))
		{
			return Row->GetButton();
		}
	}

	if (EntryBox->GetChildrenCount() > 0)
	{
		if (const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(0)))
		{
			return Row->GetButton();
		}
	}

	return BtnClose;
}

UVBInputUserSettings* UVBKeyBindingScreenWidget::GetSettings() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer
		? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	return Input ? Input->GetUserSettings<UVBInputUserSettings>() : nullptr;
}

void UVBKeyBindingScreenWidget::ShowDevice(EVBKeyDevice Device)
{
	// 장치를 바꾸기 전에 거둬 낡은 결과가 다른 탭에 적용되지 않게 한다.
	WithdrawConflict();
	StopCapture();
	ShownDevice = Device;
	BtnKeyboardTab->SetIsEnabled(Device != EVBKeyDevice::KeyboardMouse);
	BtnGamepadTab->SetIsEnabled(Device != EVBKeyDevice::Gamepad);
	EditingRow = INDEX_NONE;
	EnterBrowsing(FText::GetEmpty());
}

void UVBKeyBindingScreenWidget::Rebuild()
{
	EntryBox->ClearChildren();
	Rows.Reset();

	const UVBInputUserSettings* Settings = GetSettings();
	if (!Settings)
	{
		VB_LOG(Warning, "키 다시 지정 화면: 사용자 설정이 UVBInputUserSettings 가 아니다 - DefaultInput.ini 의 UserSettingsClass 를 본다");
		TxtStatus->SetText(NoSettingsText);
		return;
	}

	Rows = Settings->GetRows(ShownDevice);
	VBKeyBinding::OrderByCategory(Rows);
	if (Rows.Num() > 0 && !EntryWidgetClass)
	{
		VB_LOG(Warning, "키 다시 지정 화면: EntryWidgetClass 미지정 - 목록을 그릴 수 없다(WBP 클래스 기본값)");
		return;
	}

	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		const FVBKeyBindingRow& Row = Rows[RowIndex];
		UVBListRowWidget* Entry = CreateWidget<UVBListRowWidget>(this, EntryWidgetClass);
		Entry->Setup(RowIndex, RowName(Row.MappingName), KeyText(Row.Key), /*bEnabled=*/true);
		Entry->OnRowClicked.BindUObject(this, &UVBKeyBindingScreenWidget::SelectRow);
		EntryBox->AddChild(Entry);
	}
}

void UVBKeyBindingScreenWidget::SelectRow(int32 RowIndex)
{
	if (State != EState::Browsing || !Rows.IsValidIndex(RowIndex))
	{
		return;
	}

	EditingRow = RowIndex;
	State = EState::Waiting;
	TxtStatus->SetText(ShownDevice == EVBKeyDevice::Gamepad ? WaitingGamepadText : WaitingKeyboardText);
	StartCapture();
}

void UVBKeyBindingScreenWidget::HandleCapturedKey(const FKey& CapturedKey)
{
	if (State != EState::Waiting || !Rows.IsValidIndex(EditingRow))
	{
		return;
	}

	StopCapture();
	// 패드 트리거의 버튼 키와 축 키를 같은 키로 모은다.
	const FKey Key = VBKeyBinding::NormalizeKey(CapturedKey);
	const FVBKeyBindingRow& Row = Rows[EditingRow];

	// 키보드의 취소(Esc 는 지정할 수 없는 키다).
	if (Key == EKeys::Escape || Key == Row.Key)
	{
		EnterBrowsing(FText::GetEmpty());
		return;
	}

	if (!VBKeyBinding::CanBind(Key, ShownDevice))
	{
		FFormatNamedArguments Args;
		Args.Add(TEXT("Key"), KeyText(Key));
		EnterBrowsing(FText::Format(CannotBindFormat, Args));
		return;
	}

	const TArray<FName> Conflicts = VBKeyBinding::FindConflicts(Rows, Row.MappingName, ShownDevice, Key);
	if (Conflicts.Num() == 0)
	{
		ApplyRebind(Row.MappingName, Key);
		return;
	}

	TArray<FText> ConflictNames;
	for (const FName Conflict : Conflicts)
	{
		ConflictNames.Add(RowName(Conflict));
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("Key"), KeyText(Key));
	Args.Add(TEXT("Actions"), FText::Join(LOCTEXT("ConflictJoin", ", "), ConflictNames));
	FVBConfirmationRequest Request;
	Request.Body = FText::Format(ConflictFormat, Args);
	// 거절이 안전한 기본값이다.
	Request.bFocusDecline = true;
	// 공용 대화 상자가 묻는 동안 키 받기 안내를 지운다.
	TxtStatus->SetText(FText::GetEmpty());
	// Ask 전에 키 받기를 멈췄다. 대화 상자 확인 키가 새 바인딩이 되면 안 된다.
	PendingMapping = Row.MappingName;
	PendingDevice = ShownDevice;
	PendingKey = Key;
	State = EState::Confirming;
	if (!ConflictAsk.Ask(this, Request,
		FVBConfirmationResultDelegate::CreateUObject(this, &UVBKeyBindingScreenWidget::HandleConflictAnswer)))
	{
		// 표시가 실패하면 rebind 하지 않는다. 동기 Failed 가 이미 고르기 상태로 돌렸을 수 있다.
		if (State == EState::Confirming)
		{
			EnterBrowsing(FText::GetEmpty());
		}
	}
}

void UVBKeyBindingScreenWidget::WithdrawConflict()
{
	if (State == EState::Confirming)
	{
		State = EState::Browsing;
		PendingKey = FKey();
	}

	ConflictAsk.Cancel(this);
}

void UVBKeyBindingScreenWidget::HandleConflictAnswer(EVBConfirmationResult Result)
{
	// 거둘 때 상태를 먼저 바꿔 늦은 결과가 옛 키를 적용하지 못한다.
	if (State != EState::Confirming)
	{
		return;
	}

	const FName Mapping = PendingMapping;
	const FKey NewKey = PendingKey;
	// 원래 매핑 이름이 원래 장치 탭에 아직 있을 때만 확정한다.
	const bool bSameDevice = ShownDevice == PendingDevice;
	const bool bRowStillThere = Rows.ContainsByPredicate([Mapping](const FVBKeyBindingRow& Row) { return Row.MappingName == Mapping; });
	if (Result == EVBConfirmationResult::Confirmed && bSameDevice && bRowStillThere)
	{
		// RebindKey 가 겹침 정리와 입력 검증을 그대로 한다.
		ApplyRebind(Mapping, NewKey);
		return;
	}

	EnterBrowsing(FText::GetEmpty());
}

void UVBKeyBindingScreenWidget::ApplyRebind(FName MappingName, const FKey& NewKey)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (ApplyRebindForTests)
	{
		ApplyRebindForTests(MappingName, ShownDevice, NewKey);
		EnterBrowsing(FText::GetEmpty());
		return;
	}
#endif
	UVBInputUserSettings* Settings = GetSettings();
	TArray<FName> Cleared;
	if (!Settings || !Settings->RebindKey(MappingName, ShownDevice, NewKey, Cleared))
	{
		// RebindKey 가 실패 까닭을 로그에 남긴다. 화면에는 바뀌지 않았다는 것만 보인다.
		EnterBrowsing(Settings ? RebindFailedText : NoSettingsText);
		return;
	}

	SaveSettings();
	FFormatNamedArguments Args;
	Args.Add(TEXT("Action"), RowName(MappingName));
	Args.Add(TEXT("Key"), KeyText(NewKey));
	EnterBrowsing(FText::Format(ReboundFormat, Args));
}

void UVBKeyBindingScreenWidget::EnterBrowsing(const FText& Status)
{
	State = EState::Browsing;
	PendingKey = FKey();
	TxtStatus->SetText(Status);
	// 표시와 겹침 판정이 같은 설정을 보게 매번 다시 짓고, 초점을 고르던 줄로 되돌린다.
	Rebuild();
	RequestRefreshFocus();
}

void UVBKeyBindingScreenWidget::StartCapture()
{
	if (!CaptureProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		CaptureProcessor = MakeShared<FVBKeyCaptureProcessor>(*this);
		// 맨 앞(0번)이라야 CommonUI 전처리기가 확인, 뒤로 가기를 먼저 먹지 않는다.
		FSlateApplication::Get().RegisterInputPreProcessor(CaptureProcessor, 0);
	}
}

void UVBKeyBindingScreenWidget::StopCapture()
{
	if (CaptureProcessor.IsValid())
	{
		// 전처리기 콜백 안에서 불려도 된다(Slate 가 제거를 미룬다).
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(CaptureProcessor);
		}

		CaptureProcessor.Reset();
	}
}

void UVBKeyBindingScreenWidget::SaveSettings()
{
	// 키 배치는 진행 데이터가 아닌 설정이라 엔진 사용자 설정 저장이 관리한다.
	if (UVBInputUserSettings* Settings = GetSettings())
	{
		Settings->AsyncSaveSettings();
	}
}

FText UVBKeyBindingScreenWidget::KeyText(const FKey& Key) const
{
	return Key.IsValid() ? VBInputKeyText::ForKey(Key) : UnboundKeyText;
}

FText UVBKeyBindingScreenWidget::RowName(FName MappingName) const
{
	for (const FVBKeyBindingRow& Row : Rows)
	{
		if (Row.MappingName == MappingName)
		{
			// 표시 이름이 비면 줄 이름이라도 보인다.
			return Row.DisplayName.IsEmpty() ? FText::FromName(MappingName) : Row.DisplayName;
		}
	}

	return FText::FromName(MappingName);
}

void UVBKeyBindingScreenWidget::HandleKeyboardTabClicked()
{
	ShowDevice(EVBKeyDevice::KeyboardMouse);
}

void UVBKeyBindingScreenWidget::HandleGamepadTabClicked()
{
	ShowDevice(EVBKeyDevice::Gamepad);
}

void UVBKeyBindingScreenWidget::HandleResetClicked()
{
	if (State != EState::Browsing)
	{
		return;
	}

	UVBInputUserSettings* Settings = GetSettings();
	if (!Settings)
	{
		EnterBrowsing(NoSettingsText);
		return;
	}
	// 보이는 탭의 장치만 되돌린다.
	Settings->ResetDeviceToDefault(ShownDevice);
	SaveSettings();
	EditingRow = INDEX_NONE;
	EnterBrowsing(ResetDoneText);
}

void UVBKeyBindingScreenWidget::NativeOnCloseClicked()
{
	WithdrawConflict();
	StopCapture();
	DeactivateWidget();
}

#undef LOCTEXT_NAMESPACE
