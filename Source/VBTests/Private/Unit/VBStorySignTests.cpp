// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Unit/VBStoryTestWorld.h"
#include "VBIsolatedGameWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Font.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Narrative/Story/VBStorySign.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "UObject/UnrealType.h"
#include <limits>
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	FGameplayTag SignFact()
	{
		// 이미 등록된 fixture 사실을 쓴다. 병원 tag 저작은 따로 한다.
		return FGameplayTag::RequestGameplayTag(TEXT("Story.Collection.Document.TestNote"), false);
	}

	AVBStorySign* MakeTextSign(UWorld& World)
	{
		AVBStorySign* Sign = World.SpawnActor<AVBStorySign>();
		if (!Sign)
		{
			return nullptr;
		}

		Sign->FactTag = SignFact();
		Sign->RenderMode = EVBStorySignRenderMode::Text;
		Sign->Before.Text = FText::FromString(TEXT("101"));
		Sign->After.Text = FText::FromString(TEXT("102"));
		Sign->TextFont = NewObject<UFont>(Sign);
		Sign->TextFont->FontCacheType = EFontCacheType::Offline;
		Sign->TextFont->MaxCharHeight = { 16 };
		Sign->TextMaterial = UMaterial::GetDefaultMaterial(MD_Surface);
		// 합성 font 로 선택, 컴포넌트 상태만 본다. glyph 픽셀이나 자산 품질은 보지 않는다.
		return Sign;
	}

	AVBStorySign* MakeMaterialSign(UWorld& World)
	{
		AVBStorySign* Sign = World.SpawnActor<AVBStorySign>();
		if (!Sign)
		{
			return nullptr;
		}

		Sign->FactTag = SignFact();
		Sign->RenderMode = EVBStorySignRenderMode::Material;
		Sign->PanelMesh = NewObject<UStaticMesh>(Sign);
		Sign->MaterialSlotName = TEXT("Face");
		UMaterial* Parent = UMaterial::GetDefaultMaterial(MD_Surface);
		Sign->PanelMesh->GetStaticMaterials().Add(FStaticMaterial(Parent, Sign->MaterialSlotName));
		Sign->Before.Material = UMaterialInstanceDynamic::Create(Parent, Sign);
		Sign->After.Material = UMaterialInstanceDynamic::Create(Parent, Sign);
		return Sign;
	}

	UTextRenderComponent* Label(AVBStorySign& Sign)
	{
		return Sign.FindComponentByClass<UTextRenderComponent>();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStorySignSnapshotTest, "VB.Unit.Narrative.StorySign.CurrentSnapshot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStorySignSnapshotTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this) || !TestTrue(TEXT("fixture fact registered"), SignFact().IsValid()))
	{
		return false;
	}

	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(*Fixture.World);
	AVBStorySign* Sign = MakeTextSign(*Fixture.World);
	if (!TestNotNull(TEXT("story"), Story) || !TestNotNull(TEXT("sign"), Sign))
	{
		return false;
	}

	Story->SetFact(SignFact(), true);
	Sign->DispatchBeginPlay();
	UTextRenderComponent* Text = Label(*Sign);
	if (!TestNotNull(TEXT("label"), Text))
	{
		return false;
	}

	TestTrue(TEXT("already-true snapshot is visible at bind"), Text->IsVisible());
	TestEqual(TEXT("already-true snapshot selects After without a later event"), Text->Text.ToString(), FString(TEXT("102")));
	Story->SetFact(SignFact(), false);
	TestEqual(TEXT("reset selects Before"), Text->Text.ToString(), FString(TEXT("101")));
	Story->SetFact(SignFact(), true);
	TestEqual(TEXT("change selects After"), Text->Text.ToString(), FString(TEXT("102")));
	TestFalse(TEXT("sign has no replicated state channel"), Sign->GetIsReplicated());
	TestTrue(TEXT("sign is not a save participant"), Cast<IVBSaveParticipant>(Sign) == nullptr);
	TestEqual(TEXT("label does not block the real doorway"), Text->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("label cannot change navigation"), Text->CanEverAffectNavigation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStorySignRestoreTest, "VB.Unit.Narrative.StorySign.SaveRestore",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStorySignRestoreTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(*Fixture.World);
	AVBStorySign* Sign = MakeTextSign(*Fixture.World);
	if (!TestNotNull(TEXT("story"), Story) || !TestNotNull(TEXT("sign"), Sign))
	{
		return false;
	}

	Sign->DispatchBeginPlay();
	UVBSaveGame* BeforeSave = NewObject<UVBSaveGame>();
	UVBSaveGame* AfterSave = NewObject<UVBSaveGame>();
	FVBSaveContext Context;
	Context.World = Fixture.World;
	Story->WriteSave(*BeforeSave, Context);
	Story->SetFact(SignFact(), true);
	Story->WriteSave(*AfterSave, Context);
	Story->ReadSave(*BeforeSave, Context);
	TestEqual(TEXT("older snapshot reverses sign"), Label(*Sign)->Text.ToString(), FString(TEXT("101")));
	Story->ReadSave(*AfterSave, Context);
	TestEqual(TEXT("restored true snapshot updates existing sign"), Label(*Sign)->Text.ToString(), FString(TEXT("102")));
	AVBStorySign* LateSign = MakeTextSign(*Fixture.World);
	if (!TestNotNull(TEXT("streamed-in sign"), LateSign))
	{
		return false;
	}

	LateSign->DispatchBeginPlay();
	TestEqual(TEXT("restoration before sign spawn is read immediately"), Label(*LateSign)->Text.ToString(), FString(TEXT("102")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStorySignLateProviderTest, "VB.Unit.Narrative.StorySign.LateProvider",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStorySignLateProviderTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	AVBStorySign* Sign = MakeTextSign(*Fixture.World);
	if (!TestNotNull(TEXT("sign"), Sign))
	{
		return false;
	}

	Sign->DispatchBeginPlay();
	TestFalse(TEXT("pending provider is hidden, not a false fact"), Label(*Sign)->IsVisible());
	UVBStoryStateComponent* First = VBStoryTestWorld::SpawnStory(*Fixture.World);
	if (!TestNotNull(TEXT("late provider"), First))
	{
		return false;
	}

	TestTrue(TEXT("late provider enables display"), Label(*Sign)->IsVisible());
	First->SetFact(SignFact(), true);
	UVBStoryStateComponent* Second = VBStoryTestWorld::SpawnStory(*Fixture.World);
	if (!TestNotNull(TEXT("replacement"), Second))
	{
		return false;
	}

	TestEqual(TEXT("replacement reads its own false snapshot"), Label(*Sign)->Text.ToString(), FString(TEXT("101")));
	First->SetFact(SignFact(), false);
	First->SetFact(SignFact(), true);
	TestEqual(TEXT("old provider cannot update sign"), Label(*Sign)->Text.ToString(), FString(TEXT("101")));
	Second->SetFact(SignFact(), true);
	TestEqual(TEXT("new provider can update sign"), Label(*Sign)->Text.ToString(), FString(TEXT("102")));
	AddExpectedError(TEXT("has no UVBStoryStateComponent"), EAutomationExpectedErrorFlags::Contains, 1);
	Fixture.World->SpawnActor<AGameStateBase>();
	TestFalse(TEXT("wrong GameState fails unavailable"), Label(*Sign)->IsVisible());
	Fixture.World->SetGameState(CastChecked<AGameStateBase>(Second->GetOwner()));
	TestEqual(TEXT("correct provider recovers"), Label(*Sign)->Text.ToString(), FString(TEXT("102")));
	Sign->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	Second->SetFact(SignFact(), false);
	TestFalse(TEXT("ended sign remains hidden"), Label(*Sign)->IsVisible());
	TestEqual(TEXT("ended sign cannot update text"), Label(*Sign)->Text.ToString(), FString(TEXT("102")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStorySignMaterialTest, "VB.Unit.Narrative.StorySign.MaterialSelection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStorySignMaterialTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(*Fixture.World);
	AVBStorySign* Sign = MakeMaterialSign(*Fixture.World);
	if (!TestNotNull(TEXT("story"), Story) || !TestNotNull(TEXT("sign"), Sign))
	{
		return false;
	}

	Sign->DispatchBeginPlay();
	UStaticMeshComponent* Panel = Sign->FindComponentByClass<UStaticMeshComponent>();
	if (!TestNotNull(TEXT("panel"), Panel))
	{
		return false;
	}

	TestTrue(TEXT("false chooses Before material"), Panel->GetMaterial(0) == Sign->Before.Material);
	Story->SetFact(SignFact(), true);
	TestTrue(TEXT("true chooses After material"), Panel->GetMaterial(0) == Sign->After.Material);
	// sentinel 값으로 참이 안 바뀐 알림에서의 쓸데없는 대입을 잡는다.
	UMaterialInterface* Sentinel = UMaterial::GetDefaultMaterial(MD_Surface);
	Panel->SetMaterial(0, Sentinel);
	Story->AddToCounter(SignFact(), 1);
	TestTrue(TEXT("counter notification does not reassign unchanged material"), Panel->GetMaterial(0) == Sentinel);
	const FGameplayTag OtherFact = FGameplayTag::RequestGameplayTag(TEXT("Story.Collection.Enemy.TestEnemy"), false);
	if (!TestTrue(TEXT("unrelated fixture fact registered"), OtherFact.IsValid()))
	{
		return false;
	}

	Story->SetFact(OtherFact, true);
	TestTrue(TEXT("unrelated fact does not reassign unchanged material"), Panel->GetMaterial(0) == Sentinel);
	Story->ReadSave(*NewObject<UVBSaveGame>(), FVBSaveContext());
	TestTrue(TEXT("false restore replaces sentinel with Before"), Panel->GetMaterial(0) == Sign->Before.Material);
	TestEqual(TEXT("panel has no blocking collision"), Panel->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("panel cannot change navigation"), Panel->CanEverAffectNavigation());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStorySignRepNotifyTest, "VB.Unit.Narrative.StorySign.RepNotify",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStorySignRepNotifyTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(*Fixture.World);
	AVBStorySign* Sign = MakeTextSign(*Fixture.World);
	if (!TestNotNull(TEXT("story"), Story) || !TestNotNull(TEXT("sign"), Sign))
	{
		return false;
	}

	Sign->DispatchBeginPlay();
	FStructProperty* StateProperty = FindFProperty<FStructProperty>(Story->GetClass(), TEXT("State"));
	UFunction* RepNotify = Story->FindFunction(TEXT("OnRep_State"));
	if (!TestNotNull(TEXT("replicated story property"), StateProperty) || !TestNotNull(TEXT("repnotify"), RepNotify))
	{
		return false;
	}
	// SetFact 의 authority 쪽 broadcast 없이 속성 도착만 흉내낸다. 네트워크 시험은 아니다.
	FVBStoryState* State = StateProperty->ContainerPtrToValuePtr<FVBStoryState>(Story);
	State->SetFact(SignFact(), true);
	TestEqual(TEXT("no setter broadcast occurred"), Label(*Sign)->Text.ToString(), FString(TEXT("101")));
	Story->ProcessEvent(RepNotify, nullptr);
	TestEqual(TEXT("actual OnRep callback updates display"), Label(*Sign)->Text.ToString(), FString(TEXT("102")));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStorySignValidationTest, "VB.Unit.Narrative.StorySign.Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBStorySignValidationTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this) || !TestTrue(TEXT("fixture fact registered"), SignFact().IsValid()))
	{
		return false;
	}

	AVBStorySign* TextSign = MakeTextSign(*Fixture.World);
	AVBStorySign* MaterialSign = MakeMaterialSign(*Fixture.World);
	if (!TestNotNull(TEXT("text sign"), TextSign) || !TestNotNull(TEXT("material sign"), MaterialSign))
	{
		return false;
	}

	FDataValidationContext TextContext;
	FDataValidationContext MaterialContext;
	TestEqual(TEXT("complete text data valid"), TextSign->IsDataValid(TextContext), EDataValidationResult::Valid);
	TestEqual(TEXT("complete material data valid without TextFont or strings"), MaterialSign->IsDataValid(MaterialContext),
		EDataValidationResult::Valid);
	const auto Invalid = [&](const TCHAR* Name, bool bMaterial, TFunctionRef<void(AVBStorySign&)> Mutate)
	{
		AVBStorySign* Sign = bMaterial ? MakeMaterialSign(*Fixture.World) : MakeTextSign(*Fixture.World);
		if (!TestNotNull(Name, Sign))
		{
			return;
		}

		Mutate(*Sign);
		FDataValidationContext Context;
		TestEqual(Name, Sign->IsDataValid(Context), EDataValidationResult::Invalid);
		TestTrue(TEXT("invalid configuration emits an explicit error"), Context.GetNumErrors() > 0);
	};
	Invalid(TEXT("missing fact"), false, [](AVBStorySign& Sign) { Sign.FactTag = {}; });
	Invalid(TEXT("registered tag outside Story"), false, [](AVBStorySign& Sign) { Sign.FactTag = FGameplayTag::RequestGameplayTag(TEXT("Tutorial"), false); });
	Invalid(TEXT("empty Before.Text"), false, [](AVBStorySign& Sign) { Sign.Before.Text = FText::GetEmpty(); });
	Invalid(TEXT("whitespace Before.Text"), false, [](AVBStorySign& Sign) { Sign.Before.Text = FText::FromString(TEXT(" \t")); });
	Invalid(TEXT("whitespace After.Text"), false, [](AVBStorySign& Sign) { Sign.After.Text = FText::FromString(TEXT(" \t\n")); });
	Invalid(TEXT("empty After.Text"), false, [](AVBStorySign& Sign) { Sign.After.Text = FText::GetEmpty(); });
	Invalid(TEXT("missing TextFont"), false, [](AVBStorySign& Sign) { Sign.TextFont = nullptr; });
	Invalid(TEXT("runtime cache is not a TextRender atlas"), false, [](AVBStorySign& Sign) { Sign.TextFont->FontCacheType = EFontCacheType::Runtime; });
	Invalid(TEXT("missing TextMaterial"), false, [](AVBStorySign& Sign) { Sign.TextMaterial = nullptr; });
	Invalid(TEXT("zero size"), false, [](AVBStorySign& Sign) { Sign.TextWorldSize = 0.0f; });
	Invalid(TEXT("nonfinite size"), false, [](AVBStorySign& Sign) { Sign.TextWorldSize = std::numeric_limits<float>::infinity(); });
	Invalid(TEXT("invalid render enum"), false, [](AVBStorySign& Sign) { Sign.RenderMode = static_cast<EVBStorySignRenderMode>(255); });
	Invalid(TEXT("missing Before.Material"), true, [](AVBStorySign& Sign) { Sign.Before.Material = nullptr; });
	Invalid(TEXT("missing After.Material"), true, [](AVBStorySign& Sign) { Sign.After.Material = nullptr; });
	Invalid(TEXT("missing mesh"), true, [](AVBStorySign& Sign) { Sign.PanelMesh = nullptr; });
	Invalid(TEXT("missing slot"), true, [](AVBStorySign& Sign) { Sign.MaterialSlotName = NAME_None; });
	Invalid(TEXT("unresolved slot"), true, [](AVBStorySign& Sign) { Sign.MaterialSlotName = TEXT("NotASlot"); });
	TextSign->After.Text = FText::GetEmpty();
	AddExpectedError(TEXT("After.Text is empty"), EAutomationExpectedErrorFlags::Contains, 1);
	TextSign->DispatchBeginPlay();
	TestFalse(TEXT("invalid setup stays hidden at runtime"), Label(*TextSign)->IsVisible());
	return true;
}
#endif

#endif
