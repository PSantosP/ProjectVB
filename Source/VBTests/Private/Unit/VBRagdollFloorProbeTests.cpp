// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "CharacterCommon/VBRagdollBody.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

// 누운 몸의 일어나기 바닥 쓸기: 골반이 바닥 가까이 누워 있어도 바닥을 찾고, 바닥이 없거나 무엇 안(바닥 속, 낮은 천장)에서 시작하면 찾지 않는다.
//   반경 34, 반높이 88 은 엔진 ACharacter 기본 캡슐 - 기하만 본다.
//   대조: 시작점을 골반 그대로 두면 "pelvis 10 cm above the floor" 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRagdollFloorProbeTest, "VB.Unit.Character.RagdollFloorProbe",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRagdollFloorProbeTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	// 판 하나: 가운데 높이 Z, 두께 20 cm, 가로세로 10 m.
	auto Slab = [World](float CenterZ)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(500.0, 500.0, 10.0));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->RegisterComponent();
		Box->SetWorldLocation(FVector(0.0, 0.0, CenterZ));
		return Actor;
	};
	AActor* Floor = Slab(-10.0f); // 윗면 0
	if (!TestNotNull(TEXT("floor"), Floor))
	{
		return false;
	}

	const float Radius = 34.0f;
	const float HalfHeight = 88.0f;
	const FCollisionQueryParams Params(SCENE_QUERY_STAT(VBRagdollFloorProbeTest), false);
	auto Probe = [&](const FVector& Pelvis, bool& bStartedInside)
	{
		FHitResult Hit;
		const bool bFloor = UVBPlayerRagdollComponent::SweepGetUpFloor(*World, Pelvis, Radius, HalfHeight, Params, Hit);
		bStartedInside = Hit.bStartPenetrating;
		return bFloor;
	};
	bool bInside = false;
	TestTrue(TEXT("pelvis 10 cm above the floor (a downed body): the floor is found"), Probe(FVector(0.0, 0.0, 10.0), bInside));
	TestTrue(TEXT("pelvis 120 cm above the floor: the floor is found"), Probe(FVector(0.0, 0.0, 120.0), bInside));
	TestFalse(TEXT("no floor below: none"), Probe(FVector(2000.0, 0.0, 10.0), bInside));
	TestFalse(TEXT("pelvis inside the floor slab: none"), Probe(FVector(0.0, 0.0, -5.0), bInside));
	TestTrue(TEXT("pelvis inside the floor slab: reported as started inside"), bInside);

	AActor* Ceiling = Slab(80.0f); // 아랫면 70 = 골반(10) 위 60 cm
	if (!TestNotNull(TEXT("ceiling"), Ceiling))
	{
		return false;
	}

	TestFalse(TEXT("low ceiling 60 cm above the pelvis: none"), Probe(FVector(0.0, 0.0, 10.0), bInside));
	TestTrue(TEXT("low ceiling: reported as started inside"), bInside);
	return true;
}

// 래그돌 전 충돌 설정을 잡아 두면, 래그돌이 프로필과 응답을 바꿔도 나올 때 그대로 돌아온다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRagdollCollisionStateTest, "VB.Unit.Character.RagdollCollisionState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRagdollCollisionStateTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UCapsuleComponent> Capsule(NewObject<UCapsuleComponent>(GetTransientPackage()));
	Capsule->SetCollisionProfileName(TEXT("Pawn"));
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Overlap);
	Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	const FCollisionResponseContainer Responses = Capsule->GetCollisionResponseToChannels();
	const ECollisionChannel ObjectType = Capsule->GetCollisionObjectType();
	const FName Profile = Capsule->GetCollisionProfileName();

	FVBRagdollCollisionState State;
	State.Capture(*Capsule);
	VBRagdollBody::ConfigureCapsule(*Capsule);
	Capsule->SetCollisionObjectType(ECC_WorldDynamic);
	TestEqual(TEXT("ragdoll capsule ignores pawns"), Capsule->GetCollisionResponseToChannel(ECC_Pawn), ECR_Ignore);
	TestEqual(TEXT("ragdoll capsule is blocked by the world"), Capsule->GetCollisionResponseToChannel(ECC_WorldStatic), ECR_Block);

	State.Apply(*Capsule);
	TestEqual(TEXT("profile restored"), Capsule->GetCollisionProfileName(), Profile);
	TestEqual(TEXT("enabled restored"), Capsule->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("object type restored"), Capsule->GetCollisionObjectType(), ObjectType);
	TestEqual(TEXT("custom response restored"), Capsule->GetCollisionResponseToChannel(ECC_Camera), ECR_Overlap);
	TestTrue(TEXT("all responses restored"), FMemory::Memcmp(&Capsule->GetCollisionResponseToChannels(), &Responses, sizeof(Responses)) == 0);
	return true;
}

#endif
