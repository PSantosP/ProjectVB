#include "Narrative/Cinematic/VBCinematicStartupLease.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/PlayerState.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "VBCinematicDefinition.h"
#include "VB/VB.h"

bool VBCinematicStartup::ValidateDefinition(const UVBCinematicDefinition* Definition)
{
	if (!IsValid(Definition))
	{
		VB_LOG(Error, "새 게임 시네마틱 정의 거절: 정의가 없다");
		return false;
	}

	TArray<FText> Problems;
	const bool bValid = Definition->ValidateSettings(Problems);
	for (const FText& Problem : Problems)
	{
		VB_LOG(Error, "새 게임 시네마틱 정의 거절: %s", *Problem.ToString());
	}

	return bValid;
}

FVBStartupLease::FVBStartupLease(UObject* InOwner, APlayerController* InController, UVBSaveGameSubsystem* InSave, FGuid InAttempt)
	: FVBCinematicLeaseBase(InOwner, InController, InSave), Attempt(InAttempt)
{
}

bool FVBStartupLease::Initialize(const UVBCinematicDefinition* Definition)
{
	if (!VBCinematicStartup::ValidateDefinition(Definition) || !IsCurrent())
	{
		return false;
	}

	SaveHold = Save->BeginSaveHold(TEXT("StartupCinematic"));
	if (!IsCurrent())
	{
		return false;
	}

	PushInputGuard();
	PreparationHold = Readiness->AcquirePreparationHold(Owner.Get(), TEXT("StartupCinematic"));
	return IsCurrent() && PreparationHold.Id.IsValid();
}

bool FVBStartupLease::Matches(APlayerController* PC, FGuid Id) const
{
	return IsFor(PC) && Attempt == Id;
}

bool FVBStartupLease::IsCurrent() const
{
	return FVBCinematicLeaseBase::IsCurrent() && Save->GetGameStartContext().AttemptId == Attempt
		&& Save->GetGameStartContext().Intent == EVBGameStartIntent::NewGame
		&& Readiness->Query().InitializationId == Attempt;
}

bool FVBStartupLease::PrepareToPlay()
{
	if (bFinished || !IsCurrent() || !InputGuard.IsValid() || !Controller->IsInputComponentInStack(InputGuard.Get()))
	{
		return false;
	}

	if (!SaveHold.IsValid())
	{
		SaveHold = Save->BeginSaveHold(TEXT("StartupCinematic"));
	}

	if (!IsSaveHeld() || !IsCurrent())
	{
		return false;
	}

	const FVBPlayerPreparationSnapshot Preparation = Readiness->Query();
	const uint32 ExpectedBlocks = static_cast<uint32>(EVBPlayerPreparationReason::Initialization)
		| static_cast<uint32>(EVBPlayerPreparationReason::ExternalHold);
	if (Preparation.UnavailableReasons || (Preparation.BlockedReasons & ~ExpectedBlocks))
	{
		return false;
	}

	if (!IsValid(Controller->PlayerCameraManager) || !IsValid(Controller->PlayerInput)
		|| !IsValid(Pawn->InputComponent) || !Pawn->InputComponent->IsRegistered())
	{
		return false;
	}

	GameplayViewTarget = Controller->GetViewTarget();
	return GameplayViewTarget.IsValid() && GameplayViewTarget->GetWorld() == Owner->GetWorld();
}

void FVBStartupLease::TickRestore()
{
	if (!IsSaveHeld() || !IsReadyToRestore())
	{
		return;
	}

	RemoveInputGuard();
	if (!IsCurrent())
	{
		return;
	}

	bRestored = true;
	Readiness->ReleasePreparationHold(PreparationHold);
}

void FVBStartupLease::CompleteInitialization()
{
	if (IsCurrent())
	{
		Readiness->CompleteInitialization(Attempt, false);
	}
}

APlayerState* FVBStartupLease::GetInstigator() const
{
	return IsCurrent() ? Pawn->GetPlayerState() : nullptr;
}

void FVBStartupLease::Finish(bool bSucceeded)
{
	if (bFinished)
	{
		return;
	}

	bFinished = true;
	// 복귀를 확인한 뒤 완료 효과가 새 보류를 만들더라도 자기 저장 hold는 끝낸다.
	if (bSucceeded && bRestored && IsCurrent() && IsSaveHeld())
	{
		Save->EndSaveHold(SaveHold);
	}
	// 실패의 GI 저장 보류는 새 게임/성공 로드만 폐기한다. 월드 종료는 입력 가드만 정리한다.
}
