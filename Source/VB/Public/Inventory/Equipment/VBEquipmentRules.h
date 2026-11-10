// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UVBItemDefinition;

/** 장착 요청의 결과. 서버가 로그로, 시험이 판정으로 쓴다. */
enum class EVBEquipResult : uint8
{
	Changed, // 칸이 바뀌었다(같은 아이템이 다른 칸에 있었으면 그 칸은 비었다)
	Unchanged, // 이미 그 칸에 있다
	NotEquippable, // 장비 조각이 없다
	NoFittingSlot, // 맞는 칸이 없다(지정한 칸이 종류가 다르거나 범위 밖)
};

// Equip 한 번의 결과 - 판정과 고른 칸, 밀려난 아이템, 비운 칸이 함께 나온다.
struct FVBEquipOutcome
{
	EVBEquipResult Result = EVBEquipResult::NotEquippable;
	int32 Slot = INDEX_NONE;
	const UVBItemDefinition* Replaced = nullptr;
	int32 Vacated = INDEX_NONE;
};

/** 장비 칸 판정(순수 함수). 칸은 UVBEquipmentComponent 가 관리하고, 요청과 불러오기가 같은 함수를 쓴다. */
namespace VBEquipment
{
	// 이 아이템이 이 칸 종류에 들어가나.
	VB_API bool Fits(const UVBItemDefinition* Item, const FGameplayTag& SlotType);

	// Item 을 입는다. RequestedSlot 이 범위 밖이면 맞는 빈 칸, 없으면 맞는 첫 칸을 고른다.
	//   한 아이템은 한 칸에만 - 다른 칸에 있으면 비운다(OutVacated).
	VB_API FVBEquipOutcome Equip(const TArray<FGameplayTag>& Layout, TArray<const UVBItemDefinition*>& InOutEquipped,
		const UVBItemDefinition* Item,
		int32 RequestedSlot);

	VB_API const TCHAR* LexToString(EVBEquipResult Result);

	// 저장된 칸이 지금 Layout 의 몇 번 칸인가(같은 종류의 같은 번째). 없으면 INDEX_NONE.
	//   번호 그대로 읽으면 칸 배치가 바뀔 때 아이템이 밀려 버려진다.
	VB_API int32 MapSavedSlot(const TArray<FGameplayTag>& SavedLayout, int32 SavedIndex, const TArray<FGameplayTag>& Layout);

	// 칸 배치를 적지 않던 옛 저장의 배치. 옛 저장을 읽을 때만 쓴다.
	VB_API TArray<FGameplayTag> LegacySaveLayout();
}
