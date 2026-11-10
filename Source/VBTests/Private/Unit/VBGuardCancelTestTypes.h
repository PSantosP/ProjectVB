// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBInputDispatchFixtureAbilities.h" // UVBInputFixtureAbility - stays active until something ends it
#include "VBGuardCancelTestTypes.generated.h"

// VB.Unit.Combat.GuardCancelsAttack 용 취소될 때까지 도는 휘두르기. asset tag 와 공격 상태가 실제 공격과 같다
//   가드의 취소가 닿는지는 asset tag 만으로 정해진다
UCLASS(NotBlueprintable)
class UVBGuardCancelLightSwing : public UVBInputFixtureAbility
{
	GENERATED_BODY()

public:
	UVBGuardCancelLightSwing()
	{
		SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Light));
		ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	}
};

UCLASS(NotBlueprintable)
class UVBGuardCancelRiposteSwing : public UVBInputFixtureAbility
{
	GENERATED_BODY()

public:
	UVBGuardCancelRiposteSwing()
	{
		SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Riposte));
		ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	}
};
