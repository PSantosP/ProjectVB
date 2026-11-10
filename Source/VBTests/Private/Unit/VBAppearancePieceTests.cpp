// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 외형 슬롯 데이터 규칙 시험. 판정 함수는 자산 없이 돌고, 한 시험만 출시 슬롯 표를 PlayerState 장비 배치와 대조한다.

#include "Misc/AutomationTest.h"

#include "Algo/Reverse.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/Appearance/VBAppearancePiece.h"
#include "Character/Appearance/VBAppearanceSettings.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Misc/DataValidation.h"

namespace VBAppearancePieceTest
{
	namespace Tags = VBGameplayTags;
	using VBAppearancePieceRules::FPieceFacts;

	FGameplayTagContainer Hides(std::initializer_list<FGameplayTag> List)
	{
		FGameplayTagContainer Out;
		for (const FGameplayTag& Tag : List)
		{
			Out.AddTag(Tag);
		}

		return Out;
	}

	FPieceFacts GoodTop() { return {TEXT("Top"), Tags::Appearance_Slot_Top, Hides({Tags::Appearance_Section_TorsoUpperArms, Tags::Appearance_Section_Forearms})}; }
	FPieceFacts GoodBottom() { return {TEXT("Bottom"), Tags::Appearance_Slot_Bottom, Hides({Tags::Appearance_Section_HipsLegs, Tags::Appearance_Section_Feet})}; }

	TArray<FName> Lod0() { return VBAppearancePieceRules::BodySectionNames(); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceDefaultPiecesTest, "VB.Unit.Appearance.DefaultPieces",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceDefaultPiecesTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePieceTest;
	using VBAppearancePieceRules::FindDefaultPieceProblems;

	TestEqual(TEXT("top + bottom that hide their sections"), FindDefaultPieceProblems({GoodTop(), GoodBottom()}).Num(), 0);
	TestEqual(TEXT("no bottom"), FindDefaultPieceProblems({GoodTop()}).Num(), 1);
	TestEqual(TEXT("no piece at all - top and bottom both missing"), FindDefaultPieceProblems({}).Num(), 2);
	// 슬롯의 주인은 조각 하나뿐이라 두 조각이 한 슬롯을 말하면 거절한다.
	FPieceFacts SecondTop = GoodTop();
	SecondTop.Name = TEXT("SecondTop");
	TestEqual(TEXT("two default pieces claim one slot"), FindDefaultPieceProblems({GoodTop(), SecondTop, GoodBottom()}).Num(), 1);
	FPieceFacts NoSlot = GoodTop();
	NoSlot.Slot = FGameplayTag();
	TestEqual(TEXT("a default piece without a slot says so"), FindDefaultPieceProblems({NoSlot, GoodTop(), GoodBottom()}).Num(), 1);
	FPieceFacts OpenTop = GoodTop();
	OpenTop.Hides = Hides({Tags::Appearance_Section_Forearms});
	TestEqual(TEXT("top that does not hide the torso"), FindDefaultPieceProblems({OpenTop, GoodBottom()}).Num(), 1);
	FPieceFacts Barefoot = GoodBottom();
	Barefoot.Hides = Hides({Tags::Appearance_Section_HipsLegs});
	TestEqual(TEXT("bottom that does not hide the feet"), FindDefaultPieceProblems({GoodTop(), Barefoot}).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceBodySectionsTest, "VB.Unit.Appearance.BodySections",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceBodySectionsTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePieceTest;
	using VBAppearancePieceRules::FindBodySectionProblems;

	TestEqual(TEXT("seven names"), Lod0().Num(), 7);
	TArray<FName> Reordered = Lod0();
	Algo::Reverse(Reordered);
	TestEqual(TEXT("seven distinct sections at LOD0, reordered at LOD1"), FindBodySectionProblems({Lod0(), Reordered}).Num(), 0);
	TArray<FName> NoFeet = Lod0();
	NoFeet.Remove(FName(TEXT("Feet")));
	TestEqual(TEXT("LOD0 without the feet section"), FindBodySectionProblems({NoFeet}).Num(), 1);
	// 낮은 LOD 도 섹션을 합치면 안 된다 - 한 이름의 두 섹션은 따로 숨길 수 없다.
	TArray<FName> Merged = Lod0();
	Merged[1] = Merged[0];
	TestEqual(TEXT("LOD1 with two sections on one name"), FindBodySectionProblems({Lod0(), Merged}).Num(), 1);
	TArray<FName> Unknown = Lod0();
	Unknown.Add(TEXT("Cape"));
	TestEqual(TEXT("a slot outside the seven"), FindBodySectionProblems({Unknown}).Num(), 1);
	TestEqual(TEXT("no LOD at all"), FindBodySectionProblems({}).Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceHideByLodTest, "VB.Unit.Appearance.HideByLod",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceHideByLodTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePieceTest;
	using VBAppearancePieceRules::EffectiveMaterialIndex;
	using VBAppearancePieceRules::SectionsToHide;

	// LOD material map 이 재질을 말하면 그것이 이긴다. INDEX_NONE 이나 짧은 map 이면 섹션 자기 재질이다.
	TestEqual(TEXT("remapped section"), EffectiveMaterialIndex(0, {INDEX_NONE, 5}, 1), 5);
	TestEqual(TEXT("INDEX_NONE keeps the section material"), EffectiveMaterialIndex(3, {INDEX_NONE, 5}, 0), 3);
	TestEqual(TEXT("map shorter than the sections"), EffectiveMaterialIndex(4, {}, 2), 4);

	// LOD0 은 기준 순서, LOD1 은 같은 일곱 슬롯을 다른 순서로 둔다(remap 결과).
	const TArray<FName> L0 = Lod0(); // Head Hair Hands TorsoUpperArms Forearms HipsLegs Feet
	const TArray<FName> L1 = {L0[1], L0[0], L0[3], L0[2], L0[4], L0[6], L0[5]};
	const TArray<TArray<int32>> Hide = SectionsToHide({L0, L1}, {L0[3], L0[4]});
	if (TestEqual(TEXT("two LODs"), Hide.Num(), 2))
	{
		TestTrue(TEXT("LOD0 hides torso + forearms by name"), Hide[0] == TArray<int32>{3, 4});
		// LOD0 섹션 번호로 LOD1 을 숨기면 Hands, Forearms 가 숨어 상의 밑으로 몸통이 보인다.
		TestTrue(TEXT("LOD1 hides the same names at their own sections"), Hide[1] == TArray<int32>{2, 4});
	}

	TestEqual(TEXT("nothing hidden"), SectionsToHide({L0}, {})[0].Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceSlotTableTest, "VB.Unit.Appearance.SlotTable",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceSlotTableTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePieceTest;
	using VBAppearanceSettingsRules::FindTableProblems;

	const TArray<FGameplayTag> Layout = {Tags::Equipment_Slot_Head, Tags::Equipment_Slot_Body, Tags::Equipment_Slot_Arms,
		Tags::Equipment_Slot_Charm, Tags::Equipment_Slot_Charm};
	const TMap<FGameplayTag, FGameplayTag> Good = {{Tags::Equipment_Slot_Body, Tags::Appearance_Slot_Top},
		{Tags::Equipment_Slot_Head, Tags::Appearance_Slot_Head}, {Tags::Equipment_Slot_Arms, Tags::Appearance_Slot_Hands}};
	TestEqual(TEXT("one appearance slot per equipment slot, charms show nothing"), FindTableProblems(Good, Layout).Num(), 0);
	TMap<FGameplayTag, FGameplayTag> TwoToOne = Good;
	TwoToOne.Add(Tags::Equipment_Slot_Head, Tags::Appearance_Slot_Top);
	TestEqual(TEXT("two equipment slots fill one appearance slot"), FindTableProblems(TwoToOne, Layout).Num(), 1);
	TMap<FGameplayTag, FGameplayTag> CharmShows = Good;
	CharmShows.Add(Tags::Equipment_Slot_Charm, Tags::Appearance_Slot_Bottom);
	TestEqual(TEXT("a slot that appears twice in the layout maps to an appearance slot"), FindTableProblems(CharmShows, Layout).Num(), 1);
	TMap<FGameplayTag, FGameplayTag> NotASlot = Good;
	NotASlot.Add(Tags::Equipment_Slot_Charm, Tags::Appearance_Section_Hair);
	TestEqual(TEXT("a value outside Appearance.Slot"), FindTableProblems(NotASlot, {Tags::Equipment_Slot_Charm}).Num(), 1);

	// 출시 데이터: DefaultGame.ini 의 표를 출시 PlayerState 의 장비 배치와 대조한다.
	const UVBAppearanceSettings* Settings = GetDefault<UVBAppearanceSettings>();
	TestTrue(TEXT("shipped table: body -> top"),
		Settings->FindAppearanceSlot(Tags::Equipment_Slot_Body) == Tags::Appearance_Slot_Top.GetTag());
	TestTrue(TEXT("shipped table: head -> head"),
		Settings->FindAppearanceSlot(Tags::Equipment_Slot_Head) == Tags::Appearance_Slot_Head.GetTag());
	TestTrue(TEXT("shipped table: arms -> hands"),
		Settings->FindAppearanceSlot(Tags::Equipment_Slot_Arms) == Tags::Appearance_Slot_Hands.GetTag());
	TestTrue(TEXT("shipped table: legs -> bottom (user D1)"),
		Settings->FindAppearanceSlot(Tags::Equipment_Slot_Legs) == Tags::Appearance_Slot_Bottom.GetTag());
	const UClass* PlayerStateClass = LoadClass<APlayerState>(nullptr,
		TEXT("/Game/VB/Blueprints/GameModes/BP_VBPlayerState.BP_VBPlayerState_C"));
	const APlayerState* PlayerState = PlayerStateClass ? PlayerStateClass->GetDefaultObject<APlayerState>() : nullptr;
	const UVBEquipmentComponent* Equipment = PlayerState ? PlayerState->FindComponentByClass<UVBEquipmentComponent>() : nullptr;
	if (TestNotNull(TEXT("shipped player state equipment"), Equipment))
	{
		for (const FText& Problem : FindTableProblems(Settings->EquipmentToAppearanceSlot, Equipment->GetSlotLayout()))
		{
			AddError(FString::Printf(TEXT("shipped table vs layout: %s"), *Problem.ToString()));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceItemPieceSlotTest, "VB.Unit.Appearance.ItemPieceSlot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceItemPieceSlotTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePieceTest;
	using VBAppearanceSettingsRules::FindItemPieceProblem;

	TestTrue(TEXT("body armour with a top piece"),
		FindItemPieceProblem(true, Tags::Equipment_Slot_Body, Tags::Appearance_Slot_Top, Tags::Appearance_Slot_Top).IsEmpty());
	TestFalse(TEXT("arms item with a top piece"),
		FindItemPieceProblem(true, Tags::Equipment_Slot_Arms, Tags::Appearance_Slot_Hands, Tags::Appearance_Slot_Top).IsEmpty());
	TestFalse(TEXT("charm (no appearance slot) with a piece"),
		FindItemPieceProblem(true, Tags::Equipment_Slot_Charm, FGameplayTag(), Tags::Appearance_Slot_Hands).IsEmpty());
	TestFalse(TEXT("a piece on an item that is not equipment"),
		FindItemPieceProblem(false, FGameplayTag(), FGameplayTag(), Tags::Appearance_Slot_Top).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceItemLookDecisionTest, "VB.Unit.Appearance.ItemLookDecision",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceItemLookDecisionTest::RunTest(const FString& Parameters)
{
	using namespace VBAppearancePieceTest;
	using VBAppearanceSettingsRules::FindItemLookDecisionProblem;
	const FGameplayTag Body = Tags::Equipment_Slot_Body, Top = Tags::Appearance_Slot_Top;

	TestFalse(TEXT("body armour without an appearance fragment"),
		FindItemLookDecisionProblem(true, Body, Top, false, false, false).IsEmpty());
	TestTrue(TEXT("body armour with a piece"), FindItemLookDecisionProblem(true, Body, Top, true, false, true).IsEmpty());
	TestTrue(TEXT("body armour marked no-appearance"), FindItemLookDecisionProblem(true, Body, Top, true, true, false).IsEmpty());
	TestFalse(TEXT("no-appearance mark and a piece together"), FindItemLookDecisionProblem(true, Body, Top, true, true, true).IsEmpty());
	TestTrue(TEXT("charm (no appearance slot) without a fragment"),
		FindItemLookDecisionProblem(true, Tags::Equipment_Slot_Charm, FGameplayTag(), false, false, false).IsEmpty());
	TestTrue(TEXT("an item that is not equipment"),
		FindItemLookDecisionProblem(false, FGameplayTag(), FGameplayTag(), false, false, false).IsEmpty());
	return true;
}

// 입을 수 없는 외형에 장착된 조각은 버리지 않고 알린다. 로딩 중인 외형은 도착 후 다시 조립하므로 조용하다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceUnwearableReasonTest, "VB.Unit.Appearance.UnwearableReason",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceUnwearableReasonTest::RunTest(const FString& Parameters)
{
	using VBAppearancePieceRules::FindUnwearableReason;
	const FName Hero(TEXT("Protagonist"));

	TestFalse(TEXT("driver body with gear"), FindUnwearableReason(false, false, NAME_None, 1).IsEmpty());
	TestFalse(TEXT("loaded look without CharacterId with gear"), FindUnwearableReason(true, true, NAME_None, 2).IsEmpty());
	TestTrue(TEXT("slot look with gear"), FindUnwearableReason(true, true, Hero, 1).IsEmpty());
	TestTrue(TEXT("driver body without gear"), FindUnwearableReason(false, false, NAME_None, 0).IsEmpty());
	TestTrue(TEXT("look still loading"), FindUnwearableReason(false, true, NAME_None, 1).IsEmpty());
	return true;
}

#if WITH_EDITOR
// 모든 출시 아이템이 자기 저작 검사를 통과한다 - 몸에 보이는 장비는 외형이 정해져 있어야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAppearanceShippedItemsDecideLookTest, "VB.Unit.Appearance.ShippedItemsDecideLook",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAppearanceShippedItemsDecideLookTest::RunTest(const FString& Parameters)
{
	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Items;
	Registry.GetAssetsByClass(UVBItemDefinition::StaticClass()->GetClassPathName(), Items, true);
	Items.RemoveAll([](const FAssetData& Asset) { return !Asset.PackagePath.ToString().StartsWith(TEXT("/Game/VB")); });
	if (!TestTrue(TEXT("shipped item definitions found"), Items.Num() > 0))
	{
		return false;
	}

	for (const FAssetData& Asset : Items)
	{
		const UVBItemDefinition* Item = Cast<UVBItemDefinition>(Asset.GetAsset());
		if (!TestNotNull(*FString::Printf(TEXT("%s loads"), *Asset.AssetName.ToString()), Item))
		{
			continue;
		}

		FDataValidationContext Context;
		const bool bValid = Item->IsDataValid(Context) != EDataValidationResult::Invalid;
		FString Errors;
		for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
		{
			Errors += Issue.Message.ToString() + TEXT(" ");
		}

		TestTrue(*FString::Printf(TEXT("%s passes its authoring check %s"), *Asset.AssetName.ToString(), *Errors), bValid);
	}

	return true;
}
#endif
