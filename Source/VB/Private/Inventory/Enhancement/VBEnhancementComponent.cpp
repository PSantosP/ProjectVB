// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Enhancement/VBEnhancementComponent.h"

#include "GameFramework/PlayerState.h"
#include "Inventory/Enhancement/VBEnhancementConfig.h"
#include "Inventory/Enhancement/VBEnhancementSettings.h"
#include "Inventory/VBItemDefinition.h"
#include "Net/UnrealNetwork.h"
#include "Player/VBPlayerStateLookup.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

UVBEnhancementComponent::UVBEnhancementComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBEnhancementComponent* UVBEnhancementComponent::FindFor(const AActor* Actor)
{
	return VBPlayerStateLookup::FindComponent<UVBEnhancementComponent>(Actor);
}

int32 UVBEnhancementComponent::GetLevel(const FVBEnhanceTarget& Target) const
{
	if (Target.IsWeapon())
	{
		const FVBWeaponEnhanceLevel* Row = WeaponLevels.FindByPredicate(
			[&Target](const FVBWeaponEnhanceLevel& Entry) { return Entry.Weapon == Target.Weapon; });
		return Row ? Row->Level : 0;
	}

	if (Target.IsItem())
	{
		const FVBItemEnhanceLevel* Row = ItemLevels.FindByPredicate(
			[&Target](const FVBItemEnhanceLevel& Entry) { return Entry.Item == Target.Item; });
		return Row ? Row->Level : 0;
	}

	return 0;
}

int32 UVBEnhancementComponent::GetMaxLevel() const
{
	return Config ? Config->MaxLevel : 0;
}

const FVBEnhanceTrack* UVBEnhancementComponent::FindTrack(EVBEnhanceCategory Category) const
{
	return Config ? Config->FindTrack(Category) : nullptr;
}

float UVBEnhancementComponent::GetItemStatScale(const UVBItemDefinition* Item) const
{
	const FVBEnhanceTarget Target = FVBEnhanceTarget::ForItem(Item);
	const int32 Level = GetLevel(Target);
	return Level > 0 ? VBEnhancement::ScaleAt(FindTrack(VBEnhancement::CategoryOf(Target)), Level) : 1.0f;
}

float UVBEnhancementComponent::GetWeaponDamageScale(EVBWeaponType Weapon) const
{
	const int32 Level = GetLevel(FVBEnhanceTarget::ForWeapon(Weapon));
	return Level > 0 ? VBEnhancement::ScaleAt(FindTrack(EVBEnhanceCategory::Weapon), Level) : 1.0f;
}

bool UVBEnhancementComponent::SetLevel(const FVBEnhanceTarget& Target, int32 Level)
{
	// 수치 자산이 없으면 올릴 수 없다(최대가 없다) - 비용을 받는 쪽(c2)도 같은 이유로 먼저 거절한다.
	if (!HasWriteAuthority(TEXT("SetLevel")) || VBEnhancement::CategoryOf(Target) == EVBEnhanceCategory::None || GetMaxLevel() <= 0)
	{
		return false;
	}

	const int32 Clamped = VBEnhancement::NormalizeLevel(Level, GetMaxLevel());
	if (Clamped == GetLevel(Target))
	{
		return false;
	}

	if (Target.IsWeapon())
	{
		FVBWeaponEnhanceLevel* Row = WeaponLevels.FindByPredicate(
			[&Target](const FVBWeaponEnhanceLevel& Entry) { return Entry.Weapon == Target.Weapon; });
		if (!Row)
		{
			Row = &WeaponLevels.AddDefaulted_GetRef();
			Row->Weapon = Target.Weapon;
		}

		Row->Level = Clamped;
	}
	else
	{
		FVBItemEnhanceLevel* Row = ItemLevels.FindByPredicate(
			[&Target](const FVBItemEnhanceLevel& Entry) { return Entry.Item == Target.Item; });
		if (!Row)
		{
			Row = &ItemLevels.AddDefaulted_GetRef();
			Row->Item = Target.Item;
		}

		Row->Level = Clamped;
	}

	VB_LOG(Log, "강화: %s +%d",
		Target.IsWeapon() ? *StaticEnum<EVBWeaponType>()->GetNameStringByValue((int64)Target.Weapon) : *GetNameSafe(Target.Item), Clamped);
	OnLevelChanged.Broadcast(Target);
	return true;
}

void UVBEnhancementComponent::OnRep_Levels()
{
	OnLevelChanged.Broadcast(FVBEnhanceTarget());
}

void UVBEnhancementComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner())
	{
		return;
	}

	FVBEnhancementSaveData& Out = Save.Enhancement;
	Out.Items.Reset();
	Out.Weapons.Reset();
	for (const FVBItemEnhanceLevel& Row : ItemLevels)
	{
		if (Row.Item && Row.Level > 0)
		{
			Out.Items.Add({Row.Item->ItemId, Row.Level});
		}
	}

	Out.Items.Append(DormantItems);
	for (const FVBWeaponEnhanceLevel& Row : WeaponLevels)
	{
		if (Row.Level > 0)
		{
			Out.Weapons.Add({FName(*StaticEnum<EVBWeaponType>()->GetNameStringByValue((int64)Row.Weapon)), Row.Level});
		}
	}

	Out.Weapons.Append(DormantWeapons);
}

void UVBEnhancementComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner() || !HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}

	ItemLevels.Reset();
	WeaponLevels.Reset();
	DormantItems.Reset();
	DormantWeapons.Reset();
	const int32 MaxLevel = GetMaxLevel();
	for (const FVBSavedItemLevel& Saved : Save.Enhancement.Items)
	{
		const UVBItemDefinition* Item = Saved.ItemId.IsValid() ? UVBItemDefinition::FindById(Saved.ItemId) : nullptr;
		if (!Item)
		{
			DormantItems.Add(Saved);
			continue;
		}

		if (ItemLevels.ContainsByPredicate([Item](const FVBItemEnhanceLevel& Entry) { return Entry.Item == Item; }))
		{
			VB_LOG(Warning, "강화 로드: %s 가 두 번 있다 - 첫 줄을 쓴다", *Saved.ItemId.ToString());
			continue;
		}

		ItemLevels.Add({Item, VBEnhancement::NormalizeLevel(Saved.Level, MaxLevel)});
	}

	for (const FVBSavedWeaponLevel& Saved : Save.Enhancement.Weapons)
	{
		const int64 Value = StaticEnum<EVBWeaponType>()->GetValueByNameString(Saved.WeaponId.ToString());
		if (Value == INDEX_NONE || (EVBWeaponType)Value == EVBWeaponType::None)
		{
			DormantWeapons.Add(Saved);
			continue;
		}

		const EVBWeaponType Weapon = (EVBWeaponType)Value;
		if (WeaponLevels.ContainsByPredicate([Weapon](const FVBWeaponEnhanceLevel& Entry) { return Entry.Weapon == Weapon; }))
		{
			VB_LOG(Warning, "강화 로드: 무기 %s 가 두 번 있다 - 첫 줄을 쓴다", *Saved.WeaponId.ToString());
			continue;
		}

		WeaponLevels.Add({Weapon, VBEnhancement::NormalizeLevel(Saved.Level, MaxLevel)});
	}

	VB_CLOG(!DormantItems.IsEmpty() || !DormantWeapons.IsEmpty(), Warning, "강화 로드: 모르는 아이템 %d·무기 %d 줄은 적용하지 않고 남겨 둔다",
		DormantItems.Num(), DormantWeapons.Num());
	// 장비는 저장을 다 읽은 뒤(OnSaveApplied) 이 단계로 효과를 한 번 건다 - 여기서 알려도 읽는 중이라 장비는 미룬다(설계 8 F1, F3).
	OnLevelChanged.Broadcast(FVBEnhanceTarget());
}

void UVBEnhancementComponent::InitializeComponent()
{
	Super::InitializeComponent();
	Config = UVBEnhancementSettings::LoadConfig();
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBEnhancementComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBEnhancementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVBEnhancementComponent, ItemLevels, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UVBEnhancementComponent, WeaponLevels, COND_OwnerOnly);
}

bool UVBEnhancementComponent::HasWriteAuthority(const TCHAR* What) const
{
	return VBServerWrite::IsAllowed(*this, TEXT("강화"), What);
}
