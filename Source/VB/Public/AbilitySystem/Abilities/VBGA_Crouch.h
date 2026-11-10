// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "VBGA_Crouch.generated.h"

/**
 * 앉기 토글 어빌리티. 켜면 Crouch, 끝나면 UnCrouch. 두 번째 입력이 토글로 취소한다.
 * 막는 상태는 전투 규칙 표의 Ability.Locomotion.Crouch 칸이고, 일어서기는 AVBCharacter 가 같은 표에 묻는다.
 */
UCLASS(Abstract)
class VB_API UVBGA_Crouch : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	UVBGA_Crouch();

private:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
