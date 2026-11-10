#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Camera/VBEnvironmentCameraPolicy.h"
#include "Camera/VBEnvironmentCameraSolver.h"
#include "Camera/VBPlayerCameraManager.h"
#include "Camera/VBEnvironmentCameraComponent.h"
#include "Data/VBEnvironmentCameraConfig.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameplayCameraComponent.h"
#include "Core/CameraAsset.h"
#include "Misc/ScopeExit.h"
#include <limits>
#include "VBIsolatedGameWorld.h"

namespace
{
	UWorld* CreateCollisionWorld()
	{
		const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
			.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
		return UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	}

	// (0,0,90) 에 로컬 possess 한 기본 캐릭터. 이 격리 월드는 게임을 시작하지 않으므로
	//   보통 PostInitializeComponents 가 주는 camera manager 를 직접 만든다.
	ACharacter* SpawnLocalCameraPawn(FAutomationTestBase& Test, UWorld& World)
	{
		auto* Pawn = World.SpawnActor<ACharacter>();
		auto* PC = World.SpawnActor<APlayerController>();
		if (!Test.TestNotNull(TEXT("Pawn"), Pawn) || !Test.TestNotNull(TEXT("Controller"), PC))
		{
			return nullptr;
		}

		PC->SetAsLocalPlayerController();
		if (!PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager = World.SpawnActor<APlayerCameraManager>();
			PC->PlayerCameraManager->InitializeFor(PC);
		}

		PC->Possess(Pawn);
		PC->SetViewTarget(Pawn);
		if (!Test.TestTrue(TEXT("Fixture is locally possessed"), Pawn->GetController() == PC && PC->IsLocalController()))
		{
			return nullptr;
		}

		Pawn->SetActorLocation(FVector(0, 0, 90));
		return Pawn;
	}

	// SpawnCameraBlocker - camera 채널만 막는 상자.
	UBoxComponent* SpawnCameraBlocker(UWorld& World, const FVector& Extent, const FVector& Location)
	{
		auto* Actor = World.SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Ignore);
		Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
		Box->RegisterComponent();
		Actor->SetActorLocation(Location);
		return Box;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraSafetyTest, "VB.Camera.Environment.SafetyAndHysteresis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraSafetyTest::RunTest(const FString& Parameters)
{
	using namespace VBEnvironmentCameraPolicy;
	FCandidate Candidates[3];
	Candidates[0] = {FVector::ZeroVector, -1000, false};
	Candidates[1] = {FVector::ZeroVector, 10, true};
	Candidates[2] = {FVector::ZeroVector, 5, true};
	TestEqual(TEXT("Unsafe candidate cannot win by score"), SelectCandidate(Candidates, 0, 8), 2);
	TestEqual(TEXT("Small improvement retains current shoulder candidate"), SelectCandidate(Candidates, 1, 8), 1);
	Candidates[1].bSafe = false;
	TestEqual(TEXT("Safety bypasses hysteresis"), SelectCandidate(Candidates, 1, 1000), 2);
	Candidates[2].Score = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("No safe finite candidate is explicit"), SelectCandidate(Candidates, 2, 8), INDEX_NONE);
	FRecovery Recovery;
	TestEqual(TEXT("Zero dt cannot delay collision avoidance"), Recovery.Update(0.3, 0, 0.2, 0.2, false), 0.3);
	TestEqual(TEXT("Clearance flicker cannot restore distance"), Recovery.Update(1, 0.1, 0.2, 0.2, true), 0.3);
	Recovery.Update(1, 0.1, 0.2, 0.2, false);
	TestEqual(TEXT("Blocked frame resets hold"), Recovery.ClearTime, 0.0);
	Recovery.Update(1, 0.4, 0.2, 0.2, true);
	TestTrue(TEXT("One half-life after hold restores half the deficit"), FMath::IsNearlyEqual(Recovery.Fraction, 0.65, 0.00001));
	TestEqual(TEXT("New moving obstacle clamps immediately"), Recovery.Update(0.1, 1.0/120, 0.2, 0.2, false), 0.1);
	TestEqual(TEXT("Invalid sample is conservative"), Recovery.Update(std::numeric_limits<double>::quiet_NaN(), 1, 0.2, 0.2, true), 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraRateTest, "VB.Camera.Environment.FrameRateRecovery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraRateTest::RunTest(const FString& Parameters)
{
	using namespace VBEnvironmentCameraPolicy;
	for (int32 FPS : {30, 60, 120})
	{
		FRecovery Recovery;
		Recovery.Update(0.25, 0, 0.2, 0.2, false);
		for (int32 Frame = 0; Frame < FPS; ++Frame)
		{
			Recovery.Update(1, 1.0/FPS, 0.2, 0.2, true);
		}

		TestTrue(FString::Printf(TEXT("%dfps: one second includes four half-lives after hold"), FPS),
			FMath::IsNearlyEqual(Recovery.Fraction, 1.0 - 0.75/16, 0.00001));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraGeometryTest, "VB.Camera.Environment.WorldCollision",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraGeometryTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateCollisionWorld();
	if (!TestNotNull(TEXT("Collision fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACharacter* Pawn = SpawnLocalCameraPawn(*this, *World);
	if (!Pawn)
	{
		return false;
	}

	UVBEnvironmentCameraConfig* Config = NewObject<UVBEnvironmentCameraConfig>();
	FVBEnvironmentCameraSolver Solver;
	FMinimalViewInfo Desired;
	Desired.Location = FVector(-300, 0, 120);
	Desired.Rotation = FRotator(-10, 5, 0);
	Desired.FOV = 90;
	FMinimalViewInfo Pose = Desired;
	Solver.Update(Pawn, Config, 1.f/60, false, Pose);
	TestTrue(TEXT("Open space preserves desired position"), Pose.Location.Equals(Desired.Location, 0.01));

	UBoxComponent* Box = SpawnCameraBlocker(*World, FVector(10, 200, 200), FVector(-150, 0, 100));
	AActor* Wall = Box->GetOwner();
	Pose = Desired;
	Solver.Update(Pawn, Config, 0, false, Pose);
	TestTrue(TEXT("New wall immediately shortens the camera, even at zero dt"), Pose.Location.X > -140);
	TestTrue(TEXT("Environment correction preserves rotation"), Pose.Rotation.Equals(Desired.Rotation));
	TestEqual(TEXT("Environment correction preserves FOV"), Pose.FOV, Desired.FOV);
	const FVector Constrained = Pose.Location;
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pose = Desired;
	Solver.Update(Pawn, Config, 0.05f, false, Pose);
	TestTrue(TEXT("Wall removal does not immediately pop back"), FVector::Distance(Pose.Location, Desired.Location) > 50);
	for (int32 I=0; I<180; ++I)
	{
		Pose=Desired;
		Solver.Update(Pawn, Config, 1.f/60, false, Pose);
	}

	TestTrue(TEXT("Open space fully recovers, including final clearance margin"), Pose.Location.Equals(Desired.Location, 0.2));

	// 머리 위 판 아래의 실제 짧은 capsule - 서 있는 높이의 원하는 카메라 자리가 막힌다.
	Pawn->GetCapsuleComponent()->SetCapsuleSize(34, 44);
	Pawn->SetActorLocation(FVector(0,0,44));
	Box->SetBoxExtent(FVector(400,400,10));
	Wall->SetActorLocation(FVector(-150,0,110));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Pose = Desired;
	Solver.Update(Pawn, Config, 1.f/60, true, Pose);
	TestTrue(TEXT("Crouched anchor stays below slab"), Pose.Location.Z < 100);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VBCameraTest), false, Pawn);
	TestFalse(TEXT("Final camera minimum envelope is not overlapping slab"), World->OverlapBlockingTestByChannel(
		Pose.Location, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Config->MinimumProbeRadius), Query));
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Pose = Desired;
	Solver.Update(Pawn, Config, 0, true, Pose);
	TestTrue(TEXT("Camera cut resets prior constraint immediately"), Pose.Location.Equals(Desired.Location, 0.01));
	return true;
}

// 벽에 닿은 뒤 캐릭터는 가는데 카메라가 그 자리에 박혀 있다가 한참 뒤 돌아오는 증상의 재현.
//   직전 위치와 이번 위치가 둘 다 기준점에서 보이는데 그 사이 직선이 기둥 모서리를 지나면, 직전 위치 -> 이번 위치
//   재스윕이 카메라를 직전 위치에 세운다. 다음 프레임도 거기서 시작하니 길이 열릴 때까지 월드에 박힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraCornerTest, "VB.Camera.Environment.CornerDoesNotPin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraCornerTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateCollisionWorld();
	if (!TestNotNull(TEXT("Collision fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACharacter* Pawn = SpawnLocalCameraPawn(*this, *World);
	if (!Pawn)
	{
		return false;
	}

	// 기둥이 x,y 100~200 을 차지한다. 기준점(원점 근처)에서는 +X 쪽과 +Y 쪽이 다 보이지만 둘을 잇는 직선은 기둥을 지난다.
	SpawnCameraBlocker(*World, FVector(50, 50, 300), FVector(150, 150, 100));

	UVBEnvironmentCameraConfig* Config = NewObject<UVBEnvironmentCameraConfig>();
	FVBEnvironmentCameraSolver Solver;
	FMinimalViewInfo Before;
	Before.Location = FVector(300, 0, 120);
	Before.Rotation = FRotator(-10, 180, 0);
	Before.FOV = 90;
	FMinimalViewInfo Pose = Before;
	Solver.Update(Pawn, Config, 1.f/60, false, Pose);
	if (!TestTrue(TEXT("Control: the +X side is clear from the anchor"), Pose.Location.Equals(Before.Location, 0.01)))
	{
		return false;
	}

	FMinimalViewInfo After = Before;
	After.Location = FVector(0, 300, 120);
	After.Rotation = FRotator(-10, -90, 0);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(VBCameraCornerTest), false, Pawn);
	FHitResult Hit;
	TestTrue(TEXT("Control: the straight path between the two sides crosses the pillar"), World->SweepSingleByChannel(
		Hit, Before.Location, After.Location, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Config->MinimumProbeRadius), Query));
	for (int32 Frame = 0; Frame < 120; ++Frame)
	{
		Pose = After;
		Solver.Update(Pawn, Config, 1.f/60, false, Pose);
	}

	TestTrue(FString::Printf(TEXT("Clear desired camera is not held at the previous side (actual %s)"), *Pose.Location.ToString()),
		Pose.Location.Equals(After.Location, 0.2));
	return true;
}

// 안전 위치(Lyra UpdatePreventPenetration): 캡슐 축에서 시선과 가장 가까운 높이, 위아래는 HalfHeight - PushIn 안, 옆은 표면에서 PushIn 안쪽.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraSafeLocationTest, "VB.Camera.Environment.SafeLocationOnCapsule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraSafeLocationTest::RunTest(const FString& Parameters)
{
	using namespace VBEnvironmentCameraPolicy;
	const FVector Center(0, 0, 90);
	const FVector Up = FVector::UpVector;
	auto Safe = [&](const FVector& ViewLocation, const FVector& ViewDirection, double PushIn = 17)
	{
		return CapsuleSafeLocation(Center, Up, 88, 34, PushIn, ViewLocation, ViewDirection);
	};
	TestTrue(TEXT("Level view through the axis: the axis point at the view height"),
		Safe(FVector(-300, 0, 100), FVector::ForwardVector).Equals(FVector(0, 0, 100), 0.01));
	TestEqual(TEXT("View line above the capsule: clamped to HalfHeight - PushIn"),
		Safe(FVector(-300, 0, 400), FVector::ForwardVector).Z, 90.0 + 71.0, 0.01);
	TestEqual(TEXT("View line below the capsule: clamped to -(HalfHeight - PushIn)"),
		Safe(FVector(-300, 0, -200), FVector::ForwardVector).Z, 90.0 - 71.0, 0.01);
	TestTrue(TEXT("View line beside the capsule: surface point pushed in"),
		Safe(FVector(-300, 100, 90), FVector::ForwardVector).Equals(FVector(0, 17, 90), 0.01));
	const FVector Degenerate = Safe(FVector(-300, 0, 90), FVector::ZeroVector);
	TestTrue(TEXT("No view direction: finite point inside the capsule"),
		!Degenerate.ContainsNaN() && Degenerate.Equals(FVector(-17, 0, 90), 0.01));
	TestEqual(TEXT("PushIn above the half height: the capsule centre height"),
		Safe(FVector(-300, 0, 400), FVector::ForwardVector, 100).Z, 90.0, 0.01);
	return true;
}

// 캡슐이 통째로 막힌 곳에 있고 직전 기록도 없으면, 검사 안 된 입력 위치(벽 속일 수 있다) 대신 캡슐 가운데를 낸다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraUnresolvedTest, "VB.Camera.Environment.UnresolvedAnchorFailsClosed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraUnresolvedTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateCollisionWorld();
	if (!TestNotNull(TEXT("Collision fixture world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	ACharacter* Pawn = SpawnLocalCameraPawn(*this, *World);
	if (!Pawn)
	{
		return false;
	}

	SpawnCameraBlocker(*World, FVector(400, 400, 400), FVector(0, 0, 90));
	UVBEnvironmentCameraConfig* Config = NewObject<UVBEnvironmentCameraConfig>();
	FVBEnvironmentCameraSolver Solver;
	FMinimalViewInfo Desired;
	Desired.Location = FVector(-300, 0, 120);
	Desired.Rotation = FRotator(-10, 0, 0);
	Desired.FOV = 90;
	FMinimalViewInfo Pose = Desired;
	Solver.Update(Pawn, Config, 1.f/60, false, Pose);
	TestFalse(TEXT("The unchecked desired position (inside the box) is not passed out"), Pose.Location.Equals(Desired.Location, 1.0));
	TestTrue(FString::Printf(TEXT("Held at the capsule centre (actual %s)"), *Pose.Location.ToString()),
		Pose.Location.Equals(Pawn->GetCapsuleComponent()->GetComponentLocation(), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnvironmentCameraAssetsTest, "VB.Camera.Environment.AuthoredAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBEnvironmentCameraAssetsTest::RunTest(const FString& Parameters)
{
	UClass* CharacterClass = LoadClass<ACharacter>(nullptr, TEXT("/Game/VB/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C"));
	UClass* ControllerClass = LoadClass<APlayerController>(nullptr,
		TEXT("/Game/VB/Blueprints/GameModes/BP_VBPlayerController.BP_VBPlayerController_C"));
	if (!TestNotNull(TEXT("Character class"), CharacterClass) || !TestNotNull(TEXT("Controller class"), ControllerClass))
	{
		return false;
	}
	// CDO 가 아닌 실제 인스턴스 - environment camera 는 CDO 에 없는 BP_VBCharacter 의 component 목록에서 온다.
	//   월드가 BeginPlay 를 하지 않으니 여기서는 아무것도 BeginPlay 를 돌지 않는다.
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	auto* Pawn = Isolated.World->SpawnActor<ACharacter>(CharacterClass, FVector::ZeroVector, FRotator::ZeroRotator);
	auto* PC = Cast<APlayerController>(ControllerClass->GetDefaultObject());
	if (!TestNotNull(TEXT("Character instance"), Pawn))
	{
		return false;
	}

	TestTrue(TEXT("Actual controller uses final camera correction"),
		PC->PlayerCameraManagerClass->IsChildOf(AVBPlayerCameraManager::StaticClass()));
	auto* Component = Pawn->FindComponentByClass<UVBEnvironmentCameraComponent>();
	auto* Camera = Pawn->FindComponentByClass<UGameplayCameraComponent>();
	if (!TestNotNull(TEXT("Environment component"), Component) || !TestNotNull(TEXT("Gameplay camera"), Camera))
	{
		return false;
	}

	TestNotNull(TEXT("Authored settings"), Component->Config.Get());
	if (Camera->CameraReference.IsValid())
	{
		TestEqual(TEXT("Project-owned camera is connected"),
		Camera->CameraReference.GetCameraAsset()->GetPathName(), FString(TEXT("/Game/VB/Blueprints/Cameras/CameraAsset_VBCharacter.CameraAsset_VBCharacter")));
	}
	else
	{
		AddError(TEXT("Gameplay camera has no asset"));
	}

	return true;
}
#endif
