// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBWeaponStateRules.h"

EVBWeaponType VBWeaponOwnership::PickResummon(EVBWeaponType Current, EVBWeaponType LastUsed, EVBWeaponType Default,
	const TArray<EVBWeaponType>& Owned)
{
	for (const EVBWeaponType Candidate : {Current, LastUsed, Default})
	{
		if (Candidate != EVBWeaponType::None && Owned.Contains(Candidate))
		{
			return Candidate;
		}
	}

	return Owned.IsEmpty() ? EVBWeaponType::None : Owned[0];
}

EVBWeaponType VBWeaponOwnership::PickPressDraw(EVBWeaponType Current, EVBWeaponType LastUsed, EVBWeaponType Default,
	const TArray<EVBWeaponType>& Owned)
{
	if (Current != EVBWeaponType::None)
	{
		return Current;
	}

	if (Default != EVBWeaponType::None && Owned.Contains(Default))
	{
		return Default;
	}

	return PickResummon(Current, LastUsed, Default, Owned);
}

EVBWeaponGrantResult VBWeaponOwnership::Grant(TArray<EVBWeaponType>& Owned, const TArray<EVBWeaponType>& Known, EVBWeaponType Weapon)
{
	// 모르는 무기를 가진 것으로 치면 꺼낼 때 데이터가 없어 맨손 소환이 된다 - 주지 않는다.
	if (Weapon == EVBWeaponType::None || !Known.Contains(Weapon))
	{
		return EVBWeaponGrantResult::Failed;
	}

	if (Owned.Contains(Weapon))
	{
		return EVBWeaponGrantResult::AlreadyOwned;
	}

	Owned.Add(Weapon);
	return EVBWeaponGrantResult::NewlyOwned;
}

bool VBWeaponOwnership::Revoke(TArray<EVBWeaponType>& Owned, EVBWeaponType Weapon)
{
	return Weapon != EVBWeaponType::None && Owned.Remove(Weapon) > 0;
}

EVBWeaponType VBWeaponStateRules::MMPoolForState(const FVBWeaponSummonData& Data, EVBLocomotionState State)
{
	// 상태 세 개는 고정된 구조라 늘어나지 않는다 - 여기서 갈라도 무기 추가에 끌려오지 않는다.
	switch (State)
	{
	case EVBLocomotionState::ArmedCombat: return Data.ArmedMMPool;
	case EVBLocomotionState::CombatSheathed: return Data.SheathedMMPool;
	default: return EVBWeaponType::None; // 평상(무기 없음)
	}
}
