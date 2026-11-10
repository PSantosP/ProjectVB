// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Map/VBPlayerRelocationComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "TimerManager.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "Interaction/VBSavePoint.h"
#include "Map/VBTravelPointSubsystem.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "Traversal/VBTraversalComponent.h"
#include "VB/VB.h"

UVBPlayerRelocationComponent::UVBPlayerRelocationComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 서버 -> 소유 클라이언트 RPC 를 쓰려면 컴포넌트가 복제돼야 한다. 복제하는 속성은 없다.
	SetIsReplicatedByDefault(true);
}

void UVBPlayerRelocationComponent::StartPointTravel(const FGameplayTag& Destination, float InFadeOutSeconds, float InFadeInSeconds)
{
	bRelocating = true;
	PendingDestination = Destination;
	PendingFadeInSeconds = InFadeInSeconds;
	OnRelocationStarted.Broadcast();
	// 어두워지는 동안 기다렸다가 옮긴다 - 옮기는 순간이 보이지 않게. 시간이 0 이면 바로.
	ClientTravelFade( /*bOut=*/true, InFadeOutSeconds);
	if (UWorld* World = GetWorld(); World && InFadeOutSeconds > 0.0f)
	{
		World->GetTimerManager().SetTimer(FadeTimer, this, &ThisClass::MovePawnToDestination, InFadeOutSeconds, false);
	}
	else
	{
		MovePawnToDestination();
	}
}

void UVBPlayerRelocationComponent::MovePawnToDestination()
{
	const UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(this);
	const AVBSavePoint* Destination = Travel ? Travel->FindPoint(PendingDestination) : nullptr;
	APawn* Pawn = GetPawn();
	UVBPlayerRestoreComponent* Restore = Pawn ? Pawn->FindComponentByClass<UVBPlayerRestoreComponent>() : nullptr;
	// 어두워지는 사이에 바뀐 것을 다시 본다: 목적지, 폰이 사라졌거나, 죽었거나, 적에게 잡혔으면 옮기지 않는다.
	const UVBCombatCoordinatorSubsystem* Combat = UVBCombatCoordinatorSubsystem::Get(this);
	const TCHAR* Reason = !Destination ? TEXT("목적지가 사라졌다")
		: !Restore ? TEXT("폰/복원 컴포넌트가 없다")
		: UVBAbilitySystemStatics::IsActorDead(Pawn) ? TEXT("폰이 죽었다")
		: (Combat && Combat->IsEngaged(Pawn)) ? TEXT("전투가 시작됐다")
		: nullptr;
	if (Reason)
	{
		// 그 자리에서 밝힌다 - 어두운 화면에 갇히지 않게.
		VB_LOG(Warning, "빠른 이동: 옮기기 직전에 %s - 이동 취소", Reason);
		FinishTravel(PendingFadeInSeconds);
		return;
	}
	// 빠른 이동은 붙들기 없이 바로 옮기고, 도착 검사, 되돌리기를 하지 않는다.
	FVBTeleportRequest Request;
	Request.Target = Destination->GetArrivalTransform();
	if (!BeginRelocation(Pawn, Restore, Request, /*bScripted=*/false, PendingFadeInSeconds))
	{
		VB_LOG(Warning, "빠른 이동: 순간 이동을 시작하지 못했다(복원 진행 중 등) - 이동 취소");
		FinishTravel(PendingFadeInSeconds);
	}
}

bool UVBPlayerRelocationComponent::StartScriptedMove(const FTransform& Arrival, float InFadeOutSeconds, float InFadeInSeconds,
	const TArray<FInstancedStruct>& OnArrivedEffects)
{
	const AActor* Owner = GetOwner();
	APawn* Pawn = GetPawn();
	const ACharacter* Character = Cast<ACharacter>(Pawn);
	UVBPlayerRestoreComponent* Restore = Pawn ? Pawn->FindComponentByClass<UVBPlayerRestoreComponent>() : nullptr;
	const UVBTraversalComponent* Traversal = Pawn ? Pawn->FindComponentByClass<UVBTraversalComponent>() : nullptr;
	const UWorld* World = GetWorld();
	const UVBCinematicDirectorSubsystem* Director = World ? World->GetSubsystem<UVBCinematicDirectorSubsystem>() : nullptr;
	// 새 게임 시네마틱의 시작 보류는 감독이 요청을 받기 전부터 걸려 따로 본다. 소유자가 사라진 보류도 거절한다.
	// 초기화의 알 수 없음 상태는 거절하지 않는다 - 맵으로 바로 켠 PIE 는 세션 내내 그 상태다.
	const uint32 StartupHolds = static_cast<uint32>(EVBPlayerPreparationReason::Initialization)
		| static_cast<uint32>(EVBPlayerPreparationReason::ExternalHold);
	const FVBPlayerPreparationSnapshot Preparation = UVBPlayerReadinessComponent::QueryForPawn(Pawn);
	const bool bPreparationHeld = (Preparation.BlockedReasons & StartupHolds)
		|| (Preparation.UnavailableReasons & static_cast<uint32>(EVBPlayerPreparationReason::HoldOwner));
	// 받아들이기만 판정한다. 거절은 아무것도 바꾸지 않는다.
	// 트래버설이 끝난 뒤에도 끊긴 몽타주 블렌드아웃이 루트 모션을 낼 수 있어 루트 모션도 본다.
	const TCHAR* Refusal = !Owner || !Owner->HasAuthority() ? TEXT("권한 없는 쪽에서 불렀다")
		: !Arrival.IsValid() || !FMath::IsFinite(InFadeOutSeconds) || !FMath::IsFinite(InFadeInSeconds)
			|| InFadeOutSeconds < 0.0f || InFadeInSeconds < 0.0f ? TEXT("도착 자리나 페이드 시간이 잘못됐다")
		: bRelocating ? TEXT("이미 옮기는 중")
		: !Character || !Restore ? TEXT("폰/복원 컴포넌트가 없다")
		: UVBAbilitySystemStatics::IsActorDead(Pawn) ? TEXT("폰이 죽었다")
		: Restore->IsRestoring() ? TEXT("복원 중")
		: (Traversal && Traversal->IsDoingTraversal()) || Character->IsPlayingRootMotion() ? TEXT("트래버설·루트 모션 중")
		: (Director && Director->IsBusy()) || bPreparationHeld ? TEXT("시네마틱·초기화 보류 중")
		: nullptr;
	if (Refusal)
	{
		VB_LOG(Log, "자리로 옮기기 거절: %s - 폰은 하던 대로 둔다", Refusal);
		return false;
	}

	bRelocating = true;
	PendingArrivedEffects = OnArrivedEffects;
	FVBTeleportRequest Request;
	Request.Target = Arrival;
	Request.HoldSeconds = InFadeOutSeconds;
	Request.bRequireStandableArrival = true;
	Request.bReturnOnFailure = true;
	if (!BeginRelocation(Pawn, Restore, Request, /*bScripted=*/true, InFadeInSeconds))
	{
		bRelocating = false;
		PendingArrivedEffects.Reset();
		VB_LOG(Log, "자리로 옮기기 거절: 순간 이동을 시작하지 못했다 - 폰은 하던 대로 둔다");
		return false;
	}
	// 받아들인 뒤에만 알린다(거절은 아무것도 바꾸지 않는다). 붙들기 = 어두워지는 시간.
	OnRelocationStarted.Broadcast();
	ClientTravelFade( /*bOut=*/true, InFadeOutSeconds);
	VB_LOG(Log, "자리로 옮기기: %s 로", *Arrival.GetLocation().ToString());
	return true;
}

bool UVBPlayerRelocationComponent::BeginRelocation(APawn* Pawn, UVBPlayerRestoreComponent* Restore, const FVBTeleportRequest& Request,
	bool bScripted, float InFadeInSeconds)
{
	Relocation = FRelocation();
	Relocation.bScripted = bScripted;
	Relocation.Pawn = Pawn;
	Relocation.Restore = Restore;
	Relocation.FadeInSeconds = InFadeInSeconds;
	// 알림을 시작 전에 묶는다 - 시작과 끝 사이에 알림을 놓칠 틈이 없게.
	TeleportFinishedHandle = Restore->OnTeleportFinished.AddUObject(this, &ThisClass::HandleTeleportFinished);
	Relocation.OperationId = Restore->BeginTeleport(Request);
	if (!Relocation.OperationId.IsValid())
	{
		ReleaseRelocation();
		return false;
	}
	// 지키미: 폰이 도중에 사라지는 등으로 알림이 오지 않으면 붙들기 + 스트리밍 대기 상한 + 여유 뒤에 그 작업을 끝낸다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(WatchdogTimer, this, &ThisClass::AbortRelocation,
			Request.HoldSeconds + Restore->StreamingTimeoutSeconds + WatchdogSlackSeconds, false);
	}

	return true;
}

void UVBPlayerRelocationComponent::HandleTeleportFinished(FGuid OperationId, EVBPlayerRestoreOutcome Outcome)
{
	if (!Relocation.OperationId.IsValid() || OperationId != Relocation.OperationId)
	{
		return;
	}
	// 요청을 떼어 낸 뒤 효과를 적용한다 - 효과가 새 이동을 시작해도 이 요청과 섞이지 않게.
	const bool bScripted = Relocation.bScripted;
	const TWeakObjectPtr<APawn> MovedPawn = Relocation.Pawn;
	const TArray<FInstancedStruct> ArrivedEffects = MoveTemp(PendingArrivedEffects);
	const bool bArrived = Outcome == EVBPlayerRestoreOutcome::Completed;
	VB_LOG(Log, "%s: %s (결과 %d)", bScripted ? TEXT("자리로 옮기기") : TEXT("빠른 이동"),
		bArrived ? TEXT("도착") : TEXT("도착하지 못했다"), static_cast<int32>(Outcome));
	FinishTravel(Relocation.FadeInSeconds);
	if (!bScripted || !bArrived)
	{
		return;
	}
	// 같은 플레이어의 같은 폰일 때만 - 그 사이 폰이 바뀌었으면 새 폰에 옛 요청의 효과를 걸지 않는다.
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	APlayerState* PlayerState = PC ? PC->PlayerState.Get() : nullptr;
	if (!PlayerState || !MovedPawn.IsValid() || PC->GetPawn() != MovedPawn.Get())
	{
		VB_LOG(Warning, "자리로 옮기기: 도착했지만 플레이어/폰이 바뀌었다 - 도착 효과를 건너뛴다");
		return;
	}

	VBRules::ApplyAll(ArrivedEffects, FVBRuleContext::Make(this, PlayerState));
}

void UVBPlayerRelocationComponent::AbortRelocation()
{
	if (!bRelocating)
	{
		return;
	}

	VB_LOG(Warning, "옮기기: 도착 알림 없이 시간이 지났다(폰이 사라짐 등) - 그 작업을 끝낸다");
	// 제 작업만 끝낸다 - 결과(Cancelled)가 HandleTeleportFinished 로 와서 마무리한다. 복원 컴포넌트가 이미 없으면 여기서 끝낸다.
	if (UVBPlayerRestoreComponent* Restore = Relocation.Restore.Get(); Restore && Relocation.OperationId.IsValid())
	{
		Restore->CancelTeleport(Relocation.OperationId);
	}

	if (bRelocating)
	{
		FinishTravel(Relocation.bScripted ? Relocation.FadeInSeconds : PendingFadeInSeconds);
	}
}

void UVBPlayerRelocationComponent::ReleaseRelocation()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(FadeTimer);
		World->GetTimerManager().ClearTimer(WatchdogTimer);
	}

	if (UVBPlayerRestoreComponent* Restore = Relocation.Restore.Get(); Restore && TeleportFinishedHandle.IsValid())
	{
		Restore->OnTeleportFinished.Remove(TeleportFinishedHandle);
	}

	TeleportFinishedHandle.Reset();
	Relocation = FRelocation();
	PendingArrivedEffects.Reset();
}

void UVBPlayerRelocationComponent::FinishTravel(float InFadeInSeconds)
{
	ReleaseRelocation();
	PendingDestination = FGameplayTag();
	bRelocating = false;
	ClientTravelFade( /*bOut=*/false, InFadeInSeconds);
}

void UVBPlayerRelocationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 맡긴 순간 이동이 남았으면 그 작업만 끝낸다. 결과 묶음을 먼저 풀어 끝나는 컴포넌트로 알림이 오지 않게.
	UVBPlayerRestoreComponent* Restore = Relocation.Restore.Get();
	const FGuid OperationId = Relocation.OperationId;
	ReleaseRelocation();
	if (Restore && OperationId.IsValid())
	{
		Restore->CancelTeleport(OperationId);
	}

	bRelocating = false;
	Super::EndPlay(EndPlayReason);
}

APawn* UVBPlayerRelocationComponent::GetPawn() const
{
	const AController* Controller = Cast<AController>(GetOwner());
	return Controller ? Controller->GetPawn() : nullptr;
}

void UVBPlayerRelocationComponent::ClientTravelFade_Implementation(bool bOut, float Seconds)
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	APlayerCameraManager* Camera = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!Camera)
	{
		return;
	}
	// 0 초면 기다리지 않고 바로(어둡게 = 검은 화면 붙들기, 밝게 = 페이드 끄기).
	if (Seconds <= 0.0f)
	{
		if (bOut)
		{
			Camera->SetManualCameraFade(1.0f, FLinearColor::Black, /*bInFadeAudio=*/false);
		}
		else
		{
			Camera->StopCameraFade();
		}

		return;
	}
	// 어둡게는 끝에서 붙든다(옮기는 동안 검은 화면), 밝게는 끝에서 놓는다.
	Camera->StartCameraFade(bOut ? 0.0f : 1.0f, bOut ? 1.0f : 0.0f, Seconds, FLinearColor::Black,
		/*bShouldFadeAudio=*/false, /*bHoldWhenFinished=*/bOut);
}
