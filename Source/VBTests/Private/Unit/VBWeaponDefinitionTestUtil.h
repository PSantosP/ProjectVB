// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/VBAbilitySet.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "UObject/UnrealType.h"

// UVBWeaponDefinition 시험 지원 - 에디터, 파이썬이 쓰는 reflected field 로 transient 정의를 채우고 무기 상태가 그것을 가리키게 한다.
//   제품 헤더에 시험용 접근을 두지 않으려고 따로 있다.
namespace VBWeaponDefinitionTest
{
	template <typename T>
	void SetField(UObject& Object, const TCHAR* Name, const T& Value)
	{
		FProperty* Property = FindFProperty<FProperty>(Object.GetClass(), Name);
		check(Property && Property->GetElementSize() == sizeof(T));
		*Property->ContainerPtrToValuePtr<T>(&Object) = Value;
	}

	inline UVBWeaponDefinition* Make(UObject* Outer, EVBWeaponType Type)
	{
		UVBWeaponDefinition* Definition = NewObject<UVBWeaponDefinition>(Outer);
		SetField(*Definition, TEXT("WeaponType"), Type);
		SetField(*Definition, TEXT("DisplayName"), StaticEnum<EVBWeaponType>()->GetDisplayNameTextByValue(static_cast<int64>(Type)));
		return Definition;
	}

	// 데이터 자산처럼 무기의 ability set 을 만든다 - 이 능력들만, 효과 없음
	inline UVBAbilitySet* GiveSet(UVBWeaponDefinition& Definition, const TArray<TSubclassOf<UGameplayAbility>>& Abilities)
	{
		UVBAbilitySet* Set = NewObject<UVBAbilitySet>(&Definition);
		TArray<FVBAbilitySetAbility> Entries;
		for (const TSubclassOf<UGameplayAbility>& Ability : Abilities)
		{
			Entries.Add({Ability, 1});
		}

		SetField(*Set, TEXT("GrantedAbilities"), Entries);
		SetField(Definition, TEXT("AbilitySet"), TObjectPtr<const UVBAbilitySet>(Set));
		return Set;
	}

	// 에디터 목록, 소환처럼 무기 상태에 이 정의들을 주고 하나를 고른다
	inline void Equip(UVBWeaponStateComponent& Weapons, const TArray<TObjectPtr<const UVBWeaponDefinition>>& Definitions,
		EVBWeaponType Selected)
	{
		SetField(Weapons, TEXT("WeaponDefinitions"), Definitions);
		SetField(Weapons, TEXT("CurrentWeaponType"), Selected);
	}
}
