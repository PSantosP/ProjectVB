#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBInventorySaveRules.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBItemRules.h"
#include "Narrative/Collection/VBCollectionEntryDefinition.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "SaveSystem/VBSaveGame.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_AUTOMATION_TESTS
namespace
{
	FGameplayTag Tag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name), false); }

	struct FCollectionFixture
	{
		UWorld* World = nullptr;
		UVBStoryStateComponent* Story = nullptr;
		APlayerState* Player = nullptr;
		UVBInventoryComponent* Inventory = nullptr;

		FCollectionFixture()
		{
			const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!World)
			{
				return;
			}

			AGameStateBase* GS = World->SpawnActor<AGameStateBase>();
			Player = World->SpawnActor<APlayerState>();
			if (!GS || !Player)
			{
				return;
			}

			World->SetGameState(GS);
			Story = NewObject<UVBStoryStateComponent>(GS);
			GS->AddInstanceComponent(Story);
			Story->RegisterComponent();
			Inventory = NewObject<UVBInventoryComponent>(Player);
			Player->AddInstanceComponent(Inventory);
			Inventory->RegisterComponent();
		}

		~FCollectionFixture() { if (World) { World->DestroyWorld(false); } }
		bool IsValid() const { return World && Story && Player && Inventory; }
	};

	UVBItemDefinition* MakeCollectionItem(FGameplayTag Fact, int32 MaxCount = 2)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>();
		Item->ItemId = Tag(TEXT("Item.Medical.Gauze"));
		Item->MaxCount = MaxCount;
		FVBEffect_SetFact Set;
		Set.Fact = Fact;
		FVBItemFragment_AcquireEffects Acquire;
		Acquire.Effects.Add(FInstancedStruct::Make(Set));
		Item->Fragments.Add(FInstancedStruct::Make(Acquire));
		return Item;
	}

	UVBCollectionEntryDefinition* MakeEntry(FGameplayTag Fact, int32 Order)
	{
		UVBCollectionEntryDefinition* Entry = NewObject<UVBCollectionEntryDefinition>();
		Entry->DiscoveryFact = Fact;
		Entry->Title = FText::FromString(TEXT("Test record"));
		Entry->Body = FText::FromString(TEXT("Neutral test body."));
		Entry->SortOrder = Order;
		return Entry;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCollectionRowsTest, "VB.Unit.Collection.Rows",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCollectionRowsTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Note = Tag(TEXT("Story.Collection.Document.TestNote"));
	const FGameplayTag Enemy = Tag(TEXT("Story.Collection.Enemy.TestEnemy"));
	const FGameplayTag Document = Tag(TEXT("Story.Collection.Document"));
	if (!TestTrue(TEXT("shared collection test tags registered"), Note.IsValid() && Enemy.IsValid() && Document.IsValid()))
	{
		return false;
	}

	UVBCollectionEntryDefinition* A = MakeEntry(Note, 10);
	UVBCollectionEntryDefinition* B = MakeEntry(Enemy, 0);
	const TArray<const UVBCollectionEntryDefinition*> Definitions = { A, B, A, nullptr };
	FVBStoryState State;
	State.SetFact(Document, true);
	TestEqual(TEXT("parent discovery does not reveal a leaf"), VBCollection::BuildRows(Definitions, State, {}).Num(), 0);
	State.SetFact(Note, true);
	State.SetFact(Enemy, true);
	TestFalse(TEXT("same fact is idempotent"), State.SetFact(Note, true));
	const auto Rows = VBCollection::BuildRows(Definitions, State, {});
	TestEqual(TEXT("duplicate input cannot duplicate a record"), Rows.Num(), 2);
	if (Rows.Num() == 2) { TestTrue(TEXT("authored order wins"), Rows[0] == B); }
	A->SortOrder = 0;
	const auto Tied = VBCollection::BuildRows(Definitions, State, {});
	if (Tied.Num() == 2) { TestTrue(TEXT("ID breaks sort ties"), Tied[0] == A); }
	const auto Filtered = VBCollection::BuildRows(Definitions, State, Document);
	TestTrue(TEXT("category filter returns exact discovered leaves"), Filtered.Num() == 1 && Filtered[0] == A);
	TestFalse(TEXT("category root is not an entry"), VBCollection::IsEntryFact(Document));
	TestTrue(TEXT("asset identity is the discovery fact"),
		A->GetPrimaryAssetId() == UVBCollectionEntryDefinition::MakeCollectionAssetId(Note));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCollectionAcquisitionTest, "VB.Unit.Collection.Acquisition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCollectionAcquisitionTest::RunTest(const FString& Parameters)
{
	FCollectionFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	const FGameplayTag Fact = Tag(TEXT("Story.Collection.Document.TestNote"));
	if (!TestTrue(TEXT("test fact registered"), Fact.IsValid()))
	{
		return false;
	}

	UVBItemDefinition* Item = MakeCollectionItem(Fact);
	const FGameplayTag Counter = Tag(TEXT("Story.Test.ErrandDone"));
	FVBEffect_AddCounter CountEffect;
	CountEffect.Counter = Counter;
	Item->Fragments[0].GetMutable<FVBItemFragment_AcquireEffects>().Effects.Add(FInstancedStruct::Make(CountEffect));
	int32 Changes = 0;
	F.Story->OnStoryStateChanged.AddLambda([&Changes]() { ++Changes; });
	bool bFactBeforeNotice = false;
	F.Inventory->OnInventoryChanged.AddLambda([&]() { bFactBeforeNotice |= F.Story->HasFact(Fact); });
	TestEqual(TEXT("zero request cannot discover"), F.Inventory->AddItem(Item, 0), 0);
	TestFalse(TEXT("no fact after zero grant"), F.Story->HasFact(Fact));
	TestEqual(TEXT("positive partial grant discovers"), F.Inventory->AddItem(Item, 10), 2);
	TestTrue(TEXT("fact precedes inventory notice"), bFactBeforeNotice);
	TestEqual(TEXT("effects run once per grant, not per item"), F.Story->GetCounter(Counter), 1);
	TestEqual(TEXT("full inventory adds nothing"), F.Inventory->AddItem(Item, 1), 0);
	TestEqual(TEXT("no effects on full inventory"), F.Story->GetCounter(Counter), 1);
	TestTrue(TEXT("remove item"), F.Inventory->RemoveItem(Item, 2));
	TestTrue(TEXT("knowledge survives item removal"), F.Story->HasFact(Fact));
	FVBEffect_GiveItem Give;
	Give.Item = Item;
	Give.Count = 1;
	Give.Apply(FVBRuleContext::Make(F.Inventory, F.Player));
	TestEqual(TEXT("rule grant uses the same inventory hook"), F.Inventory->GetCount(Item), 1);
	TestEqual(TEXT("one discovery change plus two per-grant counter changes"), Changes, 3);
	TestEqual(TEXT("each later grant applies effects once"), F.Story->GetCounter(Counter), 2);
	F.Story->OnStoryStateChanged.Clear();
	F.Inventory->OnInventoryChanged.Clear();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCollectionRejectedGrantTest, "VB.Unit.Collection.RejectedGrant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCollectionRejectedGrantTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Fact = Tag(TEXT("Story.Collection.Document.TestNote"));
	if (!TestTrue(TEXT("test fact registered"), Fact.IsValid()))
	{
		return false;
	}

	UVBItemDefinition* Item = MakeCollectionItem(Fact);
	UVBInventoryComponent* NoStory = NewObject<UVBInventoryComponent>();
	AddExpectedMessagePlain(TEXT("Acquisition effects: rejected"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestEqual(TEXT("missing Story rejects before mutation"), NoStory->AddItem(Item, 1), 0);
	TestEqual(TEXT("rejected inventory stays empty"), NoStory->GetCount(Item), 0);
	UVBItemDefinition* Ordinary = NewObject<UVBItemDefinition>();
	TestEqual(TEXT("ordinary items need no Story"), NoStory->AddItem(Ordinary, 1), 1);
	FCollectionFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	F.Player->SetRole(ROLE_SimulatedProxy);
	AddExpectedMessagePlain(TEXT("AddItem"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("non-authority inventory rejects acquisition"), F.Inventory->AddItem(Item, 1), 0);
	TestFalse(TEXT("rejected client cannot discover"), F.Story->HasFact(Fact));
	F.Player->SetRole(ROLE_Authority);
	const FVBRuleContext Context = FVBRuleContext::Make(F.Inventory, F.Player);
	if (!TestTrue(TEXT("malformed-effect fixture resolves the world's Story"), Context.Story == F.Story)
		|| !TestTrue(TEXT("malformed-effect fixture has effect authority"), Context.CanApplyEffects()))
	{
		return false;
	}

	Item->Fragments[0].GetMutable<FVBItemFragment_AcquireEffects>().Effects.Add(FInstancedStruct::Make(FVBCondition_Fact()));
	TestEqual(TEXT("malformed effect rejects entire grant before mutation"), F.Inventory->AddItem(Item, 1), 0);
	TestEqual(TEXT("malformed-effect rejection leaves inventory unchanged"), F.Inventory->GetCount(Item), 0);
	TestFalse(TEXT("preceding valid effect was not partially applied"), F.Story->HasFact(Fact));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCollectionReentrantGrantTest, "VB.Unit.Collection.ReentrantGrant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCollectionReentrantGrantTest::RunTest(const FString& Parameters)
{
	FCollectionFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	const FGameplayTag Fact = Tag(TEXT("Story.Collection.Document.TestNote"));
	if (!TestTrue(TEXT("test fact registered"), Fact.IsValid()))
	{
		return false;
	}

	UVBItemDefinition* Item = MakeCollectionItem(Fact);
	int32 NestedResult = -1;
	F.Story->OnStoryStateChanged.AddLambda([&]()
	{
		NestedResult = F.Inventory->AddItem(Item, 1);
		for (int32 Index = 0; Index < 40; ++Index)
		{
			F.Inventory->AddItem(NewObject<UVBItemDefinition>(), 1);
		}

		F.Inventory->RemoveItem(Item, 1);
	});
	AddExpectedMessagePlain(TEXT("cyclic/excessive"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("outer grant returns actual accepted count"), F.Inventory->AddItem(Item, 1), 1);
	TestEqual(TEXT("same-item recursive grant is refused"), NestedResult, 0);
	TestEqual(TEXT("delegate may remove original entry without stale pointer access"), F.Inventory->GetCount(Item), 0);
	TestEqual(TEXT("other nested grants remain valid through reallocation"), F.Inventory->GetEntries().Num(), 40);
	F.Story->OnStoryStateChanged.Clear();
	TestEqual(TEXT("guard is released after the call"), F.Inventory->AddItem(Item, 1), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCollectionCycleTest, "VB.Unit.Collection.GrantCycle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCollectionCycleTest::RunTest(const FString& Parameters)
{
	FCollectionFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	UVBItemDefinition* A = MakeCollectionItem(Tag(TEXT("Story.Test.Talked")), 0);
	UVBItemDefinition* B = MakeCollectionItem(Tag(TEXT("Story.Test.ErrandDone")), 0);
	FVBEffect_GiveItem GiveB;
	GiveB.Item = B;
	FVBEffect_GiveItem GiveA;
	GiveA.Item = A;
	A->Fragments[0].GetMutable<FVBItemFragment_AcquireEffects>().Effects.Add(FInstancedStruct::Make(GiveB));
	B->Fragments[0].GetMutable<FVBItemFragment_AcquireEffects>().Effects.Add(FInstancedStruct::Make(GiveA));
	AddExpectedMessagePlain(TEXT("cyclic/excessive"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("root grant succeeds"), F.Inventory->AddItem(A, 1), 1);
	TestEqual(TEXT("cycle does not mutate A twice"), F.Inventory->GetCount(A), 1);
	TestEqual(TEXT("intermediate B grant succeeds once"), F.Inventory->GetCount(B), 1);
	TArray<UVBItemDefinition*> Chain;
	for (int32 Index = 0; Index < 33; ++Index) { Chain.Add(MakeCollectionItem(Tag(TEXT("Story.Test.Talked")), 0)); }
	for (int32 Index = 0; Index < Chain.Num() - 1; ++Index)
	{
		FVBEffect_GiveItem Next;
		Next.Item = Chain[Index + 1];
		Chain[Index]->Fragments[0].GetMutable<FVBItemFragment_AcquireEffects>().Effects.Add(FInstancedStruct::Make(Next));
	}

	AddExpectedMessagePlain(TEXT("cyclic/excessive"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("bounded chain accepts root"), F.Inventory->AddItem(Chain[0], 1), 1);
	TestEqual(TEXT("32nd nested effect grant is accepted"), F.Inventory->GetCount(Chain[31]), 1);
	TestEqual(TEXT("33rd nested effect grant is rejected before mutation"), F.Inventory->GetCount(Chain[32]), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCollectionSaveTest, "VB.Unit.Collection.SaveBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCollectionSaveTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Fact = Tag(TEXT("Story.Collection.Document.TestNote"));
	if (!TestTrue(TEXT("test fact registered"), Fact.IsValid()))
	{
		return false;
	}

	UVBStoryStateComponent* Story = NewObject<UVBStoryStateComponent>();
	FVBEffect_SetFact Set;
	Set.Fact = Fact;
	FVBRuleContext RuleContext;
	RuleContext.Story = Story;
	Set.Apply(RuleContext);
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	const FVBSaveContext Context;
	Story->WriteSave(*Save, Context);
	UVBStoryStateComponent* Restored = NewObject<UVBStoryStateComponent>();
	Restored->ReadSave(*Save, Context);
	TestTrue(TEXT("knowledge round-trips without inventory"), Restored->HasFact(Fact));
	UVBSaveGame* Again = NewObject<UVBSaveGame>();
	Restored->WriteSave(*Again, Context);
	TestTrue(TEXT("missing definition does not remove saved discovery"), Again->Progress.Story.HasFact(Fact));
	UVBSaveGame* Legacy = NewObject<UVBSaveGame>();
	Restored->ReadSave(*Legacy, Context);
	TestFalse(TEXT("old empty slots do not infer discovery"), Restored->HasFact(Fact));
	UVBItemDefinition* Item = MakeCollectionItem(Fact);
	FVBSavedItem Saved;
	Saved.ItemId = Item->ItemId;
	Saved.Count = 1;
	TArray<FVBInventoryEntry> Entries;
	TArray<FVBSavedItem> Kept;
	VBInventorySaveRules::ResolveSavedItems({ Saved },[Item](const FGameplayTag&) { return Item; }, Entries, Kept);
	TestEqual(TEXT("save resolution restores item directly"), Entries.Num(), 1);
	TestFalse(TEXT("save resolution cannot replay acquisition effects"), Restored->HasFact(Fact));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCollectionReaderTest, "VB.Unit.Collection.ReaderContract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCollectionReaderTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Note = Tag(TEXT("Story.Collection.Document.TestNote"));
	const FGameplayTag Token = Tag(TEXT("Story.Collection.Collectible.TestToken"));
	if (!TestTrue(TEXT("test facts registered"), Note.IsValid() && Token.IsValid()))
	{
		return false;
	}

	UVBItemDefinition* Item = MakeCollectionItem(Note);
	TestTrue(TEXT("reader resolves generic SetFact acquisition effect"), VBCollection::GetReadingFact(Item) == Note);
	TestFalse(TEXT("ordinary item has no reading command"), VBCollection::GetReadingFact(NewObject<UVBItemDefinition>()).IsValid());
	FVBEffect_SetFact Other;
	Other.Fact = Token;
	Item->Fragments[0].GetMutable<FVBItemFragment_AcquireEffects>().Effects.Add(FInstancedStruct::Make(Other));
	TestFalse(TEXT("ambiguous multiple records are not chosen arbitrarily"), VBCollection::GetReadingFact(Item).IsValid());
#if WITH_EDITOR
	FDataValidationContext PairContext;
	TestFalse(TEXT("collection-owned pairing validator rejects ambiguous/missing definitions"),
		VBCollection::ValidateItemReferences(*Item, PairContext));
	const FInstancedStruct Duplicate = Item->Fragments[0]; // 같은 배열의 원소를 그대로 Add 하면 엔진 CheckAddress 가 assert 한다
	Item->Fragments.Add(Duplicate);
	FDataValidationContext FragmentContext;
	TestTrue(TEXT("duplicate acquisition fragments are invalid"), Item->IsDataValid(FragmentContext) == EDataValidationResult::Invalid);
	UVBCollectionEntryDefinition* Entry = MakeEntry(Tag(TEXT("Story.Collection.Document")), 0);
	FDataValidationContext EntryContext;
	TestTrue(TEXT("category root cannot be an entry definition"), Entry->IsDataValid(EntryContext) == EDataValidationResult::Invalid);
#endif
	return true;
}
#endif
