#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Camera/VBFinisherCamera.h"
#include "Camera/CameraTypes.h"
#include "Components/BoxComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Engine/World.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFinisherCameraCollisionTest, "VB.Unit.Camera.Finisher.Obstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBFinisherCameraCollisionTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	ON_SCOPE_EXIT { if (World) { World->DestroyWorld(false); } };
	if (!TestNotNull(TEXT("collision world"), World)) { return false; }
	const auto SpawnBox = [&](FVector Location, FVector Extent)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->AddInstanceComponent(Box);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Actor;
	};
	AActor* Attacker = SpawnBox(FVector(0, -20, 100), FVector(20));
	AActor* Victim = SpawnBox(FVector(0, 20, 100), FVector(20));
	AActor* Actors[] = {Attacker, Victim};
	FMinimalViewInfo View;
	View.FOV = 90;
	View.AspectRatio = 16.0f / 9.0f;
	View.PerspectiveNearClipPlane = 10;
	const FMatrix Projection = View.CalculateProjectionMatrix();
	FVBFinisherCameraCollisionSettings Settings;
	const FVector Desired(-300, 0, 100), LookAt(0, 0, 100);
	const auto Solve = [&](int32 Previous = INDEX_NONE)
	{
		return AVBFinisherCamera::ResolveView(*World, Settings, Desired, LookAt, Actors, Projection, Previous);
	};
	const FVBFinisherCameraView Open = Solve();
	TestTrue(TEXT("participants do not block their own view"), Open.bSafe && Open.bParticipantsVisible);
	TestTrue(TEXT("clear authored key is unchanged"), Open.Location.Equals(Desired));
	AActor* Weapon = SpawnBox(FVector(-120, 0, 100), FVector(35));
	Weapon->SetOwner(Attacker);
	Weapon->AttachToActor(Attacker, FAttachmentTransformRules::KeepWorldTransform);
	TestTrue(TEXT("owned attached weapon is excluded"), Solve().Location.Equals(Desired));
	Weapon->Destroy();
	AActor* Wall = SpawnBox(FVector(-150, 0, 100), FVector(10, 500, 500));
	const FVBFinisherCameraView Blocked = Solve(Open.Candidate);
	TestTrue(TEXT("foreground wall keeps both participants visible"), Blocked.bSafe && Blocked.bParticipantsVisible);
	TestTrue(TEXT("camera moved to participant side of wall"), Blocked.Location.X > -140 && Blocked.Location.X < -60);
	const double OldX = Blocked.Location.X;
	View.PerspectiveNearClipPlane = 30;
	const FVBFinisherCameraView LargeNearPlane = AVBFinisherCamera::ResolveView(*World, Settings, Desired,
		LookAt, Actors, View.CalculateProjectionMatrix());
	TestTrue(TEXT("larger near plane increases wall clearance"), LargeNearPlane.bSafe && LargeNearPlane.Location.X > OldX);
	TestFalse(TEXT("singular projection is rejected"),
		AVBFinisherCamera::ResolveView(*World, Settings, Desired, LookAt, Actors, FMatrix(ForceInit)).bSafe);
	Wall->SetActorLocation(FVector(-110, 0, 100));
	const FVBFinisherCameraView Moved = Solve(Blocked.Candidate);
	TestTrue(TEXT("moving obstacle invalidates previous candidate position"), Moved.bSafe && Moved.Location.X > OldX);
	Wall->SetActorLocation(LookAt);
	TestFalse(TEXT("penetrating anchor is rejected instead of retaining an unsafe camera"), Solve(Moved.Candidate).bSafe);
	Wall->SetActorLocation(FVector(-1000, 0, 100));
	Victim->SetActorLocation(FVector(-500, 0, 100));
	const FVBFinisherCameraView Behind = Solve();
	TestTrue(TEXT("collision-safe and participant visibility are distinct"), Behind.bSafe);
	TestFalse(TEXT("participant behind the camera is not visible"), Behind.bParticipantsVisible);
	Settings.MinimumProbeRadius = -1;
	TestFalse(TEXT("invalid probe settings rejected"), Solve().bSafe);
	Settings.MinimumProbeRadius = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("nonfinite tuning rejected"), Settings.IsValid());
	return true;
}
#endif
