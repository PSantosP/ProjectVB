// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Enhancement/VBEnhancementSettings.h"

#include "Inventory/Enhancement/VBEnhancementConfig.h"
#include "VB/VB.h"

const UVBEnhancementConfig* UVBEnhancementSettings::LoadConfig(bool bWarnIfMissing)
{
	const UVBEnhancementSettings* Settings = GetDefault<UVBEnhancementSettings>();
	const UVBEnhancementConfig* Config = Settings ? Settings->Config.LoadSynchronous() : nullptr;
	// 한 번만 말한다 - 비어 있으면 모든 강화 배율이 1 이고 강화 요청이 거절된다는 뜻이 이 한 줄로 충분하다.
	static bool bWarned = false;
	if (!Config && bWarnIfMissing && !bWarned)
	{
		bWarned = true;
		VB_LOG(Warning, "강화 설정 자산이 없다(Project Settings > VB Enhancement > Config = %s) - 강화 배율 1, 강화 요청은 거절된다",
			Settings ? *Settings->Config.ToString() : TEXT("설정 없음"));
	}

	return Config;
}
