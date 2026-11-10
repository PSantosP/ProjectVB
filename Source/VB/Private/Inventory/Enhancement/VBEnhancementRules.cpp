// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Enhancement/VBEnhancementRules.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/VBItemDefinition.h"

FVBEnhanceTarget FVBEnhanceTarget::ForWeapon(EVBWeaponType InWeapon)
{
	FVBEnhanceTarget Target;
	Target.Weapon = InWeapon;
	return Target;
}

FVBEnhanceTarget FVBEnhanceTarget::ForItem(const UVBItemDefinition* InItem)
{
	FVBEnhanceTarget Target;
	Target.Item = InItem;
	return Target;
}

namespace VBEnhancement
{
	bool HasScalableStats(const FVBItemFragment_Equippable& Equippable)
	{
		for (const TPair<FGameplayTag, float>& Stat : Equippable.Stats)
		{
			if (Stat.Key.IsValid() && !FMath::IsNearlyZero(Stat.Value))
			{
				return true;
			}
		}

		return false;
	}

	EVBEnhanceCategory CategoryOf(const FVBEnhanceTarget& Target)
	{
		if (Target.IsWeapon())
		{
			return EVBEnhanceCategory::Weapon;
		}

		const FVBItemFragment_Equippable* Equippable = Target.IsItem() ? Target.Item->FindFragment<FVBItemFragment_Equippable>() : nullptr;
		if (!Equippable || !HasScalableStats(*Equippable))
		{
			return EVBEnhanceCategory::None;
		}

		if (Equippable->SlotType == VBGameplayTags::Equipment_Slot_Charm)
		{
			return EVBEnhanceCategory::Accessory;
		}

		if (Equippable->SlotType == VBGameplayTags::Equipment_Slot_Head || Equippable->SlotType == VBGameplayTags::Equipment_Slot_Body
			|| Equippable->SlotType == VBGameplayTags::Equipment_Slot_Arms || Equippable->SlotType == VBGameplayTags::Equipment_Slot_Legs)
		{
			return EVBEnhanceCategory::Armor;
		}

		return EVBEnhanceCategory::None;
	}

	int32 NormalizeLevel(int32 Level, int32 MaxLevel)
	{
		// 최대가 없으면(수치 자산 없음) 위로 자르지 않는다 - 잘라서 다시 저장하면 단계가 영영 사라진다.
		return MaxLevel > 0 ? FMath::Clamp(Level, 0, MaxLevel) : FMath::Max(Level, 0);
	}

	float ScaleAt(const FVBEnhanceTrack* Track, int32 Level)
	{
		if (!Track || Level <= 0 || !Track->Steps.IsValidIndex(Level - 1))
		{
			return 1.0f;
		}

		const float Scale = Track->Steps[Level - 1].Scale;
		return FMath::IsFinite(Scale) && Scale > 0.0f ? Scale : 1.0f;
	}

	EVBEnhanceResult Check(const FVBEnhanceQuote& Quote)
	{
		if (!Quote.bTargetValid)
		{
			return EVBEnhanceResult::Invalid;
		}

		if (Quote.Category == EVBEnhanceCategory::None)
		{
			return EVBEnhanceResult::NotEnhanceable;
		}

		if (!Quote.bOwned)
		{
			return EVBEnhanceResult::NotOwned;
		}

		if (!Quote.bTrackUsable)
		{
			return EVBEnhanceResult::NoTrack;
		}

		if (Quote.CurrentLevel >= Quote.MaxLevel)
		{
			return EVBEnhanceResult::MaxLevel;
		}

		if (!Quote.bHasMaterials)
		{
			return EVBEnhanceResult::MissingMaterials;
		}

		if (Quote.GoldHeld < Quote.GoldRequired)
		{
			return EVBEnhanceResult::NotEnoughGold;
		}

		return EVBEnhanceResult::Ok;
	}

	TMap<FGameplayTag, float> ScaledStats(const TMap<FGameplayTag, float>& Stats, float Scale)
	{
		TMap<FGameplayTag, float> Out;
		Out.Reserve(Stats.Num());
		for (const TPair<FGameplayTag, float>& Stat : Stats)
		{
			Out.Add(Stat.Key, Stat.Value * Scale);
		}

		return Out;
	}

	const TCHAR* LexToString(EVBEnhanceResult Result)
	{
		switch (Result)
		{
		case EVBEnhanceResult::Ok: return TEXT("Ok");
		case EVBEnhanceResult::Invalid: return TEXT("Invalid");
		case EVBEnhanceResult::NotEnhanceable: return TEXT("NotEnhanceable");
		case EVBEnhanceResult::NotOwned: return TEXT("NotOwned");
		case EVBEnhanceResult::MaxLevel: return TEXT("MaxLevel");
		case EVBEnhanceResult::NoTrack: return TEXT("NoTrack");
		case EVBEnhanceResult::MissingMaterials: return TEXT("MissingMaterials");
		case EVBEnhanceResult::NotEnoughGold: return TEXT("NotEnoughGold");
		}

		return TEXT("Unknown");
	}
}
