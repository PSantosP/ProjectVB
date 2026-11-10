#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayEffect.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/Package.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Inventory/Equipment/VBEquipmentRules.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Player/VBPlayerState.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"

// 장비 판정의 단위 시험.
// 아이템 ID 는 판정과 무관하다 - 네이티브 태그를 빌려 쓴다.

#if WITH_AUTOMATION_TESTS
namespace
{
	UVBItemDefinition* MakeGear(FGameplayTag Id, FGameplayTag SlotType)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>();
		Item->ItemId = Id;
		FVBItemFragment_Equippable Equippable;
		Equippable.SlotType = SlotType;
		Item->Fragments.Add(FInstancedStruct::Make(Equippable));
		return Item;
	}

	TArray<FGameplayTag> DefaultLayout()
	{
		return {VBGameplayTags::Equipment_Slot_Head, VBGameplayTags::Equipment_Slot_Body, VBGameplayTags::Equipment_Slot_Arms,
			VBGameplayTags::Equipment_Slot_Charm, VBGameplayTags::Equipment_Slot_Charm};
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEquipmentRulesTest, "VB.Unit.Equipment.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEquipmentRulesTest::RunTest(const FString& Parameters)
{
	const TArray<FGameplayTag> Layout = DefaultLayout();
	UVBItemDefinition* Hood = MakeGear(VBGameplayTags::Melee_Shape_Weapon, VBGameplayTags::Equipment_Slot_Head);
	UVBItemDefinition* CharmA = MakeGear(VBGameplayTags::Melee_Shape_HandL, VBGameplayTags::Equipment_Slot_Charm);
	UVBItemDefinition* CharmB = MakeGear(VBGameplayTags::Melee_Shape_HandR, VBGameplayTags::Equipment_Slot_Charm);
	UVBItemDefinition* CharmC = MakeGear(VBGameplayTags::Melee_Shape_FootL, VBGameplayTags::Equipment_Slot_Charm);
	UVBItemDefinition* Plain = NewObject<UVBItemDefinition>();

	TestTrue(TEXT("hood fits the head"), VBEquipment::Fits(Hood, VBGameplayTags::Equipment_Slot_Head));
	TestFalse(TEXT("hood does not fit the body"), VBEquipment::Fits(Hood, VBGameplayTags::Equipment_Slot_Body));
	TestFalse(TEXT("an item without the fragment fits nothing"), VBEquipment::Fits(Plain, VBGameplayTags::Equipment_Slot_Head));

	TArray<const UVBItemDefinition*> Worn;
	FVBEquipOutcome Out;

	TestTrue(TEXT("not equippable is refused"),
		(Out = VBEquipment::Equip(Layout, Worn, Plain, INDEX_NONE)).Result == EVBEquipResult::NotEquippable);
	TestTrue(TEXT("hood goes to the head (auto)"),
		(Out = VBEquipment::Equip(Layout, Worn, Hood, INDEX_NONE)).Result == EVBEquipResult::Changed && Out.Slot == 0);
	TestTrue(TEXT("again (auto) is unchanged"),
		(Out = VBEquipment::Equip(Layout, Worn, Hood, INDEX_NONE)).Result == EVBEquipResult::Unchanged && Out.Slot == 0);
	TestTrue(TEXT("asking a wrong-type slot is refused"),
		(Out = VBEquipment::Equip(Layout, Worn, Hood, 1)).Result == EVBEquipResult::NoFittingSlot);

	// 장신구 둘: 빈 칸 먼저, 다 찼으면 첫 장신구 칸을 바꾼다.
	TestTrue(TEXT("first charm -> first charm slot"),
		(Out = VBEquipment::Equip(Layout, Worn, CharmA, INDEX_NONE)).Result == EVBEquipResult::Changed && Out.Slot == 3);
	TestTrue(TEXT("second charm -> the empty charm slot"),
		(Out = VBEquipment::Equip(Layout, Worn, CharmB, INDEX_NONE)).Result == EVBEquipResult::Changed && Out.Slot == 4);
	TestTrue(TEXT("third charm replaces the first charm slot"),
		(Out = VBEquipment::Equip(Layout, Worn, CharmC,
		INDEX_NONE)).Result == EVBEquipResult::Changed && Out.Slot == 3 && Out.Replaced == CharmA);

	// 한 아이템 한 칸: 4번 칸의 부적을 3번으로 옮기면 4번은 빈다.
	TestTrue(TEXT("moving a charm vacates its old slot"),
		(Out = VBEquipment::Equip(Layout, Worn, CharmB,
		3)).Result == EVBEquipResult::Changed && Out.Slot == 3 && Out.Vacated == 4 && Out.Replaced == CharmC);
	TestTrue(TEXT("no charm sits in two slots"), Worn[3] == CharmB && Worn[4] == nullptr);
	TestTrue(TEXT("other slots untouched"), Worn[0] == Hood && Worn[1] == nullptr && Worn[2] == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEquipmentComponentRefuseTest, "VB.Unit.Equipment.RefuseWithoutOwner",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEquipmentComponentRefuseTest::RunTest(const FString& Parameters)
{
	// 주인 없는 장비 = 가진 것도 ASC 도 없는 자리. 판정은 통과하는 장비라도 입히지 않는다 - 효과 없는 장착은 거짓 상태다.
	UVBEquipmentComponent* Equipment = NewObject<UVBEquipmentComponent>();
	UVBItemDefinition* Hood = MakeGear(VBGameplayTags::Melee_Shape_Weapon, VBGameplayTags::Equipment_Slot_Head);
	TestFalse(TEXT("no owner (no inventory, no ASC) refuses"), Equipment->Equip(Hood, INDEX_NONE));
	TestTrue(TEXT("nothing is worn after a refusal"), Equipment->GetEquipped(0) == nullptr);
	TestEqual(TEXT("default layout = 4 armor + 2 charms"), Equipment->GetSlotLayout().Num(), 6);
	TestEqual(TEXT("legs is the fourth slot (screen order)"),
		Equipment->GetSlotLayout().IndexOfByKey(VBGameplayTags::Equipment_Slot_Legs), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEquipmentSavedSlotMapTest, "VB.Unit.Equipment.SavedSlotMap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEquipmentSavedSlotMapTest::RunTest(const FString& Parameters)
{
	// 저장은 칸 순서 번호라, 칸이 끼면 번호 그대로 읽을 때 옆 칸 아이템이 밀려 버려진다.
	const TArray<FGameplayTag> Legacy = VBEquipment::LegacySaveLayout();
	const TArray<FGameplayTag> Current = NewObject<UVBEquipmentComponent>()->GetSlotLayout();
	TestTrue(TEXT("legacy layout = the five slots before legs"), Legacy == DefaultLayout());
	TestEqual(TEXT("legacy head -> head"), VBEquipment::MapSavedSlot(Legacy, 0, Current), 0);
	TestEqual(TEXT("legacy arms -> arms"), VBEquipment::MapSavedSlot(Legacy, 2, Current), 2);
	TestEqual(TEXT("legacy first charm -> first charm (not legs)"), VBEquipment::MapSavedSlot(Legacy, 3, Current), 4);
	TestEqual(TEXT("legacy second charm -> second charm"), VBEquipment::MapSavedSlot(Legacy, 4, Current), 5);
	for (int32 Index = 0; Index < Current.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("same layout keeps slot %d"), Index), VBEquipment::MapSavedSlot(Current, Index, Current), Index);
	}

	const TArray<FGameplayTag> Gone = {VBGameplayTags::Equipment_Slot_Head, VBGameplayTags::Appearance_Slot_Top}; // 배치에 없는 아무 유효 tag
	TestEqual(TEXT("a slot kind the layout no longer has -> none"), VBEquipment::MapSavedSlot(Gone, 1, Current), INDEX_NONE);
	TestEqual(TEXT("a third charm in an old save -> none (only two now)"), VBEquipment::MapSavedSlot(
		{VBGameplayTags::Equipment_Slot_Charm, VBGameplayTags::Equipment_Slot_Charm, VBGameplayTags::Equipment_Slot_Charm}, 2, Current), INDEX_NONE);
	TestEqual(TEXT("an index past the saved layout -> none"), VBEquipment::MapSavedSlot(Legacy, Legacy.Num(), Current), INDEX_NONE);
	return true;
}

namespace
{
	// EquipmentEffect 는 BP 기본값(EditDefaultsOnly)이라 시험은 리플렉션으로 꽂는다 - 네이티브 PlayerState 에는 비어 있다.
	void SetEquipmentEffect(UVBEquipmentComponent* Equipment, UClass* EffectClass)
	{
		FClassProperty* Property = CastField<FClassProperty>(UVBEquipmentComponent::StaticClass()->FindPropertyByName(TEXT("EquipmentEffect")));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Equipment, EffectClass);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEquipmentSaveReadTest, "VB.Unit.Equipment.SaveRead",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEquipmentSaveReadTest::RunTest(const FString& Parameters)
{
	// 저장 읽기에서 1. 가지지 않은 장비가 효과와 함께 남지 않는가(읽는 순서와 무관하게 - 다 읽힌 뒤 판정),
	//   2. 효과가 안 걸리는 장비를 입은 칸으로 남기지 않는가. 판정이 실제 자산을 쓰는 길(FindById, GE_StatBonus)이라 시험 장비 자산 하나에 기댄다.
	const FGameplayTag HelmId = FGameplayTag::RequestGameplayTag(TEXT("Item.Equipment.Head.BronzeHelm"), false);
	const UVBItemDefinition* Helm = HelmId.IsValid() ? UVBItemDefinition::FindById(HelmId) : nullptr;
	// 장비, 성장 공용 능력치 효과.
	UClass* EquipmentEffect = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/VB/GAS/Effects/GE_StatBonus.GE_StatBonus_C"));
	if (!TestNotNull(TEXT("test helm (DA_Item_BronzeHelm)"), Helm) || !TestNotNull(TEXT("GE_StatBonus"), EquipmentEffect))
	{
		return false;
	}

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = GEngine ? UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num,
		&Init) : nullptr;
	if (!TestNotNull(TEXT("owned world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	// 액터 초기화를 먼저 끝내야 스폰 때 InitializeComponent 가 불린다(AActor::PostActorConstruction) - 장비가 참여자로 등록되고 OnSaveApplied 를 듣는 자리.
	World->InitializeActorsForPlay(FURL());
	AVBPlayerState* State = World->SpawnActor<AVBPlayerState>();
	UVBEquipmentComponent* Equipment = State ? State->FindComponentByClass<UVBEquipmentComponent>() : nullptr;
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(State);
	UVBSaveParticipantSubsystem* Registry = World->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (TestNotNull(TEXT("equipment on the player state"), Equipment) && TestNotNull(TEXT("ASC"), ASC)
		&& TestNotNull(TEXT("save registry"), Registry)
		// 등록이 안 됐으면 아래 첫 확인("남지 않는다")이 아무것도 안 하고 통과한다 - 먼저 막는다.
		&& TestTrue(TEXT("equipment is a registered save participant"),
			Registry->GetParticipants().Contains(static_cast<IVBSaveParticipant*>(Equipment))))
	{
		SetEquipmentEffect(Equipment, EquipmentEffect);
		FVBSaveContext Context;
		Context.World = World;
		Context.PlayerState = State;
		UVBSaveGame* Save = NewObject<UVBSaveGame>();
		Save->Equipment.Slots = {HelmId};

		// 대조: 장비 구역만 읽히고 "다 읽힘" 신호가 없으면 가지지 않은 투구가 입힌 채로 남는다 - 이 시험이 막는 결함 상태가 실제로 생긴다.
		//   효과는 읽기 두 번째 단계("다 읽힘" 신호)에서 건다 - 여기서는 아직 없다.
		Equipment->ReadSave(*Save, Context);
		TestTrue(TEXT("control: before the applied signal the unowned helm is still worn"), Equipment->GetEquipped(0) == Helm);
		TestEqual(TEXT("identity only: no effect before the applied signal"), ASC->GetGameplayEffectCount(EquipmentEffect, nullptr), 0);
		Registry->OnSaveApplied.Broadcast();
		TestNull(TEXT("the applied signal takes the unowned helm off"), Equipment->GetEquipped(0));

		Registry->ReadAll(*Save, Context);
		TestNull(TEXT("a saved helm the inventory does not hold is not worn after the load"), Equipment->GetEquipped(0));
		TestEqual(TEXT("and leaves no equipment effect"), ASC->GetGameplayEffectCount(EquipmentEffect, nullptr), 0);

		FVBSavedItem Owned;
		Owned.ItemId = HelmId;
		Owned.Count = 1;
		Save->Inventory.Items = {Owned};
		Registry->ReadAll(*Save, Context);
		TestTrue(TEXT("an owned saved helm is worn in its slot"), Equipment->GetEquipped(0) == Helm);
		TestEqual(TEXT("with exactly one equipment effect"), ASC->GetGameplayEffectCount(EquipmentEffect, nullptr), 1);

		// 읽는 순서 두 가지. 읽는 순서 = 등록 순서라, 빼고 다시 등록하면 맨 뒤로 간다.
		//   소유한 투구 + 저장에만 있는 반지(장신구 칸 3) - 어느 순서든 투구는 남고 반지는 벗겨져야 한다.
		//   BeginPlay 로 인벤토리 변경 구독을 살려, 적용 도중 인벤토리 알림이 장비에 닿는 길(미뤄져야 한다)까지 탄다.
		const FGameplayTag RingId = FGameplayTag::RequestGameplayTag(TEXT("Item.Equipment.Charm.SilverRing"), false);
		const UVBItemDefinition* Ring = RingId.IsValid() ? UVBItemDefinition::FindById(RingId) : nullptr;
		UVBInventoryComponent* Inventory = State->FindComponentByClass<UVBInventoryComponent>();
		AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
		if (TestNotNull(TEXT("test ring (DA_Item_SilverRing)"), Ring) && TestNotNull(TEXT("inventory"), Inventory)
			&& TestNotNull(TEXT("game state"), GameState))
		{
			GameState->HandleBeginPlay();
			TestTrue(TEXT("equipment has begun play (inventory subscription live)"), Equipment->HasBegunPlay());
			// 배치를 안 적은 옛 저장: 저장 칸 3 = 옛 첫 장신구 -> 지금 첫 장신구 칸(하의 뒤).
			Save->Equipment.Slots = {HelmId, FGameplayTag(), FGameplayTag(), RingId};
			Save->Equipment.Layout.Reset();
			const int32 RingSlot = Equipment->GetSlotLayout().IndexOfByKey(VBGameplayTags::Equipment_Slot_Charm);
			auto IndexOf = [Registry](IVBSaveParticipant* Participant) { return Registry->GetParticipants().IndexOfByKey(Participant); };
			for (IVBSaveParticipant* Last : {static_cast<IVBSaveParticipant*>(Inventory), static_cast<IVBSaveParticipant*>(Equipment)})
			{
				UVBSaveParticipantSubsystem::Unregister(Last);
				UVBSaveParticipantSubsystem::Register(Last);
				const bool bInventoryLast = Last == static_cast<IVBSaveParticipant*>(Inventory);
				TestTrue(bInventoryLast ? TEXT("order: inventory reads after equipment") : TEXT("order: equipment reads after inventory"),
					bInventoryLast ? IndexOf(Inventory) > IndexOf(Equipment) : IndexOf(Equipment) > IndexOf(Inventory));
				Registry->ReadAll(*Save, Context);
				TestTrue(TEXT("the owned helm stays worn whatever the read order"), Equipment->GetEquipped(0) == Helm);
				TestNull(TEXT("the unowned ring is taken off whatever the read order"), Equipment->GetEquipped(RingSlot));
				TestEqual(TEXT("one equipment effect (the helm's) whatever the read order"),
					ASC->GetGameplayEffectCount(EquipmentEffect, nullptr), 1);
			}
			// 읽기는 저장된 배치를 써야 한다 - 옛 3번의 반지는 지금 3번인 하의 칸이 아니라 첫 장신구 칸에 간다.
			FVBSavedItem OwnedRing;
			OwnedRing.ItemId = RingId;
			OwnedRing.Count = 1;
			Save->Inventory.Items = {Owned, OwnedRing};
			Registry->ReadAll(*Save, Context);
			TestTrue(TEXT("an owned ring from an old save is worn in the first charm slot"), Equipment->GetEquipped(RingSlot) == Ring);
			TestNull(TEXT("and nothing lands in the legs slot"),
				Equipment->GetEquipped(Equipment->GetSlotLayout().IndexOfByKey(VBGameplayTags::Equipment_Slot_Legs)));

			// 쓰고 다시 읽으면 쓰기가 기록한 배치로 투구와 반지가 제자리에 남는다.
			Equipment->WriteSave(*Save, Context);
			TestTrue(TEXT("round trip: the write records today's layout"), Save->Equipment.Layout == Equipment->GetSlotLayout());
			Registry->ReadAll(*Save, Context);
			TestTrue(TEXT("round trip: the helm comes back in the head slot"), Equipment->GetEquipped(0) == Helm);
			TestTrue(TEXT("round trip: the ring comes back in the first charm slot"), Equipment->GetEquipped(RingSlot) == Ring);

			// 순서가 다른 기록 배치는 번호가 아니라 칸 종류로 맞춘다. 지금 배치보다 많은 세 번째 장신구는 칸이 없어 버린다.
			Save->Equipment.Layout = {VBGameplayTags::Equipment_Slot_Charm, VBGameplayTags::Equipment_Slot_Head, VBGameplayTags::Equipment_Slot_Charm,
				VBGameplayTags::Equipment_Slot_Charm};
			Save->Equipment.Slots = {RingId, HelmId, FGameplayTag(), RingId};
			Registry->ReadAll(*Save, Context);
			TestTrue(TEXT("reordered layout: the helm lands in the head slot"), Equipment->GetEquipped(0) == Helm);
			TestTrue(TEXT("reordered layout: the ring lands in the first charm slot"), Equipment->GetEquipped(RingSlot) == Ring);
			TestEqual(TEXT("reordered layout: still one slot per kind - nothing outside the layout"), Equipment->GetSlotLayout().Num(), 6);
			Save->Equipment.Layout.Reset();
			Save->Inventory.Items = {Owned};
		}

		// 효과가 안 걸리는 경우 = 순간 GE(기본 UGameplayEffect). 활성 핸들을 남기지 않는다는 전제부터 본다.
		SetEquipmentEffect(Equipment, UGameplayEffect::StaticClass());
		const FGameplayEffectSpecHandle Probe = ASC->MakeOutgoingSpec(UGameplayEffect::StaticClass(), 1.0f, ASC->MakeEffectContext());
		TestFalse(TEXT("control: an instant effect leaves no active handle"), Probe.IsValid()
			&& ASC->ApplyGameplayEffectSpecToSelf(*Probe.Data.Get()).IsValid());
		Registry->ReadAll(*Save, Context);
		TestNull(TEXT("a helm whose effect cannot be applied is not left worn by the load"), Equipment->GetEquipped(0));
		TestFalse(TEXT("equipping it directly is refused"), Equipment->Equip(Helm, INDEX_NONE));
		TestNull(TEXT("and the refusal leaves the slot empty"), Equipment->GetEquipped(0));
		TestEqual(TEXT("no equipment effect remains"), ASC->GetGameplayEffectCount(EquipmentEffect, nullptr), 0);
	}

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	// 직접 만든 월드 패키지만 정리한다.
	Package->SetDirtyFlag(false);
	return true;
}
#endif
