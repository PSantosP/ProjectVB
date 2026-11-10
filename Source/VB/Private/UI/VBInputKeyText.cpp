// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBInputKeyText.h"

#include "CommonInputBaseTypes.h"
#include "CommonInputSubsystem.h"
#include "CommonUITypes.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "Input/VBInputUserSettings.h"

namespace
{
	FText UnmappedKeyText()
	{
		return NSLOCTEXT("VBTutorialUI", "Unmapped", "\ubbf8\ubc30\uce58");
	}
}

FText VBInputKeyText::ForAction(const APlayerController* Controller, const UInputAction* Action, ECommonInputType InputType,
	bool& bOutUnmapped)
{
	bOutUnmapped = false;
	if (InputType == ECommonInputType::Touch || !Action)
	{
		return FText::GetEmpty();
	}

	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	const UEnhancedInputLocalPlayerSubsystem* Input =
		LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (!Input)
	{
		return FText::GetEmpty();
	}

	const TArray<FKey> Keys = Input->QueryKeysMappedToAction(Action);
	const FKey* Key = Keys.FindByPredicate([](const FKey& Candidate) { return Candidate.IsValid(); });
	bOutUnmapped = Key == nullptr;
	return Key ? ForKey(*Key) : FText::GetEmpty();
}

FText VBInputKeyText::ForMapping(const ULocalPlayer* LocalPlayer, FName MappingName, EVBKeyDevice Device)
{
	if (MappingName.IsNone())
	{
		return FText::GetEmpty();
	}

	const UEnhancedInputLocalPlayerSubsystem* Input =
		LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	const UVBInputUserSettings* Settings = Input ? Input->GetUserSettings<UVBInputUserSettings>() : nullptr;
	if (Settings)
	{
		for (const FVBKeyBindingRow& Row : Settings->GetRows(Device))
		{
			if (Row.MappingName == MappingName && Row.Key.IsValid())
			{
				return ForKey(Row.Key);
			}
		}
	}

	return UnmappedKeyText();
}

FText VBInputKeyText::ForCurrentDevice(const ULocalPlayer* LocalPlayer, FName MappingName)
{
	if (MappingName.IsNone())
	{
		return FText::GetEmpty();
	}

	const TOptional<ECommonInputType> InputType = CurrentInputType(LocalPlayer);
	if (!InputType)
	{
		return UnmappedKeyText();
	}

	if (*InputType == ECommonInputType::Touch)
	{
		return FText::GetEmpty();
	}

	return ForMapping(LocalPlayer, MappingName,
		*InputType == ECommonInputType::Gamepad ? EVBKeyDevice::Gamepad : EVBKeyDevice::KeyboardMouse);
}

TOptional<ECommonInputType> VBInputKeyText::CurrentInputType(const ULocalPlayer* LocalPlayer)
{
	const UCommonInputSubsystem* CommonInput =
		LocalPlayer ? LocalPlayer->GetSubsystem<UCommonInputSubsystem>() : nullptr;
	return CommonInput ? TOptional<ECommonInputType>(CommonInput->GetCurrentInputType()) : TOptional<ECommonInputType>();
}

FText VBInputKeyText::ForKey(const FKey& Key)
{
	return Key.IsValid() ? Key.GetDisplayName( /*bLongDisplayName=*/false) : FText::GetEmpty();
}

TOptional<FSlateBrush> VBInputKeyText::IconForAction(const APlayerController* Controller, const UInputAction* Action)
{
	const ULocalPlayer* LocalPlayer = Controller ? Controller->GetLocalPlayer() : nullptr;
	const UCommonInputSubsystem* CommonInput =
		LocalPlayer ? LocalPlayer->GetSubsystem<UCommonInputSubsystem>() : nullptr;
	if (!CommonInput || !Action || CommonInput->GetCurrentInputType() == ECommonInputType::Touch) { return {}; }

	// 엔진이 적용된 매핑에서 지금 장치의 키를 고르고 CommonInput 대응표를 찾는다.
	const FSlateBrush Brush = CommonUI::GetIconForEnhancedInputAction(CommonInput, Action);
	return Brush.GetResourceObject() ? TOptional<FSlateBrush>(Brush) : TOptional<FSlateBrush>();
}

TOptional<FSlateBrush> VBInputKeyText::IconForMapping(const ULocalPlayer* LocalPlayer, FName MappingName)
{
	if (!LocalPlayer || MappingName.IsNone()) { return {}; }
	const UCommonInputSubsystem* CommonInput = LocalPlayer->GetSubsystem<UCommonInputSubsystem>();
	if (!CommonInput) { return {}; }
	const ECommonInputType InputType = CommonInput->GetCurrentInputType();
	if (InputType == ECommonInputType::Touch) { return {}; }
	const UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	const UVBInputUserSettings* Settings = Input ? Input->GetUserSettings<UVBInputUserSettings>() : nullptr;
	const UCommonInputPlatformSettings* Platform = UCommonInputPlatformSettings::Get();
	if (!Settings || !Platform) { return {}; }

	const EVBKeyDevice Device = InputType == ECommonInputType::Gamepad ? EVBKeyDevice::Gamepad : EVBKeyDevice::KeyboardMouse;
	for (const FVBKeyBindingRow& Row : Settings->GetRows(Device))
	{
		if (Row.MappingName != MappingName || !Row.Key.IsValid())
		{
			continue;
		}

		FSlateBrush Brush;
		const bool bFound = Platform->TryGetInputBrush(Brush, Row.Key, InputType, CommonInput->GetCurrentGamepadName());
		return bFound && Brush.GetResourceObject() ? TOptional<FSlateBrush>(Brush) : TOptional<FSlateBrush>();
	}

	return {};
}

void VBInputKeyText::ShowKey(UTextBlock* Label, UImage* Icon, const FText& Text, const TOptional<FSlateBrush>& Brush)
{
	const bool bShowIcon = Icon && Brush.IsSet();
	if (Icon)
	{
		if (bShowIcon)
		{
			Icon->SetBrush(Brush.GetValue());
		}

		Icon->SetVisibility(bShowIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	if (Label)
	{
		Label->SetText(Text);
		Label->SetVisibility(bShowIcon || Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}
