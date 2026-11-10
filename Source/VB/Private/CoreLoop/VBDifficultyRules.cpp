// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CoreLoop/VBDifficultyRules.h"

#include "CoreLoop/VBDifficultyConfig.h"

float VBDifficultyRules::ResolveDamageScale(const UVBDifficultyConfig* Config, FGameplayTag Level, bool bVictimIsPlayer,
	bool bFinisherDamage)
{
	if (bFinisherDamage || !Config)
	{
		return 1.0f;
	}

	const FVBDifficultyLevel* Resolved = Config->FindLevelOrDefault(Level);
	if (!Resolved)
	{
		return 1.0f;
	}

	const float Scale = bVictimIsPlayer ? Resolved->PlayerDamageTakenScale : Resolved->EnemyDamageTakenScale;
	return IsValidScale(Scale) ? Scale : 1.0f;
}

bool VBDifficultyRules::IsValidScale(float Scale)
{
	return FMath::IsFinite(Scale) && Scale > 0.0f;
}

FGameplayTag VBDifficultyRules::NormalizeLevel(const UVBDifficultyConfig* Config, FGameplayTag Level)
{
	const FVBDifficultyLevel* Resolved = Config ? Config->FindLevelOrDefault(Level) : nullptr;
	return Resolved ? Resolved->Tag : FGameplayTag();
}

#define LOCTEXT_NAMESPACE "VBDifficultyRules"
FText VBDifficultyRules::DescribeLevel(const FVBDifficultyLevel& Level)
{
	FNumberFormattingOptions Format;
	Format.MinimumFractionalDigits = 0;
	Format.MaximumFractionalDigits = 2;
	return FText::Format(LOCTEXT("Describe", "받는 피해 {0}배 · 주는 피해 {1}배"),
		FText::AsNumber(Level.PlayerDamageTakenScale, &Format), FText::AsNumber(Level.EnemyDamageTakenScale, &Format));
}
#undef LOCTEXT_NAMESPACE
