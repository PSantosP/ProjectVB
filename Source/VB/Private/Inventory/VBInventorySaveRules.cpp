// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBInventorySaveRules.h"

#include "Inventory/VBItemDefinition.h"
#include "VB/VB.h"

void VBInventorySaveRules::ResolveSavedItems(const TArray<FVBSavedItem>& Saved,
	TFunctionRef<const UVBItemDefinition*(const FGameplayTag&)> Resolve,
	TArray<FVBInventoryEntry>& OutEntries, TArray<FVBSavedItem>& OutKept)
{
	OutEntries.Reset();
	OutKept.Reset();
	for (const FVBSavedItem& Row : Saved)
	{
		if (Row.Count <= 0)
		{
			// 빈 줄은 되살릴 것도 지킬 것도 없다.
			VB_LOG(Warning, "인벤토리 로드: %s 의 수가 %d - 버린다", *Row.ItemId.ToString(), Row.Count);
			continue;
		}

		const UVBItemDefinition* Item = Resolve(Row.ItemId);
		if (!Item)
		{
			// 아이템 자산이 사라졌거나 ItemId 가 바뀐 것이다. 지켜 두면 자산, ItemId 를 되살렸을 때 돌아온다.
			VB_LOG(Warning, "인벤토리 로드: %s x%d 를 되찾지 못했다(아이템 자산·ItemId·에셋 매니저 설정을 본다) - 저장 줄은 지킨다", *Row.ItemId.ToString(), Row.Count);
			OutKept.Add(Row);
			continue;
		}
		// 같은 아이템 줄은 합치고 상한에서 자른다. 0개 줄은 만들지 않는다.
		FVBInventoryEntry* Entry = OutEntries.FindByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
		const int32 Accepted = Item->ClampToMax(Entry ? Entry->Count : 0, Row.Count);
		if (Accepted > 0)
		{
			if (!Entry)
			{
				Entry = &OutEntries.AddDefaulted_GetRef();
				Entry->Definition = Item;
				Entry->Count = 0;
			}

			Entry->Count += Accepted;
		}

		if (Accepted < Row.Count)
		{
			// 상한을 넘은 몫도 지킨다 - 상한이 다시 오르거나 가진 수가 줄면 다음 불러오기에 들어온다.
			VB_LOG(Warning, "인벤토리 로드: %s x%d 가 상한(%d)을 넘어 %d 만 넣었다 - 남은 %d 는 저장 줄로 지킨다",
				*Row.ItemId.ToString(), Row.Count, Item->MaxCount, Accepted, Row.Count - Accepted);
			FVBSavedItem& Kept = OutKept.AddDefaulted_GetRef();
			Kept.ItemId = Row.ItemId;
			Kept.Count = Row.Count - Accepted;
		}
	}
}
