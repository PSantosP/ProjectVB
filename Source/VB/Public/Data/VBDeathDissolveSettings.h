// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBDeathDissolveSettings.generated.h"

class UVBDeathDissolveConfig;

/**
 * 사망 디졸브의 프로젝트 전역 기본값(Project Settings > Game > VB Death Dissolve).
 * 모든 적이 배선 없이 디졸브를 쓰게 한다 - 적 설정은 이 적만 다르게 하는 자리다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Death Dissolve"))
class VB_API UVBDeathDissolveSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 적 설정이 비어 있을 때 쓸 디졸브 자산. 비우면 프로젝트 전체에서 꺼진다.
	// Soft 인 이유: 죽을 때 처음 필요해 부팅 때 파편 Niagara 까지 끌어오지 않는다.
	UPROPERTY(config, EditAnywhere, Category="VB|GameFeel")
	TSoftObjectPtr<UVBDeathDissolveConfig> DefaultConfig;

	// 쓸 디졸브 설정을 고르는 유일한 자리. Explicit 이 없으면 DefaultConfig, 둘 다 없으면 nullptr(연출 끔).
	// 읽는 곳이 여럿이라 폴백을 여기 하나에 둔다.
	static UVBDeathDissolveConfig* ResolveConfig(UVBDeathDissolveConfig* Explicit);
};
