// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Camera/VBFinisherCamera.h"

#include "Camera/CameraComponent.h"
#include "Camera/VBEnvironmentCameraPolicy.h"
#include "Camera/VBFinisherDirectorSubsystem.h"
#include "Character/VBFinisherComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "VB/VB.h"

FVBFinisherCameraView AVBFinisherCamera::ResolveView(UWorld& World, const FVBFinisherCameraCollisionSettings& Settings,
	const FVector& Desired, const FVector& LookAt, TConstArrayView<AActor*> Actors, const FMatrix& Projection,
	int32 PreviousCandidate)
{
	FVBFinisherCameraView Result;
	if (!Settings.IsValid() || Desired.ContainsNaN() || LookAt.ContainsNaN()
		|| Projection.ContainsNaN() || FMath::Abs(Projection.Determinant()) <= UE_SMALL_NUMBER || Actors.IsEmpty())
	{
		return Result;
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(VBFinisherCamera), false);
	TArray<FVector, TInlineAllocator<4>> ViewPoints;
	for (AActor* Actor : Actors)
	{
		if (!IsValid(Actor) || Actor->GetWorld() != &World) { return Result; }
		Query.AddIgnoredActor(Actor);
		TArray<AActor*> Attached;
		Actor->GetAttachedActors(Attached, true, true);
		for (AActor* Attachment : Attached)
		{
			if (IsValid(Attachment) && Attachment->IsOwnedBy(Actor)) { Query.AddIgnoredActor(Attachment); }
		}
		const ACharacter* Character = Cast<ACharacter>(Actor);
		const USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
		ViewPoints.Add(Mesh && Mesh->GetSkeletalMeshAsset() ? Mesh->Bounds.Origin : Actor->GetActorLocation());
	}

	const FMatrix Inverse = Projection.Inverse();
	double NearPlaneRadius = 0;
	for (double X : {-1.0, 1.0}) for (double Y : {-1.0, 1.0})
	{
		const FVector4 Corner = Inverse.TransformFVector4(FVector4(X, Y, 1, 1));
		if (!FMath::IsFinite(Corner.W) || FMath::Abs(Corner.W) <= UE_SMALL_NUMBER) { return Result; }
		const double Length = FVector(Corner.X / Corner.W, Corner.Y / Corner.W, Corner.Z / Corner.W).Length();
		if (!FMath::IsFinite(Length)) { return Result; }
		NearPlaneRadius = FMath::Max(NearPlaneRadius, Length);
	}
	const double Radius = FMath::Max(double(Settings.MinimumProbeRadius), NearPlaneRadius) + Settings.SurfaceMargin;
	const FCollisionShape Shape = FCollisionShape::MakeSphere(Radius);
	const auto IsSafe = [&](const FVector& Location)
	{
		return !World.OverlapBlockingTestByChannel(Location, FQuat::Identity, ECC_Camera, Shape, Query);
	};
	if (!IsSafe(LookAt)) { return Result; }
	const FVector Direction = (Desired - LookAt).GetSafeNormal();
	if (Direction.IsNearlyZero()) { return Result; }
	FVector Right = FVector::CrossProduct(FVector::UpVector, Direction).GetSafeNormal();
	if (Right.IsNearlyZero()) { Right = FVector::RightVector; }
	const FVector Side = Right * Settings.MaxLateralAdjustment;
	const FVector Up = FVector::UpVector * Settings.MaxHeightAdjustment;
	const FVector Targets[] = {Desired, Desired + Side, Desired - Side, Desired + Up, Desired + Side + Up, Desired - Side + Up};
	VBEnvironmentCameraPolicy::FCandidate Safe[UE_ARRAY_COUNT(Targets)];
	VBEnvironmentCameraPolicy::FCandidate Visible[UE_ARRAY_COUNT(Targets)];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Targets); ++Index)
	{
		FVector Location = Targets[Index];
		FHitResult Hit;
		if (World.SweepSingleByChannel(Hit, LookAt, Location, FQuat::Identity, ECC_Camera, Shape, Query))
		{
			if (Hit.bStartPenetrating) { continue; }
			const double Distance = FVector::Distance(LookAt, Location);
			const double Fraction = FMath::Max(0.0, double(Hit.Time) - Settings.SurfaceMargin / Distance);
			Location = FMath::Lerp(LookAt, Location, Fraction);
		}
		if (!IsSafe(Location) || FVector::Distance(Location, LookAt) < Settings.MinimumViewDistance) { continue; }
		Safe[Index] = {Location, FVector::Distance(Location, Desired), true};
		bool bVisible = true;
		const FRotationMatrix ViewBasis((LookAt - Location).Rotation());
		for (const FVector& Point : ViewPoints)
		{
			const FVector Local = ViewBasis.GetTransposed().TransformVector(Point - Location);
			const FVector4 Clip = Projection.TransformFVector4(FVector4(Local.Y, Local.Z, Local.X, 1));
			if (Clip.W <= UE_SMALL_NUMBER || FMath::Abs(Clip.X) > Clip.W || FMath::Abs(Clip.Y) > Clip.W
				|| World.LineTraceTestByChannel(Location, Point, ECC_Camera, Query))
			{
				bVisible = false;
				break;
			}
		}
		if (bVisible) { Visible[Index] = Safe[Index]; }
	}

	int32 Selected = VBEnvironmentCameraPolicy::SelectCandidate(Visible, PreviousCandidate, Settings.CandidateSwitchMargin);
	Result.bParticipantsVisible = Selected != INDEX_NONE;
	if (Selected == INDEX_NONE)
	{
		Selected = VBEnvironmentCameraPolicy::SelectCandidate(Safe, PreviousCandidate, Settings.CandidateSwitchMargin);
	}
	if (Selected != INDEX_NONE)
	{
		Result.Location = Safe[Selected].Position;
		Result.Candidate = Selected;
		Result.bSafe = true;
	}
	return Result;
}

AVBFinisherCamera::AVBFinisherCamera()
{
	PrimaryActorTick.bCanEverTick = true;

	// 카메라 매니저보다 먼저 돌아야 한다. 늦으면 이번 프레임의 뷰가 지난 프레임 자리로 잡힌다.
	PrimaryActorTick.TickGroup = TG_PostPhysics;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
	SetRootComponent(CameraComponent);

	// 보는 사람의 것이라 복제 대상이 아니다. 각 기계가 자기 것을 만든다.
	bReplicates = false;

	// 처형은 슬로우가 걸린 채로 돈다. 이 액터가 함께 느려지면 키 보간이 늦게 따라가
	// 「카메라가 밀린다」로 보인다. 시간축은 씬 재생 시간이므로 틱은 실시간이어야 한다.
	SetActorTickInterval(0.0f);
	CustomTimeDilation = 1.0f;
}

void AVBFinisherCamera::Setup(const UVBFinisherConfig* InConfig, AActor* InAnchor, AActor* InTimeSource)
{
	Config = InConfig;
	Anchor = InAnchor;
	TimeSource = InTimeSource ? InTimeSource : InAnchor;
	Participants.Reset();
	if (const UVBFinisherComponent* Finisher = UVBFinisherComponent::FindFor(TimeSource.Get()))
	{
		TArray<AActor*> Actors;
		Finisher->GetParticipants(Actors);
		for (AActor* Actor : Actors) { Participants.Add(Actor); }
	}
	LastCandidate = INDEX_NONE;
	bWasUnresolved = false;
	bHasSafeView = false;

	// 첫 프레임을 기다리지 않고 한 번 세운다. 스폰 직후 뷰타깃 블렌드가 시작되는데
	// 그때 이 액터가 원점에 있으면 블렌드가 세계 원점에서 출발한다.
	Tick(0.0f);
}

float AVBFinisherCamera::GetSceneTime() const
{
	const AActor* Source = TimeSource.Get();
	if (!Source)
	{
		return -1.0f;
	}

	const UVBFinisherComponent* Finisher = UVBFinisherComponent::FindFor(Source);
	return Finisher ? Finisher->GetPlaybackTime() : -1.0f;
}

FMatrix AVBFinisherCamera::GetViewProjection(float FOV) const
{
	FMinimalViewInfo View;
	CameraComponent->GetCameraView(0.0f, View);
	View.FOV = FOV;
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	int32 Width = 1, Height = 1;
	if (PC) { PC->GetViewportSize(Width, Height); }
	const ULocalPlayer* Player = PC ? PC->GetLocalPlayer() : nullptr;
	const FVector2D ViewSize = Player ? Player->Size : FVector2D(1, 1);
	const FIntRect Rect(0, 0, FMath::Max(1, FMath::RoundToInt(Width * ViewSize.X)),
		FMath::Max(1, FMath::RoundToInt(Height * ViewSize.Y)));
	FSceneViewProjectionData Projection;
	Projection.SetViewRectangle(Rect);
	const auto Axis = View.AspectRatioAxisConstraint.Get(Player ? Player->AspectRatioAxisConstraint.GetValue() : AspectRatio_MaintainXFOV);
	FMinimalViewInfo::CalculateProjectionMatrixGivenViewRectangle(View, Axis, Rect, Projection);
	return Projection.ProjectionMatrix;
}

void AVBFinisherCamera::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const AActor* AnchorActor = Anchor.Get();
	const float SceneTime = GetSceneTime();

	// 앵커가 사라졌거나(대상 사망 후 파괴) 시간을 못 읽으면 마지막 자리를 유지한다.
	// 원점으로 떨어뜨리면 복귀 블렌드가 세계 원점에서 출발해 화면이 통째로 날아간다.
	if (!AnchorActor || SceneTime < 0.0f)
	{
		if (bHasLastGood)
		{
			SetActorTransform(LastGoodWorld);
		}

		return;
	}

	// 판정은 자산이 한다. 저작 도구와 런타임이 같은 함수를 불러야 미리보기가 거짓말을 안 한다.
	FVector RelLocation, RelLookAt;
	float FOV = 90.0f;
	if (!Config || !Config->EvaluateCameraAt(SceneTime, RelLocation, RelLookAt, FOV))
	{
		return;
	}

	// 앵커의 회전까지 따라간다. 처형 중 앵커가 도는 클립이면 카메라도 같이 돌아야
	// 저작한 화각이 유지된다. 스케일은 쓰지 않는다 - 카메라에 스케일은 뜻이 없다.
	const FTransform AnchorWorld(AnchorActor->GetActorQuat(), AnchorActor->GetActorLocation());
	FVector WorldLocation = AnchorWorld.TransformPosition(RelLocation);
	const FVector WorldLookAt = AnchorWorld.TransformPosition(RelLookAt);
	TArray<AActor*> Actors;
	for (const TWeakObjectPtr<AActor>& Participant : Participants) { Actors.Add(Participant.Get()); }
	const FVBFinisherCameraView Corrected = ResolveView(*GetWorld(), Config->CameraCollision,
		WorldLocation, WorldLookAt, Actors, GetViewProjection(FOV), LastCandidate);
	const bool bUnresolved = !Corrected.bSafe || !Corrected.bParticipantsVisible;
	if (bUnresolved && !bWasUnresolved)
	{
		VB_LOG(Warning, "Finisher camera could not resolve a safe visible view: %s", *GetNameSafe(Config));
	}
	bWasUnresolved = bUnresolved;
	LastCandidate = Corrected.Candidate;
	bHasSafeView = Corrected.bSafe && Corrected.bParticipantsVisible;
	if (!bHasSafeView)
	{
		if (UVBFinisherDirectorSubsystem* Director = GetWorld()->GetSubsystem<UVBFinisherDirectorSubsystem>())
		{
			Director->ReleaseObstructedView(this);
		}
		return;
	}
	WorldLocation = Corrected.Location;

	// 회전은 여기서 딱 한 번 계산된다. 자산에는 회전이 아예 없다(헤더 LookAtOffset 주석).
	// 두 점이 겹치면 방향이 0 벡터라 회전이 튄다 - 그때는 앞 프레임 회전을 유지한다.
	const FVector ToTarget = WorldLookAt - WorldLocation;
	const FRotator LookRotation = ToTarget.IsNearlyZero()
		? GetActorRotation()
		: ToTarget.Rotation();

	const FTransform World(LookRotation, WorldLocation);
	SetActorLocationAndRotation(WorldLocation, LookRotation);
	if (CameraComponent)
	{
		CameraComponent->SetFieldOfView(FOV);
	}

	LastGoodWorld = World;
	bHasLastGood = true;
}

FRotator AVBFinisherCamera::GetViewRotation() const
{
	return CameraComponent ? CameraComponent->GetComponentRotation() : GetActorRotation();
}
