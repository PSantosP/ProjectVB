// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_LightAttack.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBWeaponDefinition.h"

UVBGA_LightAttack::UVBGA_LightAttack()
{
	// 이 GA 의 이름표. 전투 규칙 표가 이것으로 막고 끊는다 - 빠지면 조용히 실패하므로 BP 가 아니라 여기 심는다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Light));

	InputTag = VBGameplayTags::Input_Attack_Light;
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking_Light);
}

UVBAttackConfig* UVBGA_LightAttack::SelectWeaponLeaf(const UVBWeaponDefinition& Weapon) const
{
	return Weapon.GetLightAttack();
}
