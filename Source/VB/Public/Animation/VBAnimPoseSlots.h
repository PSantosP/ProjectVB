// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/VBWeaponStateTypes.h"

// 애님 그래프의 Blend Poses by int 핀 번호를 정하고, 슬롯 표에 없는 무기를 한 번씩 경고한다. 게임 스레드에서만 쓴다.
struct VB_API FVBAnimPoseSlotLookup
{
	// 로코모션 갈래 핀 번호를 정한다.
	static int32 ResolveLocomotionPin(EVBLocomotionState LocomotionState, bool bCrouching, EVBWeaponType MMPool);
	// 이 핀이 스테이트머신 경로인가.
	static bool IsStateMachinePin(int32 LocomotionPin);

	// 무기 -> BlendListByInt 핀 번호. 미등록은 0 + 1회 경고.
	int32 ResolveWeaponPin(const TArray<EVBWeaponType>& Slots, EVBWeaponType Weapon);
	// 무기 -> crouch 핀 번호. 미등록은 0 + 1회 경고.
	int32 ResolveCrouchPin(const TArray<EVBWeaponType>& Slots, EVBWeaponType Weapon);

private:
	// 마지막으로 경고한 무기(같은 경고 반복 방지).
	EVBWeaponType LastWarnedPoseSlotWeapon = EVBWeaponType::None;
	EVBWeaponType LastWarnedCrouchSlotWeapon = EVBWeaponType::None;
};
