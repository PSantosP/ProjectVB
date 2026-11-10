// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Animation/VBAnimPoseSlots.h"

#include "UObject/Class.h"
#include "VB/VB.h"

namespace
{
	// ABP 의 Blend Poses by int 핀 번호. ABP 핀 순서를 바꾸면 이 enum 도 같이 고쳐야 한다.
	// 0/2 는 EVBLocomotionState 값과 일치해야 한다.
	enum class EVBLocomotionPin : int32
	{
		UnarmedMM = 0, // 비무장 MM
		CombatMM = 1, // 전투 MM (stand)
		ArmedSM = 2, // 상태3 비-Fighter 스테이트머신
		CrouchSM = 3, // crouch BS/SM
	};
}

int32 FVBAnimPoseSlotLookup::ResolveLocomotionPin(EVBLocomotionState LocomotionState, bool bCrouching, EVBWeaponType MMPool)
{
	if (bCrouching)
	{
		// 모든 crouch 는 SM/BS(pin3). crouch 클립이 루프+Idle 뿐이라 BS 보간이 맞다.
		return static_cast<int32>(EVBLocomotionPin::CrouchSM);
	}

	if (MMPool != EVBWeaponType::None)
	{
		// 전투 MM(stand)
		return static_cast<int32>(EVBLocomotionPin::CombatMM);
	}

	return static_cast<int32>(LocomotionState);
}

bool FVBAnimPoseSlotLookup::IsStateMachinePin(int32 LocomotionPin)
{
	// SM 경로 = pin2, pin3. MM 핀(0, 1)과 회전 흡수 게이트가 갈린다.
	return LocomotionPin >= static_cast<int32>(EVBLocomotionPin::ArmedSM);
}

int32 FVBAnimPoseSlotLookup::ResolveWeaponPin(const TArray<EVBWeaponType>& Slots, EVBWeaponType Weapon)
{
	// 맨손은 0 번 핀. 로스터에 None 항목이 없어 먼저 거른다(안 그러면 미등록 경고가 뜬다).
	if (Weapon == EVBWeaponType::None)
	{
		return 0;
	}

	const int32 Found = Slots.IndexOfByKey(Weapon);
	if (Found != INDEX_NONE)
	{
		return Found;
	}

	if (LastWarnedPoseSlotWeapon != Weapon)
	{
		LastWarnedPoseSlotWeapon = Weapon;
		VB_LOG(Warning, "무기 %s 의 포즈 핀 미등록 - 인덱스 0 재생. ABP_VBCharacter 의 WeaponPoseSlots 와 "
			"BlendListByInt 핀 개수를 확인하라",
			*UEnum::GetValueAsString(Weapon));
	}

	return 0;
}

int32 FVBAnimPoseSlotLookup::ResolveCrouchPin(const TArray<EVBWeaponType>& Slots, EVBWeaponType Weapon)
{
	// crouch 핀 공간 전용. 인덱스 0 이 자기 콘텐츠(납도 공용 ver_A)를 가지므로 로스터에 None 항목이 있다.
	const int32 Found = Slots.IndexOfByKey(Weapon);
	if (Found != INDEX_NONE)
	{
		return Found;
	}

	if (LastWarnedCrouchSlotWeapon != Weapon)
	{
		LastWarnedCrouchSlotWeapon = Weapon;
		VB_LOG(Warning, "무기 %s 의 crouch 포즈 핀 미등록 - 인덱스 0(공용) 재생. ABP_VBCharacter 의 "
			"CrouchPoseSlots 를 확인하라",
			*UEnum::GetValueAsString(Weapon));
	}

	return 0;
}
