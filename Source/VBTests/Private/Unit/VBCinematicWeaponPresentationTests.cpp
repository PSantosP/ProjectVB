// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBSummonedWeapon.h"
#include "Character/VBWeaponStateComponent.h"
#include "Character/VBWeaponVisualComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "VBCinematicDefinition.h"
#include "VBIsolatedGameWorld.h"

// 외형 배열을 주입하지 않고 실제 생성 경로와 감독의 공통 정리 경로를 호출한다.
class FVBWeaponPresentationTestAccess
{
public:
	static void StartVisuals(UVBWeaponVisualComponent& Visuals)
	{
		Visuals.BeginPlay();
		Visuals.CreateVisuals(EVBWeaponType::Blade);
	}

	static void RecreateVisuals(UVBWeaponVisualComponent& Visuals)
	{
		Visuals.DestroyVisuals();
		Visuals.CreateVisuals(EVBWeaponType::Fighter);
	}

	static bool StartPresentation(UVBCinematicDirectorSubsystem& Director, APawn& Pawn,
		UVBCinematicDefinition& Definition)
	{
		Director.bStopping = false;
		Director.RequestId = FGuid::NewGuid();
		Director.SessionId = FGuid::NewGuid();
		Director.ActivePawn = &Pawn;
		Director.ActiveDefinition = &Definition;
		return Director.AcquireWeaponPresentation(Pawn, Definition);
	}

	static void ClosePresentation(UVBCinematicDirectorSubsystem& Director, EVBCinematicEndType Type)
	{
		Director.EndResult = FVBCinematicEndResult{Director.SessionId, Type, EVBCinematicEndReason::None};
		Director.CloseRequest(false);
	}

	static void Stop(UVBCinematicDirectorSubsystem& Director)
	{
		Director.Stop();
	}
};

namespace
{
	constexpr auto PresentationFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	struct FWeaponPresentationFixture
	{
		FVBIsolatedGameWorld Scope;
		AVBCharacter* Pawn = nullptr;
		UVBWeaponVisualComponent* Visuals = nullptr;
		UVBWeaponStateComponent* Weapons = nullptr;
		UVBCinematicDefinition* Definition = nullptr;
		UVBCinematicDirectorSubsystem* Director = nullptr;

		bool Init(FAutomationTestBase& Test)
		{
			if (!Scope.Init(Test))
			{
				return false;
			}

			USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
				TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
			Pawn = Scope.World->SpawnActor<AVBCharacter>();
			if (!Test.TestNotNull(TEXT("actual skeletal mesh"), Mesh) || !Test.TestNotNull(TEXT("visual owner"), Pawn))
			{
				return false;
			}

			Pawn->GetMesh()->SetSkeletalMeshAsset(Mesh);
			Weapons = Pawn->FindComponentByClass<UVBWeaponStateComponent>();
			Visuals = NewObject<UVBWeaponVisualComponent>(Pawn);
			Pawn->AddInstanceComponent(Visuals);
			Visuals->VisualConfig = NewObject<UVBWeaponVisualConfig>(Visuals);
			FVBWeaponVisualPart Blade;
			Blade.Socket = TEXT("hand_r");
			FVBWeaponVisualStyle Style;
			Style.Parts.Add(Blade);
			Visuals->VisualConfig->Styles.Add(EVBWeaponType::Blade, Style);
			Visuals->VisualConfig->Styles.Add(EVBWeaponType::Fighter, Style);
			Visuals->RegisterComponent();
			FVBWeaponPresentationTestAccess::StartVisuals(*Visuals);
			Director = NewObject<UVBCinematicDirectorSubsystem>(Scope.World);
			Definition = NewObject<UVBCinematicDefinition>(Director);
			Definition->bHideHeldWeaponVisuals = true;
			return Test.TestNotNull(TEXT("weapon state"), Weapons) && Test.TestNotNull(TEXT("spawned visual"), FindVisual());
		}

		AVBSummonedWeapon* FindVisual() const
		{
			for (TActorIterator<AVBSummonedWeapon> It(Scope.World); It; ++It)
			{
				if (IsValid(*It) && It->GetOwner() == Pawn)
				{
					return *It;
				}
			}

			return nullptr;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicWeaponHideOwnershipTest,
	"VB.Unit.Cinematic.WeaponPresentation.Ownership", PresentationFlags)
bool FVBCinematicWeaponHideOwnershipTest::RunTest(const FString& Parameters)
{
	FWeaponPresentationFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	AVBSummonedWeapon* Visual = F.FindVisual();
	TestFalse(TEXT("ordinary part starts shown"), Visual->IsHidden());
	const EVBWeaponType BeforeWeapon = F.Weapons->GetCurrentWeaponType();
	const EVBLocomotionState BeforeState = F.Weapons->GetLocomotionState();
	const TArray<EVBWeaponType> BeforeOwned = F.Weapons->GetOwnedWeapons();
	const FGuid First = F.Visuals->BeginPresentationHide();
	const FGuid Second = F.Visuals->BeginPresentationHide();
	TestTrue(TEXT("both callers receive distinct valid tokens"), First.IsValid() && Second.IsValid() && First != Second);
	TestTrue(TEXT("actual actor is hidden"), Visual->IsHidden());
	TestFalse(TEXT("foreign token is refused"), F.Visuals->EndPresentationHide(FGuid::NewGuid()));
	TestTrue(TEXT("first token releases"), F.Visuals->EndPresentationHide(First));
	TestTrue(TEXT("second owner still holds hiding"), Visual->IsHidden());
	TestFalse(TEXT("duplicate release is refused"), F.Visuals->EndPresentationHide(First));
	TestTrue(TEXT("last token releases"), F.Visuals->EndPresentationHide(Second));
	TestFalse(TEXT("last release restores actor"), Visual->IsHidden());
	TestEqual(TEXT("selected weapon unchanged"), F.Weapons->GetCurrentWeaponType(), BeforeWeapon);
	TestEqual(TEXT("locomotion state unchanged"), F.Weapons->GetLocomotionState(), BeforeState);
	TestTrue(TEXT("owned weapons unchanged"), F.Weapons->GetOwnedWeapons() == BeforeOwned);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicWeaponHideRecreationTest,
	"VB.Unit.Cinematic.WeaponPresentation.RecreationAndGate", PresentationFlags)
bool FVBCinematicWeaponHideRecreationTest::RunTest(const FString& Parameters)
{
	FWeaponPresentationFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	const FGuid Hold = F.Visuals->BeginPresentationHide();
	const TWeakObjectPtr<AVBSummonedWeapon> OldVisual = F.FindVisual();
	FVBWeaponPresentationTestAccess::RecreateVisuals(*F.Visuals);
	AVBSummonedWeapon* Replacement = F.FindVisual();
	if (!TestNotNull(TEXT("replacement visual"), Replacement))
	{
		return false;
	}

	TestFalse(TEXT("old visual destroyed"), OldVisual.IsValid());
	TestTrue(TEXT("new visual remains hidden during hold"), Replacement->IsHidden());
	TestTrue(TEXT("release survives recreation"), F.Visuals->EndPresentationHide(Hold));
	TestFalse(TEXT("replacement restored"), Replacement->IsHidden());

	FVBWeaponVisualStyle& GatedStyle = F.Visuals->VisualConfig->Styles.FindChecked(EVBWeaponType::Fighter);
	GatedStyle.Parts[0].RequiredPassive = VBGameplayTags::Passive_Parry;
	FVBWeaponPresentationTestAccess::RecreateVisuals(*F.Visuals);
	AVBSummonedWeapon* Gated = F.FindVisual();
	if (!TestNotNull(TEXT("gated replacement"), Gated))
	{
		return false;
	}

	TestTrue(TEXT("unearned passive part starts hidden"), Gated->IsHidden());
	const FGuid GateHold = F.Visuals->BeginPresentationHide();
	TestTrue(TEXT("gated hold releases"), F.Visuals->EndPresentationHide(GateHold));
	TestTrue(TEXT("release does not reveal unearned passive part"), Gated->IsHidden());
	const FGuid ChangedGateHold = F.Visuals->BeginPresentationHide();
	GatedStyle.Parts[0].RequiredPassive = FGameplayTag();
	FVBWeaponPresentationTestAccess::RecreateVisuals(*F.Visuals);
	AVBSummonedWeapon* Ungated = F.FindVisual();
	if (!TestNotNull(TEXT("ungated replacement"), Ungated))
	{
		return false;
	}

	TestTrue(TEXT("new ungated part still obeys active hold"), Ungated->IsHidden());
	F.Visuals->EndPresentationHide(ChangedGateHold);
	TestFalse(TEXT("release uses current part policy instead of stale visibility"), Ungated->IsHidden());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicWeaponHideCleanupTest,
	"VB.Unit.Cinematic.WeaponPresentation.Cleanup", PresentationFlags)
bool FVBCinematicWeaponHideCleanupTest::RunTest(const FString& Parameters)
{
	FWeaponPresentationFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	TestFalse(TEXT("existing definitions opt out"), GetDefault<UVBCinematicDefinition>()->bHideHeldWeaponVisuals);
	const EVBCinematicEndType Outcomes[] = {EVBCinematicEndType::Completed, EVBCinematicEndType::Skipped,
		EVBCinematicEndType::Aborted, EVBCinematicEndType::Failed};
	for (EVBCinematicEndType Outcome : Outcomes)
	{
		TestTrue(TEXT("director acquires from actual owner"),
			FVBWeaponPresentationTestAccess::StartPresentation(*F.Director, *F.Pawn, *F.Definition));
		TestTrue(TEXT("director hold hides actor"), F.FindVisual()->IsHidden());
		FVBWeaponPresentationTestAccess::ClosePresentation(*F.Director, Outcome);
		TestFalse(TEXT("shared close path restores for each outcome"), F.FindVisual()->IsHidden());
	}

	FVBWeaponPresentationTestAccess::StartPresentation(*F.Director, *F.Pawn, *F.Definition);
	FVBWeaponPresentationTestAccess::Stop(*F.Director);
	TestFalse(TEXT("world shutdown cleanup restores"), F.FindVisual()->IsHidden());
	FVBWeaponPresentationTestAccess::Stop(*F.Director);
	TestFalse(TEXT("duplicate shutdown is harmless"), F.FindVisual()->IsHidden());

	FVBWeaponPresentationTestAccess::StartPresentation(*F.Director, *F.Pawn, *F.Definition);
	F.Visuals->DestroyComponent();
	FVBWeaponPresentationTestAccess::Stop(*F.Director);
	TestNull(TEXT("component destruction removes owned actor"), F.FindVisual());
	F.Definition->bHideHeldWeaponVisuals = false;
	TestTrue(TEXT("ordinary scene needs no weapon component"),
		FVBWeaponPresentationTestAccess::StartPresentation(*F.Director, *F.Pawn, *F.Definition));
	FVBWeaponPresentationTestAccess::ClosePresentation(*F.Director, EVBCinematicEndType::Failed);
	F.Definition->bHideHeldWeaponVisuals = true;
	AddExpectedError(TEXT("requires a weapon visual owner"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("requested hide refuses missing owner"),
		FVBWeaponPresentationTestAccess::StartPresentation(*F.Director, *F.Pawn, *F.Definition));
	FVBWeaponPresentationTestAccess::ClosePresentation(*F.Director, EVBCinematicEndType::Failed);
	return true;
}
#endif
