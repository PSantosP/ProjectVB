// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Camera/VBFinisherDirectorSubsystem.h"

#include "Camera/VBFinisherCamera.h"
#include "ContextualAnimSceneActorComponent.h"
#include "ContextualAnimTypes.h"
#include "Character/VBFinisherComponent.h"
#include "Data/VBFinisherConfig.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "VB/VB.h"

namespace
{
	// 시간 배율 요청의 이름. 처형은 한 번에 하나라 고정 이름을 쓰고, 시작 때 Pop 으로 남은 요청을 지운다.
	const FName FinisherTimeDilationHandle(TEXT("Finisher"));
}

bool UVBFinisherDirectorSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UVBFinisherDirectorSubsystem::IsFeedbackViewTarget(const APlayerController* Controller, const APawn* Recipient,
	const AActor* ViewTarget) const
{
	if (!bRunning || !IsValid(Controller) || !Controller->IsLocalController() || !IsValid(Recipient) ||
		Controller->GetPawn() != Recipient || PendingAttacker.Get() != Recipient || !IsValid(ViewTarget))
	{
		return false;
	}

	const FVBFinisherViewLease* Lease = ViewLeases.FindByPredicate(
		[Controller](const FVBFinisherViewLease& Entry) { return Entry.Controller.Get() == Controller; });
	return Lease && IsValid(Lease->Camera) && Lease->Camera == ViewTarget;
}

bool UVBFinisherDirectorSubsystem::OwnsView(const AActor* OurCamera, const AActor* CurrentTarget, const AActor* PendingTarget,
	const APawn* LeasePawn, const APawn* NowPawn)
{
	if (!OurCamera || LeasePawn != NowPawn)
	{
		return false;
	}

	return PendingTarget ? PendingTarget == OurCamera : CurrentTarget == OurCamera;
}

void UVBFinisherDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 클래스 델리게이트라 PIE 의 모든 월드가 듣는다 - 핸들러가 자기 월드인지 가른다.
	UVBFinisherComponent::OnConfigArrived.AddUObject(
		this, &UVBFinisherDirectorSubsystem::HandleConfigArrived);
}

void UVBFinisherDirectorSubsystem::Deinitialize()
{
	// 월드가 먼저 죽는 경로가 있다. 남겨 두면 죽은 서브시스템으로 호출이 간다.
	UVBFinisherComponent::OnConfigArrived.RemoveAll(this);
	End();

	Super::Deinitialize();
}

void UVBFinisherDirectorSubsystem::HandleConfigArrived(AActor* Attacker, const UVBFinisherConfig* InConfig)
{
	// 남의 월드 일이면 무시한다. 클래스 델리게이트의 값은 여기서 치른다.
	if (!Attacker || Attacker->GetWorld() != GetWorld())
	{
		return;
	}

	// 설정이 비워졌다 = 서버에서 장면이 끝났다. 우리 것이면 거둔다.
	if (!InConfig)
	{
		if (PendingAttacker.Get() == Attacker)
		{
			End();
		}

		return;
	}

	if (PendingAttacker.Get() == Attacker && PendingConfig == InConfig)
	{
		TryBegin(); // MMI 재생 준비/설정 복제의 순서가 달라도 카메라는 한 번만 시작한다.
		return;
	}

	// 앞 판이 안 거둬졌으면 먼저 거둔다. 겹치는 일은 없어야 하지만, 있으면 카메라가 둘 남는다.
	if (bRunning || PendingAttacker.IsValid())
	{
		End();
	}

	PendingAttacker = Attacker;
	PendingConfig = InConfig;

	// 두 복제의 도착 순서가 정해지지 않았다. 장면이 아직이면 구독해 두고 기다린다.
	if (UContextualAnimSceneActorComponent* Scene = UVBFinisherComponent::FindSceneActorComponent(Attacker))
	{
		WatchedScene = Scene;
		Scene->OnJoinedSceneDelegate.AddUniqueDynamic(this, &UVBFinisherDirectorSubsystem::HandleJoinedScene);
		Scene->OnLeftSceneDelegate.AddUniqueDynamic(this, &UVBFinisherDirectorSubsystem::HandleLeftScene);
	}
	else
	{
		// 두 기초 클래스의 생성자가 이것을 보장한다. 여기 걸렸다면 그 둘을 안 거치는 액터다.
		VB_LOG(Warning, "처형 카메라: %s 에 씬 컴포넌트가 없다 - 카메라를 세우지 않는다",
			*GetNameSafe(Attacker));
		PendingAttacker = nullptr;
		PendingConfig = nullptr;
		return;
	}

	TryBegin();
}

void UVBFinisherDirectorSubsystem::HandleJoinedScene(UContextualAnimSceneActorComponent* /*SceneActorComponent*/)
{
	TryBegin();
}

void UVBFinisherDirectorSubsystem::HandleLeftScene(UContextualAnimSceneActorComponent* /*SceneActorComponent*/)
{
	End();
}

AActor* UVBFinisherDirectorSubsystem::ResolveAnchor(const UVBFinisherComponent* Finisher) const
{
	if (!Finisher || !PendingConfig)
	{
		return nullptr;
	}

	// 역할 이름은 설정 자산이 든다. 코드가 「피해자」를 아는 순간 역할 구성이 다른 처형에서 깨진다.
	return Finisher->GetParticipant(PendingConfig->CameraAnchorRole);
}

void UVBFinisherDirectorSubsystem::CollectViewingControllers(const TArray<AActor*>& Participants,
	TArray<APlayerController*>& OutControllers) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 로컬 플레이어만 훑는다. 원격 플레이어의 카메라는 그 기계가 정한다.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->IsLocalController())
		{
			continue;
		}

		// 「누가 조종하나」가 아니라 「무엇을 보나」를 묻는다 - 관전자도 같은 판정으로 걸린다.
		if (Participants.Contains(PC->GetViewTarget()))
		{
			OutControllers.Add(PC);
		}
	}
}

void UVBFinisherDirectorSubsystem::TryBegin()
{
	if (bRunning)
	{
		return;
	}

	AActor* Attacker = PendingAttacker.Get();
	const UVBFinisherComponent* Finisher = UVBFinisherComponent::FindFor(Attacker);
	if (!Attacker || !PendingConfig || !Finisher || !Finisher->IsPlaying())
	{
		// 아직 둘이 다 안 모였다. 나머지 신호가 오면 다시 불린다.
		return;
	}

	AActor* AnchorActor = ResolveAnchor(Finisher);
	if (!AnchorActor)
	{
		// 저작 오류는 런타임에야 드러나므로 경고한다. 아무 액터에나 붙이면 원인을 못 찾는다.
		VB_LOG(Warning, "처형 카메라: 앵커 역할 '%s' 를 씬 바인딩에서 못 찾았다 (config=%s) - 카메라를 세우지 않는다",
			*PendingConfig->CameraAnchorRole.ToString(), *GetNameSafe(PendingConfig));
		return;
	}

	// 키가 없는 설정은 연출 없는 처형(정상)이다. 신호가 안 온 것과 구별되게 로그는 남긴다.
	if (PendingConfig->CameraKeys.IsEmpty())
	{
		VB_LOG(Log, "처형 카메라: %s 에 CameraKeys 가 없다 - 연출 없는 처형으로 넘어간다",
			*GetNameSafe(PendingConfig));
		return;
	}

	TArray<AActor*> Participants;
	Finisher->GetParticipants(Participants);

	TArray<APlayerController*> Viewers;
	CollectViewingControllers(Participants, Viewers);
	if (Viewers.IsEmpty())
	{
		// 아무도 그 장면을 보고 있지 않다. 데디 서버는 정상이라 조용하다.
		//   로컬 플레이어가 있는데도 안 보면 뷰타깃이 폰이 아닌 것이라 경고한다(남의 처형 구경은 제외).
		int32 LocalParticipantCount = 0;
		if (const UWorld* World = GetWorld())
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				if (const APlayerController* PC = It->Get())
				{
					LocalParticipantCount += PC->IsLocalController() && Participants.Contains(PC->GetPawn()) ? 1 : 0;
				}
			}
		}

		if (LocalParticipantCount > 0)
		{
			VB_LOG(Warning, "처형 카메라: 처형에 참가한 로컬 플레이어 %d명이 있는데 아무도 참가자를 보고 있지 않다 "
				"- 카메라를 세우지 않는다 (참가자 %d명, 앵커=%s)",
				LocalParticipantCount, Participants.Num(), *GetNameSafe(AnchorActor));
		}

		return;
	}

	bRunning = true;

	// 사람마다 하나씩 세운다. 분할 화면에서 한 대를 나눠 쓰면 둘째 화면이 첫째를 따라간다.
	for (APlayerController* PC : Viewers)
	{
		AVBFinisherCamera* Camera = GetWorld()->SpawnActor<AVBFinisherCamera>(
			AVBFinisherCamera::StaticClass(), AnchorActor->GetActorTransform());
		if (!Camera)
		{
			continue;
		}

		Camera->SetOwner(PC);
		Camera->Setup(PendingConfig, AnchorActor, Attacker);
		if (!Camera->HasSafeView())
		{
			Camera->Destroy();
			continue;
		}

		// 첫 키의 BlendTime 으로 들어간다. 자르지 말고 블렌드하라는 것이 이 갈래의 결정이다.
		PC->SetViewTargetWithBlend(Camera, PendingConfig->CameraKeys[0].BlendTime);

		FVBFinisherViewLease& Lease = ViewLeases.AddDefaulted_GetRef();
		Lease.Controller = PC;
		Lease.Camera = Camera;
		Lease.Pawn = PC->GetPawn();
	}

	// 슬로우는 로컬에서만 건다(서버에서 걸면 모두가 느려진다). 구간마다 자기 Time 에 걸고, Time 0 구간은 여기서 바로 건다.
	if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(this))
	{
		// 앞 판이 비정상 종료로 남긴 요청을 먼저 지운다. 없으면 무동작이다.
		TimeDilation->Pop(FinisherTimeDilationHandle);
	}

	NextSlowSegment = 0;
	SkipExpiredSlowSegments(PendingConfig->TimeDilationSegments, NextSlowSegment, Finisher->GetPlaybackTime());
	Tick(0.0f);

	// 「처형 연출 시작」은 UVBGA_Execute 가 이미 쓰는 말이다. 같은 말이면 로그로 둘을 못 가른다.
	VB_LOG(Log, "처형 카메라 시작: 뷰어 %d명, 앵커=%s, config=%s",
		Viewers.Num(), *GetNameSafe(AnchorActor), *GetNameSafe(PendingConfig));
}

void UVBFinisherDirectorSubsystem::RestoreViewer(const FVBFinisherViewLease& Lease, float BlendTime) const
{
	APlayerController* PC = Lease.Controller.Get();
	const APlayerCameraManager* Manager = PC ? PC->PlayerCameraManager.Get() : nullptr;
	if (!Manager)
	{
		VB_LOG(Warning, "처형 카메라 복귀 생략: %s 에 카메라 관리자가 없다", *GetNameSafe(PC));
		return;
	}
	if (!IsValid(Lease.Camera)
		|| !OwnsView(Lease.Camera, Manager->ViewTarget.Target, Manager->PendingViewTarget.Target, Lease.Pawn.Get(), PC->GetPawn()))
	{
		return;
	}
	PC->SetControlRotation(Lease.Camera->GetViewRotation());
	PC->SetViewTargetWithBlend(PC->GetPawn(), BlendTime);
}

void UVBFinisherDirectorSubsystem::ReleaseObstructedView(AVBFinisherCamera* Camera)
{
	const int32 Index = ViewLeases.IndexOfByPredicate([Camera](const FVBFinisherViewLease& Lease) { return Lease.Camera == Camera; });
	if (Index == INDEX_NONE) { return; }
	RestoreViewer(ViewLeases[Index], 0.0f);
	ViewLeases.RemoveAt(Index);
	Camera->Destroy();
}

void UVBFinisherDirectorSubsystem::End()
{
	// 구독부터 끊는다. 아래에서 뷰타깃을 되돌리는 동안 다시 불려 들어오면 안 된다.
	if (UContextualAnimSceneActorComponent* Scene = WatchedScene.Get())
	{
		Scene->OnJoinedSceneDelegate.RemoveDynamic(this, &UVBFinisherDirectorSubsystem::HandleJoinedScene);
		Scene->OnLeftSceneDelegate.RemoveDynamic(this, &UVBFinisherDirectorSubsystem::HandleLeftScene);
	}

	WatchedScene = nullptr;

	const float ReturnBlend = PendingConfig ? PendingConfig->CameraReturnBlend : 0.0f;

	for (const FVBFinisherViewLease& Lease : ViewLeases)
	{
		RestoreViewer(Lease, ReturnBlend);
	}

	// 복귀 블렌드가 끝난 뒤 파괴한다(즉시면 화면이 튄다). 슬로우에 늦지 않게 실시간으로 잰다.
	for (const FVBFinisherViewLease& Lease : ViewLeases)
	{
		AVBFinisherCamera* Camera = Lease.Camera;
		if (!Camera)
		{
			continue;
		}

		if (ReturnBlend > KINDA_SMALL_NUMBER)
		{
			Camera->SetLifeSpan(ReturnBlend + 0.1f);
		}
		else
		{
			Camera->Destroy();
		}
	}

	ViewLeases.Reset();

	if (bPushedTimeDilation)
	{
		if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(this))
		{
			TimeDilation->Pop(FinisherTimeDilationHandle);
		}

		bPushedTimeDilation = false;
	}

	PendingAttacker = nullptr;
	PendingConfig = nullptr;
	NextSlowSegment = 0;
	bRunning = false;
}

bool UVBFinisherDirectorSubsystem::IsTickable() const
{
	return bRunning && PendingConfig && NextSlowSegment < PendingConfig->TimeDilationSegments.Num();
}

TStatId UVBFinisherDirectorSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UVBFinisherDirectorSubsystem, STATGROUP_Tickables);
}

void UVBFinisherDirectorSubsystem::Tick(float /*DeltaTime*/)
{
	if (!IsTickable())
	{
		return;
	}

	const AActor* Attacker = PendingAttacker.Get();
	const UVBFinisherComponent* Finisher = UVBFinisherComponent::FindFor(Attacker);
	if (!Finisher)
	{
		// 시계가 사라졌다. 남은 구간을 접어 틱을 멈춘다 - 장면 종료(End)는 그쪽 신호가 따로 처리한다.
		VB_LOG(Warning, "처형 슬로우: 공격자 %s 의 처형 컴포넌트가 사라져 남은 구간 %d개를 건너뛴다",
			*GetNameSafe(Attacker), PendingConfig->TimeDilationSegments.Num() - NextSlowSegment);
		NextSlowSegment = PendingConfig->TimeDilationSegments.Num();
		return;
	}
	// 카메라 키와 같은 시계(AVBFinisherCamera::GetSceneTime). 재생 전이면 음수라 아무 구간도 안 온다.
	const float SceneTime = Finisher->GetPlaybackTime();
	const int32 Due = TakeDueSlowSegment(PendingConfig->TimeDilationSegments, NextSlowSegment, SceneTime);
	if (Due == INDEX_NONE)
	{
		return;
	}

	if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(this))
	{
		const FVBFinisherTimeDilationSegment& Segment = PendingConfig->TimeDilationSegments[Due];
		TimeDilation->PushTimed(FinisherTimeDilationHandle, Segment.Scale, Segment.RealDuration);
		bPushedTimeDilation = true;
		VB_LOG(Log, "처형 슬로우: 구간 %d (씬 %.3f초에 걸림, 지정 %.3f초) 배율 %.2f, %.2f초 - %s",
			Due, SceneTime, Segment.Time, Segment.Scale, Segment.RealDuration, *GetNameSafe(PendingConfig));
	}
}

int32 UVBFinisherDirectorSubsystem::TakeDueSlowSegment(const TArray<FVBFinisherTimeDilationSegment>& Segments, int32& NextIndex,
	float SceneTime)
{
	if (SceneTime < 0.0f)
	{
		return INDEX_NONE;
	}

	int32 Due = INDEX_NONE;
	while (Segments.IsValidIndex(NextIndex) && Segments[NextIndex].Time <= SceneTime)
	{
		Due = NextIndex++;
	}

	return Due;
}

void UVBFinisherDirectorSubsystem::SkipExpiredSlowSegments(const TArray<FVBFinisherTimeDilationSegment>& Segments,
	int32& NextIndex, float SceneTime)
{
	while (Segments.IsValidIndex(NextIndex)
		&& Segments[NextIndex].Time + Segments[NextIndex].Scale * Segments[NextIndex].RealDuration < SceneTime)
	{
		++NextIndex;
	}
}
