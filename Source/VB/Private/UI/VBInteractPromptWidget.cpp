// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBInteractPromptWidget.h"

#include "Character/VBHeroComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Input/VBInputDeviceManager.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "UI/VBInputKeyText.h"
#include "VB/VB.h"

void UVBInteractPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 구성될 때마다 다시 잇는다(해체 때 푼다). 초점, 장치 관리자는 폰에 있다 - 폰이 바뀌면 새 폰의 것을 듣는다.
	if (APlayerController* PC = GetOwningPlayer())
	{
		PawnSource = PC;
		PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UVBInteractPromptWidget::HandlePossessedPawnChanged);
		BindPawn(PC->GetPawn());
	}
}

void UVBInteractPromptWidget::NativeDestruct()
{
	Unbind();
	if (APlayerController* PC = PawnSource.Get())
	{
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UVBInteractPromptWidget::HandlePossessedPawnChanged);
	}

	PawnSource.Reset();
	Super::NativeDestruct();
}

void UVBInteractPromptWidget::HandlePossessedPawnChanged(APawn* /*OldPawn*/, APawn* NewPawn)
{
	BindPawn(NewPawn);
}

void UVBInteractPromptWidget::BindPawn(APawn* Pawn)
{
	// 같은 폰 신호가 두 번 와도 구독은 하나다.
	if (Pawn && Pawn == BoundPawn.Get())
	{
		return;
	}

	Unbind();
	if (!Pawn)
	{
		return;
	}

	BoundPawn = Pawn;
	// 순서가 뜻이 있다(설계 11.4): 장치를 먼저 읽어야 바로 아래 초점 반영이 맞는 키 이름을 쓴다.
	if (UVBInputDeviceManager* Devices = UVBInputDeviceManager::FindFor(Pawn))
	{
		DeviceManager = Devices;
		Devices->OnInputDeviceChanged.AddUniqueDynamic(this, &UVBInteractPromptWidget::HandleInputDeviceChanged);
		InputType = Devices->GetCurrentDeviceType();
	}

	if (UVBInteractorComponent* Found = Pawn->FindComponentByClass<UVBInteractorComponent>())
	{
		Interactor = Found;
		Found->OnFocusChanged.AddUniqueDynamic(this, &UVBInteractPromptWidget::HandleFocusChanged);
		// 이미 잡힌 초점은 다시 방송되지 않는다(같은 초점이면 interactor 가 안 알린다) - 지금 값을 바로 받는다.
		HandleFocusChanged(Found->GetFocusedInteractable());
	}
}

void UVBInteractPromptWidget::Unbind()
{
	if (UVBInteractorComponent* Old = Interactor.Get())
	{
		Old->OnFocusChanged.RemoveDynamic(this, &UVBInteractPromptWidget::HandleFocusChanged);
	}

	if (UVBInputDeviceManager* Old = DeviceManager.Get())
	{
		Old->OnInputDeviceChanged.RemoveDynamic(this, &UVBInteractPromptWidget::HandleInputDeviceChanged);
	}

	Interactor.Reset();
	DeviceManager.Reset();
	BoundPawn.Reset();
	// 옛 폰의 대상, 글, 홀드가 새 폰에 남지 않게 비운다.
	Target.Reset();
	ShownActionText = FText::GetEmpty();
	ShownKeyText = FText::GetEmpty();
	ShownKeyIcon.Reset();
	InputType = ECommonInputType::MouseAndKeyboard;
	SetHoldProgress(-1.0f);
	Hide();
}

void UVBInteractPromptWidget::HandleFocusChanged(UVBInteractableComponent* NewFocus)
{
	Target = NewFocus;
	ShownActionText = FText::GetEmpty();
	RefreshKeyText();
}

void UVBInteractPromptWidget::HandleInputDeviceChanged(ECommonInputType NewDeviceType)
{
	InputType = NewDeviceType;
	RefreshKeyText();
}

void UVBInteractPromptWidget::RefreshKeyText()
{
	const UVBHeroComponent* Hero = UVBHeroComponent::FindFor(BoundPawn.Get());
	const UInputAction* Action = Hero ? Hero->FindInputAction(VBGameplayTags::Input_Interact) : nullptr;
	bool bUnmapped = false;
	ShownKeyText = VBInputKeyText::ForAction(GetOwningPlayer(), Action, InputType, bUnmapped);
	ShownKeyIcon = VBInputKeyText::IconForAction(GetOwningPlayer(), Action);
	// 매핑 누락이다. 프롬프트는 뜨는데 무엇을 누를지 모르는 상태라 말한다.
	VB_CLOG(bUnmapped, Warning, "상호작용 프롬프트: %s 에 매핑된 키가 지금 IMC 에 없다 - 키 이름 없음", *GetNameSafe(Action));
	if (Target.IsValid())
	{
		SetPrompt(Target->GetActionText(), ShownKeyText, ShownKeyIcon);
	}
}

void UVBInteractPromptWidget::UpdateIndicator(const FVBIndicatorView& View, UCanvasPanelSlot& IndicatorSlot)
{
	const UVBInteractableComponent* Focus = Target.Get();
	if (!Focus || !View.PC)
	{
		Hide();
		return;
	}
	// 대상 위치를 따라간다. 카메라 뒤(투영 실패)면 숨긴다. 화면 사각형 검사는 하지 않는다(FollowWhenProjected).
	const FVBIndicatorPlacement Placement = VBWorldIndicatorProjection::Place(
		View, Focus->GetComponentLocation(), EVBIndicatorScreenPolicy::FollowWhenProjected, 0.0f);
	if (!Placement.bVisible)
	{
		Hide();
		return;
	}

	IndicatorSlot.SetPosition(Placement.Position);

	const FText ActionText = Focus->GetActionText();
	if (!ActionText.EqualTo(ShownActionText))
	{
		ShownActionText = ActionText;
		SetPrompt(ShownActionText, ShownKeyText, ShownKeyIcon);
	}

	SetHoldProgress(Interactor.IsValid() ? Interactor->GetHoldProgress() : -1.0f);

	if (GetVisibility() == ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UVBInteractPromptWidget::Hide()
{
	if (GetVisibility() != ESlateVisibility::Collapsed)
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UVBInteractPromptWidget::SetPrompt(const FText& ActionText, const FText& KeyText, const TOptional<FSlateBrush>& KeyIcon)
{
	if (ActionLabel)
	{
		ActionLabel->SetText(ActionText);
	}

	VBInputKeyText::ShowKey(KeyLabel, ImgKey, KeyText, KeyIcon);
}

void UVBInteractPromptWidget::SetHoldProgress(float Progress)
{
	if (!HoldProgress)
	{
		return;
	}

	if (Progress < 0.0f)
	{
		HoldProgress->SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	HoldProgress->SetPercent(Progress);
	HoldProgress->SetVisibility(ESlateVisibility::HitTestInvisible);
}
