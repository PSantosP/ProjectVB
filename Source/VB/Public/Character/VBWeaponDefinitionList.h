// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/VBWeaponStateTypes.h"
#include "UObject/ObjectPtr.h"

class AActor;
class UVBFinisherConfig;
class UVBWeaponDefinition;

// 무기 정의 목록을 읽기만 하는 함수 묶음. 목록은 UVBWeaponStateComponent 의 설정 칸이 보유하고, LogOwner 는 경고에 쓸 액터다.
namespace VBWeaponDefinitionList
{
	using FDefinitions = TArray<TObjectPtr<const UVBWeaponDefinition>>;

	// 그 무기의 첫 정의. 맨손(None)이나 목록에 없는 무기는 nullptr.
	const UVBWeaponDefinition* Find(const FDefinitions& Definitions, EVBWeaponType Weapon);

	// 숫자 키 Slot 에 놓인 가진 무기의 정의. 없으면 nullptr, 가진 무기 둘이면 앞의 것 + 경고.
	const UVBWeaponDefinition* FindForSlot(const FDefinitions& Definitions, const TArray<EVBWeaponType>& Owned, int32 Slot,
		const AActor* LogOwner);

	// 정의가 있는 무기 종류들. 가진 무기의 시작값과 주기 검사가 쓴다.
	TArray<EVBWeaponType> GetKnownWeapons(const FDefinitions& Definitions);

	// 목록의 모든 처형을 중복 없이 모은다(처형 색인 미리 만들기용).
	void CollectFinisherConfigs(const FDefinitions& Definitions, TArray<const UVBFinisherConfig*>& OutConfigs);

	// 빈 칸, 맨손, 같은 무기 두 번을 경고한다.
	void WarnAboutList(const FDefinitions& Definitions, const AActor* LogOwner);

	// 가진 무기 둘이 한 숫자 키를 쓰면 경고한다.
	void WarnAboutSlotClash(const FDefinitions& Definitions, const TArray<EVBWeaponType>& Owned, const AActor* LogOwner);
}
