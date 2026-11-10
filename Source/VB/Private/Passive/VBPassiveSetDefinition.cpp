// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Passive/VBPassiveSetDefinition.h"

#include "AbilitySystem/VBGameplayTags.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBPassiveSetDefinition"

const FVBPassiveDefinition* UVBPassiveSetDefinition::FindPassive(const FGameplayTag& PassiveTag) const
{
	return PassiveTag.IsValid()
		? Passives.FindByPredicate([&PassiveTag](const FVBPassiveDefinition& Passive) { return Passive.PassiveTag == PassiveTag; })
		: nullptr;
}

TArray<FText> UVBPassiveSetDefinition::FindProblems() const
{
	TArray<FText> Problems;
	TSet<FGameplayTag> Seen;
	for (int32 Index = 0; Index < Passives.Num(); ++Index)
	{
		const FVBPassiveDefinition& Passive = Passives[Index];
		if (!Passive.PassiveTag.MatchesTag(VBGameplayTags::Passive) || Passive.PassiveTag == VBGameplayTags::Passive)
		{
			Problems.Add(FText::Format(LOCTEXT("BadTag", "{0}번 패시브의 태그 {1} 는 Passive 아래가 아니다."), Index,
				FText::FromName(Passive.PassiveTag.GetTagName())));
		}
		else if (Seen.Contains(Passive.PassiveTag))
		{
			Problems.Add(FText::Format(LOCTEXT("Duplicate", "{0} 가 두 번 있다 - 이름 · 설명이 어느 쪽인지 정할 수 없다."),
				FText::FromName(Passive.PassiveTag.GetTagName())));
		}

		Seen.Add(Passive.PassiveTag);
		if (Passive.DisplayName.IsEmpty())
		{
			Problems.Add(FText::Format(LOCTEXT("NoName", "{0}번 패시브에 이름이 없다 - 얻는 연출에 빈 칸이 뜬다."), Index));
		}
	}

	const FVBPassiveRevealTiming& T = RevealTiming;
	if (!(T.DipStart <= T.PanelStart && T.PanelStart <= T.TextStart && T.TextStart <= T.HintStart && T.HintStart < T.End))
	{
		Problems.Add(LOCTEXT("TimingOrder", "연출 시간은 DipStart <= PanelStart <= TextStart <= HintStart < End 순이어야 한다."));
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBPassiveSetDefinition::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif

#undef LOCTEXT_NAMESPACE
