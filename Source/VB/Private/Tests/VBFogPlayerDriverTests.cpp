// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Environment/VBFogPlayerDriver.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFogPlayerDriverTest, "VB.Unit.Environment.FogPlayerDriver.PlayerPosition",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBFogPlayerDriverTest::RunTest(const FString& Parameters)
{
	if (!TestNotNull(TEXT("Engine"), GEngine))
	{
		return false;
	}

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
		.ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("World"), World))
	{
		return false;
	}

	TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>());
	World->SetGameInstance(GameInstance.Get());
	World->InitializeActorsForPlay(FURL());
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	APawn* Pawn = World->SpawnActor<APawn>();
	AVBFogPlayerDriver* Driver = World->SpawnActor<AVBFogPlayerDriver>();
	TStrongObjectPtr<UMaterialParameterCollection> Collection(NewObject<UMaterialParameterCollection>());
	FCollectionVectorParameter Parameter;
	Parameter.ParameterName = TEXT("PlayerPosition");
	Collection->VectorParameters.Add(Parameter);
	if (!TestNotNull(TEXT("Controller"), Controller) || !TestNotNull(TEXT("Pawn"), Pawn) || !TestNotNull(TEXT("Driver"), Driver))
	{
		World->DestroyWorld(false);
		return false;
	}

	USceneComponent* Root = NewObject<USceneComponent>(Pawn);
	Pawn->SetRootComponent(Root);
	Root->RegisterComponent();
	Pawn->SetActorLocation(FVector(110.0, 220.0, 330.0));
	Controller->Possess(Pawn);
	TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
	LocalPlayer->SwitchController(Controller);
	APlayerController* FirstController = World->GetFirstPlayerController();
	AddInfo(FString::Printf(TEXT("Fog fixture: initialized=%d first=%s expected=%s local=%d pawn=%s expectedPawn=%s"),
		World->AreActorsInitialized(), *GetNameSafe(FirstController), *GetNameSafe(Controller),
		FirstController && FirstController->IsLocalController(),
		*GetNameSafe(FirstController ? FirstController->GetPawn() : nullptr), *GetNameSafe(Pawn)));
	TestTrue(TEXT("Fixture first controller is registered"), FirstController == Controller);
	TestTrue(TEXT("Fixture controller is local"), FirstController && FirstController->IsLocalController());
	TestTrue(TEXT("Fixture controller possesses pawn"), FirstController && FirstController->GetPawn() == Pawn);
	UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(Collection.Get());
	if (!TestNotNull(TEXT("Collection instance"), Instance))
	{
		Controller->Player = nullptr;
		LocalPlayer->PlayerController = nullptr;
		World->DestroyWorld(false);
		return false;
	}

	Instance->SetVectorParameterValue(TEXT("PlayerPosition"), FLinearColor(17.0f, 0.0f, 0.0f, 0.0f));
	AddExpectedError(TEXT("FogParameters is missing"), EAutomationExpectedErrorFlags::Contains, 1);
	Driver->Tick(0.05f);
	Driver->Tick(0.05f);
	FLinearColor MissingValue;
	TestTrue(TEXT("Missing collection leaves parameter readable"), Instance->GetVectorParameterValue(TEXT("PlayerPosition"), MissingValue));
	TestEqual(TEXT("Missing collection does not write"), MissingValue.R, 17.0f);
#if WITH_EDITOR
	FDataValidationContext MissingContext;
	TestTrue(TEXT("Missing collection is rejected"), Driver->IsDataValid(MissingContext) == EDataValidationResult::Invalid);
#endif
	Driver->FogParameters = Collection.Get();
#if WITH_EDITOR
	FDataValidationContext ConfiguredContext;
	TestTrue(TEXT("Configured vector is accepted"), Driver->IsDataValid(ConfiguredContext) != EDataValidationResult::Invalid);
#endif
	Driver->Tick(0.05f);
	FLinearColor Value;
	const bool bRead = Instance && Instance->GetVectorParameterValue(TEXT("PlayerPosition"), Value);
	TestTrue(TEXT("Position parameter available"), bRead);
	if (bRead)
	{
		TestEqual(TEXT("X follows pawn"), Value.R, 110.0f);
		TestEqual(TEXT("Y follows pawn"), Value.G, 220.0f);
		TestEqual(TEXT("Player is active"), Value.A, 1.0f);
	}

	Driver->UpdateFogPosition(true);
	if (Instance && Instance->GetVectorParameterValue(TEXT("PlayerPosition"), Value))
	{
		TestEqual(TEXT("Cleanup clears player"), Value.A, 0.0f);
	}

	Controller->Player = nullptr;
	LocalPlayer->PlayerController = nullptr;
	World->DestroyWorld(false);
	return true;
}
#endif
