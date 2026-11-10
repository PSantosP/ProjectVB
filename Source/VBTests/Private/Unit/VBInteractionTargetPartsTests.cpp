// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Interaction/VBPushableComponent.h"
#include "Misc/DataValidation.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractionTargetPartsTest, "VB.Unit.Interaction.TargetParts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractionTargetPartsTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this, true)) { return false; }
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	APawn* Pawn = Fixture.World->SpawnActor<APawn>();
	APlayerController* Controller = Fixture.World->SpawnActor<APlayerController>();
	AActor* Prop = Fixture.World->SpawnActor<AActor>();
	AStaticMeshActor* Leaf = Fixture.World->SpawnActor<AStaticMeshActor>();
	AStaticMeshActor* Wall = Fixture.World->SpawnActor<AStaticMeshActor>();
	if (!TestNotNull(TEXT("cube"), Cube) || !TestNotNull(TEXT("pawn"), Pawn) || !TestNotNull(TEXT("prop"), Prop)
		|| !TestNotNull(TEXT("leaf"), Leaf) || !TestNotNull(TEXT("wall"), Wall) || !TestNotNull(TEXT("controller"), Controller)) { return false; }

	USceneComponent* Origin = NewObject<USceneComponent>(Pawn);
	Pawn->SetRootComponent(Origin);
	Pawn->AddInstanceComponent(Origin);
	Origin->RegisterComponent();
	Pawn->SetActorLocation(FVector(0, 0, 100));
	Controller->Possess(Pawn);
	if (!TestTrue(TEXT("fixture controller possesses pawn"), Pawn->GetController() == Controller)) { return false; }
	Controller->SetControlRotation(FRotator::ZeroRotator);
	UVBInteractorComponent* Interactor = NewObject<UVBInteractorComponent>(Pawn);
	Pawn->AddInstanceComponent(Interactor);
	Interactor->RegisterComponent();
	UVBInteractableComponent* Target = NewObject<UVBInteractableComponent>(Prop);
	Prop->SetRootComponent(Target);
	Prop->AddInstanceComponent(Target);
	Target->RegisterComponent();
	Prop->SetActorLocation(FVector(150, 0, 100));

	for (AStaticMeshActor* Barrier : {Leaf, Wall})
	{
		UStaticMeshComponent* Mesh = Barrier->GetStaticMeshComponent();
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Barrier->SetActorScale3D(FVector(0.1, 1, 2));
	}
	Leaf->SetActorLocation(FVector(145, 0, 100));
	Wall->SetActorLocation(FVector(75, 500, 100));
	TestFalse(TEXT("an unbound secondary leaf blocks focus"), Interactor->CanReach(Target));
	TestFalse(TEXT("an unbound secondary leaf blocks keep"), Interactor->CanKeep(Target));

	FArrayProperty* Property = FindFProperty<FArrayProperty>(UVBInteractableComponent::StaticClass(), TEXT("TargetParts"));
	if (!TestNotNull(TEXT("authored target-parts property"), Property)) { return false; }
	TArray<TObjectPtr<AActor>>& Parts = *Property->ContainerPtrToValuePtr<TArray<TObjectPtr<AActor>>>(Target);
	Parts = {Leaf};
	TestTrue(TEXT("explicit target part allows focus"), Interactor->CanReach(Target));
	TestTrue(TEXT("explicit target part allows keep"), Interactor->CanKeep(Target));
	FHitResult PhysicalHit;
	const bool bStillBlocked = Fixture.World->SweepSingleByChannel(PhysicalHit, FVector(0, 0, 100), FVector(300, 0, 100),
		FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34, 88),
		FCollisionQueryParams(TEXT("TargetPartsPhysicalCollision"), false, Pawn));
	TestTrue(TEXT("target-part binding preserves physical passage collision"), bStillBlocked && PhysicalHit.GetActor() == Leaf);
	Pawn->SetActorLocation(FVector(300, 0, 100));
	Pawn->SetActorRotation(FRotator(0, 180, 0));
	TestTrue(TEXT("the other-side geometry allows keeping before the camera turns"), Interactor->CanKeep(Target));
	TestFalse(TEXT("body rotation alone does not turn the interaction view"), Interactor->CanReach(Target));
	Controller->SetControlRotation(FRotator(0, 180, 0));
	TestTrue(TEXT("the pawn reads the controller reverse-side yaw"), FMath::Abs(FMath::FindDeltaAngleDegrees(Pawn->GetControlRotation().Yaw, 180.0)) < 0.1);
	TestTrue(TEXT("the same assembly is reachable from its other side"), Interactor->CanReach(Target));
	Pawn->SetActorLocation(FVector(0, 0, 100));
	Pawn->SetActorRotation(FRotator::ZeroRotator);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	Wall->SetActorLocation(FVector(75, 0, 100));
	TestFalse(TEXT("an unrelated wall still blocks focus"), Interactor->CanReach(Target));
	TestFalse(TEXT("an unrelated wall still blocks keep"), Interactor->CanKeep(Target));
	Wall->SetActorLocation(FVector(75, 500, 100));
	Parts.Reset();
	TestFalse(TEXT("removing the binding restores the real leaf blocker"), Interactor->CanReach(Target));

#if WITH_EDITOR
	for (const TArray<TObjectPtr<AActor>>& Invalid : TArray<TArray<TObjectPtr<AActor>>>{{nullptr}, {Prop}, {Leaf, Leaf}})
	{
		Parts = Invalid;
		FDataValidationContext Context;
		TestTrue(TEXT("null, self and duplicate parts fail validation"), Target->IsDataValid(Context) == EDataValidationResult::Invalid);
	}
	FVBIsolatedGameWorld OtherWorld;
	if (!OtherWorld.Init(*this)) { return false; }
	AActor* Foreign = OtherWorld.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("foreign-world part"), Foreign)) { return false; }
	Parts = {Foreign};
	FDataValidationContext CrossWorld;
	TestTrue(TEXT("cross-world parts fail validation"), Target->IsDataValid(CrossWorld) == EDataValidationResult::Invalid);
	Parts = {Leaf};
	FDataValidationContext Valid;
	TestTrue(TEXT("valid part binding is not invalid"), Target->IsDataValid(Valid) != EDataValidationResult::Invalid);
#endif
	return true;
}

// 밀기 이동 스윕도 시야 검사와 같은 대상 조각(TargetParts)을 무시해야 한다. 처음부터 겹쳐 있는 자기 부품에
// 막히면 밀어도 0cm 다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPushSweepIgnoresTargetPartsTest, "VB.Unit.Interaction.PushSweepIgnoresTargetParts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPushSweepIgnoresTargetPartsTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this, true)) { return false; }
	AActor* Owner = Fixture.World->SpawnActor<AActor>();
	AActor* Pusher = Fixture.World->SpawnActor<AActor>();
	AActor* Part = Fixture.World->SpawnActor<AActor>();
	AActor* Wall = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("owner"), Owner) || !TestNotNull(TEXT("pusher"), Pusher) || !TestNotNull(TEXT("part"), Part) || !TestNotNull(TEXT("wall"), Wall)) { return false; }
	TArray<TObjectPtr<AActor>> Parts = { Part, nullptr };
	const FCollisionQueryParams Params = UVBPushableComponent::MakePushSweepParams(Owner, Pusher, Parts);
	const auto& Ignored = Params.GetIgnoredActors();
	TestTrue(TEXT("the pushed target itself is ignored"), Ignored.Contains(Owner->GetUniqueID()));
	TestTrue(TEXT("the pushing character is ignored"), Ignored.Contains(Pusher->GetUniqueID()));
	TestTrue(TEXT("a part of the same target is ignored"), Ignored.Contains(Part->GetUniqueID()));
	TestFalse(TEXT("an unrelated wall still blocks"), Ignored.Contains(Wall->GetUniqueID()));
	TestEqual(TEXT("an empty part slot adds nothing"), Ignored.Num(), 3);
	return true;
}
#endif
