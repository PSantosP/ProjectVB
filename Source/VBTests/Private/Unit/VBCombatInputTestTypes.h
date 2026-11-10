// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "VBPawnExtensionTestTypes.h"
#include "VBCombatInputTestTypes.generated.h"

// 실제 ASC 입력 경로에서 활성화와 후속 누름을 기록하는 공격 대역이다.
UCLASS(NotBlueprintable)
class UVBCombatInputTestAttackAbility : public UVBPawnExtensionTestHeldAbility
{
	GENERATED_BODY()

public:
	// 원격 플레이어의 서버 경로만 검사한다.
	UVBCombatInputTestAttackAbility()
	{
		SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Light));
		InputTag = VBGameplayTags::Input_Attack_Light;
		NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	}

	int32 InputPressCount = 0;

protected:
	virtual void InputPressed(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override
	{
		Super::InputPressed(Handle, ActorInfo, ActivationInfo);
		++InputPressCount;
	}
};

// 공중 추격과 같은 취소 관계를 쓰는 입력 후보다.
UCLASS(NotBlueprintable)
class UVBCombatInputTestAirAbility : public UVBCombatInputTestAttackAbility
{
	GENERATED_BODY()
public:
	UVBCombatInputTestAirAbility() { SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Air)); }
};

// 활성 공격을 취소할 권한이 없는 같은 입력 후보다.
UCLASS(NotBlueprintable)
class UVBCombatInputTestNonReplacingAbility : public UVBCombatInputTestAttackAbility
{
	GENERATED_BODY()
public:
	UVBCombatInputTestNonReplacingAbility() { SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Riposte)); }
};
