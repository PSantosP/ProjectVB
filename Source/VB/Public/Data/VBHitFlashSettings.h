// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBHitFlashSettings.generated.h"

class UVBHitDistortionConfig;
class UVBHitFlashConfig;

/**
 * 피격 플래시의 프로젝트 전역 기본값(Project Settings > Game > VB Hit Flash).
 * 새 적도 배선 없이 플래시가 나게 한다 - 컴포넌트의 HitFlashConfig 는 "이 적만 다르게" 하는 자리다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Hit Flash"))
class VB_API UVBHitFlashSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 컴포넌트에 명시된 설정이 없을 때 쓸 플래시 자산. 비워 두면 피격 플래시가 프로젝트 전체에서 꺼진다.
	UPROPERTY(config, EditAnywhere, Category="VB|GameFeel")
	TSoftObjectPtr<UVBHitFlashConfig> DefaultConfig;

	// 타격 자리 공간 왜곡의 데이터. 플래시와 같은 자리, 같은 조건에서 난다. 비워 두면 왜곡이 꺼진다.
	UPROPERTY(config, EditAnywhere, Category="VB|GameFeel")
	TSoftObjectPtr<UVBHitDistortionConfig> DistortionConfig;

	// 쓸 플래시 설정을 고르는 유일한 자리. Explicit, 없으면 DefaultConfig, 둘 다 없으면 nullptr(연출 끔).
	static UVBHitFlashConfig* ResolveConfig(UVBHitFlashConfig* Explicit);
};
