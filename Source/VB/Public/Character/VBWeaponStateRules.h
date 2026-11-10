// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/VBWeaponStateTypes.h"
#include "VBWeaponStateRules.generated.h"

// 무기 줍기의 결과 - 획득 효과(체크포인트, 대사, 사실)는 NewlyOwned 일 때만 건다.
UENUM()
enum class EVBWeaponGrantResult : uint8
{
	NewlyOwned,
	AlreadyOwned,
	Failed,
};

// 누름 발도의 결과. NotNow = 꺼낼 무기는 있지만 그 무기의 이 입력 능력이 지금 시작할 수 없다(경직, 회피 등) - 꺼내지 않는다.
enum class EVBPressDraw : uint8
{
	InHand,
	Drawn,
	NotNow,
	NoOwnedWeapon,
};

// 전투 이탈을 판단할 사실. 세계 질의와 타이머는 UVBWeaponStateComponent 가 맡고 여기에는 결과만 담는다.
struct FVBCombatExitFacts
{
	bool bLockedOn = false;
	bool bEnemiesNearby = false;
	bool bMoving = false;
};

// 가진 무기 판단 - 컴포넌트 상태와 떼어 둔 순수 함수. 단위 시험(VB.Unit.Weapon.Ownership)이 잰다.
namespace VBWeaponOwnership
{
	// 재발도할 무기: 선택 -> 마지막 사용 -> 기본 순서로 가진 것 중 첫째, 셋 다 안 가졌으면 가진 것의 첫째, 하나도 없으면 None.
	VB_API EVBWeaponType PickResummon(EVBWeaponType Current, EVBWeaponType LastUsed, EVBWeaponType Default,
		const TArray<EVBWeaponType>& Owned);

	// 칼집 누름 발도의 무기: 선택된 무기, 없으면 기본 무기, 기본 무기를 안 가졌으면 재발도 규칙.
	VB_API EVBWeaponType PickPressDraw(EVBWeaponType Current, EVBWeaponType LastUsed, EVBWeaponType Default,
		const TArray<EVBWeaponType>& Owned);

	// 주기. Known = 정의가 있는 무기(WeaponDefinitions). None, 모르는 무기는 Failed, 이미 있으면 AlreadyOwned(목록 그대로).
	VB_API EVBWeaponGrantResult Grant(TArray<EVBWeaponType>& Owned, const TArray<EVBWeaponType>& Known, EVBWeaponType Weapon);

	// 거두기. 가지고 있었으면 true.
	VB_API bool Revoke(TArray<EVBWeaponType>& Owned, EVBWeaponType Weapon);

	// 거둔 뒤의 선택된 무기 - 거둔 무기가 선택돼 있었으면(칼집에 넣은 채라도) 비운다. 거둔 정의를 계속 읽지 않게.
	inline EVBWeaponType SelectionAfterRevoke(EVBWeaponType Current, EVBWeaponType Revoked)
	{
		return Current == Revoked ? EVBWeaponType::None : Current;
	}
}

// 무기 상태의 전투 판단. 상태를 들지 않는 값 함수만 둔다.
namespace VBWeaponStateRules
{
	// 락온, 근처의 적, 이동이 모두 없으면 true - 전투를 떠나 평상으로 돌아간다.
	inline bool ShouldExitCombat(const FVBCombatExitFacts& Facts)
	{
		return !Facts.bLockedOn && !Facts.bEnemiesNearby && !Facts.bMoving;
	}

	// 평면 속도가 기준을 넘으면 true - 활동 중으로 보고 전투 이탈을 미룬다.
	inline bool IsActiveSpeed(double Speed2D, float IdleSpeedThreshold)
	{
		return Speed2D > IdleSpeedThreshold;
	}

	// 이 상태에서 쓸 로코모션 MM 풀. 평상은 None(기본 풀).
	VB_API EVBWeaponType MMPoolForState(const FVBWeaponSummonData& Data, EVBLocomotionState State);
}
