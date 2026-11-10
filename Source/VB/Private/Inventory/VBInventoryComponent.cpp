// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBInventoryComponent.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/VBInventoryExchangeRules.h"
#include "Inventory/VBInventorySaveRules.h"
#include "Inventory/VBItemActions.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBQuickSlotRules.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Player/VBPlayerStateLookup.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

// ---------------------------------------------------------------- 복제 콜백

void FVBInventoryEntry::PostReplicatedAdd(const FVBInventoryList& InList)
{
	if (InList.Owner)
	{
		InList.Owner->HandleReplicatedChange();
	}
}

void FVBInventoryEntry::PostReplicatedChange(const FVBInventoryList& InList)
{
	if (InList.Owner)
	{
		InList.Owner->HandleReplicatedChange();
	}
}

void FVBInventoryEntry::PreReplicatedRemove(const FVBInventoryList& InList)
{
	if (InList.Owner)
	{
		InList.Owner->HandleReplicatedChange();
	}
}

// ---------------------------------------------------------------- 컴포넌트

UVBInventoryComponent::UVBInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
	List.Owner = this;
}

UVBInventoryComponent* UVBInventoryComponent::FindFor(const AActor* Actor)
{
	return VBPlayerStateLookup::FindComponent<UVBInventoryComponent>(Actor);
}

int32 UVBInventoryComponent::GetCount(const UVBItemDefinition* Item) const
{
	const FVBInventoryEntry* Entry = FindEntry(Item);
	return Entry ? Entry->Count : 0;
}

namespace
{
	// 진행 중인 획득(인벤토리, 아이템). 획득 효과가 같은 획득으로 되돌아오는 고리를 끊는다.
	using FAcquisition = TPair<const UVBInventoryComponent*, const UVBItemDefinition*>;
	TArray<FAcquisition>& ActiveAcquisitions()
	{
		check(IsInGameThread());
		static TArray<FAcquisition> Active;
		return Active;
	}

	bool IsAcquisitionBlocked(const FAcquisition& Key, bool bHasEffects)
	{
		return ActiveAcquisitions().Contains(Key) || (bHasEffects && ActiveAcquisitions().Num() >= 32);
	}
}

int32 UVBInventoryComponent::AddItem(const UVBItemDefinition* Item, int32 Count)
{
	if (!Item || Count <= 0 || !HasWriteAuthority(TEXT("AddItem")))
	{
		return 0;
	}

	FVBInventoryEntry* Entry = FindEntry(Item);
	const int32 Added = Item->ClampToMax(Entry ? Entry->Count : 0, Count);
	if (Added <= 0)
	{
		VB_LOG(Log, "인벤토리: %s 가 가득 찼다(상한 %d) - 넣지 않음", *GetNameSafe(Item), Item->MaxCount);
		return 0;
	}

	const FVBItemFragment_AcquireEffects* Acquire = Item->FindFragment<FVBItemFragment_AcquireEffects>();
	const TArray<FInstancedStruct> Effects = Acquire ? Acquire->Effects : TArray<FInstancedStruct>();
	const FVBRuleContext Context = Effects.IsEmpty() ? FVBRuleContext() : FVBRuleContext::Make(this, GetOwningPlayerState());
	if (!CanApplyAcquireEffects(Item, Effects, Context))
	{
		return 0;
	}
	// 동기 효과가 다른 아이템은 줄 수 있지만 이 획득으로 되돌아오면 안 된다.
	const FAcquisition Key(this, Item);
	if (IsAcquisitionBlocked(Key, !Effects.IsEmpty()))
	{
		VB_LOG(Warning, "Acquisition effects: rejected %s (cyclic/excessive nested grant)", *GetNameSafe(Item));
		return 0;
	}

	if (!Effects.IsEmpty())
	{
		ActiveAcquisitions().Add(Key);
	}

	ON_SCOPE_EXIT { if (!Effects.IsEmpty()) { ActiveAcquisitions().Pop(EAllowShrinking::No); } };
	if (Entry)
	{
		Entry->Count += Added;
		List.MarkItemDirty(*Entry);
	}
	else
	{
		FVBInventoryEntry& NewEntry = List.Entries.AddDefaulted_GetRef();
		NewEntry.Definition = Item;
		NewEntry.Count = Added;
		List.MarkItemDirty(NewEntry);
	}

	VB_LOG(Log, "인벤토리: %s +%d -> %d", *GetNameSafe(Item), Added, GetCount(Item));
	if (!Effects.IsEmpty())
	{
		const bool bEffectsApplied = VBRules::ApplyAll(Effects, Context);
		VB_CLOG(!bEffectsApplied, Error, "아이템 %s: 사전 검사 뒤 획득 효과 실패 - 이미 바뀐 수량과 효과를 확인해야 한다", *GetNameSafe(Item));
	}

	BroadcastAcquired(Item, Added);
	return Added;
}

bool UVBInventoryComponent::CanApplyAcquireEffects(const UVBItemDefinition* Item, const TArray<FInstancedStruct>& Effects,
	const FVBRuleContext& Context) const
{
	if (!Effects.IsEmpty() && (!Context.Story || !Context.CanApplyEffects()
		|| Effects.ContainsByPredicate([](const FInstancedStruct& Effect)
		{
			return !Effect.GetPtr<FVBEffect>() || Effect.GetScriptStruct() == FVBEffect::StaticStruct();
		})))
	{
		VB_LOG(Warning, "Acquisition effects: rejected %s (missing Story, authority or valid effects)", *GetNameSafe(Item));
		return false;
	}

	for (const FInstancedStruct& Effect : Effects)
	{
		if (!Effect.Get<FVBEffect>().CanApply(Context))
		{
			VB_LOG(Warning, "Acquisition effects: rejected %s (effect preflight refused)", *GetNameSafe(Item));
			return false;
		}
	}

	return true;
}

void UVBInventoryComponent::BroadcastAcquired(const UVBItemDefinition* Item, int32 Added)
{
	OnInventoryChanged.Broadcast();
	NotifyAcquired(Item, Added);
}

void UVBInventoryComponent::NotifyAcquired(const UVBItemDefinition* Item, int32 Added)
{
	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		FVBGameEvent Event;
		Event.EventTag = VBGameplayTags::Event_Item_Acquired;
		Event.TargetTags.AddTag(Item->ItemId);
		Event.Instigator = GetOwningPlayerState();
		Event.Count = Added;
		Events->Broadcast(Event);
	}
	// 소유 클라이언트의 화면에 알린다. 리슨 서버의 호스트면 클라 RPC 가 이 자리에서 바로 돈다.
	ClientItemAcquired(Item, Added);
}

bool UVBInventoryComponent::RemoveItem(const UVBItemDefinition* Item, int32 Count)
{
	if (!Item || Count <= 0 || !HasWriteAuthority(TEXT("RemoveItem")))
	{
		return false;
	}

	const int32 Index = List.Entries.IndexOfByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
	if (Index == INDEX_NONE || List.Entries[Index].Count < Count)
	{
		return false;
	}

	FVBInventoryEntry& Entry = List.Entries[Index];
	Entry.Count -= Count;
	if (Entry.Count == 0)
	{
		// 0 인 줄은 두지 않는다 - 없음 = 0 한 가지 표현.
		List.Entries.RemoveAt(Index);
		List.MarkArrayDirty();
	}
	else
	{
		List.MarkItemDirty(Entry);
	}

	VB_LOG(Log, "인벤토리: %s -%d -> %d", *GetNameSafe(Item), Count, GetCount(Item));
	OnInventoryChanged.Broadcast();
	return true;
}

struct UVBInventoryComponent::FExchangeRow : FVBExchangeRow
{
	TArray<FInstancedStruct> Effects;
	FVBRuleContext Context;
};

bool UVBInventoryComponent::TryExchange(TConstArrayView<FVBItemAmount> Costs, TConstArrayView<FVBItemAmount> Gains)
{
	if (!HasWriteAuthority(TEXT("TryExchange")))
	{
		return false;
	}

	// 같은 아이템 줄은 합친다. 내고 받는 아이템이 같으면 한 줄의 순증감으로 본다.
	FExchangeRows Rows;
	if ((Costs.IsEmpty() && Gains.IsEmpty()) || !VBInventoryExchangeRules::MergeAmounts(Costs, true, Rows)
		|| !VBInventoryExchangeRules::MergeAmounts(Gains, false, Rows))
	{
		VB_LOG(Warning, "인벤토리 교환: 틀린 줄(빈 교환·빈 아이템·0 이하·넘침) - 아무것도 안 바꿈");
		return false;
	}

	if (!CheckExchangeRows(Rows))
	{
		return false;
	}

	WriteExchangeRows(Rows);
	AnnounceExchange(Rows);
	return true;
}

bool UVBInventoryComponent::CheckExchangeRows(FExchangeRows& Rows) const
{
	// 하나라도 안 되면 아무것도 안 뺀다. 수량 검사 -> 그 줄의 효과 사전 검사 순서를 줄마다 지킨다.
	for (FExchangeRow& Row : Rows)
	{
		const int32 Held = GetCount(Row.Item);
		const EVBExchangeRowCheck Check = VBInventoryExchangeRules::CheckRow(Row, Held);
		if (Check == EVBExchangeRowCheck::NotEnough)
		{
			VB_LOG(Log, "인벤토리 교환: %s 모자람(%d < %d) - 아무것도 안 바꿈", *GetNameSafe(Row.Item), Held, Row.Cost);
			return false;
		}

		if (Check == EVBExchangeRowCheck::NoRoom)
		{
			VB_LOG(Log, "인벤토리 교환: %s 자리 없음(상한 %d) - 아무것도 안 바꿈", *GetNameSafe(Row.Item), Row.Item->MaxCount);
			return false;
		}

		if (Row.Gain <= 0)
		{
			continue;
		}

		if (const FVBItemFragment_AcquireEffects* Acquire = Row.Item->FindFragment<FVBItemFragment_AcquireEffects>())
		{
			Row.Effects = Acquire->Effects;
		}

		if (!Row.Effects.IsEmpty())
		{
			Row.Context = FVBRuleContext::Make(this, GetOwningPlayerState());
		}

		if (!CanApplyAcquireEffects(Row.Item, Row.Effects, Row.Context))
		{
			return false;
		}

		if (IsAcquisitionBlocked(FAcquisition(this, Row.Item), !Row.Effects.IsEmpty()))
		{
			VB_LOG(Warning, "인벤토리 교환: %s 획득이 이미 진행 중(되돌아오는 획득) - 아무것도 안 바꿈", *GetNameSafe(Row.Item));
			return false;
		}
	}

	return true;
}

void UVBInventoryComponent::WriteExchangeRows(const FExchangeRows& Rows)
{
	// 중간 알림이 없어 듣는 쪽이 반쯤 바뀐 목록을 보지 않는다.
	for (const FExchangeRow& Row : Rows)
	{
		WriteCountSilently(Row.Item, VBInventoryExchangeRules::CountAfter(Row, GetCount(Row.Item)));
		VB_LOG(Log, "인벤토리 교환: %s -%d +%d -> %d", *GetNameSafe(Row.Item), Row.Cost, Row.Gain, GetCount(Row.Item));
	}
}

void UVBInventoryComponent::AnnounceExchange(const FExchangeRows& Rows)
{
	// AddItem 과 같은 순서: 획득 효과 -> 변경 알림(한 번) -> 획득 사건.
	int32 Guarded = 0;
	for (const FExchangeRow& Row : Rows)
	{
		if (Row.Gain > 0 && !Row.Effects.IsEmpty())
		{
			ActiveAcquisitions().Add(FAcquisition(this, Row.Item));
			++Guarded;
		}
	}

	ON_SCOPE_EXIT
	{
		for (int32 Index = 0; Index < Guarded; ++Index)
		{
			ActiveAcquisitions().Pop(EAllowShrinking::No);
		}
	};

	for (const FExchangeRow& Row : Rows)
	{
		if (Row.Gain > 0 && !Row.Effects.IsEmpty())
		{
			const bool bEffectsApplied = VBRules::ApplyAll(Row.Effects, Row.Context);
			VB_CLOG(!bEffectsApplied, Error, "아이템 교환 %s: 사전 검사 뒤 획득 효과 실패 - 이미 바뀐 수량과 효과를 확인해야 한다", *GetNameSafe(Row.Item));
		}
	}

	OnInventoryChanged.Broadcast();
	for (const FExchangeRow& Row : Rows)
	{
		if (Row.Gain > 0)
		{
			NotifyAcquired(Row.Item, Row.Gain);
		}
	}
}

void UVBInventoryComponent::WriteCountSilently(const UVBItemDefinition* Item, int32 NewCount)
{
	const int32 Index = List.Entries.IndexOfByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
	if (NewCount <= 0)
	{
		// 0 인 줄은 두지 않는다(RemoveItem 과 같은 규약).
		if (Index != INDEX_NONE)
		{
			List.Entries.RemoveAt(Index);
			List.MarkArrayDirty();
		}

		return;
	}

	if (Index == INDEX_NONE)
	{
		FVBInventoryEntry& NewEntry = List.Entries.AddDefaulted_GetRef();
		NewEntry.Definition = Item;
		NewEntry.Count = NewCount;
		List.MarkItemDirty(NewEntry);
		return;
	}

	List.Entries[Index].Count = NewCount;
	List.MarkItemDirty(List.Entries[Index]);
}

void UVBInventoryComponent::RequestUseItem(const UVBItemDefinition* Item)
{
	ServerUseItem(Item);
}

bool UVBInventoryComponent::CanUseItem(const UVBItemDefinition* Item) const
{
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	if (!Usable || GetCount(Item) <= 0)
	{
		return false;
	}

	const FVBRuleContext Context = FVBRuleContext::Make(this, GetOwningPlayerState());
	return VBRules::EvaluateAll(Usable->Conditions, Context);
}

void UVBInventoryComponent::ServerUseItem_Implementation(const UVBItemDefinition* Item)
{
	VBItemActions::BeginUse(*this, GetOwningPlayerState(), Item);
}

void UVBInventoryComponent::RequestDiscard(const UVBItemDefinition* Item, int32 Count)
{
	ServerDiscard(Item, Count);
}

bool UVBInventoryComponent::CanDiscard(const UVBItemDefinition* Item) const
{
	return Item && Item->bCanDiscard && GetCount(Item) > 0;
}

void UVBInventoryComponent::ServerDiscard_Implementation(const UVBItemDefinition* Item, int32 Count)
{
	Discard(Item, Count);
}

bool UVBInventoryComponent::Discard(const UVBItemDefinition* Item, int32 Count)
{
	if (!HasWriteAuthority(TEXT("Discard")))
	{
		return false;
	}

	return VBItemActions::Discard(*this, GetOwningPlayerState(), Item, Count);
}

bool UVBInventoryComponent::ConfirmUse(const UVBItemDefinition* Item)
{
	if (!HasWriteAuthority(TEXT("ConfirmUse")))
	{
		return false;
	}
	// 표시가 켠 버튼도, 동작을 시작할 때의 미리 보기도 믿지 않는다 - 수, 조각, 조건을 이 시각에 다시 본다.
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	if (!Usable || GetCount(Item) <= 0)
	{
		VB_LOG(Warning, "인벤토리: %s 사용 거절 - %s", *GetNameSafe(Item), !Usable ? TEXT("쓸 수 있는 아이템이 아니다") : TEXT("가진 것이 없다"));
		return false;
	}

	const FVBRuleContext Context = FVBRuleContext::Make(this, GetOwningPlayerState());
	if (!VBRules::EvaluateAll(Usable->Conditions, Context))
	{
		VB_LOG(Log, "인벤토리: %s 사용 거절 - 조건이 거짓", *GetNameSafe(Item));
		return false;
	}
	// 소모를 먼저 한다 - 효과가 인벤토리를 바꿔도 쓴 것이 먼저 사라지게.
	if (Usable->bConsumeOnUse && !RemoveItem(Item, 1))
	{
		return false;
	}

	VBRules::ApplyAll(Usable->Effects, Context);
	VB_LOG(Log, "인벤토리: %s 사용", *GetNameSafe(Item));

	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		FVBGameEvent Event;
		Event.EventTag = VBGameplayTags::Event_Item_Used;
		Event.TargetTags.AddTag(Item->ItemId);
		Event.Instigator = Context.Instigator;
		Events->Broadcast(Event);
	}

	return true;
}

void UVBInventoryComponent::ClientItemAcquired_Implementation(const UVBItemDefinition* Item, int32 Count)
{
	OnItemAcquired.Broadcast(Item, Count);
}

void UVBInventoryComponent::RequestAssignQuickSlot(int32 SlotIndex, const UVBItemDefinition* Item)
{
	ServerAssignQuickSlot(SlotIndex, Item);
}

void UVBInventoryComponent::ServerAssignQuickSlot_Implementation(int32 SlotIndex, const UVBItemDefinition* Item)
{
	// 표시가 보낸 칸, 아이템을 믿지 않는다 - 칸 범위와 "사용" 조각을 서버가 다시 본다.
	const EVBQuickSlotAssignResult Result = VBQuickSlot::Assign(QuickSlots, SlotIndex, Item);
	VB_LOG(Log, "퀵슬롯: 칸 %d <- %s (%s)", SlotIndex, *GetNameSafe(Item), VBQuickSlot::LexToString(Result));
	if (Result == EVBQuickSlotAssignResult::Changed)
	{
		OnQuickSlotsChanged.Broadcast();
	}
}

void UVBInventoryComponent::RequestUseQuickSlot(int32 SlotIndex)
{
	ServerUseQuickSlot(SlotIndex);
}

void UVBInventoryComponent::ServerUseQuickSlot_Implementation(int32 SlotIndex)
{
	const UVBItemDefinition* Item = GetQuickSlotItem(SlotIndex);
	if (!Item || GetCount(Item) <= 0)
	{
		// 빈 칸, 다 쓴 칸을 누른 것은 잘못이 아니다 - 경고 없이 무시한다.
		return;
	}
	// 사용의 시작은 한 함수다 - 인벤토리 화면의 "사용" 도 같은 길이다.
	VBItemActions::BeginUse(*this, GetOwningPlayerState(), Item);
}

void UVBInventoryComponent::OnRep_QuickSlots()
{
	OnQuickSlotsChanged.Broadcast();
}

void UVBInventoryComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner())
	{
		return;
	}

	Save.Inventory.Items.Reset();
	for (const FVBInventoryEntry& Entry : List.Entries)
	{
		if (Entry.Definition && Entry.Count > 0)
		{
			FVBSavedItem& Saved = Save.Inventory.Items.AddDefaulted_GetRef();
			Saved.ItemId = Entry.Definition->ItemId;
			Saved.Count = Entry.Count;
		}
	}
	// 읽을 때 넣지 못한 줄을 그대로 다시 쓴다 - 이름 변경, 상한 패치로 가진 것을 잃지 않게.
	Save.Inventory.Items.Append(KeptSavedItems);

	Save.Inventory.QuickSlots.Reset();
	for (const TObjectPtr<const UVBItemDefinition>& Item : QuickSlots)
	{
		Save.Inventory.QuickSlots.Add(Item ? Item->ItemId : FGameplayTag());
	}
}

void UVBInventoryComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner() || !HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}

	VBInventorySaveRules::ResolveSavedItems(Save.Inventory.Items, &UVBItemDefinition::FindById, List.Entries, KeptSavedItems);
	List.MarkArrayDirty();
	VB_LOG(Log, "인벤토리 로드: %d 줄(넣지 못해 지킨 저장 줄 %d)", List.Entries.Num(), KeptSavedItems.Num());
	OnInventoryChanged.Broadcast();

	// 칸은 요청과 같은 판정(VBQuickSlot::Assign)으로 넣는다. 못 찾은 아이템은 빈 칸 + 경고다.
	//   칸은 가리키는 자리일 뿐이라 가진 줄과 달리 지켜 두지 않는다.
	QuickSlots.Init(nullptr, QuickSlotCount);
	const TArray<FGameplayTag>& SavedSlots = Save.Inventory.QuickSlots;
	VB_CLOG(SavedSlots.Num() > QuickSlotCount, Warning, "퀵슬롯 로드: 저장된 칸 %d 가 칸 수 %d 보다 많다 - 넘는 칸은 버린다", SavedSlots.Num(), QuickSlotCount);
	for (int32 Index = 0; Index < FMath::Min(SavedSlots.Num(), QuickSlotCount); ++Index)
	{
		if (!SavedSlots[Index].IsValid())
		{
			continue;
		}

		const UVBItemDefinition* Item = UVBItemDefinition::FindById(SavedSlots[Index]);
		const EVBQuickSlotAssignResult Result = Item ? VBQuickSlot::Assign(QuickSlots, Index, Item) : EVBQuickSlotAssignResult::Unchanged;
		VB_CLOG(!Item || (Result != EVBQuickSlotAssignResult::Changed), Warning, "퀵슬롯 로드: 칸 %d 의 %s 를 넣지 못했다(%s) - 빈 칸",
			Index, *SavedSlots[Index].ToString(), Item ? VBQuickSlot::LexToString(Result) : TEXT("되찾지 못함"));
	}

	OnQuickSlotsChanged.Broadcast();
}

void UVBInventoryComponent::InitializeComponent()
{
	Super::InitializeComponent();
	List.Owner = this;
	// 칸 수는 BP 기본값이 정해 생성자가 아니라 여기서 만든다. 클라도 같은 빈 칸으로 시작한다.
	QuickSlots.Init(nullptr, QuickSlotCount);
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBInventoryComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 가진 것은 그 플레이어만 안다.
	DOREPLIFETIME_CONDITION(UVBInventoryComponent, List, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UVBInventoryComponent, QuickSlots, COND_OwnerOnly);
}

APlayerState* UVBInventoryComponent::GetOwningPlayerState() const
{
	return Cast<APlayerState>(GetOwner());
}

bool UVBInventoryComponent::HasWriteAuthority(const TCHAR* What) const
{
	return VBServerWrite::IsAllowed(*this, TEXT("인벤토리"), What);
}

FVBInventoryEntry* UVBInventoryComponent::FindEntry(const UVBItemDefinition* Item)
{
	return List.Entries.FindByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
}

const FVBInventoryEntry* UVBInventoryComponent::FindEntry(const UVBItemDefinition* Item) const
{
	return List.Entries.FindByPredicate([Item](const FVBInventoryEntry& E) { return E.Definition == Item; });
}

#if !UE_BUILD_SHIPPING
namespace
{
	// 개발용 콘솔 명령: 서버의 모든 플레이어에게 아이템을 넣거나(+) 뺀다(-). 출시 빌드에는 없다.
	//   예: vb.Inventory.Give Item.Medical.Gauze 10. 실제 길(AddItem, RemoveItem)을 탄다.
	FAutoConsoleCommandWithWorldAndArgs GVBInventoryGiveCommand(
		TEXT("vb.Inventory.Give"),
		TEXT("Dev only. vb.Inventory.Give <ItemId tag> <count> - add (or remove when negative) items for every player on the server world."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
		{
			const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
			if (Args.Num() < 2 || !GameState || World->GetNetMode() == NM_Client)
			{
				VB_LOG(Warning, "vb.Inventory.Give: %s",
					Args.Num() < 2 ? TEXT("인자 둘이 필요하다 - <ItemId 태그> <수>") : TEXT("서버 게임 월드에서만 된다(PIE 중에 친다)"));
				return;
			}

			const FGameplayTag ItemId = FGameplayTag::RequestGameplayTag(FName(*Args[0]), /*ErrorIfNotFound=*/false);
			const UVBItemDefinition* Item = ItemId.IsValid() ? UVBItemDefinition::FindById(ItemId) : nullptr;
			const int32 Count = FCString::Atoi(*Args[1]);
			if (!Item || Count == 0)
			{
				VB_LOG(Warning, "vb.Inventory.Give: %s", !Item ? *FString::Printf(TEXT("아이템 %s 를 못 찾았다"), *Args[0]) : TEXT("수가 0 이다"));
				return;
			}

			for (APlayerState* PlayerState : GameState->PlayerArray)
			{
				UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(PlayerState);
				if (!Inventory)
				{
					continue;
				}

				int32 Changed = 0;
				if (Count > 0)
				{
					Changed = Inventory->AddItem(Item, Count);
				}
				else
				{
					// 빼기는 가진 만큼까지만 - RemoveItem 은 모자라면 아무것도 안 뺀다.
					const int32 ToRemove = FMath::Min(-Count, Inventory->GetCount(Item));
					Changed = (ToRemove > 0 && Inventory->RemoveItem(Item, ToRemove)) ? -ToRemove : 0;
				}

				VB_LOG(Log, "vb.Inventory.Give: %s %s %+d -> 가진 수 %d", *GetNameSafe(PlayerState), *Item->GetName(), Changed,
					Inventory->GetCount(Item));
			}
		}));
}
#endif
