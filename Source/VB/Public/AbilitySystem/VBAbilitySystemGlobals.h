// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemGlobals.h"
#include "VBAbilitySystemGlobals.generated.h"

/**
 * 모든 효과 컨텍스트를 FVBGameplayEffectContext 로 만든다.
 * DefaultGame.ini 의 [/Script/GameplayAbilities.AbilitySystemGlobals] 섹션이 가리킨다(개발자 설정 섹션에 쓰면 조용히 무시된다).
 */
UCLASS()
class VB_API UVBAbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_BODY()

public:
	virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;
};
