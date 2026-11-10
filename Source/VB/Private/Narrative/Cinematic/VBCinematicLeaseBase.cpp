#include "Narrative/Cinematic/VBCinematicLeaseBase.h"

#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "InputAction.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UI/VBUIManagerSubsystem.h"

FVBCinematicLeaseBase::FVBCinematicLeaseBase(UObject* InOwner, APlayerController* InController, UVBSaveGameSubsystem* InSave)
	: Owner(InOwner), Controller(InController), Pawn(InController ? InController->GetPawn() : nullptr), Save(InSave)
{
	Readiness = Pawn.IsValid() ? Pawn->FindComponentByClass<UVBPlayerReadinessComponent>() : nullptr;
}

FVBCinematicLeaseBase::~FVBCinematicLeaseBase()
{
	RemoveInputGuard();
}

bool FVBCinematicLeaseBase::IsCurrent() const
{
	const APawn* CurrentPawn = Pawn.Get();
	const APlayerController* PC = Controller.Get();
	const UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr;
	return IsValid(World) && !World->bIsTearingDown && IsValid(CurrentPawn) && !CurrentPawn->IsActorBeingDestroyed()
		&& IsValid(PC) && !PC->IsActorBeingDestroyed() && PC->IsLocalController() && CurrentPawn->HasAuthority()
		&& CurrentPawn->GetWorld() == World && PC->GetWorld() == World
		&& PC->GetPawn() == CurrentPawn && CurrentPawn->GetController() == PC && Save.IsValid() && Readiness.IsValid();
}

bool FVBCinematicLeaseBase::IsFor(const APlayerController* PC) const
{
	return Controller.Get() == PC;
}

bool FVBCinematicLeaseBase::IsSaveHeld() const
{
	return Save.IsValid() && Save->IsSaveHoldActive(SaveHold);
}

bool FVBCinematicLeaseBase::IsGameplayRestored() const
{
	return bRestored && IsCurrent() && GameplayViewTarget.IsValid() && Controller->GetViewTarget() == GameplayViewTarget.Get()
		&& Readiness->Query().State == EVBPlayerPreparationState::Ready;
}

void FVBCinematicLeaseBase::PushInputGuard()
{
	InputGuard.Reset(NewObject<UEnhancedInputComponent>(Controller.Get(), NAME_None, RF_Transient));
	InputGuard->Priority = MAX_int32;
	InputGuard->bBlockInput = true;
	InputGuard->RegisterComponent();
	Controller->PushInputComponent(InputGuard.Get());
}

void FVBCinematicLeaseBase::RemoveInputGuard()
{
	if (SkipHold.IsValid())
	{
		SkipHold->Release();
	}

	SkipHold.Reset();
	SkipAction.Reset();

	if (!InputGuard.IsValid())
	{
		return;
	}

	if (Controller.IsValid())
	{
		Controller->PopInputComponent(InputGuard.Get());
	}

	InputGuard->DestroyComponent();
	InputGuard.Reset();
}

bool FVBCinematicLeaseBase::BindSkipInput(const UInputAction* Action)
{
	if (!Action || !InputGuard.IsValid() || SkipHold.IsValid())
	{
		return false;
	}

	SkipHold = MakeShared<FVBCinematicSkipHold>();
	SkipAction = Action;
	const TSharedRef<FVBCinematicSkipHold> State = SkipHold.ToSharedRef();
	InputGuard->BindActionInstanceLambda(Action, ETriggerEvent::Started,
		[State](const FInputActionInstance&)
		{
			State->Press(FPlatformTime::Seconds());
		});
	for (ETriggerEvent Event : {ETriggerEvent::Completed, ETriggerEvent::Canceled})
	{
		InputGuard->BindActionInstanceLambda(Action, Event,
			[State](const FInputActionInstance&)
			{
				State->Release();
			});
	}

	InputGuard->BindActionValue(Action);
	return true;
}

bool FVBCinematicLeaseBase::ConsumeSkipRequest(float HoldSeconds)
{
	return SkipHold.IsValid() && InputGuard.IsValid() && SkipAction.IsValid()
		&& SkipHold->Consume(FPlatformTime::Seconds(), HoldSeconds,
			InputGuard->GetBoundActionValue(SkipAction.Get()).IsNonZero());
}

bool FVBCinematicLeaseBase::IsReadyToRestore() const
{
	if (bFinished || bRestored || !bRestoreRequested || !IsCurrent())
	{
		return false;
	}

	UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(Controller.Get());
	const FVBUIInputAvailability Input = UI ? UI->QueryInputAvailability(Controller.Get()) : FVBUIInputAvailability{};
	return Input.bAvailable && !Input.bBlocked && GameplayViewTarget.IsValid()
		&& Controller->GetViewTarget() == GameplayViewTarget.Get()
		&& Readiness->CanFinishPreparationHold(PreparationHold);
}
