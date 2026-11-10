// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBItemActions.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBWorldItem.h"
#include "VB/VB.h"

void VBItemActions::BeginUse(UVBInventoryComponent& Inventory, APlayerState* PlayerState, const UVBItemDefinition* Item)
{
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	if (!Usable || !Usable->UseMontage)
	{
		// 캐릭터 동작이 없으면 지금처럼 바로 쓴다. 쓸 수 없는 아이템의 거절 로그도 ConfirmUse 한 곳에서 낸다.
		Inventory.ConfirmUse(Item);
		return;
	}
	// 캐릭터 동작이 있으면 사용 GA 가 맡고, 몽타주의 사용 마커 시점에 ConfirmUse 로 확정한다.
	APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerState);
	if (!Pawn || !ASC)
	{
		VB_LOG(Log, "인벤토리: %s 사용 거절 - 몸(%s)이나 ASC(%s)가 없다", *GetNameSafe(Item), *GetNameSafe(Pawn), *GetNameSafe(ASC));
		return;
	}

	FGameplayEventData Event;
	Event.EventTag = VBGameplayTags::Event_Item_Use;
	Event.Instigator = Pawn;
	Event.Target = Pawn;
	Event.OptionalObject = Item;
	// 켜진 수 0 = 쓸 수 없음, 이미 사용 중, 막힘, GA 미부여. 아무것도 소모하지 않는다.
	//   공격 중 누름처럼 흔한 경로라 경고가 아니라 Log 다.
	if (ASC->HandleGameplayEvent(Event.EventTag, &Event) == 0)
	{
		VB_LOG(Log, "인벤토리: %s 사용 동작을 지금 시작할 수 없다 - 수·조건·쓰는 중·몸이 묶임·사용 GA 미부여(BP_VBPlayerState.StartupAbilitySet)", *GetNameSafe(Item));
	}
}

bool VBItemActions::Discard(UVBInventoryComponent& Inventory, const APlayerState* PlayerState, const UVBItemDefinition* Item, int32 Count)
{
	// 표시가 켠 버튼을 믿지 않는다 - 버릴 수 있는 아이템인지, 가진 만큼인지 서버가 다시 본다.
	if (!Item || !Item->bCanDiscard || Count <= 0 || Inventory.GetCount(Item) < Count)
	{
		VB_LOG(Log, "인벤토리: %s x%d 버리기 거절 - %s", *GetNameSafe(Item), Count,
			!Item ? TEXT("아이템 없음") : !Item->bCanDiscard ? TEXT("버릴 수 없는 아이템") : TEXT("가진 수 부족"));
		return false;
	}
	// 땅의 아이템을 먼저 만든다. 실패하면 빼지 않는다 - 조용히 사라지지 않게.
	const APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr;
	AVBWorldItem* Dropped = Pawn ? AVBWorldItem::SpawnDrop(Pawn, Item, Count, 0) : nullptr;
	if (!Dropped)
	{
		VB_LOG(Warning, "인벤토리: %s x%d 버리기 실패 - 떨굴 수 없다(몸 %s). 빼지 않는다", *GetNameSafe(Item), Count, *GetNameSafe(Pawn));
		return false;
	}

	if (!Inventory.RemoveItem(Item, Count))
	{
		// 위에서 수를 봤으니 오지 않는 길이다. 와도 같은 아이템이 둘이 되지 않게 떨군 것을 거둔다.
		Dropped->Destroy();
		return false;
	}

	VB_LOG(Log, "인벤토리: %s x%d 버림 - 발밑 %s", *GetNameSafe(Item), Count, *GetNameSafe(Dropped));
	return true;
}
