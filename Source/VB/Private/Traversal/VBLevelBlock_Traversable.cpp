// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Traversal/VBLevelBlock_Traversable.h"

#include "VB/VB.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Interaction/VBPushableComponent.h"
#include "Materials/MaterialInterface.h"
#include "Traversal/VBTraversalSettings.h"

namespace VBLedgePaint
{
	// 면에서 띄우는 두께(cm). 같은 면에 겹쳐 깜박이지 않게 하는 값이라 조정 대상이 아니다.
	constexpr float Thickness = 0.4f;
	// 띠 메시 규격(한 변 100cm 상자).
	constexpr float MeshSize = 100.0f;
	// 띠 표시. 액터 복제로 딸려 온 띠도 이 표시로 찾아 지운다(LedgePaint 배열은 저장도 복제도 되지 않는다).
	const FName Tag(TEXT("VBLedgePaint"));
}

AVBLevelBlock_Traversable::AVBLevelBlock_Traversable()
{
	PrimaryActorTick.bCanEverTick = false;

	// 장애물 메시가 루트이자 전방 트레이스 대상이다.
	ObstacleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObstacleMesh"));
	SetRootComponent(ObstacleMesh);
	Pushable = CreateDefaultSubobject<UVBPushableComponent>(TEXT("Pushable"));
	Pushable->SetupAttachment(ObstacleMesh);
}

void AVBLevelBlock_Traversable::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 이전에 자동 생성한 옆면 렛지를 지운다(재구성 누적 방지).
	for (const TObjectPtr<USplineComponent>& Old : DerivedSideLedges)
	{
		OppositeLedges.Remove(Old);
		for (TMap<TObjectPtr<USplineComponent>, TObjectPtr<USplineComponent>>::TIterator It(OppositeLedges); It; ++It)
		{
			if (It.Value() == Old)
			{
				It.RemoveCurrent();
			}
		}

		if (IsValid(Old))
		{
			Old->DestroyComponent();
		}
	}

	DerivedSideLedges.Reset();

	// 디자이너가 둔 스플라인.
	TArray<USplineComponent*> Designer;
	GetComponents<USplineComponent>(Designer);

	// 앞/뒤 두 개만 있으면 좌/우 옆면 렛지를 자동 생성한다.
	if (Designer.Num() == 2
		&& Designer[0]->GetNumberOfSplinePoints() >= 2
		&& Designer[1]->GetNumberOfSplinePoints() >= 2)
	{
		USplineComponent* E1 = Designer[0];
		USplineComponent* E2 = Designer[1];
		const int32 E1Last = E1->GetNumberOfSplinePoints() - 1;
		const int32 E2Last = E2->GetNumberOfSplinePoints() - 1;
		const FVector P0 = E1->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		const FVector P1 = E1->GetLocationAtSplinePoint(E1Last, ESplineCoordinateSpace::World);
		const FVector Q0 = E2->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		const FVector Q1 = E2->GetLocationAtSplinePoint(E2Last, ESplineCoordinateSpace::World);

		// P0 에 가까운 E2 끝점과 짝지어 같은 옆면을 만든다.
		const bool bQ0NearP0 = FVector::DistSquared(P0, Q0) <= FVector::DistSquared(P0, Q1);
		const FVector Center = GetActorLocation();
		USplineComponent* SideA = CreateSideLedge(P0, bQ0NearP0 ? Q0 : Q1, Center);
		USplineComponent* SideB = CreateSideLedge(P1, bQ0NearP0 ? Q1 : Q0, Center);
		if (SideA)
		{
			DerivedSideLedges.Add(SideA);
		}

		if (SideB)
		{
			DerivedSideLedges.Add(SideB);
		}
		// 좌/우를 서로 opposite 로 잇는다. 없으면 깊이가 0 이 되어 Vault 로 오분류된다.
		if (SideA && SideB)
		{
			OppositeLedges.Add(SideA, SideB);
			OppositeLedges.Add(SideB, SideA);
		}
	}

	// 디자이너 + 파생 스플라인 전부를 렛지로 수집.
	TArray<USplineComponent*> All;
	GetComponents<USplineComponent>(All);
	Ledges.Empty(All.Num());
	for (USplineComponent* Spline : All)
	{
		Ledges.Add(Spline);
	}

	RebuildLedgePaint();
}

void AVBLevelBlock_Traversable::BeginPlay()
{
	Super::BeginPlay();
	RebuildLedgePaint();
}

void AVBLevelBlock_Traversable::ComputeLedgePaintTransforms(const FBox& Box, float Width, float FaceHeight, TArray<FTransform>& Out)
{
	using namespace VBLedgePaint;
	Out.Reset();
	const FVector Size = Box.GetSize();
	const FVector Center = Box.GetCenter();
	const float TopZ = Box.Max.Z + Thickness * 0.5f;
	auto Add = [&Out](const FVector& Location, const FVector& Extent)
	{
		Out.Add(FTransform(FQuat::Identity, Location, Extent / MeshSize));
	};
	// 윗면: 긴 쪽을 따라 모서리에서 폭 절반만큼 안쪽.
	Add(FVector(Center.X, Box.Max.Y - Width * 0.5f, TopZ), FVector(Size.X, Width, Thickness));
	Add(FVector(Center.X, Box.Min.Y + Width * 0.5f, TopZ), FVector(Size.X, Width, Thickness));
	Add(FVector(Box.Max.X - Width * 0.5f, Center.Y, TopZ), FVector(Width, Size.Y, Thickness));
	Add(FVector(Box.Min.X + Width * 0.5f, Center.Y, TopZ), FVector(Width, Size.Y, Thickness));
	if (FaceHeight <= 0.0f)
	{
		return;
	}
	// 옆면: 윗모서리 바로 아래로 내려오는 띠. 다가오는 플레이어 눈에 먼저 보이는 쪽이다.
	const float FaceZ = Box.Max.Z - FaceHeight * 0.5f;
	Add(FVector(Center.X, Box.Max.Y + Thickness * 0.5f, FaceZ), FVector(Size.X, Thickness, FaceHeight));
	Add(FVector(Center.X, Box.Min.Y - Thickness * 0.5f, FaceZ), FVector(Size.X, Thickness, FaceHeight));
	Add(FVector(Box.Max.X + Thickness * 0.5f, Center.Y, FaceZ), FVector(Thickness, Size.Y, FaceHeight));
	Add(FVector(Box.Min.X - Thickness * 0.5f, Center.Y, FaceZ), FVector(Thickness, Size.Y, FaceHeight));
}

void AVBLevelBlock_Traversable::RebuildLedgePaint()
{
	TArray<UStaticMeshComponent*> Existing;
	GetComponents<UStaticMeshComponent>(Existing);
	for (UStaticMeshComponent* Old : Existing)
	{
		if (Old != ObstacleMesh && Old->ComponentHasTag(VBLedgePaint::Tag))
		{
			Old->DestroyComponent();
		}
	}
	LedgePaint.Reset();

	const UVBTraversalSettings* Settings = GetDefault<UVBTraversalSettings>();
	if (Settings->LedgePaintMaterial.IsNull())
	{
		return; // 재질을 비우면 프로젝트 전체에서 표시를 끈다(설정 주석)
	}
	UMaterialInterface* Material = Settings->LedgePaintMaterial.LoadSynchronous();
	UStaticMesh* Mesh = Settings->LedgePaintMesh.LoadSynchronous();
	const UStaticMesh* Obstacle = ObstacleMesh ? ObstacleMesh->GetStaticMesh() : nullptr;
	if (!Material || !Mesh || !Obstacle)
	{
		VB_LOG(Error, "%s: 넘기 표시 띠를 못 그린다 - 재질 %s, 띠 메시 %s, 장애물 메시 %s (Project Settings > VB Traversal)",
			*GetName(), Material ? TEXT("있음") : TEXT("없음"), Mesh ? TEXT("있음") : TEXT("없음"), Obstacle ? TEXT("있음") : TEXT("없음"));
		return;
	}

	// 회전과 위치만 있는 블록 공간에서 크기를 맞춘 상자를 만들고, 띠는 절대 스케일로 붙여 비균등 스케일에 찌그러지지 않게 한다.
	const FVector Scale = ObstacleMesh->GetComponentScale().GetAbs();
	const FBox LocalBox = Obstacle->GetBoundingBox();
	const FBox ScaledBox(LocalBox.Min * Scale, LocalBox.Max * Scale);
	const FTransform Frame(ObstacleMesh->GetComponentQuat(), ObstacleMesh->GetComponentLocation());
	TArray<FTransform> Strips;
	ComputeLedgePaintTransforms(ScaledBox, Settings->LedgePaintWidth, Settings->LedgePaintFaceHeight, Strips);
	for (const FTransform& Strip : Strips)
	{
		UStaticMeshComponent* Paint = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient);
		Paint->ComponentTags.Add(VBLedgePaint::Tag);
		Paint->SetStaticMesh(Mesh);
		Paint->SetMaterial(0, Material);
		Paint->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Paint->SetCanEverAffectNavigation(false);
		Paint->SetMobility(EComponentMobility::Movable);
		Paint->SetUsingAbsoluteScale(true);
		Paint->SetupAttachment(ObstacleMesh);
		Paint->RegisterComponent();
		Paint->SetWorldTransform(Strip * Frame);
		LedgePaint.Add(Paint);
	}
}

USplineComponent* AVBLevelBlock_Traversable::CreateSideLedge(const FVector& A, const FVector& B, const FVector& Center)
{
	USplineComponent* Spline = NewObject<USplineComponent>(this);
	if (!Spline)
	{
		return nullptr;
	}

	Spline->SetupAttachment(RootComponent);
	Spline->RegisterComponent();
	Spline->SetMobility(EComponentMobility::Movable);
	Spline->SetClosedLoop(false, false);
	Spline->ClearSplinePoints(false);
	Spline->AddSplinePoint(A, ESplineCoordinateSpace::World, false);
	Spline->AddSplinePoint(B, ESplineCoordinateSpace::World, false);

	// up 은 중심에서 모서리 중점으로의 바깥 수평 방향(Front/Back 과 같은 규약).
	const FVector Mid = (A + B) * 0.5f;
	FVector Outward = Mid - Center;
	Outward.Z = 0.0f;
	Outward = Outward.GetSafeNormal();
	if (!Outward.IsNearlyZero())
	{
		// SetRotationAtSplinePoint 는 UpdateSpline 이 되돌리므로 SetDefaultUpVector 를 쓴다.
		Spline->SetDefaultUpVector(Outward, ESplineCoordinateSpace::World);
	}

	Spline->UpdateSpline();
	return Spline;
}

USplineComponent* AVBLevelBlock_Traversable::FindLedgeClosestToActor(const FVector& ActorLocation) const
{
	USplineComponent* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (const TObjectPtr<USplineComponent>& Ledge : Ledges)
	{
		if (!IsValid(Ledge))
		{
			continue;
		}
		// 각 렛지에서 캐릭터에 가장 가까운 점까지의 거리로 비교.
		const FVector ClosestPoint = Ledge->FindLocationClosestToWorldLocation(ActorLocation, ESplineCoordinateSpace::World);
		const float DistSq = FVector::DistSquared(ClosestPoint, ActorLocation);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Ledge;
		}
	}

	return Best;
}

void AVBLevelBlock_Traversable::GetLedgeTransforms(const FVector& HitLocation, const FVector& ActorLocation,
	FVBTraversalCheckResult& Result) const
{
	// 캐릭터에 가장 가까운 렛지.
	USplineComponent* ClosestLedge = FindLedgeClosestToActor(ActorLocation);
	if (!IsValid(ClosestLedge))
	{
		Result.bHasFrontLedge = false;
		return;
	}

	// 너무 좁은 렛지는 무효.
	const float SplineLength = ClosestLedge->GetSplineLength();
	if (SplineLength < MinLedgeWidth)
	{
		Result.bHasFrontLedge = false;
		return;
	}

	// 적중점의 렛지 위 거리를 모서리에서 절반폭 안쪽으로 클램프한다(코너에서 캐릭터가 뜨지 않게).
	const FVector ClosestPoint = ClosestLedge->FindLocationClosestToWorldLocation(HitLocation, ESplineCoordinateSpace::World);
	const float DistAtLocation = ClosestLedge->GetDistanceAlongSplineAtLocation(ClosestPoint, ESplineCoordinateSpace::World);
	const float HalfWidth = MinLedgeWidth * 0.5f;
	const float ClampedDist = FMath::Clamp(DistAtLocation, HalfWidth, SplineLength - HalfWidth);
	const FTransform FrontXform = ClosestLedge->GetTransformAtDistanceAlongSpline(ClampedDist, ESplineCoordinateSpace::World);

	Result.bHasFrontLedge = true;
	Result.FrontLedgeLocation = FrontXform.GetLocation();
	Result.FrontLedgeNormal = FrontXform.GetRotation().GetUpVector();
	Result.FrontLedgeSpline = ClosestLedge; // 매달림이 옆걸음 축으로 쓴다

	// 반대편 back 렛지.
	const TObjectPtr<USplineComponent>* FoundOpposite = OppositeLedges.Find(ClosestLedge);
	USplineComponent* OppositeLedge = FoundOpposite ? FoundOpposite->Get() : nullptr;

	// front 위치에 가장 가까운 back 렛지 트랜스폼.
	if (IsValid(OppositeLedge))
	{
		const FTransform BackXform = OppositeLedge->FindTransformClosestToWorldLocation(Result.FrontLedgeLocation,
			ESplineCoordinateSpace::World);
		Result.bHasBackLedge = true;
		Result.BackLedgeLocation = BackXform.GetLocation();
		Result.BackLedgeNormal = BackXform.GetRotation().GetUpVector();
	}
	else
	{
		Result.bHasBackLedge = false;
	}
}
