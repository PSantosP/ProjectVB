// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "GameplayEffect.h"
#include "VBTestAbilityTags.h"
#include "VBPawnExtensionTestTypes.generated.h"

// ExecuteAbility 뒤 누가 취소할 때까지 살아 있다 - 옛 몸이 돌리던 공격, 처형을 대신한다.
UCLASS(NotBlueprintable)
class UVBPawnExtensionTestHeldAbility : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBPawnExtensionTestHeldAbility() { SetAssetTags(FGameplayTagContainer(VBTestAbilityTags::AnyAbility)); }

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
		const FGameplayAbilityActivationInfo, const FGameplayEventData*) override {}
};

// OnSpawn 정책: 부여 시 활성화되고 PlayerState 부여만큼 산다. 몸 교체가 취소하면 안 된다.
UCLASS(NotBlueprintable)
class UVBPawnExtensionTestPassiveAbility : public UVBPawnExtensionTestHeldAbility
{
	GENERATED_BODY()

public:
	UVBPawnExtensionTestPassiveAbility() { ActivationPolicy = EVBAbilityActivationPolicy::OnSpawn; }
};

// 시작 효과 대역. 적용 횟수를 ASC 에서 셀 수 있게 Infinite 다(instant 는 셀 것이 안 남는다).
UCLASS(NotBlueprintable)
class UVBPawnExtensionTestStartupEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVBPawnExtensionTestStartupEffect() { DurationPolicy = EGameplayEffectDurationType::Infinite; }
};
