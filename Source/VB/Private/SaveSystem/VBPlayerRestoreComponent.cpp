// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "GameFramework/Controller.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Interaction/VBGASPSmartObjectComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "WorldPartition/WorldPartitionRuntimeCell.h"      // EWorldPartitionRuntimeCellState (Activated 값)
#include "WorldPartition/WorldPartitionStreamingSource.h"  // FWorldPartitionStreamingQuerySource
#include "VB/VB.h"

UVBPlayerRestoreComponent::UVBPlayerRestoreComponent()
{
	// 이 컴포넌트는 이벤트/타이머 구동이라 매 프레임 Tick 불필요.
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBPlayerRestoreComponent::OnRegister()
{
	Super::OnRegister();
	bEnding = false;
	// 에디터 월드(BP 미리보기 포함)에도 등록되지만 그 월드에서는 저장이 돌지 않는다. 같은 컴포넌트가 다시 등록돼도 한 번만 든다(AddUnique).
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBPlayerRestoreComponent::OnUnregister()
{
	bEnding = true;
	CancelRestoreReceipt();
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::OnUnregister();
}

void UVBPlayerRestoreComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	CancelRestoreReceipt();
	Super::EndPlay(EndPlayReason);
}

void UVBPlayerRestoreComponent::ArmInitialization(FGuid InitializationId)
{
	if (bEnding)
	{
		return;
	}

	RestoreReceipt = {};
	RestoreReceipt.InitializationId = InitializationId;
	if (bRestoring)
	{
		RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::StartFailed;
	}

	OnRestoreStateChanged.Broadcast();
}

void UVBPlayerRestoreComponent::MarkRestoreNotRequired(FGuid InitializationId)
{
	if (bEnding || !InitializationId.IsValid() || RestoreReceipt.InitializationId != InitializationId || bRestoring)
	{
		return;
	}

	RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::NotRequired;
	OnRestoreStateChanged.Broadcast();
}

void UVBPlayerRestoreComponent::SetRestoreOutcome(EVBPlayerRestoreOutcome Outcome)
{
	if (RestoreReceipt.InitializationId == RunningInitializationId && RestoreReceipt.OperationId == RunningOperationId)
	{
		RestoreReceipt.Outcome = Outcome;
		RestoreReceipt.bStreamingTimedOut = bStreamingTimedOut;
	}

	OnRestoreStateChanged.Broadcast();
}

void UVBPlayerRestoreComponent::CancelRestoreReceipt()
{
	if (!bRestoring)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(PollTimerHandle);
		}

		RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::Cancelled;
		OnRestoreStateChanged.Broadcast();
		return;
	}

	EndWithoutPlacement(EVBPlayerRestoreOutcome::Cancelled);
}

void UVBPlayerRestoreComponent::CancelTeleport(FGuid OperationId)
{
	if (bRestoring && bTeleporting && OperationId.IsValid() && OperationId == RunningOperationId)
	{
		EndWithoutPlacement(EVBPlayerRestoreOutcome::Cancelled);
	}
}

void UVBPlayerRestoreComponent::EndWithoutPlacement(EVBPlayerRestoreOutcome Outcome)
{
	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);
		World->GetTimerManager().ClearTimer(HoldTimerHandle);
	}
	// 해체 중인 월드, 파괴 중인 폰은 놓지 않는다 - 되살리거나 붙든 채 남기지 않는다(R3c).
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	if (World && !World->bIsTearingDown && IsValid(Pawn) && !Pawn->IsActorBeingDestroyed())
	{
		if (bTeleporting && ActiveTeleport.bReturnOnFailure)
		{
			Pawn->SetActorLocationAndRotation(HoldTransform.GetLocation(), HoldTransform.GetRotation(), /*bSweep=*/false);
		}

		FreezePawn(false);
	}

	bRestoring = false;
	SetRestoreOutcome(Outcome);
	NotifyTeleportFinished(Outcome);
}

void UVBPlayerRestoreComponent::NotifyTeleportFinished(EVBPlayerRestoreOutcome Outcome)
{
	if (!bTeleporting)
	{
		return;
	}
	// 알리기 전에 내리고 ID 를 잡아 둔다 - 받는 쪽이 이 안에서 새 이동을 시작해도 그 작업과 섞이지 않게.
	const FGuid FinishedId = RunningOperationId;
	bTeleporting = false;
	ActiveTeleport = FVBTeleportRequest();
	OnTeleportFinished.Broadcast(FinishedId, Outcome);
}

void UVBPlayerRestoreComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.Pawn && Context.Pawn == GetOwner())
	{
		// 로드 때 WP 스트리밍-안전 복원(BeginRestore)의 목표가 된다. 체크포인트면 지정한 자리다.
		Save.Progress.SavedPlayerTransform = Context.RestoreTransformOverride.IsSet()
			? Context.RestoreTransformOverride.GetValue()
			: Context.Pawn->GetActorTransform();
		Save.Progress.bHasSavedTransform = true;
	}
}

void UVBPlayerRestoreComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (bEnding)
	{
		return;
	}
	// 위치가 없는 세이브(새 게임 직후, 옛 슬롯)는 PlayerStart 스폰을 그대로 둔다.
	if (Context.Pawn && Context.Pawn == GetOwner())
	{
		if (Save.Progress.bHasSavedTransform)
		{
			BeginRestore(Save.Progress.SavedPlayerTransform);
		}
		else
		{
			MarkRestoreNotRequired(RestoreReceipt.InitializationId);
		}
	}
}

void UVBPlayerRestoreComponent::BeginRestore(const FTransform& Target)
{
	FVBTeleportRequest Request;
	Request.Target = Target;
	StartMove(Request, /*bWriteReceipt=*/true);
}

FGuid UVBPlayerRestoreComponent::BeginTeleport(const FVBTeleportRequest& Request)
{
	return StartMove(Request, /*bWriteReceipt=*/false) ? RunningOperationId : FGuid();
}

bool UVBPlayerRestoreComponent::StartMove(const FVBTeleportRequest& Request, bool bWriteReceipt)
{
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (bEnding || !IsRegistered() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !IsValid(World) || World->bIsTearingDown)
	{
		if (bWriteReceipt)
		{
			RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::StartFailed;
			OnRestoreStateChanged.Broadcast();
		}

		VB_LOG(Warning, "VBPlayerRestoreComponent: 소유 액터가 ACharacter 가 아니거나 월드 없음 -> %s 스킵", bWriteReceipt ? TEXT("복원") : TEXT("순간 이동"));
		return false;
	}

	if (bRestoring)
	{
		if (bWriteReceipt && RestoreReceipt.InitializationId != RunningInitializationId)
		{
			RestoreReceipt.Outcome = EVBPlayerRestoreOutcome::StartFailed;
			OnRestoreStateChanged.Broadcast();
		}

		return false; // 진행 중이면 무시(재진입 방지).
	}

	bRestoring = true;
	RunningInitializationId = RestoreReceipt.InitializationId;
	RunningOperationId = FGuid::NewGuid();
	bStreamingTimedOut = false;
	// 순간 이동은 불러오기 영수증을 건드리지 않는다: 작업 ID 를 영수증에 적지 않으면 SetRestoreOutcome, 지면 결과가 영수증에 쓰이지 않는다.
	//   땅을 못 찾은 이동이 '불러오기 실패' 로 남아 준비 상태를 영영 막는 일이 없게. 진행 중에는 IsRestoring 이 참이라 준비 상태가 그동안만 막힌다.
	if (bWriteReceipt)
	{
		RestoreReceipt.OperationId = RunningOperationId;
		RestoreReceipt.bGroundFound = false;
		RestoreReceipt.bStreamingTimedOut = false;
	}

	PendingTarget = Request.Target;
	bTeleporting = !bWriteReceipt;
	ActiveTeleport = bTeleporting ? Request : FVBTeleportRequest();
	if (UVBPlayerRagdollComponent* Ragdoll = Pawn->FindComponentByClass<UVBPlayerRagdollComponent>())
	{
		Ragdoll->CancelRagdoll();
	}

	if (UVBGASPSmartObjectComponent* SmartObject = Pawn->FindComponentByClass<UVBGASPSmartObjectComponent>())
	{
		SmartObject->AbortInteraction();
	}

	// 1. 얼리기 전 자리를 잡고 그 자리에서 얼린다 - 목표에 놓은 뒤에 잡으면 실패한 순간 이동이 돌아갈 곳이 목표가 된다(R3c).
	HoldTransform = Pawn->GetActorTransform();
	FreezePawn(true);
	SetRestoreOutcome(EVBPlayerRestoreOutcome::InProgress);

	// 2. 붙들기(화면이 어두워지는 동안)가 있으면 그 뒤에, 없으면 바로 목표에 놓는다.
	if (ActiveTeleport.HoldSeconds > 0.0f)
	{
		World->GetTimerManager().SetTimer(HoldTimerHandle, this, &UVBPlayerRestoreComponent::PlaceAndPoll,
			ActiveTeleport.HoldSeconds, /*bLoop=*/false);
	}
	else
	{
		PlaceAndPoll();
	}

	return true;
}

void UVBPlayerRestoreComponent::PlaceAndPoll()
{
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!IsValid(Pawn) || !World)
	{
		EndWithoutPlacement(EVBPlayerRestoreOutcome::Cancelled);
		return;
	}
	// 3. 저장 XY 로 텔레포트 -> 폰이 PlayerController 뷰타겟이라 그 위치 스트리밍이 자동 요청된다.
	//    Z 는 목표값으로 두되(대략), 실제 지면 정렬은 라인트레이스가 확정한다.
	Pawn->SetActorLocationAndRotation(PendingTarget.GetLocation(), PendingTarget.GetRotation(), /*bSweep=*/false);

	// 4. 스트리밍 완료 폴링 시작(타임아웃은 PollStreaming 에서 판정).
	StreamStartTime = World->GetTimeSeconds();
	World->GetTimerManager().SetTimer(PollTimerHandle, this, &UVBPlayerRestoreComponent::PollStreaming,
		FMath::Max(0.02f, PollIntervalSeconds), /*bLoop=*/true);
}

void UVBPlayerRestoreComponent::PollStreaming()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		EndWithoutPlacement(EVBPlayerRestoreOutcome::Cancelled);
		return;
	}

	bool bDone = true; // WP 미사용 맵이면 대기 없이 즉시 완료.
	if (UWorldPartitionSubsystem* WP = World->GetSubsystem<UWorldPartitionSubsystem>())
	{
		TArray<FWorldPartitionStreamingQuerySource> Sources;
		Sources.Emplace(FWorldPartitionStreamingQuerySource(PendingTarget.GetLocation()));
		// Activated = 셀이 월드에 추가되어 콜리전이 존재하는 상태(지면 트레이스가 유효해지는 시점).
		bDone = WP->IsStreamingCompleted(EWorldPartitionRuntimeCellState::Activated, Sources, /*bExactState=*/false);
	}

	const bool bTimedOut = (World->GetTimeSeconds() - StreamStartTime) >= StreamingTimeoutSeconds;
	if (!bDone && !bTimedOut)
	{
		return; // 아직 대기.
	}

	if (bTimedOut && !bDone)
	{
		bStreamingTimedOut = true;
		VB_LOG(Warning, "VBPlayerRestoreComponent: 스트리밍 완료 대기 타임아웃(%.1fs) -> 강제 지면 스냅", StreamingTimeoutSeconds);
	}

	World->GetTimerManager().ClearTimer(PollTimerHandle);
	SnapToGroundAndFinish();
}

void UVBPlayerRestoreComponent::SnapToGroundAndFinish()
{
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	UWorld* World = GetWorld();
	if (!Pawn || !World)
	{
		EndWithoutPlacement(EVBPlayerRestoreOutcome::Cancelled);
		return;
	}

	const FVector TargetLoc = PendingTarget.GetLocation();
	const float HalfHeight = Pawn->GetCapsuleComponent()
		? Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : FallbackCapsuleHalfHeight;

	// 목표 XY 에서 아래로 트레이스해 실제 지면 Z 를 얻는다(스트리밍된 콜리전 대상). WorldStatic 채널.
	const FVector TraceStart = TargetLoc + FVector(0, 0, GroundSnapTraceUpDistance);
	const FVector TraceEnd = TargetLoc - FVector(0, 0, GroundSnapTraceDownDistance);
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Pawn);
	FVector FinalLoc = TargetLoc;
	const bool bGroundFound = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params);
	if (bGroundFound)
	{
		FinalLoc = FVector(TargetLoc.X, TargetLoc.Y, Hit.ImpactPoint.Z + HalfHeight);
	}

	EVBPlayerRestoreOutcome Outcome = bStreamingTimedOut ? EVBPlayerRestoreOutcome::StreamingTimedOut
		: bGroundFound ? EVBPlayerRestoreOutcome::Completed : EVBPlayerRestoreOutcome::GroundNotFound;
	// 순간 이동의 도착 검사: 걸을 수 있는 바닥이고 설 공간이 있어야 도착이다. 불러오기 복원은 종전대로 본다.
	if (bTeleporting && ActiveTeleport.bRequireStandableArrival && Outcome == EVBPlayerRestoreOutcome::Completed)
	{
		const UCharacterMovementComponent* CMC = Pawn->GetCharacterMovement();
		if (!CMC || Hit.ImpactNormal.Z < CMC->GetWalkableFloorZ())
		{
			Outcome = EVBPlayerRestoreOutcome::GroundNotFound;
		}
		else if (!HasStandingRoom(*Pawn, FinalLoc))
		{
			Outcome = EVBPlayerRestoreOutcome::Obstructed;
		}
	}

	const bool bReturn = bTeleporting && ActiveTeleport.bReturnOnFailure && Outcome != EVBPlayerRestoreOutcome::Completed;
	if (bReturn)
	{
		Pawn->SetActorLocationAndRotation(HoldTransform.GetLocation(), HoldTransform.GetRotation(), /*bSweep=*/false);
	}
	else
	{
		Pawn->SetActorLocation(FinalLoc, /*bSweep=*/false);
	}

	// 5. 동결 해제 후 완료 통지.
	FreezePawn(false);
	bRestoring = false;
	if (RestoreReceipt.InitializationId == RunningInitializationId && RestoreReceipt.OperationId == RunningOperationId)
	{
		RestoreReceipt.bGroundFound = bGroundFound;
	}

	SetRestoreOutcome(Outcome);
	if (bReturn)
	{
		VB_LOG(Warning, "VBPlayerRestoreComponent: 순간 이동 실패(%d) - 얼린 자리 %s 로 되돌림", static_cast<int32>(Outcome),
			*HoldTransform.GetLocation().ToString());
	}
	else
	{
		VB_LOG(Log, "VBPlayerRestoreComponent: 복원 완료 @ %s", *FinalLoc.ToString());
	}
	// 순간 이동의 결과를 먼저 알린다 - OnRestoreCompleted 를 듣는 쪽이 그 안에서 새 이동을 시작해도 끝난 작업의 표시가 이미 내려가 있다.
	NotifyTeleportFinished(Outcome);
	OnRestoreCompleted.Broadcast();
}

bool UVBPlayerRestoreComponent::HasStandingRoom(const ACharacter& Pawn, const FVector& Location) const
{
	const UCapsuleComponent* Capsule = Pawn.GetCapsuleComponent();
	const UWorld* World = GetWorld();
	if (!Capsule || !World)
	{
		return false; // 잴 수 없으면 막힌 것으로 본다(규칙 27).
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(VBTeleportArrival), false, &Pawn);
	FCollisionResponseParams Responses;
	Responses.CollisionResponse = Capsule->GetCollisionResponseToChannels();
	return !World->OverlapBlockingTestByChannel(Location + FVector(0.0, 0.0, ArrivalFloorClearance), Pawn.GetActorQuat(),
		Capsule->GetCollisionObjectType(), Capsule->GetCollisionShape(), Params, Responses);
}

void UVBPlayerRestoreComponent::FreezePawn(bool bFreeze)
{
	ACharacter* Pawn = Cast<ACharacter>(GetOwner());
	if (!Pawn)
	{
		return;
	}

	UCharacterMovementComponent* CMC = Pawn->GetCharacterMovement();

	if (bFreeze)
	{
		if (CMC)
		{
			SavedGravityScale = CMC->GravityScale;
			CMC->StopMovementImmediately();
			CMC->GravityScale = 0.0f;
			CMC->SetMovementMode(MOVE_None);
		}

		Pawn->SetActorEnableCollision(false); // 스트리밍 중 지형 관통/충돌 방지.
	}
	else
	{
		Pawn->SetActorEnableCollision(true);
		if (CMC)
		{
			CMC->GravityScale = SavedGravityScale;
			CMC->SetMovementMode(MOVE_Walking);
		}
	}
}
