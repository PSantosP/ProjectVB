// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/VBInventoryTypes.h"
#include "SaveSystem/VBSaveGameTypes.h"   // FVBSavedItem - 저장 줄을 값으로 다룬다
#include "Templates/Function.h"

class UVBItemDefinition;
struct FGameplayTag;

// 저장 줄 해석. 원장에 적용하는 일과 지킨 줄의 보관은 UVBInventoryComponent 가 맡는다.
namespace VBInventorySaveRules
{
	// 저장 줄 -> 가진 줄. 같은 아이템은 합치고 상한에서 자른다.
	// 되찾지 못한 줄과 넘친 몫은 OutKept 로 보내 다음 저장에 다시 쓴다 - 이름 변경, 상한 패치로 잃지 않게.
	VB_API void ResolveSavedItems(const TArray<FVBSavedItem>& Saved,
		TFunctionRef<const UVBItemDefinition*(const FGameplayTag&)> Resolve,
		TArray<FVBInventoryEntry>& OutEntries, TArray<FVBSavedItem>& OutKept);
}
