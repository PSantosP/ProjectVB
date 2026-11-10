// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Settings/VBOptionsDraft.h"

#include "GameFramework/GameUserSettings.h"

namespace
{
	struct FQualityAccess
	{
		int32 (UGameUserSettings::*Get)() const;
		void (UGameUserSettings::*Set)(int32);
	};

	// EVBQualityGroup 순서와 같다.
	const FQualityAccess GQualityAccess[] =
	{
		{ &UGameUserSettings::GetViewDistanceQuality, &UGameUserSettings::SetViewDistanceQuality },
		{ &UGameUserSettings::GetAntiAliasingQuality, &UGameUserSettings::SetAntiAliasingQuality },
		{ &UGameUserSettings::GetShadowQuality, &UGameUserSettings::SetShadowQuality },
		{ &UGameUserSettings::GetGlobalIlluminationQuality, &UGameUserSettings::SetGlobalIlluminationQuality },
		{ &UGameUserSettings::GetReflectionQuality, &UGameUserSettings::SetReflectionQuality },
		{ &UGameUserSettings::GetPostProcessingQuality, &UGameUserSettings::SetPostProcessingQuality },
		{ &UGameUserSettings::GetTextureQuality, &UGameUserSettings::SetTextureQuality },
		{ &UGameUserSettings::GetVisualEffectQuality, &UGameUserSettings::SetVisualEffectQuality },
		{ &UGameUserSettings::GetFoliageQuality, &UGameUserSettings::SetFoliageQuality },
		{ &UGameUserSettings::GetShadingQuality, &UGameUserSettings::SetShadingQuality },
	};
	static_assert(UE_ARRAY_COUNT(GQualityAccess) == static_cast<int32>(EVBQualityGroup::Count), "quality group table out of sync");
}

FVBMachineSettings FVBMachineSettings::Read(const UGameUserSettings& Settings)
{
	FVBMachineSettings Result;
	Result.Resolution = Settings.GetScreenResolution();
	Result.WindowMode = Settings.GetFullscreenMode();
	Result.bVSync = Settings.IsVSyncEnabled();
	Result.FrameRateLimit = Settings.GetFrameRateLimit();
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GQualityAccess); ++Index)
	{
		Result.Quality[Index] = (Settings.*GQualityAccess[Index].Get)();
	}

	float Normalized = 0.0f, MinScale = 0.0f, MaxScale = 0.0f;
	Settings.GetResolutionScaleInformationEx(Normalized, Result.ResolutionQuality, MinScale, MaxScale);
	return Result;
}

void FVBMachineSettings::Write(UGameUserSettings& Settings) const
{
	Settings.SetScreenResolution(Resolution);
	Settings.SetFullscreenMode(WindowMode);
	Settings.SetVSyncEnabled(bVSync);
	Settings.SetFrameRateLimit(FrameRateLimit);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(GQualityAccess); ++Index)
	{
		(Settings.*GQualityAccess[Index].Set)(Quality[Index]);
	}
	// 해상도 다음에 건다 - 엔진이 지금 해상도로 원하는 화면 크기를 다시 센다(UGameUserSettings::SetResolutionScaleValueEx).
	Settings.SetResolutionScaleValueEx(ResolutionQuality);
}

int32 FVBMachineSettings::GetOverallQuality() const
{
	for (int32 Index = 1; Index < Quality.Num(); ++Index)
	{
		if (Quality[Index] != Quality[0])
		{
			return INDEX_NONE;
		}
	}

	return Quality[0];
}

void FVBMachineSettings::SetOverallQuality(int32 Level)
{
	for (int32& Value : Quality)
	{
		Value = Level;
	}
}

bool FVBMachineSettings::ChangesVideoMode(const FVBMachineSettings& Other) const
{
	return Resolution != Other.Resolution || WindowMode != Other.WindowMode;
}

FVBMachineCommitPlan FVBMachineSettings::PlanCommit(const FVBMachineSettings& Before) const
{
	FVBMachineCommitPlan Plan;
	Plan.Saved = *this;
	Plan.Saved.Resolution = Before.Resolution;
	Plan.Saved.WindowMode = Before.WindowMode;
	Plan.bPreviewVideoMode = ChangesVideoMode(Before);
	Plan.bResetScreenPercentage = ResolutionQuality <= 0.0f;
	return Plan;
}

bool FVBMachineSettings::operator==(const FVBMachineSettings& Other) const
{
	return !ChangesVideoMode(Other) && bVSync == Other.bVSync && FrameRateLimit == Other.FrameRateLimit && Quality == Other.Quality
		&& FMath::IsNearlyEqual(ResolutionQuality, Other.ResolutionQuality);
}

bool FVBOptionsDraft::operator==(const FVBOptionsDraft& Other) const
{
	return Machine == Other.Machine
		&& Culture == Other.Culture
		&& Difficulty == Other.Difficulty
		&& FVBSettingsSaveData::StaticStruct()->CompareScriptStruct(&User, &Other.User, PPF_None);
}
