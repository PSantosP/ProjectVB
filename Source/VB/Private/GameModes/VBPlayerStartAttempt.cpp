// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "GameModes/VBPlayerStartAttempt.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Cinematic/VBCinematicStartupPolicy.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VB/VB.h"

FVBPlayerStartAttempt::FVBPlayerStartAttempt(const AActor& InMode, APlayerController* NewPlayer, UVBSaveGameSubsystem* InSave)
	: Mode(InMode)
	, Start(InSave ? InSave->GetGameStartContext() : FVBGameStartContext())
	, Save(InSave)
	, Controller(NewPlayer)
	, Pawn(IsValid(NewPlayer) ? NewPlayer->GetPawn() : nullptr)
	, World(InMode.GetWorld())
{
	Readiness = Pawn.IsValid() ? Pawn->FindComponentByClass<UVBPlayerReadinessComponent>() : nullptr;
}

bool FVBPlayerStartAttempt::IsCurrent() const
{
	return Save.IsValid() && Save->GetGameStartContext().AttemptId == Start.AttemptId
		&& World.IsValid() && !World->bIsTearingDown && Mode.GetWorld() == World.Get()
		&& Readiness.IsValid() && Pawn.IsValid() && !Pawn->IsActorBeingDestroyed()
		&& Controller.IsValid() && !Controller->IsActorBeingDestroyed()
		&& Controller->GetPawn() == Pawn.Get() && Pawn->GetController() == Controller.Get()
		&& Pawn->GetWorld() == World.Get();
}

void FVBPlayerStartAttempt::CompleteIfCurrent(bool bContinueDispatched)
{
	if (IsCurrent())
	{
		Readiness->CompleteInitialization(Start.AttemptId, bContinueDispatched);
	}
}

void FVBPlayerStartAttempt::Run(bool bPlayInitialCinematic, UVBCinematicDefinition* InitialCinematic)
{
	if (Readiness.IsValid())
	{
		Readiness->BeginInitialization(Start.AttemptId, Start.Intent);
	}

	// Continue 로 진입한 경우에만 적용한다. 새 게임이나 세이브 없음이면 표시가 없어 기본 상태를 유지한다.
	if (Save.IsValid() && Save->ConsumePendingContinueApply())
	{
		ApplyContinueSave();
	}
	else if (!TryStartInitialCinematic(bPlayInitialCinematic, InitialCinematic))
	{
		CompleteIfCurrent(false);
	}
}

bool FVBPlayerStartAttempt::TryStartInitialCinematic(bool bPlayInitialCinematic, UVBCinematicDefinition* InitialCinematic)
{
	if (!bPlayInitialCinematic || !Save.IsValid() || Start.Intent != EVBGameStartIntent::NewGame
		|| !Save->ConsumeInitialNewGameStart(Start.AttemptId))
	{
		return false;
	}

	if (UVBCinematicStartupPolicy* Policy = Mode.GetWorld()->GetSubsystem<UVBCinematicStartupPolicy>())
	{
		Policy->BeginNewGameCinematic(Controller.Get(), InitialCinematic, Start.AttemptId);
	}
	// 시작 거절도 완료가 아니다. 이 최초 시도의 초기화는 정책만 끝낸다.
	return true;
}

void FVBPlayerStartAttempt::ApplyContinueSave()
{
	APlayerController* NewPlayer = Controller.Get();
	if (!NewPlayer)
	{
		return;
	}
	// 초기화 알림이 동기로 폰을 바꾸거나 다른 게임을 시작할 수 있다.
	if (Save->GetGameStartContext().AttemptId != Start.AttemptId || NewPlayer->GetPawn() != Pawn.Get())
	{
		return;
	}

	// 저장 참여자가 각자 자기 구역을 적용한다. 빌드 복원이 상한을 알도록 Super(GE_InitStats) 뒤여야 한다.
	FVBSaveContext Context;
	Context.World = Mode.GetWorld();
	Context.PlayerController = NewPlayer;
	Context.Pawn = NewPlayer->GetPawn();
	Context.PlayerState = NewPlayer->PlayerState;
	const int32 Applied = Save->ApplyActiveSave(Context);
	// 수는 건넨 참여자 수라 "적용 완료" 라고 쓰지 않는다. 거절은 참여자가 각자 경고한다.
	VB_LOG(Log, "AVBGameMode: Continue 세이브를 참여자 %d 에게 건넸다", Applied);
	CompleteIfCurrent(Applied > 0);
}
