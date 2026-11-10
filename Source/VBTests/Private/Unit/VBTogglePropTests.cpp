// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBToggleProp.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Unit/VBIsolatedGameWorld.h"

// 길을 막는 소품. 실제 문 메시, 피직스 애셋, 설정으로 닫힌 문짝을 놓고
//   실제 플레이어 캡슐을 양쪽에서 쓸어 본다. 충돌을 시험이 켜지 않는다 - 소품이 설정을 보고 켠 것을 본다.
//   덮지 않는 것: 물리 단계가 키네마틱 목표를 옮기는 것(시험은 순간이동으로 몸체를 자세에 맞춘다), 실제 걷기 - PIE 가 덮는다.

namespace VBTogglePropTest
{
	const TCHAR* DoorMesh = TEXT("/Game/Interaction/Props/Door/Meshes/SK_Door.SK_Door");
	const TCHAR* DoorConfig = TEXT("/Game/VB/Data/Interaction/DA_Interactable_Door.DA_Interactable_Door");
	const TCHAR* GameMode = TEXT("/Game/VB/Blueprints/GameModes/BP_VBGameMode.BP_VBGameMode_C");

	struct FDoorWorld
	{
		FVBIsolatedGameWorld Isolated;
		AVBToggleProp* Prop = nullptr;
		USkeletalMeshComponent* Leaf = nullptr;

		bool Init(FAutomationTestBase& Test, UVBInteractableConfig* Config, USkeletalMesh* Mesh)
		{
			if (!Isolated.Init(Test, /*bPhysicsScene=*/true)) return false;
			FActorSpawnParameters Spawn;
			Spawn.bDeferConstruction = true;
			const FTransform Placement(FRotator(0.0, -90.0, 0.0), FVector(-920.0, -1315.0, 0.0)); // VBMap 의 VB_Prop_Door 자리
			Prop = Isolated.World->SpawnActor<AVBToggleProp>(Placement.GetLocation(), Placement.Rotator(), Spawn);
			if (!Test.TestNotNull(TEXT("toggle prop"), Prop))
			{
				return false;
			}

			Leaf = Prop->FindComponentByClass<USkeletalMeshComponent>();
			UVBInteractableComponent* Interactable = Prop->FindComponentByClass<UVBInteractableComponent>();
			const FObjectProperty* ConfigProperty = FindFProperty<FObjectProperty>(UVBInteractableComponent::StaticClass(), TEXT("Config"));
			if (!Test.TestNotNull(TEXT("prop mesh"), Leaf) || !Test.TestNotNull(TEXT("interactable"), Interactable)
				|| !Test.TestNotNull(TEXT("Config property"), ConfigProperty))
			{
				return false;
			}

			ConfigProperty->SetObjectPropertyValue_InContainer(Interactable, Config);
			Leaf->SetSkeletalMeshAsset(Mesh);
			Prop->FinishSpawning(Placement);
			Prop->DispatchBeginPlay();
			SyncBodiesToPose();
			return Test.TestFalse(TEXT("starts closed"), Interactable->IsActivated());
		}

		// 지금 소품 애니 자세를 몸체에 바로 옮긴다. 게임에서는 물리 단계가 키네마틱 목표로 옮긴다 - 격리 월드는 물리를 돌리지 않는다.
		void SyncBodiesToPose() const
		{
			Leaf->RefreshBoneTransforms();
			Leaf->UpdateKinematicBonesToAnim(Leaf->GetComponentSpaceTransforms(), ETeleportType::TeleportPhysics, true,
				EAllowKinematicDeferral::DisallowDeferral);
		}

		void SetOpen(bool bOpen) const
		{
			Prop->FindComponentByClass<UVBInteractableComponent>()->SetActivated(bOpen, /*bInstant=*/true);
			SyncBodiesToPose();
		}
	};

	// 피직스 애셋에서 가장 큰 상자(문짝)의 닫힌 자리 - 중심과 두께 방향. 손잡이 상자는 작아서 고르지 않는다.
	bool FindLeafBox(FAutomationTestBase& Test, const USkeletalMeshComponent& Leaf, FVector& OutCenter, FVector& OutNormal)
	{
		const UPhysicsAsset* Physics = Leaf.GetPhysicsAsset();
		if (!Test.TestNotNull(TEXT("door physics asset"), Physics))
		{
			return false;
		}

		double BestVolume = 0.0;
		for (const USkeletalBodySetup* Body : Physics->SkeletalBodySetups)
		{
			const FBodyInstance* Instance = Body ? Leaf.GetBodyInstance(Body->BoneName) : nullptr;
			if (!Instance)
			{
				continue;
			}

			for (const FKBoxElem& Box : Body->AggGeom.BoxElems)
			{
				const double Volume = static_cast<double>(Box.X) * Box.Y * Box.Z;
				if (Volume <= BestVolume)
				{
					continue;
				}

				BestVolume = Volume;
				const FTransform BoxWorld = FTransform(Box.Rotation, Box.Center) * Instance->GetUnrealWorldTransform();
				const EAxis::Type Thin = Box.X <= Box.Y && Box.X <= Box.Z ? EAxis::X : (Box.Y <= Box.Z ? EAxis::Y : EAxis::Z);
				OutCenter = BoxWorld.GetLocation();
				OutNormal = BoxWorld.GetUnitAxis(Thin);
			}
		}

		return Test.TestTrue(TEXT("door physics asset has a leaf box on a live body"), BestVolume > 0.0);
	}

	// 실제 플레이어 캡슐(게임 모드의 기본 폰)을 그 캡슐의 채널, 반응으로 쓸어 본다. CMC 가 움직일 때 쓰는 질의와 같은 종류다.
	bool SweepHitsLeaf(FAutomationTestBase& Test, const FDoorWorld& World, const FVector& Center, const FVector& Direction,
		bool& bOutHitLeaf)
	{
		const UClass* ModeClass = LoadClass<AGameModeBase>(nullptr, GameMode);
		const ACharacter* Hero = ModeClass ? Cast<ACharacter>(GetDefault<AGameModeBase>(ModeClass)->DefaultPawnClass->GetDefaultObject()) : nullptr;
		if (!Test.TestNotNull(TEXT("player character CDO"), Hero))
		{
			return false;
		}

		const UCapsuleComponent* Capsule = Hero->GetCapsuleComponent();
		const double Reach = Capsule->GetScaledCapsuleRadius() + 40.0;
		FHitResult Hit;
		const bool bHit = World.Isolated.World->SweepSingleByChannel(Hit, Center - Direction * Reach, Center + Direction * Reach,
			FQuat::Identity,
			Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), FCollisionQueryParams(SCENE_QUERY_STAT(VBTogglePropTest), false),
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels()));
		bOutHitLeaf = bHit && Hit.GetComponent() == World.Leaf;
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTogglePropDoorBlocksTest, "VB.Unit.Interaction.ToggleProp.DoorClosedLeafBlocksBothSides",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTogglePropDoorBlocksTest::RunTest(const FString& Parameters)
{
	using namespace VBTogglePropTest;
	UVBInteractableConfig* Config = LoadObject<UVBInteractableConfig>(nullptr, DoorConfig);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, DoorMesh);
	if (!TestNotNull(TEXT("door config"), Config) || !TestNotNull(TEXT("door mesh"), Mesh))
	{
		return false;
	}
	// 실제 문 설정이 길을 막는다고 말해야 한다 - 예전 데이터(거짓)에서는 여기서 떨어진다.
	TestTrue(TEXT("the door config declares it blocks the passage"), Config->bBlocksPassage);

	FDoorWorld World;
	if (!World.Init(*this, Config, Mesh))
	{
		return false;
	}

	TestEqual(TEXT("door setup is valid"), World.Prop->ValidateSetup(), FString());
	FVector Center, Normal;
	if (!FindLeafBox(*this, *World.Leaf, Center, Normal))
	{
		return false;
	}

	bool bHitLeaf = false;
	if (SweepHitsLeaf(*this, World, Center, Normal, bHitLeaf))
	{
		TestTrue(TEXT("closed leaf stops the player from the front"), bHitLeaf);
	}

	if (SweepHitsLeaf(*this, World, Center, -Normal, bHitLeaf))
	{
		TestTrue(TEXT("closed leaf stops the player from the back"), bHitLeaf);
	}

	// 몸체는 문짝 뼈를 따라간다 - 열린 끝 자세에서는 같은 길이 뚫린다. 닫힌 자리에 머무는 몸체면 여기서 떨어진다.
	World.SetOpen(true);
	if (SweepHitsLeaf(*this, World, Center, Normal, bHitLeaf))
	{
		TestFalse(TEXT("open door lets the player through"), bHitLeaf);
	}
	// 저장 복원과 같은 순간 닫기도 다시 막는다.
	World.SetOpen(false);
	if (SweepHitsLeaf(*this, World, Center, Normal, bHitLeaf))
	{
		TestTrue(TEXT("closing again blocks again"), bHitLeaf);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTogglePropPassageRequirementTest, "VB.Unit.Interaction.ToggleProp.PassageRequirement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTogglePropPassageRequirementTest::RunTest(const FString& Parameters)
{
	using namespace VBTogglePropTest;
	UVBInteractableConfig* Real = LoadObject<UVBInteractableConfig>(nullptr, DoorConfig);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, DoorMesh);
	if (!TestNotNull(TEXT("door config"), Real) || !TestNotNull(TEXT("door mesh"), Mesh))
	{
		return false;
	}

	// 대조: 길을 막지 않는 종류(스위치 등)는 엔진 기본값 그대로 - 같은 문짝이라도 사람이 지나간다. 약속이 충돌을 정한다.
	//   충돌이 없으면 몸체도 없어 문짝 자리를 잴 수 없다 - 같은 자리에 놓은 막는 문에서 먼저 잰다.
	FVector Center, Normal;
	{
		UVBInteractableConfig* Required = DuplicateObject(Real, GetTransientPackage());
		Required->bBlocksPassage = true;
		FDoorWorld Reference;
		if (!Reference.Init(*this, Required, Mesh) || !FindLeafBox(*this, *Reference.Leaf, Center, Normal))
		{
			return false;
		}
	}

	{
		UVBInteractableConfig* Free = DuplicateObject(Real, GetTransientPackage());
		Free->bBlocksPassage = false;
		FDoorWorld World;
		if (!World.Init(*this, Free, Mesh))
		{
			return false;
		}

		bool bHitLeaf = true;
		if (SweepHitsLeaf(*this, World, Center, Normal, bHitLeaf))
		{
			TestFalse(TEXT("a prop that does not block a passage keeps no collision"), bHitLeaf);
		}

		TestEqual(TEXT("no requirement, no fault"), World.Prop->ValidateSetup(), FString());
	}
	// 길을 막아야 하는데 막을 것이 없다 - 조용히 통과가 아니라 결함이다.
	{
		UVBInteractableConfig* Required = DuplicateObject(Real, GetTransientPackage());
		Required->bBlocksPassage = true;
		// 시작 때 서버가 오류로 말해야 한다(규칙 27) - 정확히 한 번.
		AddExpectedMessagePlain(TEXT("충돌이 있는 대리 조각도 없다"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
		FDoorWorld World;
		if (!World.Init(*this, Required, nullptr))
		{
			return false;
		}

		TestFalse(TEXT("a required blocker with no mesh and no proxy is a fault"), World.Prop->ValidateSetup().IsEmpty());
	}

	return true;
}

// 대리 조각의 재질: 조각 컴포넌트는 Transient 라 레벨에서 칠한 재질이 사라진다 - 재질은 조각 데이터가 갖는다.
//   재질을 준 조각은 다시 지어도 그 재질, 비운 조각은 메시 자기 재질 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTogglePropProxyMaterialTest, "VB.Unit.Interaction.ToggleProp.ProxyPartMaterial",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTogglePropProxyMaterialTest::RunTest(const FString& Parameters)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Given = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial.DefaultMaterial"));
	if (!TestNotNull(TEXT("cube"), Cube) || !TestNotNull(TEXT("material"), Given))
	{
		return false;
	}

	UMaterialInterface* Own = Cube->GetMaterial(0);
	if (!TestTrue(TEXT("the test material differs from the cube's own"), Own != Given))
	{
		return false;
	}

	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	FActorSpawnParameters Spawn;
	Spawn.bDeferConstruction = true;
	AVBToggleProp* Prop = Isolated.World->SpawnActor<AVBToggleProp>(FVector::ZeroVector, FRotator::ZeroRotator, Spawn);
	const FArrayProperty* PartsProperty = FindFProperty<FArrayProperty>(AVBToggleProp::StaticClass(), TEXT("ProxyParts"));
	if (!TestNotNull(TEXT("toggle prop"), Prop) || !TestNotNull(TEXT("ProxyParts property"), PartsProperty))
	{
		return false;
	}

	TArray<FVBPropProxyPart>& Parts = *PartsProperty->ContainerPtrToValuePtr<TArray<FVBPropProxyPart>>(Prop);
	Parts.SetNum(2);
	Parts[0].Mesh = Cube;
	Parts[0].Material = Given;
	Parts[1].Mesh = Cube;
	Parts[1].bVisualOnly = true;
	Prop->FinishSpawning(FTransform::Identity);

	auto Check = [this, Prop, Given, Own](const TCHAR* When)
	{
		TArray<UStaticMeshComponent*> Components;
		Prop->GetComponents<UStaticMeshComponent>(Components);
		UStaticMeshComponent* const* Given0 = Components.FindByPredicate([](const UStaticMeshComponent* C) { return C->GetFName() == TEXT("ProxyPart_0"); });
		UStaticMeshComponent* const* Plain1 = Components.FindByPredicate([](const UStaticMeshComponent* C) { return C->GetFName() == TEXT("ProxyPart_1"); });
		if (!TestTrue(FString::Printf(TEXT("%s: both parts built"), When), Given0 && Plain1))
		{
			return;
		}

		TestTrue(FString::Printf(TEXT("%s: a part with a material shows it"), When), (*Given0)->GetMaterial(0) == Given);
		TestTrue(FString::Printf(TEXT("%s: a part without one keeps the mesh's own"), When), (*Plain1)->GetMaterial(0) == Own);
		// 보이기만 하는 조각은 충돌이 없고, 보통 조각은 메시 충돌을 그대로 둔다.
		TestTrue(FString::Printf(TEXT("%s: a visual-only part has no collision"), When),
			(*Plain1)->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
		TestTrue(FString::Printf(TEXT("%s: a normal part keeps its collision"), When),
			(*Given0)->GetCollisionEnabled() != ECollisionEnabled::NoCollision);
	};
	Check(TEXT("built"));
	// RerunConstructionScripts 는 에디터 전용 - ExecuteConstruction 이 스폰과 에디터가 함께 타는 길이다(OnConstruction 호출).
	Prop->ExecuteConstruction(Prop->GetActorTransform(), nullptr, nullptr);
	Check(TEXT("rebuilt"));
	return true;
}

namespace VBGateTest
{
	const TCHAR* GateConfig = TEXT("/Game/VB/Data/Interaction/DA_Interactable_Gate.DA_Interactable_Gate");
	// VBMap 의 VB_Prop_Gate 에서 읽은 Interactable(닿는 목표) 자리, 소품 루트 기준. 닫힌 판은 y97.
	const FVector ReachTarget(0.0, 90.0, 110.0);
	const FVector FrontPoint(0.0, -60.0, 0.0);
	const FVector BackPoint(0.0, 250.0, 0.0);

	bool StandsFront(const UVBInteractableConfig& Config, const FVBInteractionMotion& Motion)
	{
		return FVector::DotProduct(Motion.StandTransform.GetLocation() - Config.SidePlaneOrigin,
			Config.SidePlaneNormal.GetSafeNormal2D()) >= 0.0;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGateTwoSidedTest, "VB.Unit.Interaction.Gate.AuthoredTwoSidedMotion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGateTwoSidedTest::RunTest(const FString& Parameters)
{
	using namespace VBGateTest;
	const UVBInteractableConfig* Gate = LoadObject<UVBInteractableConfig>(nullptr, GateConfig);
	if (!TestNotNull(TEXT("gate config"), Gate))
	{
		return false;
	}

	TestTrue(TEXT("the gate is a passage"), Gate->bBlocksPassage);
	TestEqual(TEXT("the gate passes the passage rule"), Gate->ValidatePassage(), FString());

	const UClass* ModeClass = LoadClass<AGameModeBase>(nullptr, VBTogglePropTest::GameMode);
	const ACharacter* Hero = ModeClass ? Cast<ACharacter>(GetDefault<AGameModeBase>(ModeClass)->DefaultPawnClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("player character CDO"), Hero))
	{
		return false;
	}

	const FVector CapsuleCenter(0.0, 0.0, Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());

	// 맵의 자리와, 돌리고 키운 자리 - 앞뒤는 소품 루트 기준이라 배치가 바뀌어도 같아야 한다.
	const FTransform Placements[] = {
		FTransform(FRotator::ZeroRotator, FVector(-1600.0, -1700.0, 600.0)),
		FTransform(FRotator(0.0, 37.0, 0.0), FVector(120.0, -45.0, 0.0), FVector(1.5))};
	for (const FTransform& Placement : Placements)
	{
		for (const bool bActivating : {true, false})
		{
			const FVBInteractionMotion* Front = Gate->SelectMotion(bActivating, Placement, Placement.TransformPosition(FrontPoint));
			const FVBInteractionMotion* Back = Gate->SelectMotion(bActivating, Placement, Placement.TransformPosition(BackPoint));
			if (!TestNotNull(TEXT("front side has a motion"), Front) || !TestNotNull(TEXT("back side has a motion"), Back))
			{
				return false;
			}

			TestEqual(TEXT("front side gets the front motion"), Gate->GetMotionIndex(*Front), static_cast<uint8>(bActivating ? 0 : 1));
			TestEqual(TEXT("back side gets the back motion"), Gate->GetMotionIndex(*Back), static_cast<uint8>(bActivating ? 2 : 3));
			TestTrue(TEXT("front stand is on the front side"), StandsFront(*Gate, *Front));
			TestFalse(TEXT("back stand is on the back side"), StandsFront(*Gate, *Back));
			for (const FVBInteractionMotion* Motion : {Front, Back})
			{
				TestTrue(TEXT("the reach target is in range from the stand"),
					FVector::Dist(Motion->StandTransform.GetLocation() + CapsuleCenter, ReachTarget) <= Gate->InteractRange);
			}
		}
	}

	// 대조: 예전 데이터(뒤쪽 없음, 판정면 없음)는 검사에 걸리고, 뒤에서도 앞 동작을 골라 위의 단언이 떨어진다.
	UVBInteractableConfig* OneSided = DuplicateObject(Gate, GetTransientPackage());
	OneSided->bUseSidePlane = false;
	OneSided->ActivateMotionBack = FVBInteractionMotion();
	OneSided->DeactivateMotionBack = FVBInteractionMotion();
	TestFalse(TEXT("control: one-sided passage data is reported"), OneSided->ValidatePassage().IsEmpty());
	const FVBInteractionMotion* Reused = OneSided->SelectMotion(true, Placements[0], Placements[0].TransformPosition(BackPoint));
	if (TestNotNull(TEXT("control: one-sided data still selects something"), Reused))
	{
		TestEqual(TEXT("control: one-sided data reuses the front motion from the back"), OneSided->GetMotionIndex(*Reused),
			static_cast<uint8>(0));
	}

	return true;
}
#endif
