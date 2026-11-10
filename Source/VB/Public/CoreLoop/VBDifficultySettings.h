// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBDifficultySettings.generated.h"

class UVBDifficultyConfig;

/**
 * 난이도 단계 목록 자산을 가리키는 프로젝트 전역 설정(Project Settings > Game > VB Difficulty).
 * GameState 가 아닌 이유: 게임 GameState 가 없는 메인 메뉴도 목록을 읽는다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Difficulty"))
class VB_API UVBDifficultySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 비면 난이도 없는 게임과 같다(배수 1). LoadConfig 가 경고한다.
	UPROPERTY(config, EditAnywhere, Category="VB|Difficulty")
	TSoftObjectPtr<UVBDifficultyConfig> Config;

	// 목록 자산을 읽는 유일한 길. 비었거나 못 읽으면 nullptr. 처음 쓸 때 동기 로드한다(작은 자산이다).
	static const UVBDifficultyConfig* LoadConfig(bool bWarnIfMissing = true);
};
