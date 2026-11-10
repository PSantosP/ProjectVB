// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameplayEffect.h"
#include "UObject/Package.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Inventory/Enhancement/VBEnhancementComponent.h"
#include "Inventory/Enhancement/VBEnhancementConfig.h"
#include "Inventory/Enhancement/VBEnhancementRules.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/Crafting/VBCraftingComponent.h"
#include "Inventory/Crafting/VBCraftingStationDefinition.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Player/VBPlayerController.h"
#include "Player/VBPlayerState.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "Unit/VBIsolatedGameWorld.h"

// 강화 핵심 규칙의 시험.

class FVBEnhancementTestAccess
{
public:
	static void SetConfig(UVBEnhancementComponent& Component, const UVBEnhancementConfig* Config) { Component.Config = Config; }
};

namespace VBEnhancementTest
{
	UVBItemDefinition* MakeGear(FGameplayTag SlotType, TMap<FGameplayTag, float> Stats)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>();
		FVBItemFragment_Equippable Equippable;
		Equippable.SlotType = SlotType;
		Equippable.Stats = MoveTemp(Stats);
		Item->Fragments.Add(FInstancedStruct::Make(Equippable));
		return Item;
	}

	FVBEnhanceTrack MakeTrack(std::initializer_list<float> Scales)
	{
		FVBEnhanceTrack Track;
		for (const float Scale : Scales)
		{
			FVBEnhanceStep& Step = Track.Steps.AddDefaulted_GetRef();
			Step.Scale = Scale;
			Step.Gold = 10;
		}

		return Track;
	}

	UVBEnhancementConfig* MakeConfig()
	{
		UVBEnhancementConfig* Config = NewObject<UVBEnhancementConfig>();
		Config->MaxLevel = 5;
		Config->WeaponTrack = MakeTrack({1.08f, 1.16f, 1.25f, 1.35f, 1.45f});
		Config->ArmorTrack = MakeTrack({1.2f, 1.5f, 2.0f, 2.2f, 2.5f});
		Config->AccessoryTrack = MakeTrack({1.1f, 1.2f, 1.3f, 1.4f, 1.5f});
		return Config;
	}

	// EquipmentEffect 는 BP 기본값이라 리플렉션으로 꽂는다(VBEquipmentTests 와 같은 방법).
	void SetEquipmentEffect(UVBEquipmentComponent* Equipment, UClass* EffectClass)
	{
		FClassProperty* Property = CastField<FClassProperty>(UVBEquipmentComponent::StaticClass()->FindPropertyByName(TEXT("EquipmentEffect")));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Equipment, EffectClass);
	}

	using FWorld = FVBIsolatedGameWorld;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnhancementRulesTest, "VB.Unit.Enhancement.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnhancementRulesTest::RunTest(const FString& Parameters)
{
	using namespace VBEnhancementTest;
	const FGameplayTag MaxHealth = FGameplayTag::RequestGameplayTag(TEXT("Data.Stat.MaxHealth"));
	const FGameplayTag DamageTaken = FGameplayTag::RequestGameplayTag(TEXT("Data.Stat.DamageTakenScale"));
	UVBItemDefinition* Helm = MakeGear(VBGameplayTags::Equipment_Slot_Head, {{MaxHealth, 10.0f}});
	UVBItemDefinition* Ring = MakeGear(VBGameplayTags::Equipment_Slot_Charm, {{MaxHealth, 5.0f}});
	UVBItemDefinition* EffectOnlyCharm = MakeGear(VBGameplayTags::Equipment_Slot_Charm, {{MaxHealth, 0.0f}});
	UVBItemDefinition* Plain = NewObject<UVBItemDefinition>();

	TestEqual(TEXT("weapon -> Weapon"), VBEnhancement::CategoryOf(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Blade)),
		EVBEnhanceCategory::Weapon);
	TestEqual(TEXT("head gear -> Armor"), VBEnhancement::CategoryOf(FVBEnhanceTarget::ForItem(Helm)), EVBEnhanceCategory::Armor);
	TestEqual(TEXT("charm with a stat -> Accessory"), VBEnhancement::CategoryOf(FVBEnhanceTarget::ForItem(Ring)),
		EVBEnhanceCategory::Accessory);
	TestEqual(TEXT("charm whose stats are all zero -> None (nothing to scale)"),
		VBEnhancement::CategoryOf(FVBEnhanceTarget::ForItem(EffectOnlyCharm)), EVBEnhanceCategory::None);
	TestEqual(TEXT("non-gear item -> None"), VBEnhancement::CategoryOf(FVBEnhanceTarget::ForItem(Plain)), EVBEnhanceCategory::None);
	FVBEnhanceTarget Both = FVBEnhanceTarget::ForItem(Helm);
	Both.Weapon = EVBWeaponType::Blade;
	TestFalse(TEXT("weapon and item together is not a target"), Both.IsValid());
	TestEqual(TEXT("ambiguous target -> None"), VBEnhancement::CategoryOf(Both), EVBEnhanceCategory::None);

	FVBEnhanceQuote Ok;
	Ok.bTargetValid = true; Ok.Category = EVBEnhanceCategory::Armor; Ok.bOwned = true; Ok.CurrentLevel = 2; Ok.MaxLevel = 5;
	Ok.bTrackUsable = true; Ok.bHasMaterials = true; Ok.GoldRequired = 100; Ok.GoldHeld = 100;
	TestEqual(TEXT("all good -> Ok"), VBEnhancement::Check(Ok), EVBEnhanceResult::Ok);
	FVBEnhanceQuote Q = Ok; Q.bTargetValid = false;
	TestEqual(TEXT("invalid target"), VBEnhancement::Check(Q), EVBEnhanceResult::Invalid);
	Q = Ok; Q.Category = EVBEnhanceCategory::None;
	TestEqual(TEXT("not enhanceable"), VBEnhancement::Check(Q), EVBEnhanceResult::NotEnhanceable);
	Q = Ok; Q.bOwned = false;
	TestEqual(TEXT("not owned"), VBEnhancement::Check(Q), EVBEnhanceResult::NotOwned);
	Q = Ok; Q.bTrackUsable = false;
	TestEqual(TEXT("broken track refuses (no fallback charge)"), VBEnhancement::Check(Q), EVBEnhanceResult::NoTrack);
	Q = Ok; Q.CurrentLevel = 5;
	TestEqual(TEXT("at max"), VBEnhancement::Check(Q), EVBEnhanceResult::MaxLevel);
	Q = Ok; Q.bHasMaterials = false;
	TestEqual(TEXT("missing materials"), VBEnhancement::Check(Q), EVBEnhanceResult::MissingMaterials);
	Q = Ok; Q.GoldHeld = 99;
	TestEqual(TEXT("not enough gold"), VBEnhancement::Check(Q), EVBEnhanceResult::NotEnoughGold);

	const FVBEnhanceTrack Track = MakeTrack({1.1f, 1.2f, 1.3f, 1.4f, 1.5f});
	TestEqual(TEXT("level 0 scale = 1"), VBEnhancement::ScaleAt(&Track, 0), 1.0f);
	TestEqual(TEXT("level 3 scale = third step"), VBEnhancement::ScaleAt(&Track, 3), 1.3f);
	TestEqual(TEXT("level past the track = 1 (apply side cannot block)"), VBEnhancement::ScaleAt(&Track, 9), 1.0f);
	TestEqual(TEXT("no track = 1"), VBEnhancement::ScaleAt(nullptr, 3), 1.0f);

	TestEqual(TEXT("negative level clamps to 0"), VBEnhancement::NormalizeLevel(-2, 5), 0);
	TestEqual(TEXT("level above max clamps"), VBEnhancement::NormalizeLevel(7, 5), 5);
	TestEqual(TEXT("no max (config missing) keeps the saved level"), VBEnhancement::NormalizeLevel(7, 0), 7);

	const TMap<FGameplayTag, float> Scaled = VBEnhancement::ScaledStats({{MaxHealth, 10.0f}, {DamageTaken, -0.08f}}, 1.5f);
	TestEqual(TEXT("positive stat scales up"), Scaled.FindRef(MaxHealth), 15.0f);
	TestEqual(TEXT("negative stat keeps its sign (more reduction)"), Scaled.FindRef(DamageTaken), -0.12f, 1e-5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnhancementConfigTest, "VB.Unit.Enhancement.ConfigTrack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnhancementConfigTest::RunTest(const FString& Parameters)
{
	using namespace VBEnhancementTest;
	UVBEnhancementConfig* Config = MakeConfig();
	TestTrue(TEXT("good weapon track"), Config->IsTrackUsable(EVBEnhanceCategory::Weapon));
	TestFalse(TEXT("None has no track"), Config->IsTrackUsable(EVBEnhanceCategory::None));
	Config->ArmorTrack.Steps.Pop();
	TestFalse(TEXT("a track shorter than MaxLevel is unusable"), Config->IsTrackUsable(EVBEnhanceCategory::Armor));
	Config->AccessoryTrack.Steps[2].Scale = 1.05f;
	TestFalse(TEXT("a decreasing scale is unusable"), Config->IsTrackUsable(EVBEnhanceCategory::Accessory));
	Config->WeaponTrack.Steps[0].Materials.Add(FVBItemAmount());
	TestFalse(TEXT("a material row without an item is unusable"), Config->IsTrackUsable(EVBEnhanceCategory::Weapon));
	UVBEnhancementConfig* Free = MakeConfig();
	Free->WeaponTrack.Steps[1].Gold = 0;
	TestFalse(TEXT("a step with neither gold nor materials is unusable (no free levels)"), Free->IsTrackUsable(EVBEnhanceCategory::Weapon));
	return true;
}

// VB-BEffect 조건 4: +0 과 +N 인 무기 둘 - 단계가 있는 무기만 배율이 붙는다. 손에 든 무기에 거는 것은 무기 상태(VB.Unit.Weapon.Stats).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnhancementWeaponScaleTest, "VB.Unit.Enhancement.WeaponScale",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnhancementWeaponScaleTest::RunTest(const FString& Parameters)
{
	using namespace VBEnhancementTest;
	FWorld W;
	if (!W.Init(*this))
	{
		return false;
	}

	AVBPlayerState* State = W.World->SpawnActor<AVBPlayerState>();
	UVBEnhancementComponent* Enhancement = State ? State->FindComponentByClass<UVBEnhancementComponent>() : nullptr;
	if (!TestNotNull(TEXT("enhancement on the player state"), Enhancement))
	{
		return false;
	}

	FVBEnhancementTestAccess::SetConfig(*Enhancement, MakeConfig());

	TestTrue(TEXT("katana +3 set"), Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Blade), 3));
	TestFalse(TEXT("same level again is no change"), Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Blade), 3));
	TestEqual(TEXT("katana +3 scales"), Enhancement->GetWeaponDamageScale(EVBWeaponType::Blade), 1.25f);
	TestEqual(TEXT("big sword +0 does not scale"), Enhancement->GetWeaponDamageScale(EVBWeaponType::BigSword), 1.0f);
	TestEqual(TEXT("no weapon (None) does not scale"), Enhancement->GetWeaponDamageScale(EVBWeaponType::None), 1.0f);

	TestTrue(TEXT("level above max is clamped"), Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::BigSword), 99));
	TestEqual(TEXT("clamped to 5"), Enhancement->GetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::BigSword)), 5);
	FVBEnhancementTestAccess::SetConfig(*Enhancement, nullptr);
	TestFalse(TEXT("without a config nothing can be raised"),
		Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Fighter), 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnhancementSaveTest, "VB.Unit.Enhancement.SaveRoundTrip",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnhancementSaveTest::RunTest(const FString& Parameters)
{
	using namespace VBEnhancementTest;
	const FGameplayTag HelmId = FGameplayTag::RequestGameplayTag(TEXT("Item.Equipment.Head.BronzeHelm"), false);
	const UVBItemDefinition* Helm = HelmId.IsValid() ? UVBItemDefinition::FindById(HelmId) : nullptr;
	if (!TestNotNull(TEXT("test helm (DA_Item_BronzeHelm)"), Helm))
	{
		return false;
	}

	FWorld W;
	if (!W.Init(*this))
	{
		return false;
	}

	AVBPlayerState* State = W.World->SpawnActor<AVBPlayerState>();
	AVBPlayerState* Other = W.World->SpawnActor<AVBPlayerState>();
	UVBEnhancementComponent* Enhancement = State ? State->FindComponentByClass<UVBEnhancementComponent>() : nullptr;
	UVBEnhancementComponent* OtherEnhancement = Other ? Other->FindComponentByClass<UVBEnhancementComponent>() : nullptr;
	if (!TestNotNull(TEXT("enhancement"), Enhancement) || !TestNotNull(TEXT("other player's enhancement"), OtherEnhancement))
	{
		return false;
	}

	FVBEnhancementTestAccess::SetConfig(*Enhancement, MakeConfig());
	FVBEnhancementTestAccess::SetConfig(*OtherEnhancement, MakeConfig());
	Enhancement->SetLevel(FVBEnhanceTarget::ForItem(Helm), 2);
	Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Fighter), 4);

	FVBSaveContext Context;
	Context.World = W.World;
	Context.PlayerState = State;
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	Enhancement->WriteSave(*Save, Context);
	TestEqual(TEXT("one item row"), Save->Enhancement.Items.Num(), 1);
	TestEqual(TEXT("one weapon row by enum name"), Save->Enhancement.Weapons.Num(), 1);
	TestTrue(TEXT("weapon saved by name"), Save->Enhancement.Weapons.Num() == 1
		&& Save->Enhancement.Weapons[0].WeaponId == FName(TEXT("Fighter")));

	// 손본 세이브: 같은 아이템이 두 번(첫 줄이 이긴다), 모르는 아이템, 무기(남겨 둔다), 최대 초과(자른다).
	Save->Enhancement.Items.Add({HelmId, 5});
	Save->Enhancement.Items.Add({FGameplayTag::RequestGameplayTag(TEXT("Item"), false), 3});
	Save->Enhancement.Weapons.Add({FName(TEXT("NoSuchWeapon")), 2});
	Save->Enhancement.Weapons[0].Level = 9;
	Enhancement->ReadSave(*Save, Context);
	TestEqual(TEXT("duplicate item row: the first wins"), Enhancement->GetLevel(FVBEnhanceTarget::ForItem(Helm)), 2);
	TestEqual(TEXT("saved level above max is clamped"), Enhancement->GetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Fighter)), 5);

	// 다른 플레이어의 문맥이면 읽지도 쓰지도 않는다.
	OtherEnhancement->ReadSave(*Save, Context);
	TestEqual(TEXT("another player's component ignores this context"), OtherEnhancement->GetLevel(FVBEnhanceTarget::ForItem(Helm)), 0);

	UVBSaveGame* Again = NewObject<UVBSaveGame>();
	Enhancement->WriteSave(*Again, Context);
	TestTrue(TEXT("unknown item row is written back unchanged"), Again->Enhancement.Items.ContainsByPredicate(
		[](const FVBSavedItemLevel& Row) { return Row.ItemId == FGameplayTag::RequestGameplayTag(TEXT("Item"), false) && Row.Level == 3; }));
	TestTrue(TEXT("unknown weapon row is written back unchanged"), Again->Enhancement.Weapons.ContainsByPredicate(
		[](const FVBSavedWeaponLevel& Row) { return Row.WeaponId == FName(TEXT("NoSuchWeapon")) && Row.Level == 2; }));
	TestEqual(TEXT("the duplicate is not written twice"), Again->Enhancement.Items.FilterByPredicate(
		[HelmId](const FVBSavedItemLevel& Row) { return Row.ItemId == HelmId; }).Num(), 1);
	return true;
}

// 리뷰 F1, F3: 읽기 세 단계. 저장된 체력이 강화 전 상한을 넘고 강화 뒤 상한 안이면, 읽는 순서와 무관하게 그 값 그대로 돌아온다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnhancementFinalizeTest, "VB.Unit.Enhancement.SaveFinalize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnhancementFinalizeTest::RunTest(const FString& Parameters)
{
	using namespace VBEnhancementTest;
	const FGameplayTag HelmId = FGameplayTag::RequestGameplayTag(TEXT("Item.Equipment.Head.BronzeHelm"), false);
	const UVBItemDefinition* Helm = HelmId.IsValid() ? UVBItemDefinition::FindById(HelmId) : nullptr;
	UClass* StatEffect = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/VB/GAS/Effects/GE_StatBonus.GE_StatBonus_C"));
	if (!TestNotNull(TEXT("test helm (MaxHealth +10)"), Helm) || !TestNotNull(TEXT("GE_StatBonus"), StatEffect))
	{
		return false;
	}

	FWorld W;
	if (!W.Init(*this))
	{
		return false;
	}

	AVBPlayerState* State = W.World->SpawnActor<AVBPlayerState>();
	UVBEnhancementComponent* Enhancement = State ? State->FindComponentByClass<UVBEnhancementComponent>() : nullptr;
	UVBEquipmentComponent* Equipment = State ? State->FindComponentByClass<UVBEquipmentComponent>() : nullptr;
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(State);
	UVBSaveParticipantSubsystem* Registry = W.World->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (!TestNotNull(TEXT("enhancement"), Enhancement) || !TestNotNull(TEXT("equipment"), Equipment)
		|| !TestNotNull(TEXT("ASC"), ASC) || !TestNotNull(TEXT("registry"), Registry))
	{
		return false;
	}

	FVBEnhancementTestAccess::SetConfig(*Enhancement, MakeConfig()); // 방어구 +3 = x2.0
	SetEquipmentEffect(Equipment, StatEffect);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);

	FVBSaveContext Context;
	Context.World = W.World;
	Context.PlayerState = State;
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	Save->Inventory.Items = {{HelmId, 1}};
	Save->Equipment.Slots = {HelmId};
	Save->Enhancement.Items = {{HelmId, 3}};
	// +0 상한 110(100 + 10), +3 상한 120(100 + 10 x 2). 저장 체력 115 = +0 상한 위, +3 상한 안.
	Save->PlayerBuild.AttributeValues.Add(FName(TEXT("Health")), 115.0f);

	IVBSaveParticipant* AsParticipant = static_cast<IVBSaveParticipant*>(State);
	auto IndexOf = [Registry](IVBSaveParticipant* Participant) { return Registry->GetParticipants().IndexOfByKey(Participant); };
	for (const bool bPlayerStateFirst : {false, true})
	{
		// 읽는 순서 = 등록 순서. PlayerState 를 맨 앞으로: 나머지를 빼고 다시 등록한다.
		if (bPlayerStateFirst)
		{
			for (IVBSaveParticipant* Participant : Registry->GetParticipants())
			{
				if (Participant != AsParticipant)
				{
					UVBSaveParticipantSubsystem::Unregister(Participant);
					UVBSaveParticipantSubsystem::Register(Participant);
				}
			}
		}

		const FString Order = bPlayerStateFirst ? TEXT("player state reads first") : TEXT("player state reads last");
		TestTrue(*(Order + TEXT(": order arranged")), bPlayerStateFirst
			? IndexOf(AsParticipant) < IndexOf(static_cast<IVBSaveParticipant*>(Enhancement))
			: IndexOf(AsParticipant) > IndexOf(static_cast<IVBSaveParticipant*>(Enhancement)));
		Registry->ReadAll(*Save, Context);
		TestEqual(*(Order + TEXT(": final cap uses the enhanced helm")),
			ASC->GetNumericAttribute(UVBHealthAttributeSet::GetMaxHealthAttribute()), 120.0f);
		TestEqual(*(Order + TEXT(": saved health above the +0 cap survives")),
			ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 115.0f);
		TestEqual(*(Order + TEXT(": one effect for the one occupied slot")), ASC->GetGameplayEffectCount(StatEffect, nullptr), 1);
	}

	// 살아 있는 강화(저장 읽기 밖): 단계를 올리면 입은 칸만 다시 걸린다 - 효과는 여전히 하나, 상한이 오른다.
	TestTrue(TEXT("helm +5"), Enhancement->SetLevel(FVBEnhanceTarget::ForItem(Helm), 5));
	TestEqual(TEXT("level change re-applies the worn slot (100 + 10 x 2.5)"),
		ASC->GetNumericAttribute(UVBHealthAttributeSet::GetMaxHealthAttribute()), 125.0f);
	// 떼고 다시 거는 사이에도 체력은 비율을 지킨다(UVBHealthAttributeSet::PostAttributeChange): 115 / 120 x 125.
	TestEqual(TEXT("health keeps its ratio across the remove+apply refresh"),
		ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 115.0f / 120.0f * 125.0f, 0.05f);
	TestEqual(TEXT("still one equipment effect after the refresh"), ASC->GetGameplayEffectCount(StatEffect, nullptr), 1);
	Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Blade), 2);
	TestEqual(TEXT("a weapon level change leaves equipment alone"), ASC->GetGameplayEffectCount(StatEffect, nullptr), 1);
	return true;
}

// 제작대의 강화 - 비용은 교환 한 번, 단계는 교환 뒤에만. 거절은 아무것도 안 바꾼다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnhancementStationTest, "VB.Unit.Enhancement.StationEnhance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnhancementStationTest::RunTest(const FString& Parameters)
{
	using namespace VBEnhancementTest;
	FWorld W;
	if (!W.Init(*this))
	{
		return false;
	}

	const UVBItemDefinition* Currency = UVBShopComponent::GetCurrency();
	AVBPlayerState* State = W.World->SpawnActor<AVBPlayerState>();
	AVBPlayerController* PC = W.World->SpawnActor<AVBPlayerController>();
	if (!TestNotNull(TEXT("currency from the economy settings"), Currency) || !TestNotNull(TEXT("player state"), State)
		|| !TestNotNull(TEXT("controller"), PC))
	{
		return false;
	}

	PC->PlayerState = State;
	UVBInventoryComponent* Inventory = State->FindComponentByClass<UVBInventoryComponent>();
	UVBEnhancementComponent* Enhancement = State->FindComponentByClass<UVBEnhancementComponent>();
	UVBCraftingComponent* Crafting = PC->FindComponentByClass<UVBCraftingComponent>();
	if (!TestNotNull(TEXT("inventory"), Inventory) || !TestNotNull(TEXT("enhancement"), Enhancement)
		|| !TestNotNull(TEXT("crafting"), Crafting))
	{
		return false;
	}

	FVBEnhancementTestAccess::SetConfig(*Enhancement, MakeConfig()); // 단계마다 돈 10, 최대 5

	const FGameplayTag MaxHealth = FGameplayTag::RequestGameplayTag(TEXT("Data.Stat.MaxHealth"));
	UVBItemDefinition* Helm = MakeGear(VBGameplayTags::Equipment_Slot_Head, {{MaxHealth, 10.0f}});
	Helm->ItemId = VBGameplayTags::Melee_Shape_HandL;
	UVBItemDefinition* Ring = MakeGear(VBGameplayTags::Equipment_Slot_Charm, {{MaxHealth, 5.0f}});
	Ring->ItemId = VBGameplayTags::Melee_Shape_FootR;
	Inventory->AddItem(Helm, 1);
	Inventory->AddItem(Currency, 25);
	const FVBEnhanceTarget HelmTarget = FVBEnhanceTarget::ForItem(Helm);
	auto Snapshot = [&]() { return FString::Printf(TEXT("helm +%d gold %d"), Enhancement->GetLevel(HelmTarget), Inventory->GetCount(Currency)); };

	UVBCraftingStationDefinition* Smith = NewObject<UVBCraftingStationDefinition>();
	TestTrue(TEXT("smith opens"), Crafting->OpenStation(Smith, nullptr));
	Crafting->RequestEnhance(HelmTarget);
	TestEqual(TEXT("enhance takes the step cost and raises the level"), Snapshot(), FString(TEXT("helm +1 gold 15")));
	Crafting->RequestEnhance(HelmTarget);
	TestEqual(TEXT("second step"), Snapshot(), FString(TEXT("helm +2 gold 5")));
	TestEqual(TEXT("preview agrees: not enough gold"), Crafting->PreviewEnhance(HelmTarget), EVBEnhanceResult::NotEnoughGold);
	Crafting->RequestEnhance(HelmTarget);
	TestEqual(TEXT("not enough gold changes nothing"), Snapshot(), FString(TEXT("helm +2 gold 5")));

	Inventory->AddItem(Currency, 100);
	Crafting->RequestEnhance(FVBEnhanceTarget::ForItem(Ring));
	TestEqual(TEXT("an item not held is refused"), Enhancement->GetLevel(FVBEnhanceTarget::ForItem(Ring)), 0);
	Crafting->RequestEnhance(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Blade));
	TestEqual(TEXT("a weapon the pawn does not own is refused"),
		Enhancement->GetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Blade)), 0);
	TestEqual(TEXT("refusals take no gold"), Inventory->GetCount(Currency), 105);

	Enhancement->SetLevel(HelmTarget, 5);
	Crafting->RequestEnhance(HelmTarget);
	TestEqual(TEXT("max level changes nothing"), Snapshot(), FString(TEXT("helm +5 gold 105")));
	Enhancement->SetLevel(HelmTarget, 2);
	FVBEnhancementTestAccess::SetConfig(*Enhancement, nullptr);
	Crafting->RequestEnhance(HelmTarget);
	TestEqual(TEXT("no config: refused without charge"), Snapshot(), FString(TEXT("helm +2 gold 105")));
	FVBEnhancementTestAccess::SetConfig(*Enhancement, MakeConfig());

	// 강화를 하지 않는 제작대: 서버가 거절한다(화면은 탭을 숨긴다).
	Crafting->CloseStation();
	UVBCraftingStationDefinition* Bench = NewObject<UVBCraftingStationDefinition>();
	Bench->bAllowEnhancement = false;
	TestTrue(TEXT("bench opens"), Crafting->OpenStation(Bench, nullptr));
	AddExpectedMessagePlain(TEXT("강화를 하지 않는 제작대"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Crafting->RequestEnhance(HelmTarget);
	TestEqual(TEXT("a station without enhancement changes nothing"), Snapshot(), FString(TEXT("helm +2 gold 105")));
	return true;
}

#endif
