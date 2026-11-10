// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Enhancement/VBEnhancementConfig.h"

#include "Inventory/VBItemDefinition.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBEnhancementConfig"

const FVBEnhanceTrack* UVBEnhancementConfig::FindTrack(EVBEnhanceCategory Category) const
{
	switch (Category)
	{
	case EVBEnhanceCategory::Weapon: return &WeaponTrack;
	case EVBEnhanceCategory::Armor: return &ArmorTrack;
	case EVBEnhanceCategory::Accessory: return &AccessoryTrack;
	default: return nullptr;
	}
}

bool UVBEnhancementConfig::IsTrackUsable(EVBEnhanceCategory Category) const
{
	const FVBEnhanceTrack* Track = FindTrack(Category);
	if (!Track || MaxLevel < 1 || Track->Steps.Num() != MaxLevel)
	{
		return false;
	}

	float Previous = 1.0f;
	for (const FVBEnhanceStep& Step : Track->Steps)
	{
		// 강화는 약해지지 않는다(첫 배율도 1 이상), 공짜 단계는 없다(골드나 재료 중 하나는 든다 - 설계 b3 8 F4).
		if (!FMath::IsFinite(Step.Scale) || Step.Scale <= 0.0f || Step.Scale < Previous || Step.Gold < 0
			|| (Step.Gold == 0 && Step.Materials.IsEmpty()))
		{
			return false;
		}

		for (const FVBItemAmount& Material : Step.Materials)
		{
			if (!Material.Item || Material.Count <= 0)
			{
				return false;
			}
		}

		Previous = Step.Scale;
	}

	return true;
}

#if WITH_EDITOR
EDataValidationResult UVBEnhancementConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	const TPair<EVBEnhanceCategory, const TCHAR*> Categories[] = {
		{EVBEnhanceCategory::Weapon, TEXT("WeaponTrack")}, {EVBEnhanceCategory::Armor, TEXT("ArmorTrack")},
		{EVBEnhanceCategory::Accessory, TEXT("AccessoryTrack")}};
	for (const TPair<EVBEnhanceCategory, const TCHAR*>& Entry : Categories)
	{
		if (!IsTrackUsable(Entry.Key))
		{
			Context.AddError(FText::Format(LOCTEXT("BadTrack",
					"{0}: 단계 수가 MaxLevel({1}) 이 아니거나, 배율이 1 이상으로 줄지 않는 유한한 수가 아니거나, 재료 줄이 비었다 - 이 갈래는 강화되지 않는다"),
				FText::FromString(Entry.Value), FText::AsNumber(MaxLevel)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
