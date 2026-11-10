// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBWeaponPickup.h"
#include "Inventory/VBWorldItem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

// 바닥에 놓인 줍기 대상에 닿는다 - 시야 capsule(DA_Interactable_Pickup 규칙)이 몸에서 상호작용 점까지 간다.
//   점을 들어올리지 않으면(UVBInteractableComponent::PlaceAboveRestingMesh) 바닥에 묻혀 프롬프트가 안 뜬다.
//   음성 대조: 무기 줍기에서 PlaceAboveRestingMesh 호출을 빼면 "reachable" 줄이 실패한다.
namespace VBPickupReachTest
{
	bool Reaches(const AActor& Body, const UVBInteractableComponent& Point)
	{
		const FVector To = Point.GetComponentLocation() - Body.GetActorLocation();
		return UVBAbilitySystemStatics::PassesReachRules(&Body, To.Rotation().Yaw, Point.GetComponentLocation(), Point.GetOwner(),
			Point.GetConfig()->ReachRules);
	}

	void UseConfig(UVBInteractableComponent& Point, const UVBInteractableConfig* Config)
	{
		FObjectProperty* Property = FindFProperty<FObjectProperty>(UVBInteractableComponent::StaticClass(), TEXT("Config"));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(&Point, const_cast<UVBInteractableConfig*>(Config));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPickupReachTest, "VB.Unit.Interaction.PickupReach",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPickupReachTest::RunTest(const FString& Parameters)
{
	using namespace VBPickupReachTest;
	const UVBInteractableConfig* PickupConfig = LoadObject<UVBInteractableConfig>(nullptr,
		TEXT("/Game/VB/Data/Interaction/DA_Interactable_Pickup.DA_Interactable_Pickup"));
	UStaticMesh* Blade = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/VB/Weapons/Meshes/SM_SummonedBlade.SM_SummonedBlade"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("shipped DA_Interactable_Pickup"), PickupConfig) || !TestNotNull(TEXT("SM_SummonedBlade"), Blade)
		|| !TestNotNull(TEXT("engine cube"), Cube))
	{
		return false;
	}

	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	World->InitializeActorsForPlay(FURL());

	// 윗면 z = 0 인 20 m 바닥, 120 cm 떨어져 선 몸(capsule 중심 92 cm 위), 바닥에 놓인 줍기 대상 둘
	AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, -50.0), FRotator::ZeroRotator);
	AStaticMeshActor* Body = World->SpawnActor<AStaticMeshActor>(FVector(-120.0, 0.0, 92.0), FRotator::ZeroRotator);
	AVBWeaponPickup* Weapon = World->SpawnActor<AVBWeaponPickup>(FVector(0.0, 0.0, 2.0), FRotator::ZeroRotator);
	AVBWorldItem* Item = World->SpawnActor<AVBWorldItem>(FVector(0.0, 60.0, 2.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("floor"), Floor) || !TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("weapon pickup"), Weapon)
		|| !TestNotNull(TEXT("item pickup"), Item))
	{
		return false;
	}

	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Floor->SetActorScale3D(FVector(20.0, 20.0, 1.0));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));

	for (AActor* Pickup : TArray<AActor*>{Weapon, Item})
	{
		UVBInteractableComponent* Point = Pickup->FindComponentByClass<UVBInteractableComponent>();
		UStaticMeshComponent* Mesh = Pickup->FindComponentByClass<UStaticMeshComponent>();
		if (!TestNotNull(TEXT("interaction point"), Point) || !TestNotNull(TEXT("pickup mesh"), Mesh))
		{
			continue;
		}

		UseConfig(*Point, PickupConfig);
		Mesh->SetStaticMesh(Blade);
		const FString Name = Pickup->GetClass()->GetName();
		TestFalse(*FString::Printf(TEXT("%s: the point on the floor is blocked by the floor (the old placement)"), *Name),
			Reaches(*Body, *Point));
		Pickup->DispatchBeginPlay();
		TestTrue(*FString::Printf(TEXT("%s: after BeginPlay the body reaches it"), *Name), Reaches(*Body, *Point));
		TestTrue(*FString::Printf(TEXT("%s: the point sits above the resting mesh"), *Name),
			Point->GetComponentLocation().Z > Mesh->Bounds.Origin.Z + Mesh->Bounds.BoxExtent.Z);
	}

	for (AActor* Actor : TArray<AActor*>{Floor, Body, Weapon, Item}) Actor->Destroy();
	World->DestroyWorld(false);
	if (Package)
	{
		Package->SetDirtyFlag(false);
	}

	return true;
}
#endif
