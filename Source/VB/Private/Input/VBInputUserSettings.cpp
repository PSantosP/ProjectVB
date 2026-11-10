// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Input/VBInputUserSettings.h"

#include "EnhancedActionKeyMapping.h"
#include "GameFramework/InputSettings.h"
#include "InputMappingContext.h"
#include "UObject/ObjectKey.h"
#include "Input/VBKeyBindingRules.h"
#include "VB/VB.h"

namespace
{
	// 장치 -> 엔진 장치 식별자. 등록, 찾기, 다시 짓기가 이 하나를 같이 쓴다.
	const FHardwareDeviceIdentifier& HardwareOf(EVBKeyDevice Device)
	{
		return Device == EVBKeyDevice::Gamepad ? FHardwareDeviceIdentifier::DefaultGamepad
			: FHardwareDeviceIdentifier::DefaultKeyboardAndMouse;
	}
}

FHardwareDeviceIdentifier UVBInputUserSettings::DetermineHardwareDeviceForActionMapping(const FEnhancedActionKeyMapping& ActionMapping,
	const UInputMappingContext* IMC) const
{
	if (ActionMapping.Key.IsValid() || !IMC)
	{
		return HardwareOf(VBKeyBinding::DeviceOf(ActionMapping.Key));
	}
	// 빈 줄은 키로 장치를 알 수 없어 같은 맥락의 다른 키로 정한다.
	for (const FEnhancedActionKeyMapping& Other : IMC->GetMappings())
	{
		if (Other.Key.IsValid())
		{
			return HardwareOf(VBKeyBinding::DeviceOf(Other.Key));
		}
	}
	// 등록, 다시 짓기마다 불리므로 맥락마다 한 번만 알린다.
	static TSet<FObjectKey> Reported;
	bool bAlreadyReported = false;
	Reported.Add(FObjectKey(IMC), &bAlreadyReported);
	VB_CLOG(!bAlreadyReported, Warning, "맥락 %s 에 키가 하나도 없다 - 빈 줄 %s 를 키보드 줄로 친다", *IMC->GetName(),
		*ActionMapping.GetMappingName().ToString());
	return HardwareOf(EVBKeyDevice::KeyboardMouse);
}

TArray<FVBKeyBindingRow> UVBInputUserSettings::GetRows(EVBKeyDevice Device) const
{
	TArray<FVBKeyBindingRow> Rows;
	const UEnhancedPlayerMappableKeyProfile* Profile = GetActiveKeyProfile();
	if (!Profile)
	{
		return Rows;
	}

	const EHardwareDevicePrimaryType DeviceType = HardwareOf(Device).PrimaryDeviceType;
	for (const TPair<FName, FKeyMappingRow>& Pair : Profile->GetPlayerMappingRows())
	{
		for (const FPlayerKeyMapping& Mapping : Pair.Value.Mappings)
		{
			if (Mapping.GetSlot() == EPlayerMappableKeySlot::First && Mapping.GetPrimaryDeviceType() == DeviceType)
			{
				FVBKeyBindingRow& Row = Rows.AddDefaulted_GetRef();
				Row.MappingName = Pair.Key;
				Row.Device = Device;
				Row.Key = Mapping.GetCurrentKey();
				Row.DefaultKey = Mapping.GetDefaultKey();
				Row.DisplayName = Mapping.GetDisplayName();
				Row.DisplayCategory = Mapping.GetDisplayCategory();
			}
		}
	}

	return Rows;
}

bool UVBInputUserSettings::RebindKey(FName MappingName, EVBKeyDevice Device, const FKey& InNewKey, TArray<FName>& OutCleared)
{
	OutCleared.Reset();
	// 트리거 버튼은 축으로 모은다 - IMC 기본 키가 축이라 기본 복귀와 겹침이 맞게 보인다.
	const FKey NewKey = VBKeyBinding::NormalizeKey(InNewKey);
	if (!VBKeyBinding::CanBind(NewKey, Device))
	{
		return false;
	}

	const TArray<FVBKeyBindingRow> Rows = GetRows(Device);
	const auto RowOf = [&Rows](FName Name) -> const FVBKeyBindingRow*
	{
		return Rows.FindByPredicate([Name](const FVBKeyBindingRow& Row) { return Row.MappingName == Name; });
	};
	const FVBKeyBindingRow* Target = RowOf(MappingName);
	if (!Target)
	{
		return false;
	}

	// 바꿀 줄 전부와 바꾸기 전 키. 겹친 줄은 Invalid 로 둔다 - UnMapPlayerKey 는 기본 키로 되돌린다.
	struct FChange
	{
		FName Name;
		FKey Before;
		FKey After;
	};
	TArray<FChange> Changes;
	for (const FName Other : VBKeyBinding::FindConflicts(Rows, MappingName, Device, NewKey))
	{
		const FVBKeyBindingRow* Row = RowOf(Other);
		Changes.Add({Other, Row ? Row->Key : FKey(), EKeys::Invalid});
	}

	Changes.Add({MappingName, Target->Key, NewKey});

	// 하나라도 못 찾으면 아무것도 바꾸지 않는다. MapPlayerKey 는 칸, 장치가 없어도 실패를 알리지 않으므로 같은 찾기로 먼저 본다.
	const UEnhancedPlayerMappableKeyProfile* Profile = GetActiveKeyProfile();
	FMapPlayerKeyArgs Args;
	const auto CurrentKey = [Profile, &Args, Device](FName Name) -> TOptional<FKey>
	{
		FillArgs(Args, Name, Device, FKey());
		const FPlayerKeyMapping* Mapping = Profile ? Profile->FindKeyMapping(Args) : nullptr;
		return Mapping ? TOptional<FKey>(Mapping->GetCurrentKey()) : TOptional<FKey>();
	};
	for (const FChange& Change : Changes)
	{
		if (!CurrentKey(Change.Name).IsSet())
		{
			VB_LOG(Warning, "InputUserSettings: %s 의 %s 칸을 찾지 못해 아무것도 바꾸지 않는다", *Change.Name.ToString(),
				*HardwareOf(Device).HardwareDeviceIdentifier.ToString());
			return false;
		}
	}

	FGameplayTagContainer Failure;
	for (const FChange& Change : Changes)
	{
		FillArgs(Args, Change.Name, Device, Change.After);
		MapPlayerKey(Args, Failure);
	}
	// 그래도 결과가 다르면 바꾸기 전 키로 되돌려 반쯤 바뀐 배치가 저장되지 않게 한다.
	const bool bApplied = Failure.IsEmpty() && !Changes.ContainsByPredicate([&CurrentKey](const FChange& Change)
	{
		const TOptional<FKey> Now = CurrentKey(Change.Name);
		return !Now.IsSet() || Now.GetValue() != Change.After;
	});
	if (!bApplied)
	{
		VB_LOG(Error, "InputUserSettings: 키 바꾸기 실패 %s -> %s (%s) - 바꾸기 전으로 되돌린다", *MappingName.ToString(), *NewKey.ToString(),
			*Failure.ToStringSimple());
		FGameplayTagContainer RestoreFailure;
		for (int32 Index = Changes.Num() - 1; Index >= 0; --Index)
		{
			FillArgs(Args, Changes[Index].Name, Device, Changes[Index].Before);
			MapPlayerKey(Args, RestoreFailure);
		}

		VB_CLOG(!RestoreFailure.IsEmpty(), Error, "InputUserSettings: 되돌리기도 일부 실패 (%s)", *RestoreFailure.ToStringSimple());
		return false;
	}

	for (int32 Index = 0; Index < Changes.Num() - 1; ++Index)
	{
		OutCleared.Add(Changes[Index].Name);
	}
	// 무엇이 무엇으로 바뀌고 누가 비워졌는지 한 줄로 남긴다.
	VB_LOG(Log, "InputUserSettings: %s [%s] -> %s, 비운 줄 %d", *MappingName.ToString(),
		Device == EVBKeyDevice::Gamepad ? TEXT("Gamepad") : TEXT("KBM"),
		*NewKey.ToString(), OutCleared.Num());
	return true;
}

void UVBInputUserSettings::ResetDeviceToDefault(EVBKeyDevice Device)
{
	// 여기서는 UnMapPlayerKey 로 줄을 IMC 기본 키로 되돌린다.
	FGameplayTagContainer Failure;
	FMapPlayerKeyArgs Args;
	for (const FVBKeyBindingRow& Row : GetRows(Device))
	{
		FillArgs(Args, Row.MappingName, Device, FKey());
		UnMapPlayerKey(Args, Failure);
	}

	VB_CLOG(!Failure.IsEmpty(), Warning, "InputUserSettings: 기본값으로 되돌리기 일부 실패 (%s)", *Failure.ToStringSimple());
}

void UVBInputUserSettings::FillArgs(FMapPlayerKeyArgs& Args, FName MappingName, EVBKeyDevice Device, const FKey& NewKey)
{
	Args.MappingName = MappingName;
	Args.Slot = EPlayerMappableKeySlot::First;
	Args.NewKey = NewKey;
	Args.HardwareDeviceId = HardwareOf(Device).HardwareDeviceIdentifier;
	// 없는 칸을 새로 만들지 않는다. 줄은 IMC 등록이 만든다.
	Args.bCreateMatchingSlotIfNeeded = false;
}

int32 UVBPlayerMappableKeyProfile::GetPlayerMappedKeysForRebuildControlMappings(const FEnhancedActionKeyMapping& DefaultMapping,
	TArray<FKey>& OutKeys) const
{
	OutKeys.Reset();
	if (!DefaultMapping.IsPlayerMappable())
	{
		return 0;
	}
	// KeyToMatch 를 비워 키 종류 견주기를 건너뛰고 줄의 장치로 고른다.
	// 장치 없는 옛 줄은 비고, 그러면 엔진이 기본 키를 쓴다.
	FPlayerMappableKeyQueryOptions Options;
	Options.MappingName = DefaultMapping.GetMappingName();
	Options.RequiredDeviceType = HardwareOf(VBKeyBinding::DeviceOf(DefaultMapping.Key)).PrimaryDeviceType;
	return QueryPlayerMappedKeys(Options, OutKeys);
}
