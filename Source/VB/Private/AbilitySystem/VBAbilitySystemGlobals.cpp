// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBAbilitySystemGlobals.h"

#include "AbilitySystem/VBGameplayEffectContext.h"

FGameplayEffectContext* UVBAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FVBGameplayEffectContext();
}
