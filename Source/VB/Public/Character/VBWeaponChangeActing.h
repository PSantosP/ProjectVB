// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/VBWeaponStateTypes.h"

class AVBCharacter;
class UVBWeaponDefinition;

// 무기 바꾸기 연기 한 번의 내용. 수납이면 ToType 이 None 이고, bCombatStance 는 바꾸기 전에 무기를 들었는지다.
struct FVBWeaponChangeRequest
{
	EVBWeaponType FromType = EVBWeaponType::None;
	EVBWeaponType ToType = EVBWeaponType::None;
	bool bCombatStance = false;
};

// 무기 바꾸기의 몽타주와 소환 VFX 를 재생한다. 코스메틱 전용이라 게임 상태를 바꾸지 않는다.
namespace VBWeaponChangeActing
{
	// 연기 데이터를 낼 무기: 수납이면 사라지는 무기, 그 밖에는 들어오는 무기.
	EVBWeaponType GetDataWeapon(const FVBWeaponChangeRequest& Request);

	// Definition 이 없으면 무기 외형 전환만 알리고 끝낸다. MaxFullBodySpeed 를 넘으면 전신 몽타주를 건너뛴다.
	void Play(AVBCharacter& Character, const UVBWeaponDefinition* Definition, const FVBWeaponChangeRequest& Request,
		float MaxFullBodySpeed);
}
