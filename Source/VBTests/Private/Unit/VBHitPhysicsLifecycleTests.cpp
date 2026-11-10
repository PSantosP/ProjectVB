// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "CharacterCommon/VBHitPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBHitPhysicsConfig.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitPhysicsLifecycleTest, "VB.Unit.Combat.HitPhysics.Handoff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBHitPhysicsLifecycleTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}
	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		Package->SetDirtyFlag(false);
	};
	World->InitializeActorsForPlay(FURL());
	ACharacter* Character = World->SpawnActor<ACharacter>();
	USkeletalMeshComponent* Mesh = Character->GetMesh();
	USkeletalMesh* MeshAsset = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin.SKM_UEFN_Mannequin"));
	if (!TestNotNull(TEXT("mesh asset"), MeshAsset))
	{
		return false;
	}
	Mesh->SetSkeletalMeshAsset(MeshAsset);
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	UVBHitPhysicsConfig* Config = NewObject<UVBHitPhysicsConfig>(Character);
	Config->SimulateRootBone = TEXT("spine_02");
	Config->PhysicalAnimation.OrientationStrength = 1000.0f;
	FVBHitPhysicsTier& Tier = Config->Tiers.Add(EVBHitImpact::Light);
	Tier.ImpulseSpeed = 100.0f;
	Tier.MaxBlendWeight = 0.5f;
	UVBHitPhysicsComponent* Hit = NewObject<UVBHitPhysicsComponent>(Character);
	Hit->RegisterComponent();
	Hit->SetConfig(Config);
	UPhysicalAnimationComponent* Drive = Character->FindComponentByClass<UPhysicalAnimationComponent>();
	if (!TestNotNull(TEXT("drive component"), Drive))
	{
		return false;
	}
	TestNull(TEXT("inactive response owns no mesh motors"), Drive->GetSkeletalMesh());
	for (int32 Entry = 0; Entry < 3; ++Entry)
	{
		Hit->ApplyHit(TEXT("spine_03"), FVector::ForwardVector, EVBHitImpact::Light);
		TestTrue(TEXT("response activates repeatedly"), Hit->IsActive());
		TestEqual(TEXT("active response owns the mesh"), Drive->GetSkeletalMesh(), Mesh);
		Drive->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		if (Entry == 0)
		{
			Hit->StopAll();
		}
		else if (Entry == 1)
		{
			Hit->TickComponent(Tier.TotalSeconds() + 0.1f, LEVELTICK_All, nullptr);
		}
		else
		{
			Hit->ApplyHit(NAME_None, FVector::ForwardVector, EVBHitImpact::Knockdown);
		}
		TestFalse(TEXT("response stopped"), Hit->IsActive());
		TestNull(TEXT("motors released before another engine tick"), Drive->GetSkeletalMesh());
		TestFalse(TEXT("inactive drive does not tick"), Drive->IsComponentTickEnabled());
		TestEqual(TEXT("collision restored"), Mesh->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Mesh->SetAllBodiesBelowSimulatePhysics(TEXT("pelvis"), true);
		Hit->StopAll();
		TestTrue(TEXT("repeated stop preserves the next ragdoll owner"), Mesh->IsSimulatingPhysics(TEXT("spine_02")));
		Mesh->SetAllBodiesSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	return true;
}
#endif
