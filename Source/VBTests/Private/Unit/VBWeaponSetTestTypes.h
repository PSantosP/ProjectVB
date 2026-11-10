// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBInputDispatchFixtureAbilities.h" // UVBInputFixtureAbility - stays active until something ends it
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "GameplayEffect.h"
#include "VBWeaponSetTestTypes.generated.h"

// VB.Unit.Weapon.Set: 무기 세트가 주는 약공격을 입력과 정체로만 둔다 - combat rule table 과 press draw 는 이 둘만 읽는다.
UCLASS(NotBlueprintable)
class UVBWeaponSetTestAttack : public UVBInputFixtureAbility
{
	GENERATED_BODY()

public:
	UVBWeaponSetTestAttack()
	{
		InputTag = VBGameplayTags::Input_Attack_Light;
		SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Light));
	}
};

// VB.Unit.Weapon.Stats: 실제 자산 모양의 무기 강화 effect - infinite 이고, 무기 상태가 넘긴 SetByCaller 값으로 WeaponDamageScale 을 덮는다.
UCLASS(NotBlueprintable)
class UVBWeaponSetTestEnhancementEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVBWeaponSetTestEnhancementEffect()
	{
		DurationPolicy = EGameplayEffectDurationType::Infinite;
		FGameplayModifierInfo Modifier;
		Modifier.Attribute = UVBCombatAttributeSet::GetWeaponDamageScaleAttribute();
		Modifier.ModifierOp = EGameplayModOp::Override;
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = VBGameplayTags::Data_WeaponDamageScale;
		Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		Modifiers.Add(Modifier);
	}
};
