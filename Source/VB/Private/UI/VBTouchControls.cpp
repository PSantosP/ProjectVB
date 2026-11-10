// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBTouchControls.h"

#include "CommonInputSubsystem.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/VBInputModifiers.h"
#include "InputAction.h"
#include "UI/Slots/VBUISlotWidget.h"
#include "UI/VBHUDWidget.h"
#include "VB/VB.h"

namespace
{
	// 콘솔 변수 바뀜 알림. 숨겨진 묶음은 틱하지 않아 틱에서 읽을 수 없다.
	FSimpleMulticastDelegate& OnTouchControlsModeChanged()
	{
		static FSimpleMulticastDelegate Delegate;
		return Delegate;
	}

	// 화면 터치 조작 표시. 실행 중에 바꿔 보는 시험용 값이라 콘솔 변수에 둔다.
	TAutoConsoleVariable<int32> CVarTouchControls(
		TEXT("vb.TouchControls"), 0,
		TEXT("화면 터치 조작 표시. 0 = 마지막 입력이 터치일 때 / 1 = 늘 보임 / 2 = 늘 숨김"),
		FConsoleVariableDelegate::CreateLambda([](IConsoleVariable*) { OnTouchControlsModeChanged().Broadcast(); }),
		ECVF_Default);
}

// ---- 공통 ----

UEnhancedInputLocalPlayerSubsystem* UVBTouchInputWidget::GetInputSubsystem() const
{
	const APlayerController* PC = GetOwningPlayer();
	const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
	return LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
}

FReply UVBTouchInputWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	// 두 번째 손가락은 뒤의 위젯(시점 패드 등)으로 전달한다.
	if (ActivePointerIndex != INDEX_NONE)
	{
		return FReply::Unhandled();
	}

	if (!InputAction)
	{
		// 배치 실수 - 조용히 무시하면 원인을 못 찾는다.
		VB_LOG(Warning, "터치 조작 %s 에 InputAction 이 없다", *GetName());
		return FReply::Unhandled();
	}

	ActivePointerIndex = InGestureEvent.GetPointerIndex();
	OnTouchBegin(InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition()));
	// 시작 중에 놓였으면(화면 열기 -> Menu 전환) 캡처하지 않는다.
	if (!HasActiveTouch())
	{
		return FReply::Handled();
	}
	// 이 손가락만 캡처해 위젯 밖으로 나가도 이동, 떼기가 여기로 온다.
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UVBTouchInputWidget::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (InGestureEvent.GetPointerIndex() != ActivePointerIndex)
	{
		return FReply::Unhandled();
	}

	OnTouchMove(InGeometry.AbsoluteToLocal(InGestureEvent.GetScreenSpacePosition()), InGestureEvent.GetCursorDelta());
	return FReply::Handled();
}

FReply UVBTouchInputWidget::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (InGestureEvent.GetPointerIndex() != ActivePointerIndex)
	{
		return FReply::Unhandled();
	}

	EndActiveTouch();
	return FReply::Handled().ReleaseMouseCapture();
}

void UVBTouchInputWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	// 떼기 이벤트 없이 캡처가 풀릴 수 있다. 그대로 두면 눌린 채 남는다.
	Super::NativeOnMouseCaptureLost(CaptureLostEvent);
	if (CaptureLostEvent.PointerIndex == ActivePointerIndex)
	{
		EndActiveTouch();
	}
}

void UVBTouchInputWidget::NativeDestruct()
{
	EndActiveTouch();
	Super::NativeDestruct();
}

void UVBTouchInputWidget::EndActiveTouch()
{
	if (ActivePointerIndex == INDEX_NONE)
	{
		return;
	}

	ActivePointerIndex = INDEX_NONE;
	OnTouchEnd();
	// 파생이 잊어도 여기서 주입을 멈춘다.
	StopInjection();
}

void UVBTouchInputWidget::StartInjection(const FInputActionValue& Value)
{
	if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(); Input && InputAction)
	{
		Input->StartContinuousInputInjectionForAction(InputAction, Value, {}, {});
		bInjecting = true;
	}
}

void UVBTouchInputWidget::UpdateInjection(const FInputActionValue& Value)
{
	if (!bInjecting)
	{
		StartInjection(Value);
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(); Input && InputAction)
	{
		Input->UpdateValueOfContinuousInputInjectionForAction(InputAction, Value);
	}
}

void UVBTouchInputWidget::StopInjection()
{
	if (!bInjecting)
	{
		return;
	}

	bInjecting = false;
	if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(); Input && InputAction)
	{
		Input->StopContinuousInputInjectionForAction(InputAction);
	}
}

void UVBTouchInputWidget::InjectOnce(const FInputActionValue& Value, const TArray<UInputModifier*>& Modifiers)
{
	if (UEnhancedInputLocalPlayerSubsystem* Input = GetInputSubsystem(); Input && InputAction)
	{
		Input->InjectInputForAction(InputAction, Value, Modifiers, {});
	}
}

// ---- 스틱 ----

FVector2D UVBTouchStick::ComputeValue(const FVector2D& LocalPosition) const
{
	const FVector2D Center = GetCachedGeometry().GetLocalSize() * 0.5f;
	// 화면 좌표는 아래가 +Y 라 뒤집는다.
	FVector2D Value((LocalPosition.X - Center.X) / Radius, -(LocalPosition.Y - Center.Y) / Radius);
	if (Value.SizeSquared() > 1.0f)
	{
		Value.Normalize();
	}

	return Value;
}

void UVBTouchStick::PlaceThumb(const FVector2D& Value)
{
	if (Thumb)
	{
		Thumb->SetRenderTranslation(FVector2D(Value.X, -Value.Y) * Radius);
	}
}

void UVBTouchStick::OnTouchBegin(const FVector2D& LocalPosition)
{
	const FVector2D Value = ComputeValue(LocalPosition);
	StartInjection(FInputActionValue(Value));
	PlaceThumb(Value);
}

void UVBTouchStick::OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta)
{
	const FVector2D Value = ComputeValue(LocalPosition);
	UpdateInjection(FInputActionValue(Value));
	PlaceThumb(Value);
}

void UVBTouchStick::OnTouchEnd()
{
	PlaceThumb(FVector2D::ZeroVector);
}

// ---- 시점 패드 ----

void UVBTouchLookPad::OnTouchMove(const FVector2D& LocalPosition, const FVector2D& ScreenDelta)
{
	if (!LookCalibration)
	{
		LookCalibration = NewObject<UVBInputModifier_LookCalibration>(this);
		LookCalibration->Device = EVBLookDevice::Touch;
	}

	InjectOnce(FInputActionValue(ScreenDelta * LookScale), { LookCalibration.Get() });
}

// ---- 버튼 ----

void UVBTouchButton::OnTouchBegin(const FVector2D& LocalPosition)
{
	ReleasedOpacity = GetRenderOpacity();
	SetRenderOpacity(PressedOpacity);
	// CommonUI 화면 열기는 키로만 발화해 주입으로 안 열린다. HUD 처리 함수로 바로 보낸다.
	if (UIActionHandler.IsBound() && UIActionHandler.Execute(InputAction))
	{
		return;
	}
	// 처리기 안에서 이미 놓였으면 주입하지 않는다(눌린 채 남는다).
	if (!HasActiveTouch())
	{
		return;
	}

	StartInjection(FInputActionValue(true));
}

void UVBTouchButton::OnTouchEnd()
{
	SetRenderOpacity(ReleasedOpacity);
}

void UVBTouchButton::SetLabel(const FText& InLabel)
{
	if (Label)
	{
		Label->SetText(InLabel);
	}
}

// ---- 묶음 ----

void UVBTouchControlsWidget::ReleaseAllTouches()
{
	for (UVBTouchInputWidget* Control : CreatedControls)
	{
		if (Control)
		{
			Control->ReleaseTouch();
		}
	}
}

bool UVBTouchControlsWidget::ShouldShow(int32 Mode, ECommonInputMode InputMode, ECommonInputType InputType)
{
	if (InputMode == ECommonInputMode::Menu)
	{
		return false;
	}

	return Mode == 1 || (Mode == 0 && InputType == ECommonInputType::Touch);
}

void UVBTouchControlsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 구성마다 다시 잇고 지금 값을 먼저 읽는다(이미 그 상태면 알림이 안 온다).
	UnbindSources();
	BindScreenActionHandler();
	if (const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer())
	{
		// 장치 관리자는 Touch 를 키보드로 접으므로 로컬 플레이어의 CommonInput 을 직접 듣는다.
		if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(LocalPlayer))
		{
			InputSource = CommonInput;
			CommonInput->OnInputMethodChangedNative.AddUObject(this, &UVBTouchControlsWidget::HandleInputMethodChanged);
			InputType = CommonInput->GetCurrentInputType();
		}

		if (UCommonUIActionRouterBase* Router = LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>())
		{
			ModeSource = Router;
			Router->OnActiveInputModeChanged().AddUObject(this, &UVBTouchControlsWidget::HandleInputModeChanged);
			InputMode = Router->GetActiveInputMode();
		}
	}

	OnTouchControlsModeChanged().AddUObject(this, &UVBTouchControlsWidget::RefreshVisibility);
	RefreshVisibility();
}

void UVBTouchControlsWidget::NativeDestruct()
{
	ReleaseAllTouches();
	UnbindSources();
	Super::NativeDestruct();
}

void UVBTouchControlsWidget::UnbindSources()
{
	if (UCommonInputSubsystem* CommonInput = InputSource.Get())
	{
		CommonInput->OnInputMethodChangedNative.RemoveAll(this);
	}

	if (UCommonUIActionRouterBase* Router = ModeSource.Get())
	{
		Router->OnActiveInputModeChanged().RemoveAll(this);
	}

	OnTouchControlsModeChanged().RemoveAll(this);
	InputSource.Reset();
	ModeSource.Reset();
	InputType = ECommonInputType::MouseAndKeyboard;
	InputMode = ECommonInputMode::All;
}

void UVBTouchControlsWidget::BindScreenActionHandler()
{
	FVBTouchUIActionHandler Handler;
	if (const UVBHUDWidget* HUD = Cast<UVBHUDWidget>(UVBUISlotWidget::FindSlotOwnerOf(*this)))
	{
		Handler = FVBTouchUIActionHandler::CreateUObject(HUD, &UVBHUDWidget::TryHandleScreenAction);
	}
	else
	{
		// 처리기가 없으면 화면 버튼이 화면을 못 연다 - 알린다.
		VB_CLOG(!bWarnedNoScreenHandler, Warning, "터치 조작 묶음 %s: HUD 가 놓은 자리가 아니라 화면 버튼(메뉴·가방·스킬·일지)이 화면을 못 연다", *GetName());
		bWarnedNoScreenHandler = true;
	}

	ApplyScreenActionHandler(Handler);
}

void UVBTouchControlsWidget::ApplyScreenActionHandler(const FVBTouchUIActionHandler& Handler)
{
	for (UVBTouchInputWidget* Control : CreatedControls)
	{
		if (UVBTouchButton* Button = Cast<UVBTouchButton>(Control))
		{
			Button->SetUIActionHandler(Handler);
		}
	}
}

void UVBTouchControlsWidget::HandleInputMethodChanged(ECommonInputType NewInputType)
{
	InputType = NewInputType;
	RefreshVisibility();
}

void UVBTouchControlsWidget::HandleInputModeChanged(ECommonInputMode NewMode)
{
	InputMode = NewMode;
	RefreshVisibility();
}

void UVBTouchControlsWidget::RefreshVisibility()
{
	++RefreshCount;
	const bool bShow = ShouldShow(CVarTouchControls.GetValueOnGameThread(), InputMode, InputType);
	const ESlateVisibility Wanted = bShow ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed;
	if (GetVisibility() == Wanted)
	{
		return;
	}
	// 숨기기 전에 놓는다. 감추기만 하면 손 뗌이 안 와 주입이 계속된다.
	if (!bShow)
	{
		ReleaseAllTouches();
	}

	SetVisibility(Wanted);
}

void UVBTouchControlsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UCanvasPanel* RootCanvas = Cast<UCanvasPanel>(GetRootWidget());
	if (!RootCanvas)
	{
		VB_LOG(Warning, "터치 조작 묶음 %s: 루트가 CanvasPanel 이 아니라 조작을 만들 수 없다", *GetName());
		return;
	}

	for (const FVBTouchControlSlot& Entry : Controls)
	{
		if (!Entry.WidgetClass || !Entry.InputAction)
		{
			VB_LOG(Warning, "터치 조작 묶음 %s: 클래스나 입력 액션이 빈 칸이 있어 건너뛴다", *GetName());
			continue;
		}

		UVBTouchInputWidget* Control = CreateWidget<UVBTouchInputWidget>(GetOwningPlayer(), Entry.WidgetClass);
		if (!Control)
		{
			continue;
		}

		Control->SetInputAction(Entry.InputAction);
		CreatedControls.Add(Control);
		if (UVBTouchButton* Button = Cast<UVBTouchButton>(Control))
		{
			Button->SetLabel(Entry.Label);
		}

		if (UCanvasPanelSlot* ControlSlot = RootCanvas->AddChildToCanvas(Control))
		{
			ControlSlot->SetAnchors(Entry.Anchors);
			ControlSlot->SetAlignment(Entry.Alignment);
			ControlSlot->SetPosition(Entry.Position);
			ControlSlot->SetSize(Entry.Size);
			ControlSlot->SetZOrder(Entry.ZOrder);
		}
		// 조작은 손가락을 받는다. 묶음 자신은 SelfHitTestInvisible 이다.
		Control->SetVisibility(ESlateVisibility::Visible);
	}
	// 구성 때 정하기 전까지 숨겨 둔다(첫 그림 번쩍임 방지).
	SetVisibility(ESlateVisibility::Collapsed);
}
