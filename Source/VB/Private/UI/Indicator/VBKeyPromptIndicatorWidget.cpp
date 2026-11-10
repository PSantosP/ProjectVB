// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Indicator/VBKeyPromptIndicatorWidget.h"

#include "CommonInputSubsystem.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "UI/VBInputKeyText.h"
#include "VB/VB.h"

void UVBKeyPromptIndicatorWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ActionLabel)
	{
		ActionLabel->SetText(ActionText);
	}

	if (ULocalPlayer* Player = GetOwningLocalPlayer())
	{
		InputSource = Player->GetSubsystem<UCommonInputSubsystem>();
		if (UCommonInputSubsystem* Input = InputSource.Get())
		{
			InputMethodHandle = Input->OnInputMethodChangedNative.AddUObject(this, &ThisClass::HandleInputMethodChanged);
		}

		MappingSource = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		if (UEnhancedInputLocalPlayerSubsystem* Input = MappingSource.Get())
		{
			Input->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this, &ThisClass::RefreshKey);
		}
	}

	RefreshKey();
}

void UVBKeyPromptIndicatorWidget::NativeDestruct()
{
	if (UCommonInputSubsystem* Input = InputSource.Get())
	{
		Input->OnInputMethodChangedNative.Remove(InputMethodHandle);
	}

	if (UEnhancedInputLocalPlayerSubsystem* Input = MappingSource.Get())
	{
		Input->ControlMappingsRebuiltDelegate.RemoveDynamic(this, &ThisClass::RefreshKey);
	}

	InputSource.Reset();
	MappingSource.Reset();
	InputMethodHandle.Reset();
	Super::NativeDestruct();
}

void UVBKeyPromptIndicatorWidget::HandleInputMethodChanged(ECommonInputType Type) { RefreshKey(); }

void UVBKeyPromptIndicatorWidget::RefreshKey()
{
	// 행동이 없거나 그 키가 지금 묶음에 없으면 키 칸이 빈다 - 표시는 뜨는데 무엇을 누를지 모르는 상태라 말한다.
	const TOptional<ECommonInputType> Type = VBInputKeyText::CurrentInputType(GetOwningLocalPlayer());
	bool bUnmapped = !Action;
	const FText KeyText = Action && Type.IsSet()
		? VBInputKeyText::ForAction(GetOwningPlayer(), Action, Type.GetValue(), bUnmapped) : FText::GetEmpty();
	VB_CLOG(bUnmapped, Warning, "키 표시 %s: 행동 %s 의 키를 지금 묶음에서 못 찾았다 - 키 칸이 빈다", *GetName(), *GetNameSafe(Action));
	VBInputKeyText::ShowKey(KeyLabel, ImgKey, KeyText,
		Action ? VBInputKeyText::IconForAction(GetOwningPlayer(), Action) : TOptional<FSlateBrush>());
}
