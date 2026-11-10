#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AI/VBAIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherWaitFacingTest, "VB.Unit.Finisher.WaitFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBFinisherWaitFacingTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};
	ACharacter* Pawn = World->SpawnActor<ACharacter>();
	AVBAIController* Controller = World->SpawnActor<AVBAIController>();
	if (!TestNotNull(TEXT("pawn"), Pawn) || !TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Pawn);
	Pawn->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Pawn, Pawn);
	Controller->Possess(Pawn);
	if (!TestTrue(TEXT("possessed fixture pawn"), Controller->GetPawn() == Pawn))
	{
		return false;
	}

	Pawn->SetActorRotation(FRotator(0, 90, 0));
	Controller->SetFocalPoint(FVector(0, -1000, 0), EAIFocusPriority::Gameplay);
	Controller->UpdateControlRotation(.016f, false);
	TestTrue(TEXT("normal focus faces opposite direction"), FMath::IsNearlyEqual(Controller->GetControlRotation().Yaw, -90.0, .01));
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);
	Controller->UpdateControlRotation(.016f, false);
	TestTrue(TEXT("reaction wait ignores stale gameplay focus"), FMath::IsNearlyEqual(Controller->GetControlRotation().Yaw, 90.0, .01));
	Controller->ClearFocus(EAIFocusPriority::Gameplay);
	Controller->SetFocalPoint(FVector(0, -1000, 0), EAIFocusPriority::Move);
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindow);
	Controller->UpdateControlRotation(.016f, false);
	TestTrue(TEXT("open window ignores lower priority movement focus"),
		FMath::IsNearlyEqual(Controller->GetControlRotation().Yaw, 90.0, .01));
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
	Controller->UpdateControlRotation(.016f, false);
	TestTrue(TEXT("uninterrupted counterattack retains aiming"), FMath::IsNearlyEqual(Controller->GetControlRotation().Yaw, -90.0, .01));
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
	Pawn->SetActorRotation(FRotator(0, 45, 0));
	Controller->UpdateControlRotation(.016f, false);
	TestTrue(TEXT("follows current root motion facing without restoring old yaw"),
		FMath::IsNearlyEqual(Controller->GetControlRotation().Yaw, 45.0, .01));
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindow);
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);
	Controller->UpdateControlRotation(.016f, false);
	TestTrue(TEXT("expiration restores normal focus"), FMath::IsNearlyEqual(Controller->GetControlRotation().Yaw, -90.0, .01));
	Pawn->SetActorRotation(FRotator(0, -30, 0));
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);
	Controller->UpdateControlRotation(.016f, false);
	TestTrue(TEXT("new window uses new direction"), FMath::IsNearlyEqual(Controller->GetControlRotation().Yaw, -30.0, .01));
	Controller->UnPossess();
	Controller->UpdateControlRotation(.016f, false);
	TestNull(TEXT("unpossessed update remains safe"), Controller->GetPawn());
	return true;
}
#endif
