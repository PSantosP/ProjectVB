// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/Appearance/VBAppearanceSettings.h"

#include "AbilitySystem/VBGameplayTags.h"

#define LOCTEXT_NAMESPACE "VBAppearanceSettings"

FGameplayTag UVBAppearanceSettings::FindAppearanceSlot(const FGameplayTag& EquipmentSlot) const
{
	const FGameplayTag* Found = EquipmentToAppearanceSlot.Find(EquipmentSlot);
	return Found ? *Found : FGameplayTag();
}

namespace VBAppearanceSettingsRules
{
	TArray<FText> FindTableProblems(const TMap<FGameplayTag, FGameplayTag>& Table, const TArray<FGameplayTag>& SlotLayout)
	{
		TArray<FText> Problems;
		TMap<FGameplayTag, FGameplayTag> ByAppearance;
		for (const TPair<FGameplayTag, FGameplayTag>& Row : Table)
		{
			if (!Row.Value.IsValid())
			{
				continue;
			}

			if (!Row.Value.MatchesTag(VBGameplayTags::Appearance_Slot) || Row.Value == VBGameplayTags::Appearance_Slot)
			{
				Problems.Add(FText::Format(LOCTEXT("NotSlot", "{0} 의 값 {1} 은 Appearance.Slot 아래 칸이 아니다."),
					FText::FromString(Row.Key.ToString()), FText::FromString(Row.Value.ToString())));
				continue;
			}

			if (const FGameplayTag* Other = ByAppearance.Find(Row.Value))
			{
				Problems.Add(FText::Format(LOCTEXT("TwoToOne", "장비 칸 {0} 과 {1} 이 같은 외형 칸 {2} 을 채운다."),
					FText::FromString(Other->ToString()), FText::FromString(Row.Key.ToString()), FText::FromString(Row.Value.ToString())));
				continue;
			}

			ByAppearance.Add(Row.Value, Row.Key);
		}

		TSet<FGameplayTag> Seen;
		for (const FGameplayTag& EquipmentSlot : SlotLayout)
		{
			bool bAlreadySeen = false;
			Seen.Add(EquipmentSlot, &bAlreadySeen);
			const FGameplayTag* Mapped = Table.Find(EquipmentSlot);
			if (bAlreadySeen && Mapped && Mapped->IsValid())
			{
				Problems.Add(FText::Format(LOCTEXT("RepeatedSlot", "장비 칸 배치에 {0} 이 두 번 있는데 외형 칸 {1} 에 대응한다 - 두 칸이 한 외형 칸을 다툰다."),
					FText::FromString(EquipmentSlot.ToString()), FText::FromString(Mapped->ToString())));
			}
		}

		return Problems;
	}

	FText FindItemPieceProblem(bool bHasEquippable, const FGameplayTag& EquipmentSlot, const FGameplayTag& MappedSlot,
		const FGameplayTag& PieceSlot)
	{
		if (!bHasEquippable)
		{
			return LOCTEXT("PieceNotEquipment", "외형 조각이 있는데 장비 조각이 없다 - 입을 칸이 없어 겉모습이 바뀔 일이 없다.");
		}

		if (!MappedSlot.IsValid())
		{
			return FText::Format(LOCTEXT("SlotShowsNothing", "장비 칸 {0} 은 외형 칸에 대응하지 않는다(UVBAppearanceSettings) - 이 조각은 보이지 않는다."),
				FText::FromString(EquipmentSlot.ToString()));
		}

		if (PieceSlot != MappedSlot)
		{
			return FText::Format(LOCTEXT("WrongPieceSlot", "장비 칸 {0} 은 외형 칸 {1} 을 채우는데 조각의 칸은 {2} 다."),
				FText::FromString(EquipmentSlot.ToString()), FText::FromString(MappedSlot.ToString()),
				FText::FromString(PieceSlot.ToString()));
		}

		return FText::GetEmpty();
	}

	FText FindItemLookDecisionProblem(bool bHasEquippable, const FGameplayTag& EquipmentSlot, const FGameplayTag& MappedSlot,
		bool bHasAppearanceFragment, bool bNoAppearance, bool bNamesPiece)
	{
		if (!bHasAppearanceFragment)
		{
			return bHasEquippable && MappedSlot.IsValid()
				? FText::Format(LOCTEXT("LookUndecided", "장비 칸 {0} 은 몸의 외형 칸 {1} 에 보이는데 외형 조각이 없다 - 조각을 달거나 «겉모습 없음» 으로 표시한다."),
					FText::FromString(EquipmentSlot.ToString()), FText::FromString(MappedSlot.ToString()))
				: FText::GetEmpty();
		}

		if (bNoAppearance && bNamesPiece)
		{
			return LOCTEXT("NoAppearanceWithPiece", "«겉모습 없음» 인데 조각도 있다 - 둘 중 하나만 둔다.");
		}

		return FText::GetEmpty();
	}
}

#undef LOCTEXT_NAMESPACE
