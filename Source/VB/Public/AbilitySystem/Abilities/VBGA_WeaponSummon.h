// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "VBGA_WeaponSummon.generated.h"

/**
 * 무기 소환/수납 GA. Input_WeaponSummon 으로 트리거되어 현재 무기 상태에 따라 소환 또는 수납한다.
 * 캐릭터가 무기 상태 컴포넌트를 직접 조작하지 않고 이 GA 를 거친다.
 */
UCLASS()
class VB_API UVBGA_WeaponSummon : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_WeaponSummon();

protected:
	// 무장 상태에 따라 Summon / Sheathe 로 갈라 실행하고, 어느 경로든 EndAbility 한다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
};
