// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBEconomySettings.generated.h"

class UVBEconomyConfig;

/** 경제 설정 자산을 가리키는 프로젝트 전역 설정(Project Settings > Game > VB Economy). 돈은 프로젝트에 하나뿐이다. */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Economy"))
class VB_API UVBEconomySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 비면 상점이 열리지 않는다 - 돈을 모르면 거래를 판정할 수 없다(조용히 공짜로 떨어지지 않게).
	UPROPERTY(config, EditAnywhere, Category="VB|Economy")
	TSoftObjectPtr<UVBEconomyConfig> Economy;

	// 경제 자산을 읽는 유일한 길(처음 쓸 때 동기 로드). 비었거나 못 읽으면 nullptr.
	static const UVBEconomyConfig* LoadEconomy(bool bWarnIfMissing = true);
};
