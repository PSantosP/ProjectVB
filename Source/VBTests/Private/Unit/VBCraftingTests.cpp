// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBGameplayTags.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Crafting/VBCraftingComponent.h"
#include "Inventory/Crafting/VBCraftingRules.h"
#include "Inventory/Crafting/VBCraftingStationDefinition.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Player/VBPlayerController.h"
#include "Player/VBPlayerState.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Inventory/Enhancement/VBEnhancementConfig.h"
#include "Inventory/Enhancement/VBEnhancementSettings.h"
#include "UI/Crafting/VBCraftingScreenWidget.h"
#include "UI/VBUISettings.h"

// 제작: 판정 표, 그리고 서버 길(열기 -> 요청 -> 다시 판정 -> 교환 한 번).
//   돈은 실제 경제 설정의 돈 아이템이다(상점과 같은 출처) - 없으면 시험이 그 사실로 실패한다.

namespace VBCraftingTest
{
	UVBItemDefinition* MakeItem(FGameplayTag Id, int32 MaxCount)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>();
		Item->ItemId = Id;
		Item->MaxCount = MaxCount;
		return Item;
	}

	FVBCraftRecipe MakeRecipe(const UVBItemDefinition* Output, const UVBItemDefinition* Input, int32 InputCount, int32 Gold)
	{
		FVBCraftRecipe Recipe;
		Recipe.Output.Item = Output;
		Recipe.Output.Count = 1;
		FVBItemAmount& Row = Recipe.Inputs.AddDefaulted_GetRef();
		Row.Item = Input;
		Row.Count = InputCount;
		Recipe.Gold = Gold;
		return Recipe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCraftingCheckTest, "VB.Unit.Crafting.CheckCraft",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCraftingCheckTest::RunTest(const FString& Parameters)
{
	FVBCraftQuote Ok;
	Ok.bValid = true;
	Ok.bHasInputs = true;
	Ok.GoldRequired = 10;
	Ok.GoldHeld = 10;
	Ok.bOutputFits = true;
	TestEqual(TEXT("everything present"), VBCrafting::CheckCraft(Ok), EVBCraftResult::Ok);
	FVBCraftQuote Q = Ok; Q.bValid = false;
	TestEqual(TEXT("invalid first"), VBCrafting::CheckCraft(Q), EVBCraftResult::Invalid);
	Q = Ok; Q.bAvailable = false; Q.bHasInputs = false;
	TestEqual(TEXT("locked before inputs"), VBCrafting::CheckCraft(Q), EVBCraftResult::Locked);
	Q = Ok; Q.bHasInputs = false; Q.GoldHeld = 0;
	TestEqual(TEXT("inputs before gold"), VBCrafting::CheckCraft(Q), EVBCraftResult::MissingInputs);
	Q = Ok; Q.GoldHeld = 9; Q.bOutputFits = false;
	TestEqual(TEXT("gold before room"), VBCrafting::CheckCraft(Q), EVBCraftResult::NotEnoughGold);
	Q = Ok; Q.bOutputFits = false;
	TestEqual(TEXT("no room"), VBCrafting::CheckCraft(Q), EVBCraftResult::NoRoom);

	// 비용 줄: 재료 + 돈 한 줄. 돈이 0 이면 돈 줄이 없다.
	UVBItemDefinition* Herb = VBCraftingTest::MakeItem(VBGameplayTags::Melee_Shape_Weapon, 10);
	UVBItemDefinition* Gold = VBCraftingTest::MakeItem(VBGameplayTags::Melee_Shape_HandR, 0);
	UVBItemDefinition* Gauze = VBCraftingTest::MakeItem(VBGameplayTags::Melee_Shape_FootL, 5);
	const TArray<FVBItemAmount> Costs = VBCrafting::RecipeCosts(VBCraftingTest::MakeRecipe(Gauze, Herb, 2, 10), Gold);
	TestEqual(TEXT("inputs + gold row"), Costs.Num(), 2);
	TestTrue(TEXT("gold row is last"), Costs.Num() == 2 && Costs[1].Item == Gold && Costs[1].Count == 10);
	TestEqual(TEXT("no gold row for a materials-only recipe"),
		VBCrafting::RecipeCosts(VBCraftingTest::MakeRecipe(Gauze, Herb, 2, 0), Gold).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCraftingServerTest, "VB.Unit.Crafting.ServerCraft",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCraftingServerTest::RunTest(const FString& Parameters)
{
	using namespace VBCraftingTest;
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld* World = Isolated.World;

	const UVBItemDefinition* Currency = UVBShopComponent::GetCurrency();
	AVBPlayerState* State = World->SpawnActor<AVBPlayerState>();
	AVBPlayerController* PC = World->SpawnActor<AVBPlayerController>();
	if (!TestNotNull(TEXT("currency from the economy settings"), Currency) || !TestNotNull(TEXT("player state"), State)
		|| !TestNotNull(TEXT("controller"), PC))
	{
		return false;
	}

	PC->PlayerState = State;
	UVBInventoryComponent* Inventory = State->FindComponentByClass<UVBInventoryComponent>();
	UVBCraftingComponent* Crafting = PC->FindComponentByClass<UVBCraftingComponent>();
	if (!TestNotNull(TEXT("inventory"), Inventory) || !TestNotNull(TEXT("crafting component on the controller"), Crafting))
	{
		return false;
	}

	UVBItemDefinition* Herb = MakeItem(VBGameplayTags::Melee_Shape_Weapon, 10);
	UVBItemDefinition* Gauze = MakeItem(VBGameplayTags::Melee_Shape_FootL, 2);
	UVBCraftingStationDefinition* Station = NewObject<UVBCraftingStationDefinition>();
	Station->Recipes.Add(MakeRecipe(Gauze, Herb, 2, 10));
	FVBCraftRecipe Locked = MakeRecipe(Gauze, Herb, 1, 0);
	FVBCondition_Fact Never;
	Never.Fact = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.Never"));
	Locked.Conditions.Add(FInstancedStruct::Make(Never));
	Station->Recipes.Add(Locked);
	Inventory->AddItem(Herb, 3);
	Inventory->AddItem(Currency, 15);
	auto Snapshot = [&]() { return FString::Printf(TEXT("herb %d gold %d gauze %d"), Inventory->GetCount(Herb), Inventory->GetCount(Currency), Inventory->GetCount(Gauze)); };

	// 이 세계엔 사실 저장소가 없어 잠긴 제작법의 조건이 거짓으로 떨어진다(경고와 함께).
	AddExpectedMessagePlain(TEXT("문맥에 사실 저장소가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 0);
	TestTrue(TEXT("station opens"), Crafting->OpenStation(Station, nullptr));
	TestFalse(TEXT("a second open is refused"), Crafting->OpenStation(Station, nullptr));
	const FVBCraftingView& View = Crafting->GetView();
	TestEqual(TEXT("view lists both recipes"), View.Entries.Num(), 2);
	TestTrue(TEXT("locked recipe is listed as unavailable"), View.Entries.Num() == 2 && !View.Entries[1].bAvailable);

	Crafting->RequestCraft(1);
	TestEqual(TEXT("locked recipe takes nothing"), Snapshot(), FString(TEXT("herb 3 gold 15 gauze 0")));
	Crafting->RequestCraft(0);
	TestEqual(TEXT("craft takes inputs and gold, gives output"), Snapshot(), FString(TEXT("herb 1 gold 5 gauze 1")));
	Crafting->RequestCraft(0);
	TestEqual(TEXT("missing inputs take nothing"), Snapshot(), FString(TEXT("herb 1 gold 5 gauze 1")));
	Inventory->AddItem(Herb, 4);
	Crafting->RequestCraft(0);
	TestEqual(TEXT("not enough gold takes nothing"), Snapshot(), FString(TEXT("herb 5 gold 5 gauze 1")));
	Inventory->AddItem(Currency, 100);
	Inventory->AddItem(Gauze, 1);
	TestEqual(TEXT("preview agrees: output full"), Crafting->PreviewCraft(View.Entries[0]), EVBCraftResult::NoRoom);
	Crafting->RequestCraft(0);
	TestEqual(TEXT("no room takes nothing"), Snapshot(), FString(TEXT("herb 5 gold 105 gauze 2")));
	AddExpectedMessagePlain(TEXT("번 제작법이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Crafting->RequestCraft(99);
	TestEqual(TEXT("unknown recipe takes nothing"), Snapshot(), FString(TEXT("herb 5 gold 105 gauze 2")));

	Crafting->RequestClose();
	TestFalse(TEXT("closed on request"), Crafting->IsStationOpen());
	TestFalse(TEXT("client view cleared"), Crafting->GetView().IsActive());
	Inventory->RemoveItem(Gauze, 2);
	Crafting->RequestCraft(0);
	TestEqual(TEXT("a request after close does nothing"), Inventory->GetCount(Gauze), 0);
	return true;
}

// 실제 자산 배선: 화면 태그가 GameMenu 층의 WBP_Crafting 으로 가고, 강화 수치가 설정에서 풀리며 세 갈래가 다 쓸 수 있고,
//   두 제작대의 제작법이 결과, 비용을 가졌다. 끊기면 제작대가 아무것도 안 띄우거나(UI 관리자는 경고만) 강화가 조용히 꺼진다 - 그래서 실제 자산으로 잠근다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCraftingAssetsTest, "VB.Unit.Crafting.Assets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCraftingAssetsTest::RunTest(const FString& Parameters)
{
	const FVBScreenRoute* Route = GetDefault<UVBUISettings>()->Screens.Find(VBGameplayTags::UI_Screen_Crafting);
	if (!TestNotNull(TEXT("crafting screen has a route (DefaultGame.ini Screens)"), Route))
	{
		return false;
	}

	const FGameplayTag GameMenu = FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.GameMenu"), false);
	TestTrue(TEXT("GameMenu layer tag exists (else an unset layer would compare equal)"), GameMenu.IsValid());
	TestTrue(TEXT("route layer is GameMenu (same as the shop)"), GameMenu.IsValid() && Route->Layer == GameMenu);
	UClass* ScreenClass = Route->ScreenClass.LoadSynchronous();
	if (!TestNotNull(TEXT("route class loads"), ScreenClass))
	{
		return false;
	}

	TestTrue(TEXT("route class is the crafting screen"), ScreenClass->IsChildOf(UVBCraftingScreenWidget::StaticClass()));
	const FClassProperty* RowClass = FindFProperty<FClassProperty>(UVBCraftingScreenWidget::StaticClass(), TEXT("EntryWidgetClass"));
	TestTrue(TEXT("WBP sets EntryWidgetClass"), RowClass
		&& RowClass->GetPropertyValue_InContainer(GetDefault<UObject>(ScreenClass)) != nullptr);
	// 선택 바인딩은 빠져도 조용하다 - 행동에 꼭 필요한 위젯이 WBP 에 있는지 본다.
	const UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(ScreenClass);
	const UWidgetTree* Tree = WidgetClass ? WidgetClass->GetWidgetTreeArchetype() : nullptr;
	for (const TCHAR* Name : {TEXT("EntryBox"), TEXT("BtnAction"), TEXT("BtnTabCraft"), TEXT("BtnTabEnhance"), TEXT("TxtCost"), TEXT("TxtStatus")})
	{
		TestTrue(FString::Printf(TEXT("WBP has %s"), Name), Tree && Tree->FindWidget(Name) != nullptr);
	}

	const UVBEnhancementConfig* Config = UVBEnhancementSettings::LoadConfig( /*bWarnIfMissing=*/false);
	if (!TestNotNull(TEXT("enhancement config resolves from the settings"), Config))
	{
		return false;
	}

	for (const EVBEnhanceCategory Category : {EVBEnhanceCategory::Weapon, EVBEnhanceCategory::Armor, EVBEnhanceCategory::Accessory})
	{
		TestTrue(FString::Printf(TEXT("track usable: %s"), *UEnum::GetValueAsString(Category)), Config->IsTrackUsable(Category));
	}

	for (const TCHAR* Path : {TEXT("/Game/VB/Items/Crafting/DA_CraftStation_Smith.DA_CraftStation_Smith"),
		TEXT("/Game/VB/Items/Crafting/DA_CraftStation_Bench.DA_CraftStation_Bench")})
	{
		const UVBCraftingStationDefinition* Station = LoadObject<UVBCraftingStationDefinition>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("station loads: %s"), Path), Station))
		{
			continue;
		}

		TestTrue(FString::Printf(TEXT("%s has recipes"), Path), !Station->Recipes.IsEmpty());
		for (int32 Index = 0; Index < Station->Recipes.Num(); ++Index)
		{
			const FVBCraftRecipe& Recipe = Station->Recipes[Index];
			const FString Where = FString::Printf(TEXT("%s recipe %d"), *Station->GetName(), Index);
			TestTrue(*(Where + TEXT(": has an output")), Recipe.Output.Item != nullptr && Recipe.Output.Count > 0);
			TestTrue(*(Where + TEXT(": costs something")), !Recipe.Inputs.IsEmpty() || Recipe.Gold > 0);
			TestFalse(*(Where + TEXT(": inputs are set")), Recipe.Inputs.ContainsByPredicate([](const FVBItemAmount& Input) { return !Input.Item; }));
		}
	}

	return true;
}
#endif
