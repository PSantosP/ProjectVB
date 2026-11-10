// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "CoreLoop/VBDifficultySettings.h"

#include "CoreLoop/VBDifficultyConfig.h"
#include "VB/VB.h"

const UVBDifficultyConfig* UVBDifficultySettings::LoadConfig(bool bWarnIfMissing)
{
	const UVBDifficultySettings* Settings = GetDefault<UVBDifficultySettings>();
	const UVBDifficultyConfig* Config = Settings ? Settings->Config.LoadSynchronous() : nullptr;
	// 한 번만 경고한다 - 난이도 상태 컴포넌트가 월드마다 읽고, 비어 있으면 모든 피해가 배수 1 이라는 뜻이 이 한 줄로 충분하다.
	static bool bWarned = false;
	if (!Config && bWarnIfMissing && !bWarned)
	{
		bWarned = true;
		VB_LOG(Warning, "난이도 설정 자산이 없다(Project Settings > VB Difficulty > Config = %s) - 모든 피해가 배수 1(난이도 없음)로 들어간다",
			Settings ? *Settings->Config.ToString() : TEXT("설정 없음"));
	}

	return Config;
}
