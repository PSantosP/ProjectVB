// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBElevator.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Interaction/VBInteractableComponent.h"
#include "Net/UnrealNetwork.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "VB/VB.h"

AVBElevator::AVBElevator()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	// 움직이는 동안만 틱한다.
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	Deck = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Deck"));
	Deck->SetupAttachment(RootComponent);
	Deck->SetMobility(EComponentMobility::Movable);

	// 조작판은 발판과 같이 오르내린다.
	Interactable = CreateDefaultSubobject<UVBInteractableComponent>(TEXT("Interactable"));
	Interactable->SetupAttachment(Deck);
}

void AVBElevator::BeginPlay()
{
	Super::BeginPlay();

	// 시작 자리 = 원하는 층. 저장 복원도 멈춰 있을 때만 저장되므로(이동 중 저장 막기) 끝 자리로 맞추면 된다.
	if (HasAuthority())
	{
		SnapTo(Interactable->IsActivated() ? 1.0f : 0.0f);
		bShaftClear = IsShaftClear();
	}

	PlaceDeck(CurrentAlpha());

	Interactable->OnActivationChanged.AddUniqueDynamic(this, &AVBElevator::HandleActivationChanged);
	if (HasAuthority())
	{
		Interactable->OnInteracted.AddUniqueDynamic(this, &AVBElevator::HandleInteracted);
	}
}

void AVBElevator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthority() && IsMoving())
	{
		// 월드를 끄는 것이 아니면(레벨에서 빠지는 등) 중간 높이로 남기지 않는다 - 원하는 층에 내려놓고 막기를 푼다.
		const bool bWorldEnding = EndPlayReason == EEndPlayReason::Quit || EndPlayReason == EEndPlayReason::EndPlayInEditor
			|| EndPlayReason == EEndPlayReason::LevelTransition;
		if (!bWorldEnding)
		{
			PlaceDeck(Move.ToAlpha);
		}
	}

	ReleaseSaveHold();
	Super::EndPlay(EndPlayReason);
}

void AVBElevator::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVBElevator, Move);
}

bool AVBElevator::RequestStop(const bool bUp)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (!bShaftClear)
	{
		VB_LOG(Warning, "%s: 승강로가 막혀 있어 움직이지 않는다(BeginPlay 검사)", *GetName());
		return false;
	}

	if (Interactable->IsActivated() == bUp)
	{
		return true; // 이미 그 층으로 가는 중이거나 서 있다.
	}
	// 저장 막기를 먼저 얻는다. 못 얻으면 원하는 층을 바꾸지 않는다 - 바꾼 뒤에 거절하면 값과 위치가 어긋난다.
	if (!AcquireSaveHold())
	{
		VB_LOG(Warning, "%s: 저장 막기를 얻지 못해 출발하지 않는다", *GetName());
		return false;
	}

	Interactable->SetActivated(bUp);
	return true;
}

void AVBElevator::HandleInteracted(AActor* /*InteractingActor*/)
{
	RequestStop(!Interactable->IsActivated());
}

void AVBElevator::HandleActivationChanged(const bool bActivated, const bool bInstant)
{
	// 클라는 Move 복제를 따른다 - 움직임은 서버만 정한다.
	if (!HasAuthority())
	{
		return;
	}

	const float Target = bActivated ? 1.0f : 0.0f;
	if (bInstant)
	{
		// 세이브 복원: 멈춘 상태로만 저장되므로 그 층에 바로 둔다.
		SnapTo(Target);
		return;
	}
	// 움직여도 되는지는 여기 한 곳에서 본다 - RequestStop 밖에서 바뀐 경우(계약 위반)도 같은 문을 지난다.
	//   막힌 승강로이거나 저장 막기가 없으면 중간 높이를 만들지 않게 그 층에 바로 둔다.
	if (!bShaftClear || (!SaveHold.IsValid() && !AcquireSaveHold()))
	{
		VB_LOG(Error, "%s: 움직일 수 없는 상태에서 층이 바뀌었다(승강로 %s, 저장 막기 %s) - 그 층에 바로 둔다",
			*GetName(), bShaftClear ? TEXT("비었음") : TEXT("막힘"), SaveHold.IsValid() ? TEXT("있음") : TEXT("없음"));
		SnapTo(Target);
		ReleaseSaveHold();
		return;
	}

	StartMove(Target);
}

void AVBElevator::StartMove(const float ToAlpha)
{
	const float From = CurrentAlpha();
	Move.FromAlpha = From;
	Move.ToAlpha = ToAlpha;
	Move.ServerStartTime = VBTimedMove::ServerNow(this);
	Move.Duration = VBTimedMove::TripDuration(From, ToAlpha, static_cast<float>(UpperOffset.Size()), Speed);
	Move.bStopped = false;
	SetActorTickEnabled(true);
	ForceNetUpdate();
}

void AVBElevator::SnapTo(const float Alpha)
{
	Move = FVBTimedMove();
	Move.FromAlpha = Alpha;
	Move.ToAlpha = Alpha;
	Move.ServerStartTime = VBTimedMove::ServerNow(this);
	PlaceDeck(Alpha);
	SetActorTickEnabled(false);
	ForceNetUpdate();
}

void AVBElevator::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	float Alpha = VBTimedMove::EvaluateAlpha(Move, VBTimedMove::ServerNow(this));
	if (HasAuthority())
	{
		if (Move.bStopped)
		{
			// 막혀 기다리는 중: 다음 한 걸음이 비면 지금 자리에서 다시 출발한다(멈춘 시간만큼 앞으로 뛰지 않게 기준 시각을 새로 잡는다).
			const double Now = VBTimedMove::ServerNow(this);
			if (Now < NextResumeProbeTime)
			{
				return;
			}

			NextResumeProbeTime = Now + ResumeProbeInterval;
			const float Travel = FMath::Max(static_cast<float>(UpperOffset.Size()), 1.0f);
			const float Step = FMath::Sign(Move.ToAlpha - Move.FromAlpha) * ResumeProbeDistance / Travel;
			const float Probe = FMath::Clamp(Move.FromAlpha + Step, 0.0f, 1.0f);
			if (!IsStepBlocked(Move.FromAlpha, Probe))
			{
				StartMove(Move.ToAlpha);
			}

			return;
		}

		if (!FMath::IsNearlyEqual(Alpha, LastPlacedAlpha) && IsStepBlocked(LastPlacedAlpha, Alpha))
		{
			Move.FromAlpha = LastPlacedAlpha;
			Move.bStopped = true;
			ForceNetUpdate();
			VB_LOG(Log, "%s: 길이 막혀 알파 %.3f 에서 멈춘다", *GetName(), LastPlacedAlpha);
			return;
		}
	}

	PlaceDeck(Alpha);
	if (!Move.bStopped && FMath::IsNearlyEqual(Alpha, Move.ToAlpha))
	{
		ArriveAndRest();
	}
}

void AVBElevator::ArriveAndRest()
{
	SetActorTickEnabled(false);
	if (HasAuthority())
	{
		// 층에 도착한 뒤에만 막기를 푼다 - 멈춤, 되돌림 중에는 계속 유지한다.
		ReleaseSaveHold();
	}
}

void AVBElevator::OnRep_Move()
{
	const float Alpha = VBTimedMove::EvaluateAlpha(Move, VBTimedMove::ServerNow(this));
	PlaceDeck(Alpha);
	SetActorTickEnabled(Move.bStopped || !FMath::IsNearlyEqual(Alpha, Move.ToAlpha));
}

bool AVBElevator::IsMoving() const
{
	return Move.bStopped || !FMath::IsNearlyEqual(CurrentAlpha(), Move.ToAlpha);
}

bool AVBElevator::IsBaseMoving(const ACharacter* Character)
{
	// 5.8: GetMovementBase 는 지워질 예정 - 바닥 객체를 받아 컴포넌트로 본다.
	const UActorComponent* Base = Character ? Cast<UActorComponent>(Character->GetMovementBaseObject()) : nullptr;
	const AVBElevator* Elevator = Base ? Cast<AVBElevator>(Base->GetOwner()) : nullptr;
	return Elevator && Elevator->IsMoving();
}

float AVBElevator::CurrentAlpha() const
{
	return VBTimedMove::EvaluateAlpha(Move, VBTimedMove::ServerNow(this));
}

void AVBElevator::PlaceDeck(const float Alpha)
{
	Deck->SetRelativeLocation(UpperOffset * Alpha);
	LastPlacedAlpha = Alpha;
}

bool AVBElevator::IsStepBlocked(const float FromAlpha, const float ToAlpha) const
{
	UWorld* World = GetWorld();
	if (!World || !Deck->GetStaticMesh())
	{
		return true; // 잴 수 없으면 막힌 것으로 본다(규칙 27). 발판 메시가 없으면 IsShaftClear 가 이미 여행을 막는다.
	}

	const FTransform& Base = RootComponent->GetComponentTransform();
	const FVector FromWorld = Base.TransformPosition(UpperOffset * FromAlpha);
	const FVector ToWorld = Base.TransformPosition(UpperOffset * ToAlpha);
	const FVector Delta = ToWorld - FromWorld;
	const FVector Extent = (Deck->Bounds.BoxExtent - FVector(SweepInset)).ComponentMax(FVector(1.0));
	const FVector DeckCenterOffset = Deck->Bounds.Origin - Deck->GetComponentLocation();
	const FCollisionShape DeckShape = FCollisionShape::MakeBox(Extent);

	// (1) 발판 상자가 이 걸음에 다른 폰을 치는가(밑에 깔림, 위로 밀어 올림). 발판에 서 있는 사람은 뺀다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBElevatorStep), false, this);
	TArray<FHitResult> PawnHits;
	World->SweepMultiByObjectType(PawnHits, FromWorld + DeckCenterOffset, ToWorld + DeckCenterOffset, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), DeckShape, Params);
	for (const FHitResult& Hit : PawnHits)
	{
		ACharacter* Character = Cast<ACharacter>(Hit.GetActor());
		if (Character && Character->GetMovementBaseObject() == Deck)
		{
			continue;
		}

		if (Hit.GetActor())
		{
			return true;
		}
	}
	// (2) 발판에 서 있는 사람(바닥 = 이 발판)의 캡슐이 같은 걸음만큼 갈 자리가 비었나 - 머리 위, 층 가장자리.
	TArray<FOverlapResult> Above;
	const FVector RiderZone = FVector(Extent.X, Extent.Y, Extent.Z + RiderSearchHeight * 0.5);
	World->OverlapMultiByObjectType(Above, FromWorld + DeckCenterOffset + FVector(0.0, 0.0, RiderSearchHeight * 0.5), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeBox(RiderZone), Params);
	TArray<ACharacter*> Riders;
	for (const FOverlapResult& Overlap : Above)
	{
		ACharacter* Rider = Cast<ACharacter>(Overlap.GetActor());
		if (Rider && Rider->GetCapsuleComponent() && Rider->GetMovementBaseObject() == Deck)
		{
			Riders.AddUnique(Rider);
		}
	}

	for (ACharacter* Rider : Riders)
	{
		const UCapsuleComponent* Capsule = Rider->GetCapsuleComponent();
		// 같이 탄 사람끼리는 함께 움직이므로 서로를 막지 않는다. 처음부터 겹쳐 있던 것은 이 걸음이 만든 막힘이 아니다.
		FCollisionQueryParams RiderParams(SCENE_QUERY_STAT(VBElevatorRider), false, this);
		RiderParams.AddIgnoredActors(TArray<AActor*>(Riders));
		RiderParams.bFindInitialOverlaps = false;
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Capsule->GetComponentLocation(), Capsule->GetComponentLocation() + Delta,
			Capsule->GetComponentQuat(), Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), RiderParams,
			FCollisionResponseParams(Capsule->GetCollisionResponseToChannels())))
		{
			return true;
		}
	}

	return false;
}

bool AVBElevator::IsShaftClear() const
{
	UWorld* World = GetWorld();
	if (!World || !Deck->GetStaticMesh())
	{
		VB_LOG(Warning, "%s: 발판 메시가 없다 - 승강로를 잴 수 없어 움직이지 않는다", *GetName());
		return false; // 잴 수 없으면 막힌 것으로 본다(규칙 27).
	}

	const FTransform& Base = RootComponent->GetComponentTransform();
	const FVector Extent = (Deck->Bounds.BoxExtent - FVector(SweepInset)).ComponentMax(FVector(1.0));
	const FVector DeckCenterOffset = Deck->Bounds.Origin - Deck->GetComponentLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBElevatorShaft), false, this);
	FHitResult Hit;
	// 발판 상자 + 그 위 서 있는 사람 높이(StandingHeadroom)를 아래 층에서 위 층까지 쓸어 본다 - 정적 지형만.
	//   상자를 반높이만큼 올리고 반높이만큼 키우면 아래 끝은 발판 그대로, 위 끝만 StandingHeadroom 늘어난다.
	const FVector Headroom(0.0, 0.0, StandingHeadroom * 0.5);
	const FCollisionShape Shape = FCollisionShape::MakeBox(Extent + Headroom);
	const bool bBlocked = World->SweepSingleByObjectType(Hit, Base.TransformPosition(FVector::ZeroVector) + DeckCenterOffset + Headroom,
		Base.TransformPosition(UpperOffset) + DeckCenterOffset + Headroom, FQuat::Identity,
		FCollisionObjectQueryParams(ECC_WorldStatic), Shape, Params);
	VB_CLOG(bBlocked, Warning, "%s: 승강로가 %s 에 막혀 있다 - 이 승강기는 움직이지 않는다", *GetName(), *GetNameSafe(Hit.GetActor()));
	return !bBlocked;
}

bool AVBElevator::AcquireSaveHold()
{
	if (SaveHold.IsValid())
	{
		return true;
	}

	UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this);
	if (!Save)
	{
		return false;
	}

	SaveHold = Save->BeginSaveHold(TEXT("ElevatorTrip"));
	if (SaveHold.IsValid() && Save->IsSaveHoldActive(SaveHold))
	{
		return true;
	}
	// 쓰는 중, 꺼지는 중이면 무효 토큰이 오거나, 알림 도중 풀렸을 수 있다 - 보유한 것으로 남기지 않는다.
	SaveHold = FVBSaveHoldToken();
	return false;
}

void AVBElevator::ReleaseSaveHold()
{
	if (!SaveHold.IsValid())
	{
		return;
	}

	if (UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this))
	{
		Save->EndSaveHold(SaveHold);
	}

	SaveHold = FVBSaveHoldToken();
}
