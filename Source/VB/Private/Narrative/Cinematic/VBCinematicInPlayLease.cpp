#include "Narrative/Cinematic/VBCinematicInPlayLease.h"

#include "GameFramework/PlayerController.h"
#include "SaveSystem/VBSaveGameSubsystem.h"

FVBInPlayCinematicLease::FVBInPlayCinematicLease(UObject* InOwner, APlayerController* InController, UVBSaveGameSubsystem* InSave)
	: FVBCinematicLeaseBase(InOwner, InController, InSave)
{
}

FVBInPlayCinematicLease::~FVBInPlayCinematicLease()
{
	ReleaseHolds();
}

bool FVBInPlayCinematicLease::Acquire()
{
	return IsCurrent() && Readiness->Query().State == EVBPlayerPreparationState::Ready && TakeHolds();
}

bool FVBInPlayCinematicLease::TakeHolds()
{
	SaveHold = Save->BeginSaveHold(TEXT("InPlayCinematic"));
	if (!IsSaveHeld())
	{
		return false;
	}

	PushInputGuard();
	PreparationHold = Readiness->AcquirePreparationHold(Owner.Get(), TEXT("InPlayCinematic"));
	GameplayViewTarget = Controller->GetViewTarget();
	if (!PreparationHold.Id.IsValid() || !GameplayViewTarget.IsValid() || !IsCurrent())
	{
		ReleaseHolds();
		return false;
	}

	return true;
}

void FVBInPlayCinematicLease::TickRestore()
{
	if (!IsReadyToRestore())
	{
		return;
	}

	RemoveInputGuard();
	Readiness->ReleasePreparationHold(PreparationHold);
	PreparationHold = FVBPlayerPreparationHoldToken();
	bRestored = true;
}

void FVBInPlayCinematicLease::Finish(bool bSucceeded)
{
	if (bFinished)
	{
		return;
	}

	bFinished = true;
	ReleaseHolds();
}

void FVBInPlayCinematicLease::ReleaseHolds()
{
	RemoveInputGuard();
	if (PreparationHold.Id.IsValid() && Readiness.IsValid())
	{
		Readiness->ReleasePreparationHold(PreparationHold);
	}

	PreparationHold = FVBPlayerPreparationHoldToken();
	if (SaveHold.IsValid() && Save.IsValid())
	{
		Save->EndSaveHold(SaveHold);
	}

	SaveHold = FVBSaveHoldToken();
}
