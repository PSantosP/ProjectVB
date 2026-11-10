// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "AbilitySystem/VBAbilitySet.h"
#include "UObject/UnrealType.h"
#include "VBAbilitySetTestEffects.generated.h"

// UVBAbilitySet 시험 지원 - modifier 없는 효과 둘(남아서 영수증에 뗄 handle 을 두는 것, 실행만 하고 세기만 하는 것)과
//   자산 없이 채우는 transient set.
UCLASS(NotBlueprintable)
class UVBAbilitySetTestInfiniteEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVBAbilitySetTestInfiniteEffect() { DurationPolicy = EGameplayEffectDurationType::Infinite; }
};

UCLASS(NotBlueprintable)
class UVBAbilitySetTestInstantEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVBAbilitySetTestInstantEffect() { DurationPolicy = EGameplayEffectDurationType::Instant; }
};

namespace VBAbilitySetTest
{
	inline FVBAbilitySetAbility Ability(TSubclassOf<UGameplayAbility> Class, int32 Level = 1)
	{
		FVBAbilitySetAbility Entry;
		Entry.Ability = Class;
		Entry.Level = Level;
		return Entry;
	}

	inline FVBAbilitySetEffect Effect(TSubclassOf<UGameplayEffect> Class, float Level = 1.0f)
	{
		FVBAbilitySetEffect Entry;
		Entry.Effect = Class;
		Entry.Level = Level;
		return Entry;
	}

	// 에디터, 파이썬이 쓰는 reflected field 로 set 을 채운다 - 제품 헤더에 시험용 접근을 두지 않는다
	inline void Fill(UVBAbilitySet& Set, TArray<FVBAbilitySetAbility> Abilities, TArray<FVBAbilitySetEffect> Effects)
	{
		const FArrayProperty* AbilitiesProp = FindFProperty<FArrayProperty>(UVBAbilitySet::StaticClass(), TEXT("GrantedAbilities"));
		const FArrayProperty* EffectsProp = FindFProperty<FArrayProperty>(UVBAbilitySet::StaticClass(), TEXT("GrantedEffects"));
		check(AbilitiesProp && EffectsProp);
		*AbilitiesProp->ContainerPtrToValuePtr<TArray<FVBAbilitySetAbility>>(&Set) = MoveTemp(Abilities);
		*EffectsProp->ContainerPtrToValuePtr<TArray<FVBAbilitySetEffect>>(&Set) = MoveTemp(Effects);
	}

	inline UVBAbilitySet* MakeSet(UObject* Outer, TArray<FVBAbilitySetAbility> Abilities, TArray<FVBAbilitySetEffect> Effects)
	{
		UVBAbilitySet* Set = NewObject<UVBAbilitySet>(Outer);
		Fill(*Set, MoveTemp(Abilities), MoveTemp(Effects));
		return Set;
	}
}
