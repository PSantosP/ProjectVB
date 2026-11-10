// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "VBGA_Sprint.generated.h"

/**
 * 스프린트 어빌리티(누르는 동안). 켜면 StartSprint, 끝나면 StopSprint 로 속도, 회전 모드를 되돌린다.
 * 막는 상태는 전투 규칙 표(DA_CombatRules)에 있다.
 */
UCLASS(Abstract)
class VB_API UVBGA_Sprint : public UVBGameplayAbility
{
	GENERATED_BODY()
	
public:
	UVBGA_Sprint();
	
private:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
