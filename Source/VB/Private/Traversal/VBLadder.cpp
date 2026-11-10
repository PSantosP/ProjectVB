// Copyright VB. All Rights Reserved.

#include "Traversal/VBLadder.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AVBLadder::AVBLadder()
{
	PrimaryActorTick.bCanEverTick = false;

	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	SetRootComponent(Pivot);

	Parts = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Parts"));
	Parts->SetupAttachment(Pivot);
	// 형체는 충돌 없음. 막게 두면 붙는 순간 캡슐이 형체와 싸운다.
	Parts->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Parts->SetGenerateOverlapEvents(false);
	// 기본 메시는 엔진 큐브.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		PartMesh = CubeFinder.Object;
	}

	Marker = CreateDefaultSubobject<UBoxComponent>(TEXT("Marker"));
	Marker->SetupAttachment(Pivot);
	Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Marker->SetGenerateOverlapEvents(false);
	Marker->SetBoxExtent(FVector(5.0f, 30.0f, 100.0f));
	Marker->SetHiddenInGame(true);

	// 형체 범위를 막는 상자. Pawn 이동만 막고 카메라, 트레이스, 물리, 내비는 건드리지 않는다.
	Blocker = CreateDefaultSubobject<UBoxComponent>(TEXT("Blocker"));
	Blocker->SetupAttachment(Pivot);
	Blocker->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Blocker->SetCollisionObjectType(ECC_WorldStatic);
	Blocker->SetCollisionResponseToAllChannels(ECR_Ignore);
	Blocker->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Blocker->SetGenerateOverlapEvents(false);
	Blocker->SetCanEverAffectNavigation(false);
	Blocker->SetHiddenInGame(true);
	// 못 밟는 면으로 둔다. 윗면이 착지대와 같은 높이라 바닥 판정이 이 상자를 고르면 착지가 실패한다.
	Blocker->SetWalkableSlopeOverride(FWalkableSlopeOverride(WalkableSlope_Unwalkable, 0.0f));
	Blocker->CanCharacterStepUpOn = ECB_No;
}

void AVBLadder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildVisual();
	UpdateBlocker();
}

void AVBLadder::BeginPlay()
{
	Super::BeginPlay();
	UpdateBlocker();
}

FBox AVBLadder::ComputeBlockerBounds(float Height, float Width, float Depth, float Thickness)
{
	// RebuildVisual 과 같은 치수(세로대 x=0, 가로대 x=-Depth, 폭은 세로대 바깥 면까지).
	const float HalfT = FMath::Max(Thickness, 1.0f) * 0.5f;
	const float HalfW = FMath::Max(Width, 0.0f) * 0.5f + HalfT;
	return FBox(FVector(-(FMath::Max(Depth, 0.0f) + HalfT), -HalfW, 0.0f),
		FVector(HalfT, HalfW, FMath::Max(Height, 0.0f)));
}

void AVBLadder::UpdateBlocker()
{
	if (!Blocker)
	{
		return;
	}

	const FBox Bounds = ComputeBlockerBounds(LadderHeight, LadderWidth, RungDepth, PartThickness);
	Blocker->SetRelativeLocation(Bounds.GetCenter());
	Blocker->SetBoxExtent(Bounds.GetExtent());
	// 높이 0 이면 납작한 상자로 바닥을 막지 않게 끈다.
	Blocker->SetCollisionEnabled(LadderHeight > 0.0f ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void AVBLadder::RebuildVisual()
{
	if (!Parts)
	{
		return;
	}

	Parts->SetStaticMesh(PartMesh);
	Parts->ClearInstances();
	if (!PartMesh || LadderHeight <= 0.0f)
	{
		return;
	}
	// 엔진 기본 도형은 한 변 100 이라 '치수 / 100' 이 배율이다(튜닝값 아님).
	const float CubeSide = 100.0f;
	const float T = FMath::Max(PartThickness, 1.0f) / CubeSide;
	const float HalfW = LadderWidth * 0.5f;

	// 세로대 둘. 타는 면인 x=0 평면에 둔다.
	for (int32 Side = -1; Side <= 1; Side += 2)
	{
		FTransform Rail;
		Rail.SetLocation(FVector(0.0f, HalfW * Side, LadderHeight * 0.5f));
		Rail.SetScale3D(FVector(T, T, LadderHeight / CubeSide));
		Parts->AddInstance(Rail);
	}

	// 가로대. 세로대 면에서 -X 로 RungDepth 만큼 물러나 있고 보이는 간격은 디딤 간격을 정수로 쪼갠 값이다.
	const float Spacing = FMath::Max(RungSpacing, 5.0f) / FMath::Max(RungSubdivision, 1);
	const float Depth = FMath::Max(RungDepth, 0.0f);
	const int32 Count = FMath::Max(1, FMath::FloorToInt(LadderHeight / Spacing));
	// 격자를 통째로 내린다. 내린 만큼 맨 아래 것이 바닥 밑으로 가므로 그것만 건너뛴다.
	const float Drop = FMath::Clamp(RungDropOffset, 0.0f, Spacing);
	for (int32 i = 0; i <= Count + 1; ++i)
	{
		const float Z = i * Spacing - Drop;
		if (Z < 0.0f || Z > LadderHeight)
		{
			continue;
		}

		FTransform Rung;
		Rung.SetLocation(FVector(-Depth, 0.0f, Z));
		// 가로대는 Y 로 눕힌다. 배율은 회전 전 공간에 걸리므로 길이는 Z 성분이다.
		Rung.SetRotation(FQuat(FVector::ForwardVector, HALF_PI));
		Rung.SetScale3D(FVector(T, T, LadderWidth / CubeSide));
		Parts->AddInstance(Rung);

		// 브래킷 둘. 세로대(x=0)에서 가로대(x=-Depth)까지 잇는다. 깊이가 0 이면 만들지 않는다.
		if (Depth > KINDA_SMALL_NUMBER)
		{
			for (int32 Side = -1; Side <= 1; Side += 2)
			{
				FTransform Arm;
				Arm.SetLocation(FVector(-Depth * 0.5f, HalfW * Side, Z));
				// 브래킷은 X 로 눕힌다.
				Arm.SetRotation(FQuat(FVector::RightVector, HALF_PI));
				Arm.SetScale3D(FVector(T, T, Depth / CubeSide));
				Parts->AddInstance(Arm);
			}
		}
	}
}

FVector AVBLadder::GetAxisPoint(float Height) const
{
	return GetActorLocation() + GetActorUpVector() * FMath::Clamp(Height, 0.0f, LadderHeight);
}

FVector AVBLadder::GetFacingNormal() const
{
	return GetActorForwardVector();
}

float AVBLadder::ProjectToHeight(const FVector& WorldPoint) const
{
	const float Raw = static_cast<float>(FVector::DotProduct(WorldPoint - GetActorLocation(), GetActorUpVector()));
	return FMath::Clamp(Raw, 0.0f, LadderHeight);
}
