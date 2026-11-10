// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Loot/VBLootContainerSubsystem.h"

#include "Engine/World.h"
#include "SaveSystem/VBSaveGame.h"
#include "VB/VB.h"

UVBLootContainerSubsystem* UVBLootContainerSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UVBLootContainerSubsystem>() : nullptr;
}

const TArray<FVBSavedItem>* UVBLootContainerSubsystem::Find(FName Key) const
{
	return Remaining.Find(Key);
}

void UVBLootContainerSubsystem::SetRemaining(FName Key, const TArray<FVBSavedItem>& InRemaining)
{
	if (!HasWriteAuthority())
	{
		VB_LOG(Warning, "서랍장 남은 몫: 클라에서 %s 를 바꾸려 했다 - 무시한다(쓰기는 서버만)", *Key.ToString());
		return;
	}

	if (Key.IsNone())
	{
		VB_LOG(Warning, "서랍장 남은 몫: 키가 비었다 - 기록하지 않는다");
		return;
	}

	Remaining.Add(Key, InRemaining);
}

void UVBLootContainerSubsystem::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	// 덮어쓰지 않고 합친다 - 다른 맵에서 저장해도 앞 맵 서랍장 기록이 지워져 다시 굴리지 않게.
	TArray<FVBSavedLootContainer>& Out = Save.LootContainers.Containers;
	for (const TPair<FName, TArray<FVBSavedItem>>& Pair : Remaining)
	{
		FVBSavedLootContainer* Row = Out.FindByPredicate([&Pair](const FVBSavedLootContainer& Saved) { return Saved.Key == Pair.Key; });
		if (!Row)
		{
			Row = &Out.AddDefaulted_GetRef();
			Row->Key = Pair.Key;
		}

		Row->Remaining = Pair.Value;
	}
}

void UVBLootContainerSubsystem::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	Remaining.Reset();
	for (const FVBSavedLootContainer& Row : Save.LootContainers.Containers)
	{
		if (Row.Key.IsNone())
		{
			VB_LOG(Warning, "서랍장 남은 몫 불러오기: 키가 빈 기록 - 버린다(어느 서랍장의 것인지 알 수 없다)");
			continue;
		}
		// 빈 태그, 0 개 줄만 거른다. 못 찾는 태그는 지킨다(되찾기는 줄 때 한다).
		TArray<FVBSavedItem>& Rows = Remaining.Add(Row.Key);
		for (const FVBSavedItem& Item : Row.Remaining)
		{
			if (Item.ItemId.IsValid() && Item.Count > 0)
			{
				Rows.Add(Item);
			}
			else
			{
				VB_LOG(Warning, "서랍장 남은 몫 불러오기: %s 의 읽을 수 없는 줄(%s x%d) - 버린다", *Row.Key.ToString(), *Item.ItemId.ToString(), Item.Count);
			}
		}
	}

	VB_LOG(Log, "서랍장 남은 몫 불러오기: 서랍장 %d 곳", Remaining.Num());
	OnContentsLoaded.Broadcast();
}

bool UVBLootContainerSubsystem::HasWriteAuthority() const
{
	// 월드가 없으면(단위 시험) 쓴다.
	const UWorld* World = GetWorld();
	return !World || World->GetNetMode() != NM_Client;
}
