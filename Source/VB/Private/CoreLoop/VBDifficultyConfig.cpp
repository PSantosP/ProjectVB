// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CoreLoop/VBDifficultyConfig.h"

#include "CoreLoop/VBDifficultyRules.h"

const FVBDifficultyLevel* UVBDifficultyConfig::FindLevel(FGameplayTag Tag) const
{
	return Tag.IsValid() ? Levels.FindByPredicate([Tag](const FVBDifficultyLevel& Level) { return Level.Tag == Tag; }) : nullptr;
}

const FVBDifficultyLevel* UVBDifficultyConfig::FindLevelOrDefault(FGameplayTag Tag) const
{
	const FVBDifficultyLevel* Level = FindLevel(Tag);
	return Level ? Level : FindLevel(DefaultLevel);
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#define LOCTEXT_NAMESPACE "VBDifficultyConfig"

EDataValidationResult UVBDifficultyConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FGameplayTag> Seen;
	for (const FVBDifficultyLevel& Level : Levels)
	{
		// 빈 태그, 같은 태그 둘은 저장과 요청이 단계를 못 가리키거나 둘 중 아무거나 가리킨다.
		if (!Level.Tag.IsValid() || Seen.Contains(Level.Tag))
		{
			Context.AddError(FText::Format(LOCTEXT("BadTag", "난이도 단계 «{0}» 의 Tag 가 비었거나 다른 단계와 같습니다."), Level.DisplayName));
			Result = EDataValidationResult::Invalid;
		}

		Seen.Add(Level.Tag);
		// 0 은 피해 없음이라 반응, 섬광, 추가 효과까지 꺼진다 - 난이도가 할 일이 아니다. NaN 은 음수 검사를 지나므로 따로 본다.
		if (!VBDifficultyRules::IsValidScale(Level.PlayerDamageTakenScale) || !VBDifficultyRules::IsValidScale(Level.EnemyDamageTakenScale))
		{
			Context.AddError(FText::Format(LOCTEXT("BadScale", "난이도 단계 «{0}» 의 피해 배수는 0 보다 큰 유한한 수여야 합니다."), Level.DisplayName));
			Result = EDataValidationResult::Invalid;
		}
	}

	if (!FindLevel(DefaultLevel))
	{
		Context.AddError(LOCTEXT("NoDefault", "DefaultLevel 이 Levels 에 없습니다. 옛 슬롯과 새 게임의 첫 선택이 단계를 못 찾아 배수 1 로 떨어집니다."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
