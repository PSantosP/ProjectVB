// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Equipment/VBEquipmentComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBStatBonus.h"
#include "Inventory/Equipment/VBEquipmentRules.h"
#include "Inventory/Enhancement/VBEnhancementComponent.h"
#include "Inventory/Enhancement/VBEnhancementRules.h"
#include "Player/VBPlayerStateLookup.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

UVBEquipmentComponent::UVBEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
	// 순서는 장비 화면의 순서다 - 저장은 배치를 같이 적어 순서와 무관하다(WriteSave/ReadSave).
	SlotLayout = {VBGameplayTags::Equipment_Slot_Head, VBGameplayTags::Equipment_Slot_Body, VBGameplayTags::Equipment_Slot_Arms,
		VBGameplayTags::Equipment_Slot_Legs, VBGameplayTags::Equipment_Slot_Charm, VBGameplayTags::Equipment_Slot_Charm};
}

UVBEquipmentComponent* UVBEquipmentComponent::FindFor(const AActor* Actor)
{
	return VBPlayerStateLookup::FindComponent<UVBEquipmentComponent>(Actor);
}

int32 UVBEquipmentComponent::FindSlotOf(const UVBItemDefinition* Item) const
{
	return Item ? Equipped.IndexOfByKey(Item) : INDEX_NONE;
}

void UVBEquipmentComponent::RequestEquip(const UVBItemDefinition* Item, int32 SlotIndex)
{
	ServerEquip(Item, SlotIndex);
}

void UVBEquipmentComponent::RequestUnequip(int32 SlotIndex)
{
	ServerUnequip(SlotIndex);
}

void UVBEquipmentComponent::ServerEquip_Implementation(const UVBItemDefinition* Item, int32 SlotIndex)
{
	Equip(Item, SlotIndex);
}

void UVBEquipmentComponent::ServerUnequip_Implementation(int32 SlotIndex)
{
	Unequip(SlotIndex);
}

bool UVBEquipmentComponent::Equip(const UVBItemDefinition* Item, int32 SlotIndex)
{
	if (!HasWriteAuthority(TEXT("Equip")))
	{
		return false;
	}
	// 표시가 보낸 아이템, 칸을 믿지 않는다 - 가진 것인지, 칸이 맞는지 서버가 다시 본다.
	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	if (!Inventory || Inventory->GetCount(Item) <= 0)
	{
		VB_LOG(Log, "장비: %s 입기 거절 - 가진 것이 없다", *GetNameSafe(Item));
		return false;
	}

	if (!GetAbilitySystem() || !EquipmentEffect)
	{
		VB_LOG(Warning, "장비: %s 입기 거절 - ASC(%s)나 EquipmentEffect(%s)가 없다. 효과 없는 장착은 거짓 상태라 입히지 않는다",
			*GetNameSafe(Item), *GetNameSafe(GetAbilitySystem()), *GetNameSafe(EquipmentEffect.Get()));
		return false;
	}

	TArray<const UVBItemDefinition*> Next;
	for (const TObjectPtr<const UVBItemDefinition>& Each : Equipped)
	{
		Next.Add(Each.Get());
	}

	const FVBEquipOutcome Outcome = VBEquipment::Equip(SlotLayout, Next, Item, SlotIndex);
	const EVBEquipResult Result = Outcome.Result;
	const int32 Slot = Outcome.Slot;
	const int32 Vacated = Outcome.Vacated;
	const UVBItemDefinition* Replaced = Outcome.Replaced;
	VB_LOG(Log, "장비: %s -> 칸 %d (%s)", *GetNameSafe(Item), Slot, VBEquipment::LexToString(Result));
	if (Result != EVBEquipResult::Changed)
	{
		return Result == EVBEquipResult::Unchanged;
	}
	// 효과를 먼저 뗀다(옮겨 오는 칸, 바뀌는 칸). 그다음 칸을 바꾸고 새 효과를 건다.
	const TArray<TObjectPtr<const UVBItemDefinition>> Previous = Equipped;
	if (Vacated != INDEX_NONE)
	{
		RemoveEffect(Vacated);
	}

	RemoveEffect(Slot);
	Equipped.SetNum(SlotLayout.Num());
	for (int32 Index = 0; Index < Next.Num(); ++Index)
	{
		Equipped[Index] = Next[Index];
	}

	if (!ApplyEffect(Slot))
	{
		// 효과가 안 걸렸으면(스펙, 적용 실패) 칸을 되돌린다 - 효과 없는 장착은 거짓 상태다. 옛 효과도 다시 못 걸면 그 칸을 비우고 알린다.
		VB_LOG(Warning, "장비: %s 의 효과(%s)를 걸지 못했다 - 칸 %d 를 되돌린다", *GetNameSafe(Item), *GetNameSafe(EquipmentEffect.Get()), Slot);
		Equipped = Previous;
		Equipped.SetNum(SlotLayout.Num());
		bool bDropped = false;
		for (const int32 Index : {Slot, Vacated})
		{
			if (Equipped.IsValidIndex(Index) && Equipped[Index] && !ApplyEffect(Index))
			{
				// 비운 뒤에 알린다(Unequip 과 같은 순서) - 듣는 쪽이 아직 입은 상태를 보지 않게.
				const UVBItemDefinition* Dropped = Equipped[Index];
				Equipped[Index] = nullptr;
				BroadcastEvent(VBGameplayTags::Event_Item_Unequipped, Dropped);
				bDropped = true;
			}
		}

		if (bDropped)
		{
			OnEquipmentChanged.Broadcast();
		}

		return false;
	}

	if (Replaced)
	{
		BroadcastEvent(VBGameplayTags::Event_Item_Unequipped, Replaced);
	}

	BroadcastEvent(VBGameplayTags::Event_Item_Equipped, Item);
	OnEquipmentChanged.Broadcast();
	return true;
}

bool UVBEquipmentComponent::Unequip(int32 SlotIndex)
{
	if (!HasWriteAuthority(TEXT("Unequip")) || !Equipped.IsValidIndex(SlotIndex) || !Equipped[SlotIndex])
	{
		return false;
	}

	const UVBItemDefinition* Item = Equipped[SlotIndex];
	RemoveEffect(SlotIndex);
	Equipped[SlotIndex] = nullptr;
	VB_LOG(Log, "장비: 칸 %d 의 %s 벗음", SlotIndex, *GetNameSafe(Item));
	BroadcastEvent(VBGameplayTags::Event_Item_Unequipped, Item);
	OnEquipmentChanged.Broadcast();
	return true;
}

bool UVBEquipmentComponent::ApplyEffect(int32 SlotIndex)
{
	UAbilitySystemComponent* ASC = GetAbilitySystem();
	const UVBItemDefinition* Item = GetEquipped(SlotIndex);
	const FVBItemFragment_Equippable* Equippable = Item ? Item->FindFragment<FVBItemFragment_Equippable>() : nullptr;
	if (!ASC || !EquipmentEffect || !Equippable)
	{
		return false;
	}
	// 지도 -> SetByCaller 채우기는 성장과 같이 쓴다(VBStatBonus, 설계 b2-growth.md 2절). 강화 단계가 있으면 그 배율을 곱한 지도.
	const UVBEnhancementComponent* Enhancement = GetOwner() ? GetOwner()->FindComponentByClass<UVBEnhancementComponent>() : nullptr;
	const float Scale = Enhancement ? Enhancement->GetItemStatScale(Item) : 1.0f;
	const bool bScaled = !FMath::IsNearlyEqual(Scale, 1.0f);
	const TMap<FGameplayTag, float> ScaledStats = bScaled ? VBEnhancement::ScaledStats(Equippable->Stats,
		Scale) : TMap<FGameplayTag, float>();
	EffectHandles.SetNum(SlotLayout.Num());
	EffectHandles[SlotIndex] = VBStatBonus::Apply(ASC, EquipmentEffect, bScaled ? ScaledStats : Equippable->Stats, GetNameSafe(Item));
	return EffectHandles[SlotIndex].IsValid();
}

void UVBEquipmentComponent::RemoveEffect(int32 SlotIndex)
{
	if (!EffectHandles.IsValidIndex(SlotIndex) || !EffectHandles[SlotIndex].IsValid())
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = GetAbilitySystem())
	{
		ASC->RemoveActiveGameplayEffect(EffectHandles[SlotIndex]);
	}

	EffectHandles[SlotIndex].Invalidate();
}

void UVBEquipmentComponent::RemoveAllEffects()
{
	for (int32 Index = 0; Index < EffectHandles.Num(); ++Index)
	{
		RemoveEffect(Index);
	}
}

void UVBEquipmentComponent::HandleInventoryChanged()
{
	// 버리기, 팔기로 가진 수가 0 이 된 장비는 벗긴다 - 없는 것을 입은 채 남지 않게.
	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	if (!Inventory || !VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}
	// 저장을 읽히는 중이면 미룬다 - 인벤토리가 장비보다 먼저 읽혔는지에 따라 판정이 갈리지 않게 다 읽힌 뒤(HandleSaveApplied) 본다.
	if (const UVBSaveParticipantSubsystem* Registry = SaveRegistry.Get(); Registry && Registry->IsApplyingSave())
	{
		return;
	}

	for (int32 Index = 0; Index < Equipped.Num(); ++Index)
	{
		if (Equipped[Index] && Inventory->GetCount(Equipped[Index]) <= 0)
		{
			Unequip(Index);
		}
	}
}

void UVBEquipmentComponent::HandleSaveApplied()
{
	// 저장에만 있고 가진 수가 0 인 장비(인벤토리 쪽에서 못 되찾은 아이템, 손으로 고친 세이브)를 여기서 벗긴다 - 읽는 순서와 무관하다.
	HandleInventoryChanged();
	// 효과는 읽기 두 번째 단계인 여기서 한 번 건다 - 강화 단계가 이미 읽혀 최종 배율로 걸린다. 체력 같은 현재값은 세 번째 단계에서 이 상한에 맞춘다.
	// 저장을 읽지 않은 플레이어의 장비도 같은 효과를 다시 걸 뿐이다.
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	RemoveAllEffects();
	for (int32 Index = 0; Index < Equipped.Num(); ++Index)
	{
		if (Equipped[Index] && !ApplyEffect(Index))
		{
			VB_LOG(Warning, "장비 로드: 칸 %d 의 %s 효과(%s)를 걸지 못했다 - 빈 칸으로 둔다(효과 없는 장착은 거짓 상태)",
				Index, *GetNameSafe(Equipped[Index].Get()), *GetNameSafe(EquipmentEffect.Get()));
			Equipped[Index] = nullptr;
			OnEquipmentChanged.Broadcast();
		}
	}
}

void UVBEquipmentComponent::RefreshItem(const UVBItemDefinition* Item)
{
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}
	// 저장을 읽는 중이면 두 번째 단계(HandleSaveApplied)가 최종 단계로 건다 - 중간 단계로 걸었다 떼지 않는다.
	if (const UVBSaveParticipantSubsystem* Registry = SaveRegistry.Get(); Registry && Registry->IsApplyingSave())
	{
		return;
	}

	for (int32 Index = 0; Index < Equipped.Num(); ++Index)
	{
		if (!Equipped[Index] || (Item && Equipped[Index] != Item))
		{
			continue;
		}
		// 단계만 바뀐 것이라 입기, 벗기 사건은 내지 않는다. 옛 효과를 먼저 떼야 겹치지 않는다(ApplyEffect 는 떼지 않는다).
		RemoveEffect(Index);
		VB_CLOG(!ApplyEffect(Index), Warning, "장비: 칸 %d 의 %s 효과를 다시 걸지 못했다(%s)", Index, *GetNameSafe(Equipped[Index].Get()),
			*GetNameSafe(EquipmentEffect.Get()));
	}
}

void UVBEquipmentComponent::HandleEnhancementChanged(const FVBEnhanceTarget& Target)
{
	// 무기 단계는 장비와 무관하다. 빈 대상(복제, 저장 읽기 = 전부)은 입은 칸 모두.
	if (Target.IsWeapon())
	{
		return;
	}

	RefreshItem(Target.IsItem() ? Target.Item.Get() : nullptr);
}

void UVBEquipmentComponent::BroadcastEvent(const FGameplayTag& EventTag, const UVBItemDefinition* Item) const
{
	if (UVBGameEventSubsystem* Events = Item ? UVBGameEventSubsystem::Get(this) : nullptr)
	{
		FVBGameEvent Event;
		Event.EventTag = EventTag;
		Event.TargetTags.AddTag(Item->ItemId);
		Event.Instigator = GetOwningPlayerState();
		Events->Broadcast(Event);
	}
}

void UVBEquipmentComponent::OnRep_Equipped()
{
	OnEquipmentChanged.Broadcast();
}

void UVBEquipmentComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner())
	{
		return;
	}

	Save.Equipment.Slots.Reset();
	for (const TObjectPtr<const UVBItemDefinition>& Item : Equipped)
	{
		Save.Equipment.Slots.Add(Item ? Item->ItemId : FGameplayTag());
	}

	Save.Equipment.Layout = SlotLayout;
}

void UVBEquipmentComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner() || !HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}
	// 칸은 요청과 같은 판정(VBEquipment::Equip)으로 넣는다. 가진 것인지는 여기서 보지 않는다 - 인벤토리가 아직 안 읽혔을 수 있다.
	//   소유는 참여자가 모두 읽힌 뒤 HandleSaveApplied 가 한 번 본다(읽는 순서는 등록 순서라 정해져 있지 않다).
	RemoveAllEffects();
	TArray<const UVBItemDefinition*> Next;
	Next.Init(nullptr, SlotLayout.Num());
	const TArray<FGameplayTag>& Saved = Save.Equipment.Slots;
	// 저장된 칸은 저장 때의 배치로 읽고, 지금 배치의 같은 종류 같은 번째 칸에 넣는다. 배치를 안 적은 옛 저장은 옛 배치다.
	const TArray<FGameplayTag> SavedLayout = Save.Equipment.Layout.IsEmpty() ? VBEquipment::LegacySaveLayout() : Save.Equipment.Layout;
	VB_CLOG(Saved.Num() > SavedLayout.Num(), Warning, "장비 로드: 저장 칸 %d 개가 저장 배치 %d 개보다 많다 - 넘는 칸은 버린다",
		Saved.Num(), SavedLayout.Num());
	for (int32 Index = 0; Index < Saved.Num(); ++Index)
	{
		if (!Saved[Index].IsValid())
		{
			continue;
		}

		const int32 TargetSlot = VBEquipment::MapSavedSlot(SavedLayout, Index, SlotLayout);
		const UVBItemDefinition* Item = UVBItemDefinition::FindById(Saved[Index]);
		const EVBEquipResult Result = !Item ? EVBEquipResult::NotEquippable
			: TargetSlot == INDEX_NONE ? EVBEquipResult::NoFittingSlot
			: VBEquipment::Equip(SlotLayout, Next, Item, TargetSlot).Result;
		VB_CLOG(Result != EVBEquipResult::Changed, Warning, "장비 로드: 저장 칸 %d 의 %s 를 입히지 못했다(%s) - 빈 칸",
			Index, *Saved[Index].ToString(), Item ? VBEquipment::LexToString(Result) : TEXT("되찾지 못함"));
	}

	Equipped.Reset();
	for (const UVBItemDefinition* Item : Next)
	{
		Equipped.Add(Item);
	}
	// 효과는 여기서 걸지 않는다 - 강화 단계가 아직 안 읽혔을 수 있다. 다 읽힌 뒤 HandleSaveApplied 가 한 번 건다(설계 b3 8 F1).
	//   읽기가 도중에 끊기면(더 새 로드가 대신한다) 다음 HandleSaveApplied 까지 효과 없이 입은 상태다 - 끊긴 로드는 곧 덮인다.
	OnEquipmentChanged.Broadcast();
}

void UVBEquipmentComponent::InitializeComponent()
{
	Super::InitializeComponent();
	Equipped.Init(nullptr, SlotLayout.Num());
	EffectHandles.SetNum(SlotLayout.Num());
	UVBSaveParticipantSubsystem::Register(this);
	// 이어하기는 BeginPlay 보다 먼저 읽힌다(VBSaveParticipant.h) - 그 끝 신호를 놓치지 않게 등록과 같은 자리에서 듣는다.
	if (UVBSaveParticipantSubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr)
	{
		SaveRegistry = Registry;
		SaveAppliedHandle = Registry->OnSaveApplied.AddUObject(this, &UVBEquipmentComponent::HandleSaveApplied);
	}
	// 강화 단계가 바뀌면 그 아이템을 입은 칸의 효과를 다시 건다. 같은 PlayerState 의 부품이라 생성자에서 이미 있다.
	if (UVBEnhancementComponent* Enhancement = GetOwner() ? GetOwner()->FindComponentByClass<UVBEnhancementComponent>() : nullptr)
	{
		EnhancementSource = Enhancement;
		EnhancementChangedHandle = Enhancement->OnLevelChanged.AddUObject(this, &UVBEquipmentComponent::HandleEnhancementChanged);
	}
}

void UVBEquipmentComponent::UninitializeComponent()
{
	if (UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner()))
	{
		Inventory->OnInventoryChanged.Remove(InventoryChangedHandle);
	}

	if (UVBSaveParticipantSubsystem* Registry = SaveRegistry.Get())
	{
		Registry->OnSaveApplied.Remove(SaveAppliedHandle);
	}

	SaveRegistry.Reset();
	if (UVBEnhancementComponent* Enhancement = EnhancementSource.Get())
	{
		Enhancement->OnLevelChanged.Remove(EnhancementChangedHandle);
	}

	EnhancementSource.Reset();
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner()))
		{
			InventoryChangedHandle = Inventory->OnInventoryChanged.AddUObject(this, &UVBEquipmentComponent::HandleInventoryChanged);
		}
	}
}

void UVBEquipmentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVBEquipmentComponent, Equipped, COND_OwnerOnly);
}

bool UVBEquipmentComponent::HasWriteAuthority(const TCHAR* What) const
{
	return VBServerWrite::IsAllowed(*this, TEXT("장비"), What);
}

APlayerState* UVBEquipmentComponent::GetOwningPlayerState() const
{
	return Cast<APlayerState>(GetOwner());
}

UAbilitySystemComponent* UVBEquipmentComponent::GetAbilitySystem() const
{
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner());
}
