// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBWeaponDefinitionList.h"

#include "Data/VBFinisherConfig.h"
#include "Data/VBWeaponDefinition.h"
#include "GameFramework/Actor.h"
#include "VB/VB.h"

namespace
{
	bool IsOwned(const TArray<EVBWeaponType>& Owned, EVBWeaponType Weapon)
	{
		return Weapon != EVBWeaponType::None && Owned.Contains(Weapon);
	}
}

const UVBWeaponDefinition* VBWeaponDefinitionList::Find(const FDefinitions& Definitions, EVBWeaponType Weapon)
{
	if (Weapon == EVBWeaponType::None)
	{
		return nullptr;
	}

	for (const UVBWeaponDefinition* Definition : Definitions)
	{
		if (Definition && Definition->GetWeaponType() == Weapon)
		{
			return Definition;
		}
	}

	return nullptr;
}

const UVBWeaponDefinition* VBWeaponDefinitionList::FindForSlot(const FDefinitions& Definitions, const TArray<EVBWeaponType>& Owned,
	int32 Slot, const AActor* LogOwner)
{
	if (Slot <= 0)
	{
		return nullptr;
	}

	const UVBWeaponDefinition* Found = nullptr;
	for (const UVBWeaponDefinition* Definition : Definitions)
	{
		if (!Definition || Definition->GetInputSlot() != Slot || !IsOwned(Owned, Definition->GetWeaponType()))
		{
			continue;
		}

		if (!Found)
		{
			Found = Definition;
			continue;
		}

		VB_LOG(Warning, "%s: 가진 무기 둘이 숫자 키 %d 에 있다(%s, %s) - 앞의 것을 꺼낸다", *GetNameSafe(LogOwner), Slot,
			*GetNameSafe(Found), *GetNameSafe(Definition));
	}

	return Found;
}

TArray<EVBWeaponType> VBWeaponDefinitionList::GetKnownWeapons(const FDefinitions& Definitions)
{
	TArray<EVBWeaponType> Known;
	for (const UVBWeaponDefinition* Definition : Definitions)
	{
		if (Definition)
		{
			Known.AddUnique(Definition->GetWeaponType());
		}
	}

	return Known;
}

void VBWeaponDefinitionList::CollectFinisherConfigs(const FDefinitions& Definitions, TArray<const UVBFinisherConfig*>& OutConfigs)
{
	OutConfigs.Reset();
	for (const UVBWeaponDefinition* Definition : Definitions)
	{
		if (!Definition)
		{
			continue;
		}

		for (const TObjectPtr<UVBFinisherConfig>& Config : Definition->GetSummonData().FinisherConfigs)
		{
			if (Config)
			{
				OutConfigs.AddUnique(Config);
			}
		}
	}
}

void VBWeaponDefinitionList::WarnAboutList(const FDefinitions& Definitions, const AActor* LogOwner)
{
	// 뒤의 같은 무기 정의는 Find 가 영영 찾지 않는다 - 데이터 실수가 조용히 묻히지 않게 한 번 말한다.
	TSet<EVBWeaponType> Seen;
	for (const UVBWeaponDefinition* Definition : Definitions)
	{
		bool bAlreadySeen = false;
		if (Definition)
		{
			Seen.Add(Definition->GetWeaponType(), &bAlreadySeen);
		}

		if (!Definition || Definition->GetWeaponType() == EVBWeaponType::None || bAlreadySeen)
		{
			VB_LOG(Warning, "%s: 무기 정의 목록(WeaponDefinitions)에 빈 칸 · 맨손 · 같은 무기 두 번 중 하나가 있다(%s) - 그 정의는 쓰이지 않는다",
				*GetNameSafe(LogOwner), *GetNameSafe(Definition));
		}
	}
}

void VBWeaponDefinitionList::WarnAboutSlotClash(const FDefinitions& Definitions, const TArray<EVBWeaponType>& Owned, const AActor* LogOwner)
{
	TMap<int32, const UVBWeaponDefinition*> Slots;
	for (const UVBWeaponDefinition* Definition : Definitions)
	{
		if (!Definition || Definition->GetInputSlot() == 0 || !IsOwned(Owned, Definition->GetWeaponType()))
		{
			continue;
		}

		if (const UVBWeaponDefinition* const* Other = Slots.Find(Definition->GetInputSlot()))
		{
			VB_LOG(Warning, "%s: 가진 무기 둘이 숫자 키 %d 를 쓴다(%s, %s) - 한 칸 무기는 하나를 거둔 뒤 다른 하나를 준다",
				*GetNameSafe(LogOwner), Definition->GetInputSlot(), *GetNameSafe(*Other), *GetNameSafe(Definition));
			continue;
		}

		Slots.Add(Definition->GetInputSlot(), Definition);
	}
}
