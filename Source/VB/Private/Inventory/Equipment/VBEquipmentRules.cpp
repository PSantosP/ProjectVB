// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Equipment/VBEquipmentRules.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/VBItemDefinition.h"

bool VBEquipment::Fits(const UVBItemDefinition* Item, const FGameplayTag& SlotType)
{
	const FVBItemFragment_Equippable* Equippable = Item ? Item->FindFragment<FVBItemFragment_Equippable>() : nullptr;
	return Equippable && SlotType.IsValid() && Equippable->SlotType.MatchesTagExact(SlotType);
}

FVBEquipOutcome VBEquipment::Equip(const TArray<FGameplayTag>& Layout, TArray<const UVBItemDefinition*>& InOutEquipped,
	const UVBItemDefinition* Item,
	int32 RequestedSlot)
{
	FVBEquipOutcome Out;
	if (!Item || !Item->FindFragment<FVBItemFragment_Equippable>())
	{
		return Out;
	}

	InOutEquipped.SetNum(Layout.Num());

	int32 Slot = INDEX_NONE;
	if (Layout.IsValidIndex(RequestedSlot))
	{
		Slot = Fits(Item, Layout[RequestedSlot]) ? RequestedSlot : INDEX_NONE;
	}
	else
	{
		// 이미 입은 칸이 있으면 그 칸 그대로다 - 자동 요청이 같은 부적을 다른 장신구 칸으로 옮기지 않게.
		const int32 Current = InOutEquipped.IndexOfByKey(Item);
		if (Current != INDEX_NONE)
		{
			Out.Slot = Current;
			Out.Result = EVBEquipResult::Unchanged;
			return Out;
		}

		for (int32 Index = 0; Index < Layout.Num() && Slot == INDEX_NONE; ++Index)
		{
			Slot = Fits(Item, Layout[Index]) && !InOutEquipped[Index] ? Index : INDEX_NONE;
		}

		for (int32 Index = 0; Index < Layout.Num() && Slot == INDEX_NONE; ++Index)
		{
			Slot = Fits(Item, Layout[Index]) ? Index : INDEX_NONE;
		}
	}

	if (Slot == INDEX_NONE)
	{
		Out.Result = EVBEquipResult::NoFittingSlot;
		return Out;
	}

	Out.Slot = Slot;
	if (InOutEquipped[Slot] == Item)
	{
		Out.Result = EVBEquipResult::Unchanged;
		return Out;
	}

	const int32 Previous = InOutEquipped.IndexOfByKey(Item);
	if (Previous != INDEX_NONE)
	{
		InOutEquipped[Previous] = nullptr;
		Out.Vacated = Previous;
	}

	Out.Replaced = InOutEquipped[Slot];
	InOutEquipped[Slot] = Item;
	Out.Result = EVBEquipResult::Changed;
	return Out;
}

const TCHAR* VBEquipment::LexToString(EVBEquipResult Result)
{
	switch (Result)
	{
	case EVBEquipResult::Changed: return TEXT("바뀜");
	case EVBEquipResult::Unchanged: return TEXT("그대로");
	case EVBEquipResult::NotEquippable: return TEXT("장비가 아님");
	case EVBEquipResult::NoFittingSlot: return TEXT("맞는 칸 없음");
	}

	return TEXT("?");
}

int32 VBEquipment::MapSavedSlot(const TArray<FGameplayTag>& SavedLayout, int32 SavedIndex, const TArray<FGameplayTag>& Layout)
{
	if (!SavedLayout.IsValidIndex(SavedIndex) || !SavedLayout[SavedIndex].IsValid())
	{
		return INDEX_NONE;
	}

	const FGameplayTag& SlotType = SavedLayout[SavedIndex];
	int32 Occurrence = 0;
	for (int32 Index = 0; Index < SavedIndex; ++Index)
	{
		Occurrence += SavedLayout[Index] == SlotType ? 1 : 0;
	}

	for (int32 Index = 0; Index < Layout.Num(); ++Index)
	{
		if (Layout[Index] == SlotType && Occurrence-- == 0)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

TArray<FGameplayTag> VBEquipment::LegacySaveLayout()
{
	return {VBGameplayTags::Equipment_Slot_Head, VBGameplayTags::Equipment_Slot_Body, VBGameplayTags::Equipment_Slot_Arms,
		VBGameplayTags::Equipment_Slot_Charm, VBGameplayTags::Equipment_Slot_Charm};
}
