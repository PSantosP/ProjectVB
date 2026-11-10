// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBGA_GuardShield.h"
#include "AbilitySystem/VBGameplayTags.h"

UVBGA_GuardShield::UVBGA_GuardShield()
{
	FGameplayTagContainer Identity = GetAssetTags();
	Identity.AddTag(VBGameplayTags::Ability_Defense_Guard_Shield);
	SetAssetTags(Identity);
}
