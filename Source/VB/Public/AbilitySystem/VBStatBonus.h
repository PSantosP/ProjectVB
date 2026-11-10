// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ActiveGameplayEffectHandle.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"

class UAbilitySystemComponent;
class UGameplayEffect;

/**
 * 능력치 덧셈 효과(무한 GE) 하나를 건다 - 장비와 성장이 같이 쓴다. SetByCaller 를 지도 값으로 채우고 없는 능력치는 0.
 * GE 가 받지 않는 지도 항목은 경고한다. 실패하면 무효 핸들.
 */
namespace VBStatBonus
{
	VB_API FActiveGameplayEffectHandle Apply(UAbilitySystemComponent* ASC, TSubclassOf<UGameplayEffect> Effect,
		const TMap<FGameplayTag, float>& Stats, const FString& SourceName);
}
