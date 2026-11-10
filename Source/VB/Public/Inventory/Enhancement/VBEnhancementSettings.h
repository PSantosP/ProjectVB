// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBEnhancementSettings.generated.h"

class UVBEnhancementConfig;

/** 강화 수치 자산을 가리키는 프로젝트 전역 설정(Project Settings > Game > VB Enhancement). */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Enhancement"))
class VB_API UVBEnhancementSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 비면 강화가 없는 게임과 같다(배율 1, 강화 요청은 거절) - 경고로 말한다.
	UPROPERTY(config, EditAnywhere, Category="VB|Enhancement")
	TSoftObjectPtr<UVBEnhancementConfig> Config;

	// 수치 자산을 읽는 유일한 길. 비었거나 못 읽으면 nullptr.
	static const UVBEnhancementConfig* LoadConfig(bool bWarnIfMissing = true);
};
