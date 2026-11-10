#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/Appearance/VBAppearancePiece.h"
#include "Character/Appearance/VBCharacterAppearance.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "VBIsolatedGameWorld.h"

struct FVBEquipmentAppearanceTestAccess
{
	static void Prepare(UVBCharacterAppearanceComponent& Component, UVBCharacterAppearance* Definition, USkeletalMeshComponent* Body)
	{
		Component.ActiveDefinition = Definition;
		Component.PrimaryVisualMesh = Body;
		Component.ComposePieces();
	}

	static int32 PieceCount(const UVBCharacterAppearanceComponent& Component) { return Component.PieceComponents.Num(); }
	static USkeletalMeshComponent* FirstPiece(const UVBCharacterAppearanceComponent& Component) { return Component.PieceComponents.IsEmpty() ? nullptr : Component.PieceComponents[0].Get(); }
	static int32 PublishedCount(const UVBCharacterAppearanceComponent& Component) { return Component.EquippedPieces.Num(); }
	static int32 Generation(const UVBCharacterAppearanceComponent& Component) { return Component.PieceGeneration; }
	static bool Pending(const UVBCharacterAppearanceComponent& Component) { return Component.bEquipmentRefreshPending; }
};

namespace VBEquipmentAppearanceTest
{
	const FName Hero(TEXT("EquipmentAppearanceHero"));

	UVBAppearancePiece* MakePiece(FGameplayTag Slot, TSoftObjectPtr<USkeletalMesh> Mesh = {})
	{
		UVBAppearancePiece* Piece = NewObject<UVBAppearancePiece>(GetTransientPackage(), NAME_None, RF_Transient);
		Piece->AppearanceSlot = Slot;
		if (!Mesh.IsNull())
		{
			Piece->FittedMeshes.Add(Hero, Mesh);
		}

		if (Slot == VBGameplayTags::Appearance_Slot_Top)
		{
			Piece->HidesSections.AddTag(VBGameplayTags::Appearance_Section_TorsoUpperArms);
		}

		if (Slot == VBGameplayTags::Appearance_Slot_Bottom)
		{
			Piece->HidesSections.AddTag(VBGameplayTags::Appearance_Section_HipsLegs);
			Piece->HidesSections.AddTag(VBGameplayTags::Appearance_Section_Feet);
		}

		return Piece;
	}

	UVBItemDefinition* MakeItem(UVBAppearancePiece* Piece, FGameplayTag Slot = VBGameplayTags::Equipment_Slot_Head)
	{
		UVBItemDefinition* Item = NewObject<UVBItemDefinition>(GetTransientPackage(), NAME_None, RF_Transient);
		FVBItemFragment_Equippable Gear;
		Gear.SlotType = Slot;
		Item->Fragments.Add(FInstancedStruct::Make(Gear));
		FVBItemFragment_Appearance Appearance;
		Appearance.Piece = Piece;
		Item->Fragments.Add(FInstancedStruct::Make(Appearance));
		return Item;
	}

	// fixture 상태만 둔다. 시험하는 길은 실제 source delegate -> authority 투영 -> 로컬 조립이다.
	bool SeedHead(FAutomationTestBase& Test, UVBEquipmentComponent& Equipment, const UVBItemDefinition* Item)
	{
		FArrayProperty* Property = FindFProperty<FArrayProperty>(UVBEquipmentComponent::StaticClass(), TEXT("Equipped"));
		if (!Test.TestNotNull(TEXT("real equipment state property"), Property))
		{
			return false;
		}

		FObjectPropertyBase* Inner = CastField<FObjectPropertyBase>(Property->Inner);
		if (!Test.TestNotNull(TEXT("equipment item property"), Inner))
		{
			return false;
		}

		FScriptArrayHelper Values(Property, Property->ContainerPtrToValuePtr<void>(&Equipment));
		Values.Resize(Equipment.GetSlotLayout().Num());
		for (int32 Slot = 0; Slot < Values.Num(); ++Slot)
		{
			Inner->SetObjectPropertyValue(Values.GetRawPtr(Slot), Slot == 0 ? const_cast<UVBItemDefinition*>(Item) : nullptr);
		}

		return Test.TestTrue(TEXT("fixture seeds actual head slot"), Equipment.GetEquipped(0) == Item);
	}

	struct FFixture
	{
		FVBIsolatedGameWorld Isolated;
		UVBCharacterAppearanceComponent* Appearance = nullptr;
		UVBEquipmentComponent* Equipment = nullptr;
		USkeletalMeshComponent* Body = nullptr;
		AActor* Owner = nullptr;
		TStrongObjectPtr<UVBCharacterAppearance> Definition;
		TStrongObjectPtr<UVBAppearancePiece> Top, Bottom, Head;
		TStrongObjectPtr<UVBItemDefinition> Hat;
		bool bStarted = false;

		bool Init(FAutomationTestBase& Test)
		{
			if (!Isolated.Init(Test))
			{
				return false;
			}

			USkeletalMesh* Cube = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
			if (!Test.TestNotNull(TEXT("same engine mesh as the existing ApplyPieces test"), Cube))
			{
				return false;
			}

			if (!Test.TestNotNull(TEXT("fitting engine mesh skeleton"), Cube->GetSkeleton()))
			{
				return false;
			}

			Owner = Isolated.World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("authority visual owner"), Owner))
			{
				return false;
			}

			Body = NewObject<USkeletalMeshComponent>(Owner);
			Owner->SetRootComponent(Body);
			Body->SetSkeletalMeshAsset(Cube);
			Body->RegisterComponent();
			Appearance = NewObject<UVBCharacterAppearanceComponent>(Owner);
			Appearance->RegisterComponent();
			if (!Appearance->HasBegunPlay())
			{
				Appearance->BeginPlay();
			}

			bStarted = true;
			Equipment = NewObject<UVBEquipmentComponent>(Owner);
			Equipment->RegisterComponent();
			UVBSaveParticipantSubsystem::Register(Equipment);
			Definition.Reset(NewObject<UVBCharacterAppearance>());
			Top.Reset(MakePiece(VBGameplayTags::Appearance_Slot_Top, Cube));
			Bottom.Reset(MakePiece(VBGameplayTags::Appearance_Slot_Bottom, Cube));
			Head.Reset(MakePiece(VBGameplayTags::Appearance_Slot_Head, Cube));
			Hat.Reset(MakeItem(Head.Get()));
			Definition->CharacterId = Hero;
			Definition->DefaultPieces = {Top.Get(), Bottom.Get()};
			FVBEquipmentAppearanceTestAccess::Prepare(*Appearance, Definition.Get(), Body);
			return Test.TestEqual(TEXT("a completed default appearance exists before equipment binding"),
				FVBEquipmentAppearanceTestAccess::PieceCount(*Appearance), 2);
		}

		void Stop()
		{
			if (bStarted)
			{
				Appearance->EndPlay(EEndPlayReason::RemovedFromWorld);
			}

			bStarted = false;
		}

		~FFixture() { Stop(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEquipmentAppearanceProjectionTest, "VB.Unit.Appearance.Equipment.Projection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEquipmentAppearanceProjectionTest::RunTest(const FString& Parameters)
{
	using namespace VBEquipmentAppearanceTest;
	using namespace VBAppearancePieceRules;
	const TStrongObjectPtr<UVBAppearancePiece> TopA(MakePiece(VBGameplayTags::Appearance_Slot_Top));
	const TStrongObjectPtr<UVBAppearancePiece> TopB(MakePiece(VBGameplayTags::Appearance_Slot_Top));
	const TStrongObjectPtr<UVBAppearancePiece> Head(MakePiece(VBGameplayTags::Appearance_Slot_Head));
	const TStrongObjectPtr<UVBItemDefinition> ItemA(MakeItem(TopA.Get(), VBGameplayTags::Equipment_Slot_Body));
	const TStrongObjectPtr<UVBItemDefinition> ItemB(MakeItem(TopB.Get(), VBGameplayTags::Equipment_Slot_Body));
	const TStrongObjectPtr<UVBItemDefinition> Hat(MakeItem(Head.Get()));
	const TStrongObjectPtr<UVBItemDefinition> Plain(NewObject<UVBItemDefinition>());
	const auto Result = ProjectEquippedPieces({{nullptr, nullptr}, {Plain.Get(), nullptr}, {ItemA.Get(), TopA.Get()}, {Hat.Get(), Head.Get()}, {ItemB.Get(), TopB.Get()}});
	TestEqual(TEXT("one piece per appearance slot"), Result.Pieces.Num(), 2);
	if (Result.Pieces.Num() == 2)
	{
		TestTrue(TEXT("first equipment slot wins a corrupt duplicate"), Result.Pieces[0] == TopA.Get());
		TestTrue(TEXT("different slots retain equipment order"), Result.Pieces[1] == Head.Get());
	}

	TestEqual(TEXT("duplicate slot reports an error value without logging"), Result.Errors.Num(), 1);
	const auto Missing = ProjectEquippedPieces({{Hat.Get(), nullptr}});
	TestEqual(TEXT("unresolved DA omitted"), Missing.Pieces.Num(), 0);
	TestEqual(TEXT("unresolved DA explained"), Missing.Errors.Num(), 1);
	TestEqual(TEXT("empty equipment"), ProjectEquippedPieces({}).Pieces.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEquipmentAppearanceChoiceTest, "VB.Unit.Appearance.Equipment.Choice",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEquipmentAppearanceChoiceTest::RunTest(const FString& Parameters)
{
	using namespace VBEquipmentAppearanceTest;
	using namespace VBAppearancePieceRules;
	const TSoftObjectPtr<USkeletalMesh> Unloaded(FSoftObjectPath(TEXT("/Game/__VBUnitOnly__/NotLoadedMesh.NotLoadedMesh")));
	const TStrongObjectPtr<UVBAppearancePiece> Top(MakePiece(VBGameplayTags::Appearance_Slot_Top, Unloaded));
	const TStrongObjectPtr<UVBAppearancePiece> Bottom(MakePiece(VBGameplayTags::Appearance_Slot_Bottom, Unloaded));
	const TStrongObjectPtr<UVBAppearancePiece> Worn(MakePiece(VBGameplayTags::Appearance_Slot_Top, Unloaded));
	const TStrongObjectPtr<UVBAppearancePiece> Unfitted(MakePiece(VBGameplayTags::Appearance_Slot_Top));
	const TArray<const UVBAppearancePiece*> Defaults{Top.Get(), Bottom.Get()};
	const auto Fit = ChoosePieces(Hero, Defaults, {Worn.Get()});
	TestEqual(TEXT("equipped overrides one default slot"), Fit.Pieces.Num(), 2);
	if (Fit.Pieces.Num() == 2)
	{
		TestTrue(TEXT("choice is deterministic in default slot order"), Fit.Pieces[0] == Worn.Get() && Fit.Pieces[1] == Bottom.Get());
	}

	TestEqual(TEXT("fitted override needs no fallback"), Fit.Warnings.Num(), 0);
	TestFalse(TEXT("pure choice did not load the mesh path"), Unloaded.IsValid());
	const auto Fallback = ChoosePieces(Hero, Defaults, {Unfitted.Get()});
	if (TestEqual(TEXT("unfitted equipment preserves defaults"), Fallback.Pieces.Num(), 2))
	{
		TestTrue(TEXT("fallback chooses the default top"), Fallback.Pieces[0] == Top.Get());
	}

	TestEqual(TEXT("fallback has exactly one diagnostic value"), Fallback.Warnings.Num(), 1);
	TestEqual(TEXT("ordinary defaults with no equipment"), ChoosePieces(Hero, Defaults, {}).Pieces.Num(), 2);
	const auto OtherCharacter = ChoosePieces(FName(TEXT("UnfittedCharacter")), Defaults, {});
	TestEqual(TEXT("mesh lookup uses the requested CharacterId"), OtherCharacter.Pieces.Num(), 0);
	TestEqual(TEXT("missing fitted defaults are errors"), OtherCharacter.Errors.Num(), 2);
	TestEqual(TEXT("slotless character has no pieces"), ChoosePieces(NAME_None, Defaults, {Worn.Get()}).Pieces.Num(), 0);
	TestEqual(TEXT("missing optional slot fallback is empty"), ChoosePieces(Hero, {}, {Unfitted.Get()}).Pieces.Num(), 0);
	const auto Duplicate = ChoosePieces(Hero, {Top.Get(), Worn.Get(), Bottom.Get()}, {});
	TestEqual(TEXT("corrupt default slot duplicate explained"), Duplicate.Errors.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEquipmentAppearanceHostTest, "VB.Unit.Appearance.Equipment.HostApply",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEquipmentAppearanceHostTest::RunTest(const FString& Parameters)
{
	using namespace VBEquipmentAppearanceTest;
	using Access = FVBEquipmentAppearanceTestAccess;
	FFixture Fixture;
	if (!Fixture.Init(*this) || !SeedHead(*this, *Fixture.Equipment, Fixture.Hat.Get()))
	{
		return false;
	}

	USkeletalMeshComponent* Old = Access::FirstPiece(*Fixture.Appearance);
	Fixture.Appearance->SetEquipmentSource(Fixture.Equipment); // Real authority write path; never call OnRep in this test.
	TestEqual(TEXT("authority publishes current equipment immediately"), Access::PublishedCount(*Fixture.Appearance), 1);
	TestEqual(TEXT("authority applies without OnRep to the existing visual"), Access::PieceCount(*Fixture.Appearance), 3);
	TestTrue(TEXT("host replaced the completed default components"), !IsValid(Old) || Old->IsBeingDestroyed());
	const int32 Generation = Access::Generation(*Fixture.Appearance);
	Fixture.Appearance->SetEquipmentSource(Fixture.Equipment);
	TestEqual(TEXT("same source and state do not compose twice"), Access::Generation(*Fixture.Appearance), Generation);
	if (!SeedHead(*this, *Fixture.Equipment, nullptr))
	{
		return false;
	}

	Fixture.Equipment->OnEquipmentChanged.Broadcast();
	TestEqual(TEXT("unequip clears public pieces"), Access::PublishedCount(*Fixture.Appearance), 0);
	TestEqual(TEXT("unequip restores defaults"), Access::PieceCount(*Fixture.Appearance), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEquipmentAppearanceLifetimeTest, "VB.Unit.Appearance.Equipment.SaveAndLifetime",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEquipmentAppearanceLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace VBEquipmentAppearanceTest;
	using Access = FVBEquipmentAppearanceTestAccess;
	FFixture Fixture;
	if (!Fixture.Init(*this) || !SeedHead(*this, *Fixture.Equipment, Fixture.Hat.Get()))
	{
		return false;
	}

	Fixture.Appearance->SetEquipmentSource(Fixture.Equipment);
	UVBSaveParticipantSubsystem* Registry = Fixture.Isolated.World->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (!TestNotNull(TEXT("real save registry"), Registry))
	{
		return false;
	}

	const int32 Generation = Access::Generation(*Fixture.Appearance);
	bool bInjected = false;
	bool bSawSecondStage = false;
	const FDelegateHandle SecondStage = Registry->OnSaveApplied.AddLambda([&]()
	{
		bSawSecondStage = true;
		TestFalse(TEXT("engine save registry already cleared application flag in stage two"), Registry->IsApplyingSave());
		TestTrue(TEXT("pending projection still waits for finalized"), Access::Pending(*Fixture.Appearance));
		TestEqual(TEXT("no intermediate stage-two display"), Access::PieceCount(*Fixture.Appearance), 3);
	});
	const TStrongObjectPtr<UVBSaveGame> Save(NewObject<UVBSaveGame>());
	FVBSaveContext Context;
	Context.World = Fixture.Isolated.World;
	const int32 Delivered = Registry->ReadAll(*Save, Context, [&]()
	{
		if (Registry->IsApplyingSave() && !bInjected)
		{
			bInjected = true;
			SeedHead(*this, *Fixture.Equipment, nullptr);
			Fixture.Equipment->OnEquipmentChanged.Broadcast();
			TestTrue(TEXT("real read phase defers projection"), Access::Pending(*Fixture.Appearance));
			TestEqual(TEXT("read-phase public list stays completed"), Access::PublishedCount(*Fixture.Appearance), 1);
			TestEqual(TEXT("read phase does not start a compose"), Access::Generation(*Fixture.Appearance), Generation);
		}

		return true;
	});
	Registry->OnSaveApplied.Remove(SecondStage);
	TestTrue(TEXT("real registered participant received a save cycle"), Delivered > 0 && bInjected && bSawSecondStage);
	TestFalse(TEXT("finalized clears pending projection"), Access::Pending(*Fixture.Appearance));
	TestEqual(TEXT("finalized publishes final equipment only"), Access::PublishedCount(*Fixture.Appearance), 0);
	TestEqual(TEXT("finalized restores default pieces"), Access::PieceCount(*Fixture.Appearance), 2);
	UVBEquipmentComponent* Other = NewObject<UVBEquipmentComponent>(Fixture.Owner);
	Other->RegisterComponent();
	if (!SeedHead(*this, *Other, Fixture.Hat.Get()))
	{
		return false;
	}

	Fixture.Appearance->SetEquipmentSource(Other);
	TestFalse(TEXT("old equipment subscription removed"), Fixture.Equipment->OnEquipmentChanged.IsBoundToObject(Fixture.Appearance));
	Fixture.Equipment->OnEquipmentChanged.Broadcast();
	TestEqual(TEXT("old source cannot replace the new source projection"), Access::PublishedCount(*Fixture.Appearance), 1);
	Fixture.Appearance->SetEquipmentSource(nullptr);
	TestFalse(TEXT("unpossess-style clear unbinds equipment"), Other->OnEquipmentChanged.IsBoundToObject(Fixture.Appearance));
	TestFalse(TEXT("clear unbinds finalized"), Registry->OnSaveFinalized.IsBoundToObject(Fixture.Appearance));
	Fixture.Appearance->SetEquipmentSource(Other);
	Fixture.Stop();
	TestFalse(TEXT("EndPlay unbinds equipment"), Other->OnEquipmentChanged.IsBoundToObject(Fixture.Appearance));
	TestFalse(TEXT("EndPlay unbinds finalized"), Registry->OnSaveFinalized.IsBoundToObject(Fixture.Appearance));
	return true;
}
#endif
