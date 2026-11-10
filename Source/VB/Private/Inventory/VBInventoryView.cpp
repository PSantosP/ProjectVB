// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBInventoryView.h"

#include "Algo/StableSort.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Collection/VBCollectionEntryDefinition.h"

bool VBInventoryView::PassesFilter(const UVBItemDefinition* Item, const FGameplayTag& Filter)
{
	return Item && (!Filter.IsValid() || Item->ItemId.MatchesTag(Filter));
}

TArray<const UVBItemDefinition*> VBInventoryView::BuildRows(const TArray<FVBInventoryEntry>& Entries, const FGameplayTag& Filter,
	EVBInventorySort Sort)
{
	struct FRow
	{
		const UVBItemDefinition* Item;
		int32 Count;
	};
	TArray<FRow> Rows;
	for (const FVBInventoryEntry& Entry : Entries)
	{
		if (PassesFilter(Entry.Definition, Filter))
		{
			Rows.Add({Entry.Definition.Get(), Entry.Count});
		}
	}

	switch (Sort)
	{
	case EVBInventorySort::Name:
		Algo::StableSort(Rows, [](const FRow& A, const FRow& B) { return A.Item->DisplayName.CompareTo(B.Item->DisplayName) < 0; });
		break;
	case EVBInventorySort::Count:
		Algo::StableSort(Rows, [](const FRow& A, const FRow& B) { return A.Count > B.Count; });
		break;
	case EVBInventorySort::Kind:
		Algo::StableSort(Rows,
			[](const FRow& A, const FRow& B) { return A.Item->ItemId.GetTagName().LexicalLess(B.Item->ItemId.GetTagName()); });
		break;
	case EVBInventorySort::Acquired:
		break;
	}

	TArray<const UVBItemDefinition*> Result;
	Result.Reserve(Rows.Num());
	for (const FRow& Row : Rows)
	{
		Result.Add(Row.Item);
	}

	return Result;
}

EVBInventorySort VBInventoryView::Next(EVBInventorySort Sort)
{
	switch (Sort)
	{
	case EVBInventorySort::Acquired: return EVBInventorySort::Name;
	case EVBInventorySort::Name: return EVBInventorySort::Count;
	case EVBInventorySort::Count: return EVBInventorySort::Kind;
	case EVBInventorySort::Kind: return EVBInventorySort::Acquired;
	}

	return EVBInventorySort::Acquired;
}

FVBInventoryActions VBInventoryView::DescribeActions(const UVBItemDefinition* Item, const UVBInventoryComponent* Inventory,
	const UVBEquipmentComponent* Equipment)
{
	FVBInventoryActions Actions;
	Actions.bShowUse = Item && Item->FindFragment<FVBItemFragment_Usable>();
	Actions.bEnableUse = Actions.bShowUse && Inventory && Inventory->CanUseItem(Item);
	Actions.bShowRead = VBCollection::GetReadingFact(Item).IsValid();
	Actions.bEnableRead = Actions.bShowRead && Inventory && Inventory->GetCount(Item) > 0;
	// 버릴 수 없는 아이템이면 접는다. 가진 것이 없으면 보이되 끈다.
	Actions.bShowDiscard = Item && Item->bCanDiscard;
	Actions.bEnableDiscard = Inventory && Inventory->CanDiscard(Item);
	Actions.bShowEquip = Equipment && Item && Item->FindFragment<FVBItemFragment_Equippable>();
	Actions.bEquipped = Equipment && Equipment->FindSlotOf(Item) != INDEX_NONE;
	return Actions;
}
