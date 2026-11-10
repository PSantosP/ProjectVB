#include "Enemy/VBKnockdownTagPolicy.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"

void VBKnockdownTagPolicy::Grant(UAbilitySystemComponent& ASC)
{
	ASC.AddLooseGameplayTag(VBGameplayTags::State_Combat_Downed, 1, EGameplayTagReplicationState::CountToOwner);
}

void VBKnockdownTagPolicy::Release(UAbilitySystemComponent& ASC)
{
	ASC.RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Downed, 1, EGameplayTagReplicationState::CountToOwner);
}
