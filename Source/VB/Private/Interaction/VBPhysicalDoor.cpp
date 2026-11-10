// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBPhysicalDoor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/VBInteractableComponent.h"
#include "Narrative/Story/VBAreaTrigger.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "VB/VB.h"
#include "Interaction/VBSetupValidation.h"

namespace
{
	// 한 걸음 알파의 하한 - 아주 큰 문, 아주 작은 여유에서도 한 틱의 걸음 수가 끝없이 늘지 않게(만 걸음).
	constexpr float MinStepAlpha = 1e-4f;
	// 여는 각 0 으로 나누지 않게 쓰는 하한(도). 여는 각 0 은 ValidateSetup 이 결함으로 막는다 - 이 값은 나눗셈 보호다.
	constexpr double MinOpenAngle = 1e-3;

	// 각 Target(라디안, 2π 주기)이 [Lo, Hi] 안에 드는가.
	bool AngleInRange(const double Target, const double Lo, const double Hi)
	{
		const double K = FMath::CeilToDouble((Lo - Target) / UE_DOUBLE_TWO_PI);
		return Target + K * UE_DOUBLE_TWO_PI <= Hi;
	}
}

AVBPhysicalDoor::AVBPhysicalDoor()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	// 움직이는 동안만 틱한다.
	PrimaryActorTick.bStartWithTickEnabled = false;

	Frame = CreateDefaultSubobject<USceneComponent>(TEXT("Frame"));
	RootComponent = Frame;

	Hinge = CreateDefaultSubobject<USceneComponent>(TEXT("Hinge"));
	Hinge->SetupAttachment(Frame);
	Hinge->SetMobility(EComponentMobility::Movable);

	Leaf = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Leaf"));
	Leaf->SetupAttachment(Hinge);
	Leaf->SetMobility(EComponentMobility::Movable);
	Leaf->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);

	// 프롬프트는 문짝을 따라 돌지 않는다.
	Interactable = CreateDefaultSubobject<UVBInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(Frame);
}

void AVBPhysicalDoor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// 문짝이 놓인 배치 자리(알파 0)를 기준으로 삼는다 - 움직이면 Hinge 의 상대 변환을 덮어쓰므로 처음 한 번 잡는다.
	//   BeginPlay 보다 앞이라 원격 클라의 첫 복제(OnRep)보다도 앞선다.
	HingeRest = Hinge->GetRelativeTransform();
}

void AVBPhysicalDoor::BeginPlay()
{
	Super::BeginPlay();
	FaultReason = ValidateSetup();
	if (HasAuthority())
	{
		// 저장 복원은 등록 때 bool 을 이미 넣었다 - 지금 값에서 한 번 다시 짓는다(알림 없음).
		RestAt(Interactable->IsActivated() ? 1.0f : 0.0f);
		if (FaultReason.IsEmpty())
		{
			FaultReason = CheckStaticPath();
		}

		Interactable->OnInteracted.AddUniqueDynamic(this, &AVBPhysicalDoor::HandleInteracted);
	}
	else
	{
		OnRep_Move(); // 시작 전에 도착한 복제는 버렸다 - 지금 다시 맞춘다.
	}
	// 결함은 서버가 말한다(요청을 받는 쪽). 클라마다 같은 줄을 남기지 않는다.
	VB_CLOG(HasFault() && HasAuthority(), Error, "%s: 물리 문을 쓸 수 없다 - %s", *GetName(), *FaultReason);
	if (HasAuthority() && HasStoryControl())
	{
		StartStoryControl();
	}
}

void AVBPhysicalDoor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority())
	{
		// 끝나는 동안의 Cancelled 가 다시 맞추기를 부르지 않게 먼저 끊는다.
		StopStoryControl();
		GetWorldTimerManager().ClearTimer(ZeroDistanceTimer);
		Finish(EVBDoorMoveResult::Cancelled);
	}

	Super::EndPlay(EndPlayReason);
}

void AVBPhysicalDoor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVBPhysicalDoor, Move);
}

uint32 AVBPhysicalDoor::RequestMove(const bool bOpen, FString* OutRefusal)
{
	const TCHAR* Refusal = !HasAuthority() ? TEXT("권한 없는 쪽에서 불렀다")
		: !HasActorBegunPlay() ? TEXT("문이 아직 시작하지 않았다")
		: HasFault() ? *FaultReason
		: nullptr;
	if (Refusal)
	{
		if (OutRefusal)
		{
			*OutRefusal = Refusal;
		}

		VB_LOG(Warning, "%s: 문 요청 거절 - %s", *GetName(), Refusal);
		return 0;
	}
	// 새 요청의 상태를 먼저 다 쓰고, 앞 요청의 Cancelled 는 맨 끝에 알린다 - 그 알림 안에서 또 요청해도 번호가 섞이지 않는다.
	//   (그 안에서 온 요청은 이 요청을 다시 Cancelled 로 끝낸다 - 더 새 요청이 이긴다.)
	const uint32 Superseded = PendingSerial;
	GetWorldTimerManager().ClearTimer(ZeroDistanceTimer);
	const float From = LastPlacedAlpha;
	const float To = bOpen ? 1.0f : 0.0f;
	NextSerial = NextSerial == MAX_uint32 ? 1 : NextSerial + 1;
	const uint32 Serial = NextSerial;
	PendingSerial = Serial;
	Move.FromAlpha = From;
	Move.ToAlpha = To;
	Move.ServerStartTime = VBTimedMove::ServerNow(this);
	Move.Duration = VBTimedMove::TripDuration(From, To, TravelDistance(), Speed);
	Move.bStopped = false;
	ForceNetUpdate();
	if (FMath::IsNearlyEqual(From, To, 0.0f))
	{
		// 이미 그 끝이다. 부른 쪽이 번호를 받아 둔 뒤에 알리도록 다음 틱에 끝낸다.
		SetActorTickEnabled(false);
		ZeroDistanceTimer = GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Serial, To]()
		{
			if (PendingSerial == Serial)
			{
				RestAt(To);
				Interactable->SetActivated(To >= 1.0f);
				Finish(EVBDoorMoveResult::Reached);
			}
		}));
	}
	else
	{
		SetActorTickEnabled(true);
	}

	if (Superseded != 0)
	{
		OnDoorMoveFinished.Broadcast(Superseded, EVBDoorMoveResult::Cancelled);
	}

	return Serial;
}

void AVBPhysicalDoor::SetPoseInstant(const bool bOpen)
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(ZeroDistanceTimer);
	// 상태를 먼저 바꾸고 앞 요청을 끝낸다 - 알림 안에서 새 요청이 와도 이 자세 위에서 시작한다.
	RestAt(bOpen ? 1.0f : 0.0f);
	Interactable->SetActivated(bOpen, /*bInstant=*/true);
	Finish(EVBDoorMoveResult::Cancelled);
}

bool AVBPhysicalDoor::IsOpen() const
{
	return Interactable->IsActivated();
}

bool AVBPhysicalDoor::IsMoving() const
{
	return !Move.bStopped && !FMath::IsNearlyEqual(CurrentAlpha(), Move.ToAlpha, 0.0f);
}

void AVBPhysicalDoor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Target = VBTimedMove::EvaluateAlpha(Move, VBTimedMove::ServerNow(this));
	if (!HasAuthority())
	{
		if (!HasActorBegunPlay())
		{
			return;
		}

		PlaceLeaf(Target);
		SetActorTickEnabled(!Move.bStopped && Target != Move.ToAlpha);
		return;
	}

	if (Move.bStopped || PendingSerial == 0)
	{
		SetActorTickEnabled(false);
		return;
	}
	// 이번 틱에 갈 구간을 잘게 나눠, 각 조각을 지나는 문짝 전체가 비었을 때만 그 끝으로 옮긴다.
	const float Step = MaxStepAlpha();
	float From = LastPlacedAlpha;
	while (From != Target)
	{
		const float To = Target > From ? FMath::Min(From + Step, Target) : FMath::Max(From - Step, Target);
		if (IsIntervalBlocked(From, To, /*bStaticOnly=*/false, /*bIgnoreRestAgainst=*/true))
		{
			PlaceLeaf(From);
			Move.FromAlpha = From;
			Move.ServerStartTime = VBTimedMove::ServerNow(this);
			Move.bStopped = true;
			SetActorTickEnabled(false);
			ForceNetUpdate();
			VB_LOG(Log, "%s: 문짝이 알파 %.3f 에서 막혀 멈춘다", *GetName(), From);
			Finish(EVBDoorMoveResult::Blocked);
			return;
		}

		From = To;
	}

	PlaceLeaf(Target);
	if (Target == Move.ToAlpha)
	{
		const bool bOpened = Move.ToAlpha >= 1.0f;
		RestAt(Move.ToAlpha);
		Interactable->SetActivated(bOpened);
		Finish(EVBDoorMoveResult::Reached);
	}
}

void AVBPhysicalDoor::OnRep_Move()
{
	if (!HasActorBegunPlay())
	{
		return; // BeginPlay 가 다시 맞춘다.
	}

	const float Alpha = CurrentAlpha();
	PlaceLeaf(Alpha);
	SetActorTickEnabled(!Move.bStopped && Alpha != Move.ToAlpha);
}

void AVBPhysicalDoor::HandleInteracted(AActor* /*InteractingActor*/)
{
	// 손으로 누르면 지금 향하는 쪽의 반대로 - 막혀 서 있어도 다시 누르면 그 자리에서 다시 움직인다.
	RequestMove(Move.ToAlpha < 0.5f);
}

bool AVBPhysicalDoor::HasStoryControl() const
{
	return ClosedWhileOccupied != nullptr || !ClosedWhile.IsEmpty();
}

void AVBPhysicalDoor::StartStoryControl()
{
	if (HasFault())
	{
		// 결함 오류는 BeginPlay 가 이미 한 줄 남겼다. 요청은 거절되므로 바로 연다.
		bStoryFailed = true;
		SetPoseInstant(true);
		return;
	}
	// 묶는 즉시 한 번 불려, 이미 준비됐으면 여기서 시작한다.
	StoryBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &AVBPhysicalDoor::HandleStoryChanged));
	if (!bStoryStarted && !bStoryFailed)
	{
		StoryStartupDeadline = GetWorld()->GetTimeSeconds() + StoryStartupTimeoutSeconds;
		GetWorldTimerManager().SetTimer(StoryStartupTimer, this, &AVBPhysicalDoor::HandleStoryStartupTick, 0.1f, /*bLoop=*/true);
	}
}

bool AVBPhysicalDoor::TryStartStory()
{
	AVBAreaTrigger* Area = ClosedWhileOccupied.Get();
	const bool bAreaReady = !Area || (IsValid(Area) && Area->GetWorld() == GetWorld() && Area->HasActorBegunPlay());
	const bool bStoryReady = ClosedWhile.IsEmpty() || StoryBinding.GetStory() != nullptr;
	if (!bAreaReady || !bStoryReady)
	{
		return false;
	}

	GetWorldTimerManager().ClearTimer(StoryStartupTimer);
	if (Area)
	{
		BoundArea = Area;
		AreaOccupancyHandle = Area->OnOccupancyChanged.AddUObject(this, &AVBPhysicalDoor::ReconcileStory);
	}

	StoryMoveHandle = OnDoorMoveFinished.AddUObject(this, &AVBPhysicalDoor::HandleStoryMoveFinished);
	bStoryStarted = true;
	// 저장된 끝보다 지금의 사실, 점유가 이긴다. 시작 자세라 쓸기 없이 바로 둔다.
	SetPoseInstant(!WantsClosed());
	return true;
}

void AVBPhysicalDoor::HandleStoryStartupTick()
{
	if (bStoryStarted || bStoryFailed || TryStartStory())
	{
		return;
	}

	if (GetWorld()->GetTimeSeconds() >= StoryStartupDeadline)
	{
		FailStoryOpen(TEXT("시작 때 사실 저장소나 점유 영역이 준비되지 않았다"));
	}
}

void AVBPhysicalDoor::HandleStoryChanged()
{
	if (bStoryFailed)
	{
		return;
	}

	if (!bStoryStarted)
	{
		TryStartStory();
		return;
	}

	ReconcileStory();
}

void AVBPhysicalDoor::ReconcileStory()
{
	if (!bStoryStarted || !HasAuthority())
	{
		return;
	}

	const bool bWantOpen = !WantsClosed();
	const float WantAlpha = bWantOpen ? 1.0f : 0.0f;
	if (PendingSerial != 0 && Move.ToAlpha == WantAlpha)
	{
		return; // 이미 그쪽으로 가는 중.
	}

	if (Move.bStopped && Move.ToAlpha == WantAlpha && bBlockedRetryPending)
	{
		return; // 막혀 다시 갈 차례를 기다리는 중.
	}

	GetWorldTimerManager().ClearTimer(BlockedRetryTimer);
	bBlockedRetryPending = false;
	if (PendingSerial == 0 && !Move.bStopped && IsOpen() == bWantOpen)
	{
		return; // 바라는 끝에 서 있다.
	}

	FString Why;
	if (RequestMove(bWantOpen, &Why) == 0)
	{
		FailStoryOpen(*Why);
	}
}

void AVBPhysicalDoor::HandleStoryMoveFinished(uint32 /*Serial*/, const EVBDoorMoveResult Result)
{
	if (!bStoryStarted)
	{
		return;
	}

	if (Result == EVBDoorMoveResult::Reached)
	{
		bBlockedWarned = false;
		return;
	}

	if (Result != EVBDoorMoveResult::Blocked)
	{
		return;
	}

	const bool bBlockedOpening = Move.ToAlpha >= 1.0f;
	if (!bBlockedWarned)
	{
		bBlockedWarned = true;
		VB_LOG(Warning, "%s: 이야기가 바라는 자세로 가다 막혔다(%s) - 비킬 때까지 %.1f초마다 다시 간다",
			*GetName(), bBlockedOpening ? TEXT("열기") : TEXT("닫기"), BlockedRetrySeconds);
	}

	if (bBlockedOpening == !WantsClosed())
	{
		bBlockedRetryPending = true;
		GetWorldTimerManager().SetTimer(BlockedRetryTimer, this, &AVBPhysicalDoor::HandleBlockedRetry,
			BlockedRetrySeconds, /*bLoop=*/false);
	}
	else
	{
		ReconcileStory(); // 막힌 사이 바람이 바뀌었다 - 반대쪽으로 바로 간다.
	}
}

void AVBPhysicalDoor::HandleBlockedRetry()
{
	bBlockedRetryPending = false;
	ReconcileStory();
}

void AVBPhysicalDoor::FailStoryOpen(const TCHAR* Why)
{
	StopStoryControl();
	bStoryFailed = true;
	VB_LOG(Error, "%s: 이야기 조건을 따를 수 없어 문을 연다 - %s", *GetName(), Why);
	if (HasFault() || RequestMove(true) == 0)
	{
		SetPoseInstant(true);
	}
}

void AVBPhysicalDoor::StopStoryControl()
{
	bStoryStarted = false;
	GetWorldTimerManager().ClearTimer(StoryStartupTimer);
	GetWorldTimerManager().ClearTimer(BlockedRetryTimer);
	bBlockedRetryPending = false;
	if (AVBAreaTrigger* Area = BoundArea.Get())
	{
		Area->OnOccupancyChanged.Remove(AreaOccupancyHandle);
	}

	AreaOccupancyHandle.Reset();
	BoundArea.Reset();
	OnDoorMoveFinished.Remove(StoryMoveHandle);
	StoryMoveHandle.Reset();
	StoryBinding.Disconnect();
}

bool AVBPhysicalDoor::WantsClosed() const
{
	if (ClosedWhileOccupied != nullptr && !IsPlayerInsideArea())
	{
		return false;
	}

	return VBRules::EvaluateAll(ClosedWhile, FVBRuleContext::Make(this, FirstPlayerState()));
}

bool AVBPhysicalDoor::IsPlayerInsideArea() const
{
	const AVBAreaTrigger* Area = ClosedWhileOccupied.Get();
	UWorld* World = GetWorld();
	if (!IsValid(Area) || !World)
	{
		return false;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		if (Controller && Area->IsPawnInside(Controller->GetPawn()))
		{
			return true;
		}
	}

	return false;
}

APlayerState* AVBPhysicalDoor::FirstPlayerState() const
{
	const UWorld* World = GetWorld();
	const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	return Controller ? Controller->PlayerState.Get() : nullptr;
}

void AVBPhysicalDoor::Finish(const EVBDoorMoveResult Result)
{
	if (PendingSerial == 0)
	{
		return;
	}

	const uint32 Serial = PendingSerial;
	PendingSerial = 0;
	OnDoorMoveFinished.Broadcast(Serial, Result);
}

void AVBPhysicalDoor::RestAt(const float Alpha)
{
	Move = FVBTimedMove();
	Move.FromAlpha = Alpha;
	Move.ToAlpha = Alpha;
	Move.ServerStartTime = VBTimedMove::ServerNow(this);
	PlaceLeaf(Alpha);
	SetActorTickEnabled(false);
	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

float AVBPhysicalDoor::CurrentAlpha() const
{
	return VBTimedMove::EvaluateAlpha(Move, VBTimedMove::ServerNow(this));
}

float AVBPhysicalDoor::TravelDistance() const
{
	return Motion == EVBDoorMotion::Swing ? FMath::Abs(OpenAngle) : static_cast<float>(SlideOffset.Size());
}

FTransform AVBPhysicalDoor::HingeRelativeAt(const float Alpha) const
{
	// 경첩 자기 공간에서 먼저 돌리거나 옮긴 뒤 배치 자리(HingeRest)를 적용한다.
	const FTransform Delta = Motion == EVBDoorMotion::Swing
		? FTransform(FQuat(FVector::UpVector, FMath::DegreesToRadians(OpenAngle * Alpha)))
		: FTransform(SlideOffset * Alpha);
	return Delta * HingeRest;
}

void AVBPhysicalDoor::PlaceLeaf(const float Alpha)
{
	Hinge->SetRelativeTransform(HingeRelativeAt(Alpha));
	LastPlacedAlpha = Alpha;
}

void AVBPhysicalDoor::LeafBoxInHinge(FVector& OutCenter, FVector& OutExtent) const
{
	const UStaticMesh* Mesh = Leaf->GetStaticMesh();
	const FBox Local = Mesh ? Mesh->GetBoundingBox() : FBox(FVector::ZeroVector, FVector::ZeroVector);
	// 문짝의 경첩 기준 변환(피벗, 회전, 크기)으로 옮긴 축 정렬 상자 - 회전된 문짝이면 그 상자를 감싸는 쪽으로 커진다(보수적).
	const FBox InHinge = Local.TransformBy(Leaf->GetRelativeTransform());
	OutCenter = InHinge.GetCenter();
	OutExtent = InHinge.GetExtent();
}

void AVBPhysicalDoor::SweptSwingBox(const FVector& Center, const FVector& Extent, const float Angle0, const float Angle1,
	FVector& OutCenter, FVector& OutExtent, float& OutMidAngle)
{
	OutMidAngle = 0.5f * (Angle0 + Angle1);
	const double Half = 0.5 * FMath::DegreesToRadians(FMath::Abs(static_cast<double>(Angle1) - Angle0));
	FVector Min(TNumericLimits<double>::Max()), Max(-TNumericLimits<double>::Max());
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector P = Center + FVector((Corner & 1) ? Extent.X : -Extent.X, (Corner & 2) ? Extent.Y : -Extent.Y,
			(Corner & 4) ? Extent.Z : -Extent.Z);
		const double R = FMath::Sqrt(P.X * P.X + P.Y * P.Y);
		// Angle0..Angle1 만큼 돈 모서리를 중간 각 틀에서 보면 각이 [Phi - Half, Phi + Half] 를 지난다(Phi = 돌기 전 각).
		const double Phi = FMath::Atan2(P.Y, P.X);
		const double Lo = Phi - Half;
		const double Hi = Phi + Half;
		double XMin = FMath::Min(R * FMath::Cos(Lo), R * FMath::Cos(Hi));
		double XMax = FMath::Max(R * FMath::Cos(Lo), R * FMath::Cos(Hi));
		double YMin = FMath::Min(R * FMath::Sin(Lo), R * FMath::Sin(Hi));
		double YMax = FMath::Max(R * FMath::Sin(Lo), R * FMath::Sin(Hi));
		// 구간 안에서 축 방향을 지나면 그 축의 끝값이 반지름이다 - 두 축 모두 본다(10절).
		if (AngleInRange(0.0, Lo, Hi))
		{
			XMax = R;
		}

		if (AngleInRange(UE_DOUBLE_PI, Lo, Hi))
		{
			XMin = -R;
		}

		if (AngleInRange(UE_DOUBLE_HALF_PI, Lo, Hi))
		{
			YMax = R;
		}

		if (AngleInRange(-UE_DOUBLE_HALF_PI, Lo, Hi))
		{
			YMin = -R;
		}

		Min = Min.ComponentMin(FVector(XMin, YMin, P.Z));
		Max = Max.ComponentMax(FVector(XMax, YMax, P.Z));
	}

	OutCenter = (Min + Max) * 0.5;
	OutExtent = (Max - Min) * 0.5;
}

float AVBPhysicalDoor::MaxStepAlpha() const
{
	if (Motion == EVBDoorMotion::Slide)
	{
		// 미끄럼은 쓸기가 정확하지만, 막히면 그 걸음의 시작에 서므로 걸음을 여유만큼으로 나눠 닿는 자리 가까이 세운다.
		return FMath::Clamp(SweepTolerance / FMath::Max(TravelDistance(), 1.0f), MinStepAlpha, 1.0f);
	}

	FVector Center, Extent;
	LeafBoxInHinge(Center, Extent);
	// 경첩 축에서 가장 먼 모서리 반지름 R. 한 걸음(반각 h)의 상자는 걸음 시작의 문짝보다 접선 쪽으로 2, R, sin(h) 커진다 -
	//   그것을 SweepTolerance 안에 두려고 sin(h) = tol / 2R 로 고른다(반지름 쪽 초과는 R, sin²(h)/2 로 더 작다).
	const double R = FMath::Max(FVector2D(FMath::Abs(Center.X) + Extent.X, FMath::Abs(Center.Y) + Extent.Y).Size(), 1.0);
	const double StepRadians = 2.0 * FMath::Asin(FMath::Min(1.0, static_cast<double>(SweepTolerance) / (2.0 * R)));
	const double StepAlpha = FMath::RadiansToDegrees(StepRadians) / FMath::Max(FMath::Abs(static_cast<double>(OpenAngle)), MinOpenAngle);
	return static_cast<float>(FMath::Clamp(StepAlpha, static_cast<double>(MinStepAlpha), 1.0));
}

bool AVBPhysicalDoor::IsIntervalBlocked(const float A0, const float A1, const bool bStaticOnly, const bool bIgnoreRestAgainst) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return true; // 잴 수 없으면 막힌 것으로 본다(규칙 27).
	}

	FVector Center, Extent;
	LeafBoxInHinge(Center, Extent);
	const FTransform FrameWorld = Frame->GetComponentTransform();
	const ECollisionChannel Channel = Leaf->GetCollisionObjectType();
	// 서로 막을 때만 막힘이다: 상대가 문짝의 채널을 막고, 문짝이 상대의 종류를 막아야 한다(겹침만 하는 영역 트리거는 걸리지 않는다).
	const FCollisionResponseParams Responses(Leaf->GetCollisionResponseToChannels());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBPhysicalDoorLeaf), false, this);
	if (bIgnoreRestAgainst)
	{
		for (const AActor* Actor : RestAgainst)
		{
			Params.AddIgnoredActor(Actor);
		}
	}

	auto Blocks = [bStaticOnly](const UPrimitiveComponent* Component, const bool bBlockingHit)
	{
		return bBlockingHit && Component && (!bStaticOnly || Component->GetCollisionObjectType() == ECC_WorldStatic);
	};
	if (Motion == EVBDoorMotion::Swing)
	{
		FVector BoxCenter, BoxExtent;
		float MidAngle = 0.0f;
		SweptSwingBox(Center, Extent, OpenAngle * A0, OpenAngle * A1, BoxCenter, BoxExtent, MidAngle);
		const FTransform MidFrame = FTransform(FQuat(FVector::UpVector, FMath::DegreesToRadians(MidAngle))) * HingeRest * FrameWorld;
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByChannel(Overlaps, MidFrame.TransformPosition(BoxCenter), MidFrame.GetRotation(), Channel,
			FCollisionShape::MakeBox(BoxExtent * MidFrame.GetScale3D().GetAbs()), Params, Responses);
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (Blocks(Overlap.GetComponent(), Overlap.bBlockingHit))
			{
				return true;
			}
		}

		return false;
	}

	const FTransform Start = HingeRelativeAt(A0) * FrameWorld;
	const FTransform End = HingeRelativeAt(A1) * FrameWorld;
	TArray<FHitResult> Hits;
	World->SweepMultiByChannel(Hits, Start.TransformPosition(Center), End.TransformPosition(Center), Start.GetRotation(), Channel,
		FCollisionShape::MakeBox(Extent * Start.GetScale3D().GetAbs()), Params, Responses);
	for (const FHitResult& Hit : Hits)
	{
		if (Blocks(Hit.GetComponent(), Hit.bBlockingHit))
		{
			return true;
		}
	}

	return false;
}

FString AVBPhysicalDoor::ValidateSetup() const
{
	if (!Leaf->GetStaticMesh())
	{
		return TEXT("문짝 메시가 없다");
	}

	if (Leaf->GetCollisionEnabled() == ECollisionEnabled::NoCollision || Leaf->GetCollisionEnabled() == ECollisionEnabled::PhysicsOnly)
	{
		return TEXT("문짝의 질의 충돌이 꺼져 있다");
	}

	if (Leaf->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
	{
		return TEXT("문짝이 Pawn 을 막지 않는다 - 사람이 뚫고 지나간다");
	}

	if (!FMath::IsFinite(Speed) || Speed <= 0.0f)
	{
		return TEXT("속도가 잘못됐다");
	}

	if (!FMath::IsFinite(TravelDistance()) || TravelDistance() <= KINDA_SMALL_NUMBER)
	{
		return TEXT("여는 거리(각)가 0 이다");
	}

	FVector Center, Extent;
	LeafBoxInHinge(Center, Extent);
	if (!FMath::IsFinite(SweepTolerance) || SweepTolerance <= 0.0f || SweepTolerance > Extent.GetMin() * 2.0f)
	{
		return TEXT("SweepTolerance 가 0 이하이거나 문짝 두께보다 크다");
	}

	if (HasStoryControl())
	{
		if (Interactable->IsInteractionEnabled())
		{
			return TEXT("이야기 닫힘을 따르는 문은 상호작용을 꺼야 한다 - 손으로 연 문을 이야기가 바로 되돌린다");
		}

		if (!FMath::IsFinite(BlockedRetrySeconds) || BlockedRetrySeconds <= 0.0f
			|| !FMath::IsFinite(StoryStartupTimeoutSeconds) || StoryStartupTimeoutSeconds <= 0.0f)
		{
			return TEXT("막힘 재시도 간격이나 이야기 시작 대기가 0 이하다");
		}
	}

	return FString();
}

FString AVBPhysicalDoor::CheckStaticPath() const
{
	const float Step = MaxStepAlpha();
	for (float A = 0.0f; A < 1.0f;)
	{
		const float Next = FMath::Min(A + Step, 1.0f);
		if (IsIntervalBlocked(A, Next, /*bStaticOnly=*/true, /*bIgnoreRestAgainst=*/true))
		{
			return FString::Printf(TEXT("정적 지형이 여는 길을 막는다(알파 %.2f~%.2f)"), A, Next);
		}
		// 기대는 액터는 양 끝 가까이에서만 닿아도 된다 - 그 사이에서 막으면 배치 결함이다.
		const bool bInterior = A >= RestAgainstTravelFraction && Next <= 1.0f - RestAgainstTravelFraction;
		if (bInterior && RestAgainst.Num() > 0 && IsIntervalBlocked(A, Next, /*bStaticOnly=*/true, /*bIgnoreRestAgainst=*/false))
		{
			return FString::Printf(TEXT("RestAgainst 액터가 끝이 아닌 곳(알파 %.2f~%.2f)에서 길을 막는다"), A, Next);
		}

		A = Next;
	}

	return FString();
}

#if WITH_EDITOR
EDataValidationResult AVBPhysicalDoor::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = VBReportSetupProblem(Context, *this, ValidateSetup(), Super::IsDataValid(Context));
	if (!VBRules::ValidateStructArray(ClosedWhile, FVBCondition::StaticStruct(),
		NSLOCTEXT("VBPhysicalDoor", "ClosedWhileWhere", "닫힘 조건"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	// 다른 레벨의 영역은 스트리밍으로 따로 사라져 점유를 읽을 수 없다.
	if (ClosedWhileOccupied != nullptr && ClosedWhileOccupied->GetLevel() != GetLevel())
	{
		Context.AddError(FText::Format(NSLOCTEXT("VBPhysicalDoor", "AreaOtherLevel", "{0}: ClosedWhileOccupied 영역은 문과 같은 레벨에 있어야 한다."),
			FText::FromString(GetName())));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif
