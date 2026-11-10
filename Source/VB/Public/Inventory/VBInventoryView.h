// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UVBEquipmentComponent;
class UVBInventoryComponent;
class UVBItemDefinition;
struct FVBInventoryEntry;

/** 고른 아이템의 상세 버튼 상태. 표시용이고 결정은 서버가 다시 한다. */
struct FVBInventoryActions
{
	bool bShowUse = false;
	bool bEnableUse = false;
	bool bShowRead = false;
	bool bEnableRead = false;
	bool bShowDiscard = false;
	bool bEnableDiscard = false;
	bool bShowEquip = false;
	// 입은 아이템이면 참이다. 장착 버튼 글이 해제로 바뀐다.
	bool bEquipped = false;
};

/** 인벤토리 화면의 정렬. 화면 상태라 저장하지 않는다. */
enum class EVBInventorySort : uint8
{
	Acquired, // 가진 순서 그대로(획득 순)
	Name, // 표시 이름
	Count, // 많은 것 먼저
	Kind, // 아이템 ID 태그 이름 - 같은 종류(Item.Medical...)끼리 모인다
};

/**
 * 인벤토리 화면의 표시 판정(순수 함수): 무엇을 어떤 순서로 보이고 어떤 버튼을 켜나.
 * 필터는 아이템 ID 태그 계층을 쓴다(Item.Medical -> Item.Medical.Gauze) - 따로 종류 칸을 두지 않는다.
 */
namespace VBInventoryView
{
	// 필터에 맞나. 빈 태그 = 전체.
	VB_API bool PassesFilter(const UVBItemDefinition* Item, const FGameplayTag& Filter);

	// 보일 줄 순서. 같은 값끼리는 가진 순서를 지킨다(안정 정렬). 정의가 빈 줄은 뺀다.
	VB_API TArray<const UVBItemDefinition*> BuildRows(const TArray<FVBInventoryEntry>& Entries, const FGameplayTag& Filter,
		EVBInventorySort Sort);

	// 정렬 버튼이 누를 때마다 넘어갈 다음 정렬(끝에서 처음으로).
	VB_API EVBInventorySort Next(EVBInventorySort Sort);

	// 고른 아이템으로 지금 할 수 있는 일. 아이템, 인벤토리, 장비는 비어 있어도 된다.
	VB_API FVBInventoryActions DescribeActions(const UVBItemDefinition* Item, const UVBInventoryComponent* Inventory,
		const UVBEquipmentComponent* Equipment);
}
