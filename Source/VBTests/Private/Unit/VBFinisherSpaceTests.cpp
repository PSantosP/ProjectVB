#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/VBAnimNotifyState_InteractionWarp.h"
#include "Character/VBFinisherSpaceRules.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Data/VBFinisherConfig.h"
#include "Engine/World.h"
#include "PoseSearch/PoseSearchLibrary.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherSpacePathTest, "VB.Unit.Finisher.Space.Path",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBFinisherSpacePathTest::RunTest(const FString& Parameters)
{
	FVBFinisherSpaceSettings Settings;
	const FTransform MeshRelative(FRotator(0, -90, 0), FVector(0, 0, -90));
	const FTransform Roots[] = {FTransform(FVector(100, 0, 0)), FTransform(FVector(400, 0, 80))};
	const FVector Start(0, 0, 90);
	FVBFinisherSpacePath Path;
	TestTrue(TEXT("valid route built"), VBFinisherSpaceRules::BuildPath(Roots, MeshRelative, Start, true, 35, 90, Settings, Path));
	TestEqual(TEXT("actual start is retained before aligned entry"), Path.Centers.Num(), 3);
	if (Path.Centers.Num() != 3)
	{
		return false;
	}
	TestTrue(TEXT("entry sweep starts at actual capsule"), Path.Centers[0].Equals(Start));
	TestTrue(TEXT("mesh offset becomes capsule height"), Path.Centers[1].Equals(FVector(100, 0, 90)));
	TestTrue(TEXT("walking capsule ignores authored root Z"), Path.Centers[2].Equals(FVector(400, 0, 90)));
	TestTrue(TEXT("airborne route built"), VBFinisherSpaceRules::BuildPath(Roots, MeshRelative, Start, false, 35, 90, Settings, Path));
	TestEqual(TEXT("airborne capsule retains root height"), Path.Centers.Last().Z, 170.0);
	Settings.SampleInterval = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("nonfinite settings rejected"), VBFinisherSpaceRules::BuildPath(Roots, MeshRelative, Start, true, 35, 90, Settings, Path));
	TestTrue(TEXT("failed construction clears prior route"), Path.Centers.IsEmpty());
	Settings = FVBFinisherSpaceSettings();
	TestFalse(TEXT("impossible capsule margin rejected"), VBFinisherSpaceRules::BuildPath(Roots, MeshRelative, Start, true, 90, 90, Settings, Path));
	FTransform Aligned;
	TestFalse(TEXT("missing interaction data is not an origin fallback"),
		UVBAnimNotifyState_InteractionWarp::CalculateAlignedTransform(FPoseSearchBlueprintResult(), 0, NAME_None, Aligned));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherSpaceCollisionTest, "VB.Unit.Finisher.Space.Collision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBFinisherSpaceCollisionTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	ON_SCOPE_EXIT
	{
		if (World)
		{
			World->DestroyWorld(false);
		}
	};
	if (!TestNotNull(TEXT("collision world"), World))
	{
		return false;
	}
	const auto SpawnBox = [&](FVector Location, FVector Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	};
	AActor* Attacker = SpawnBox(FVector(0, 0, 90), FVector(35, 35, 90));
	AActor* Victim = SpawnBox(FVector(0, 190, 90), FVector(35, 35, 90));
	const AActor* Participants[] = {Attacker, Victim};
	SpawnBox(FVector(200, 0, -10), FVector(1000, 1000, 10));
	FVBFinisherSpacePath Paths[2];
	const FVBFinisherSpaceSettings Settings;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector Start(0, Index * 190.0, 90);
		const FTransform Roots[] = {FTransform(Start), FTransform(Start + FVector(400, 0, 0))};
		if (!TestTrue(TEXT("pair path built"), VBFinisherSpaceRules::BuildPath(
			Roots, FTransform::Identity, Start, true, 35, 90, Settings, Paths[Index])))
		{
			return false;
		}
	}
	const auto Clear = [&]
	{
		return VBFinisherSpaceRules::IsClear(*World, Paths, Participants);
	};
	TestTrue(TEXT("ground contact and both participants are excluded"), Clear());
	AActor* Wall = SpawnBox(FVector(200, 0, 90), FVector(5, 50, 90));
	TestFalse(TEXT("wall between clear endpoints rejects attacker route"), Clear());
	Wall->SetActorLocation(FVector(200, 190, 90));
	TestFalse(TEXT("victim-only obstacle rejects whole pair"), Clear());
	Wall->SetActorLocation(FVector(800, 190, 90));
	TestTrue(TEXT("moving wall clear restores admission"), Clear());
	AActor* Prop = SpawnBox(FVector(200, 0, 90), FVector(10));
	Prop->AttachToActor(Attacker, FAttachmentTransformRules::KeepWorldTransform);
	TestFalse(TEXT("unowned attachment still blocks"), Clear());
	Prop->SetOwner(Attacker);
	TestTrue(TEXT("owned weapon attachment does not block own execution"), Clear());
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Bystander = World->SpawnActor<ACharacter>(FVector(200, 0, 90), FRotator::ZeroRotator, SpawnParameters);
	if (!TestNotNull(TEXT("third pawn"), Bystander))
	{
		return false;
	}
	UCapsuleComponent* ThirdCapsule = Bystander->GetCapsuleComponent();
	ThirdCapsule->SetCapsuleSize(35, 90);
	ThirdCapsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ThirdCapsule->SetCollisionObjectType(ECC_Pawn);
	ThirdCapsule->SetCollisionResponseToAllChannels(ECR_Block);
	TestFalse(TEXT("blocking third pawn rejects the pair without being ignored"), Clear());
	Paths[0].Responses.SetResponse(ECC_Pawn, ECR_Ignore);
	TestTrue(TEXT("authored pawn-ignore response is respected"), Clear());
	Paths[0].Responses.SetResponse(ECC_Pawn, ECR_Block);
	Bystander->SetActorLocation(FVector(200, 190, 90));
	TestFalse(TEXT("third pawn blocking only victim route also rejects"), Clear());
	Bystander->SetActorLocation(FVector(900, 600, 90));
	TestTrue(TEXT("bystander moving clear restores admission"), Clear());
	Paths[1].Centers[1] = FVector(800, 190, 90);
	TestFalse(TEXT("initial aligned location inside obstacle rejects"), Clear());
	Paths[1].Centers[1] = FVector(std::numeric_limits<double>::quiet_NaN(), 0, 90);
	TestFalse(TEXT("nonfinite route rejects"), Clear());
	return true;
}
#endif
