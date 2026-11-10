#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Character/VBWeaponVisualComponent.h"
#include "Character/VBHeroComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "UI/VBUIManagerSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ScopeExit.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VB/VB.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCinematicDefinition.h"
#include "VBCinematicPlaybackSubsystem.h"

const TCHAR* LexToString(EVBGameCinematicStartStatus Status)
{
	switch (Status)
	{
	case EVBGameCinematicStartStatus::Accepted: return TEXT("Accepted");
	case EVBGameCinematicStartStatus::Busy: return TEXT("Busy");
	case EVBGameCinematicStartStatus::InvalidContext: return TEXT("InvalidContext");
	case EVBGameCinematicStartStatus::SafetyUnavailable: return TEXT("SafetyUnavailable");
	case EVBGameCinematicStartStatus::CoreRejected: return TEXT("CoreRejected");
	}

	return TEXT("Unknown");
}

bool UVBCinematicDirectorSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UVBCinematicDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bStopping = false;
	Playback = Collection.InitializeDependency<UVBCinematicPlaybackSubsystem>();
	if (Playback.IsValid())
	{
		EndHandle = Playback->OnSessionEndedNative.AddUObject(this, &ThisClass::HandleSessionEnded);
	}
}

void UVBCinematicDirectorSubsystem::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

void UVBCinematicDirectorSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	Stop();
	Super::OnWorldEndPlay(InWorld);
}

void UVBCinematicDirectorSubsystem::Stop()
{
	if (bStopping)
	{
		return;
	}

	bStopping = true;
	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	if (Playback.IsValid())
	{
		Playback->OnSessionEndedNative.Remove(EndHandle);
	}

	if (RequestId.IsValid())
	{
		if (Playback.IsValid() && SessionId.IsValid())
		{
			Playback->Cancel(SessionId);
		}

		if (!EndResult.IsSet()) EndResult = FVBCinematicEndResult{
			SessionId, EVBCinematicEndType::Aborted, EVBCinematicEndReason::WorldEnded};
		CloseRequest(false);
	}

	Playback.Reset();
	AcquireSafety.Unbind();
	ProviderOwner.Reset();
	InPlayAcquireSafety.Unbind();
	InPlayProviderOwner.Reset();
	Receipts.Reset();
}

bool UVBCinematicDirectorSubsystem::InstallSafetyProvider(UObject* Owner, FVBCinematicAcquireSafety Provider)
{
	if (bStopping || bProcessing || RequestId.IsValid() || !IsValid(Owner) || !Provider.IsBound())
	{
		return false;
	}

	if (ProviderOwner.IsValid() && ProviderOwner.Get() != Owner)
	{
		return false;
	}

	ProviderOwner = Owner;
	AcquireSafety = MoveTemp(Provider);
	return true;
}

void UVBCinematicDirectorSubsystem::RemoveSafetyProvider(UObject* Owner)
{
	if (!Owner || ProviderOwner.Get() != Owner)
	{
		return;
	}

	ProviderOwner.Reset();
	AcquireSafety.Unbind();
	CancelActive();
}

bool UVBCinematicDirectorSubsystem::InstallInPlaySafetyProvider(UObject* Owner, FVBCinematicAcquireSafety Provider)
{
	if (bStopping || bProcessing || RequestId.IsValid() || !IsValid(Owner) || !Provider.IsBound())
	{
		return false;
	}

	if (InPlayProviderOwner.IsValid() && InPlayProviderOwner.Get() != Owner)
	{
		return false;
	}

	InPlayProviderOwner = Owner;
	InPlayAcquireSafety = MoveTemp(Provider);
	return true;
}

void UVBCinematicDirectorSubsystem::RemoveInPlaySafetyProvider(UObject* Owner)
{
	if (!Owner || InPlayProviderOwner.Get() != Owner)
	{
		return;
	}

	InPlayProviderOwner.Reset();
	InPlayAcquireSafety.Unbind();
	if (LeaseOwner.Get() == Owner)
	{
		CancelActive();
	}
}

bool UVBCinematicDirectorSubsystem::HasLeaseOwner() const
{
	return Lease ? LeaseOwner.IsValid() : (ProviderOwner.IsValid() && AcquireSafety.IsBound()) || (InPlayProviderOwner.IsValid()
		&& InPlayAcquireSafety.IsBound());
}

TSharedPtr<FVBCinematicGameLease> UVBCinematicDirectorSubsystem::AcquireLease(APlayerController* PC, FGuid Attempt)
{
	// 특별한 장면의 정책이 먼저, 그것이 이 요청에 줄 것이 없으면 진행 중 장면의 정책. 대리자는 복사해 부른다 - 부르는 동안 바뀔 수 있다.
	const FVBCinematicAcquireSafety Override = AcquireSafety;
	if (ProviderOwner.IsValid() && Override.IsBound())
	{
		if (TSharedPtr<FVBCinematicGameLease> Granted = Override.Execute(PC, Attempt))
		{
			LeaseOwner = ProviderOwner;
			return Granted;
		}
	}

	const FVBCinematicAcquireSafety InPlay = InPlayAcquireSafety;
	if (InPlayProviderOwner.IsValid() && InPlay.IsBound())
	{
		if (TSharedPtr<FVBCinematicGameLease> Granted = InPlay.Execute(PC, Attempt))
		{
			LeaseOwner = InPlayProviderOwner;
			return Granted;
		}
	}

	return nullptr;
}

FVBGameCinematicStartResult UVBCinematicDirectorSubsystem::RequestPlay(UVBCinematicDefinition* Definition,
	APlayerState* Instigator, FName PlayerBindingTag, const TArray<FInstancedStruct>& Effects, AActor* Counterpart,
	FVBOnDeferredEffectClosed OnClosed, const TArray<FInstancedStruct>& AfterHandoffEffects)
{
	FVBGameCinematicStartResult Result;
	if (bProcessing || RequestId.IsValid())
	{
		Result.Status = EVBGameCinematicStartStatus::Busy;
		return Result;
	}

	UWorld* World = GetWorld();
	APlayerController* PC = IsValid(Instigator) ? Instigator->GetPlayerController() : nullptr;
	APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
	UVBSaveGameSubsystem* Save = IsValid(Pawn) ? UVBSaveGameSubsystem::Get(Pawn) : nullptr;
	if (bStopping || !IsValid(World) || World->bIsTearingDown || World->GetNetMode() == NM_Client
		|| !IsValid(Definition) || !IsValid(PC) || !PC->IsLocalController() || !IsValid(Pawn)
		|| Pawn->GetWorld() != World || Pawn->GetPlayerState() != Instigator || !Save
		|| (Counterpart && (!IsValid(Counterpart) || Counterpart->GetWorld() != World))
		|| !Save->GetGameStartContext().AttemptId.IsValid() || !Playback.IsValid())
	{
		return Result;
	}

	for (const TArray<FInstancedStruct>* List : {&Effects, &AfterHandoffEffects})
	{
		for (const FInstancedStruct& Effect : *List)
		{
			if (!Effect.GetPtr<FVBEffect>())
			{
				return Result;
			}
		}
	}

	if (!HasLeaseOwner())
	{
		Result.Status = EVBGameCinematicStartStatus::SafetyUnavailable;
		return Result;
	}

	TStrongObjectPtr<ThisClass> KeepAlive(this);
	ON_SCOPE_EXIT { if (!RequestId.IsValid()) ReleaseRequestData(); };
	TGuardValue<bool> Processing(bProcessing, true);
	RequestId = FGuid::NewGuid();
	const FGuid ThisRequest = RequestId;
	AttemptId = Save->GetGameStartContext().AttemptId;
	ActivePawn = Pawn;
	ActiveController = PC;
	ActiveInstigator = Instigator;
	ActiveCounterpart = Counterpart;
	bHadCounterpart = Counterpart != nullptr;
	ActiveDefinition = Definition;
	CompletionEffects = Effects;
	HandoffEffects = AfterHandoffEffects;
	if (!IsContextCurrent())
	{
		CloseRequest(false);
		return Result;
	}

	TSharedPtr<FVBCinematicGameLease> AcquiredLease = AcquireLease(PC, AttemptId);
	if (RequestId != ThisRequest || !IsContextCurrent())
	{
		if (AcquiredLease)
		{
			AcquiredLease->Finish(false);
		}

		CloseRequest(false);
		return Result;
	}

	Lease = MoveTemp(AcquiredLease);
	const TSharedPtr<FVBCinematicGameLease> Guard = Lease;
	const bool bHeld = Guard && Guard->IsSaveHeld();
	if (!bHeld || RequestId != ThisRequest || !IsContextCurrent())
	{
		CloseRequest(false);
		Result.Status = EVBGameCinematicStartStatus::SafetyUnavailable;
		return Result;
	}

	if (Definition->bCanSkip)
	{
		const UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Pawn);
		const UInputAction* Action = Hero ? Hero->FindInputAction(VBGameplayTags::Input_Interact) : nullptr;
		if (!Action || !Guard->BindSkipInput(Action))
		{
			VB_LOG(Error, "Cinematic %s has no usable interaction input for hold-to-skip", *GetNameSafe(Definition));
			CloseRequest(false);
			Result.Status = EVBGameCinematicStartStatus::SafetyUnavailable;
			return Result;
		}
	}

	if (!AcquireWeaponPresentation(*Pawn, *Definition) || !AcquireHUDPresentation(*Pawn, *Definition))
	{
		CloseRequest(false);
		Result.Status = EVBGameCinematicStartStatus::SafetyUnavailable;
		return Result;
	}

	TArray<FVBCinematicBinding> Bindings;
	if (!PlayerBindingTag.IsNone())
	{
		FVBCinematicBinding Binding;
		Binding.Tag = PlayerBindingTag;
		Binding.Actors.Add(Pawn);
		Bindings.Add(MoveTemp(Binding));
	}

	Result.Core = Playback->TryPlay(Definition, Bindings);
	if (Result.Core.Status != EVBCinematicStartStatus::Accepted)
	{
		CloseRequest(false);
		Result.Status = EVBGameCinematicStartStatus::CoreRejected;
		return Result;
	}

	SessionId = Result.Core.SessionId;
	if (RequestId != ThisRequest || !IsContextCurrent())
	{
		if (Playback.IsValid())
		{
			Playback->Cancel(Result.Core.SessionId);
		}

		CloseRequest(false);
		return Result;
	}

	Result.Status = EVBGameCinematicStartStatus::Accepted;
	bAdmitted = true;
	ActiveOnClosed = MoveTemp(OnClosed);
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ThisClass::TickHandoff));
	return Result;
}

bool UVBCinematicDirectorSubsystem::IsContextCurrent() const
{
	const APawn* Pawn = ActivePawn.Get();
	const APlayerController* PC = ActiveController.Get();
	const UWorld* World = GetWorld();
	if (bStopping || !RequestId.IsValid() || !IsValid(World) || World->bIsTearingDown
		|| !HasLeaseOwner() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed()
		|| !IsValid(PC) || PC->IsActorBeingDestroyed() || Pawn->GetWorld() != World
		|| PC->GetWorld() != World
		|| PC->GetPawn() != Pawn || Pawn->GetController() != PC
		|| !ActiveInstigator.IsValid() || Pawn->GetPlayerState() != ActiveInstigator.Get()
		|| (bHadCounterpart && (!ActiveCounterpart.IsValid() || ActiveCounterpart->IsActorBeingDestroyed())))
	{
		return false;
	}

	const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(Pawn);
	const FVBPlayerPreparationSnapshot Preparation = UVBPlayerReadinessComponent::QueryForPawn(Pawn);
	return Save && Save->GetGameStartContext().AttemptId == AttemptId
		&& Preparation.InitializationId == AttemptId && Preparation.State != EVBPlayerPreparationState::Unavailable;
}

void UVBCinematicDirectorSubsystem::HandleSessionEnded(const FVBCinematicEndResult& Result)
{
	if (!bStopping && RequestId.IsValid() && Result.SessionId == SessionId && !EndResult.IsSet())
	{
		EndResult = Result;
	}
}

bool UVBCinematicDirectorSubsystem::TickHandoff(float DeltaTime)
{
	if (bStopping || !RequestId.IsValid())
	{
		TickerHandle.Reset();
		return false;
	}

	if (bProcessing)
	{
		return true;
	}

	TStrongObjectPtr<ThisClass> KeepAlive(this);
	ON_SCOPE_EXIT { if (!RequestId.IsValid()) ReleaseRequestData(); };
	TGuardValue<bool> Processing(bProcessing, true);
	const FGuid ThisRequest = RequestId;
	const TSharedPtr<FVBCinematicGameLease> Guard = Lease;
	const bool bHeld = Guard && Guard->IsSaveHeld();
	if (!bHeld || RequestId != ThisRequest || !IsContextCurrent())
	{
		if (Playback.IsValid() && SessionId.IsValid())
		{
			Playback->Cancel(SessionId);
		}

		CloseRequest(false);
		TickerHandle.Reset();
		return false;
	}

	if (!EndResult.IsSet() && ActiveDefinition && ActiveDefinition->bCanSkip
		&& Guard->ConsumeSkipRequest(ActiveDefinition->SkipHoldSeconds))
	{
		if (!SkipPlayback())
		{
			VB_LOG(Warning, "Cinematic %s refused the held skip request", *GetNameSafe(ActiveDefinition));
		}
	}

	if (!EndResult.IsSet())
	{
		return true;
	}

	if (EndResult->Type != EVBCinematicEndType::Completed && EndResult->Type != EVBCinematicEndType::Skipped)
	{
		CloseRequest(false); TickerHandle.Reset(); return false;
	}

	if (!bRestoreRequested)
	{
		bRestoreRequested = true;
		Guard->RestoreGameplay();
	}

	if (RequestId != ThisRequest || !IsContextCurrent())
	{
		CloseRequest(false); TickerHandle.Reset(); return false;
	}

	const bool bRestored = Guard->IsGameplayRestored();
	if (RequestId != ThisRequest || !IsContextCurrent())
	{
		CloseRequest(false); TickerHandle.Reset(); return false;
	}

	if (!bRestored || UVBPlayerReadinessComponent::QueryForPawn(ActivePawn.Get()).State != EVBPlayerPreparationState::Ready)
	{
		return true;
	}

	bRestorationConfirmed = true;
	// 동기 Stop 이 와도 바깥 효과 스택이 풀릴 때까지 reflected 데이터를 붙잡아 둔다.
	const VBRules::EApplyResult Applied = VBRules::ApplyAllUntilFailure(CompletionEffects, [&](FVBRuleContext& Context)
	{
		if (!Guard->IsSaveHeld() || RequestId != ThisRequest || !IsContextCurrent())
		{
			return false;
		}

		Context = FVBRuleContext::Make(this, ActiveInstigator.Get());
		Context.Counterpart = ActiveCounterpart.Get();
		return Context.CanApplyEffects();
	});
	if (Applied == VBRules::EApplyResult::Interrupted)
	{
		CloseRequest(false); TickerHandle.Reset(); return false;
	}
	// 완료 효과가 해내지 못했으면 복귀는 성공으로 끝내되 장면은 성공으로 알리지 않는다 - 영수증, 영역 OnceFact 가 없어 다시 시도된다.
	const bool bEffectsLanded = Applied == VBRules::EApplyResult::Landed;
	if (!bEffectsLanded)
	{
		VB_LOG(Warning, "시네마틱 %s: 완료 효과가 해내지 못했다 - 장면을 성공으로 알리지 않는다", *GetNameSafe(ActiveDefinition));
	}

	const bool bStillHeld = Guard->IsSaveHeld();
	CloseRequest(bStillHeld && RequestId == ThisRequest && IsContextCurrent(), bEffectsLanded);
	TickerHandle.Reset();
	return false;
}

void UVBCinematicDirectorSubsystem::CloseRequest(bool bSucceeded, bool bEffectsLanded)
{
	if (!RequestId.IsValid())
	{
		return;
	}

	bSucceeded = bSucceeded && bRestorationConfirmed;
	const FVBCinematicEndResult Result = EndResult.Get(FVBCinematicEndResult{
		SessionId, EVBCinematicEndType::Aborted, EVBCinematicEndReason::Cancelled});
	const TWeakObjectPtr<APawn> Pawn = ActivePawn;
	const TWeakObjectPtr<APlayerController> Controller = ActiveController;
	const TWeakObjectPtr<APlayerState> Instigator = ActiveInstigator;
	const TWeakObjectPtr<UVBCinematicDefinition> Definition = ActiveDefinition;
	const FGuid CompletedAttempt = AttemptId;
	const bool bWasAdmitted = bAdmitted;
	TSharedPtr<FVBCinematicGameLease> FinishedLease = MoveTemp(Lease);
	LeaseOwner.Reset();
	RequestId.Invalidate();
	SessionId.Invalidate();
	EndResult.Reset();
	bRestoreRequested = false;
	bAdmitted = false;
	bRestorationConfirmed = false;
	if (FinishedLease)
	{
		FinishedLease->Finish(bSucceeded);
	}

	bSucceeded = bSucceeded && bEffectsLanded && !bStopping && Definition.IsValid() && Pawn.IsValid()
		&& !Pawn->IsActorBeingDestroyed() && Controller.IsValid() && !Controller->IsActorBeingDestroyed()
		&& Pawn->GetWorld() == GetWorld() && Pawn->GetController() == Controller.Get()
		&& Controller->GetPawn() == Pawn.Get() && Instigator.IsValid() && Pawn->GetPlayerState() == Instigator.Get();
	if (bSucceeded)
	{
		const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(Pawn.Get());
		// 완료 효과가 새 UI/상호작용 보류를 만들 수 있다. 복귀는 효과 전에 확인했다.
		bSucceeded = Save && Save->GetGameStartContext().AttemptId == CompletedAttempt;
	}

	if (bSucceeded && Definition.IsValid()) Receipts.Add(Definition, {Pawn, CompletedAttempt});
	if (bWasAdmitted && Result.SessionId.IsValid())
	{
		OnHandoffEnded.Broadcast(Result, bSucceeded);
	}

	const FVBOnDeferredEffectClosed Closed = MoveTemp(ActiveOnClosed);
	ActiveOnClosed.Unbind();
	Closed.ExecuteIfBound(bSucceeded);
	if (bSucceeded && !HandoffEffects.IsEmpty())
	{
		const VBRules::EApplyResult Applied = VBRules::ApplyAllUntilFailure(HandoffEffects,
			[this](FVBRuleContext& Context) { return PrepareHandoffContext(Context); });
		if (Applied != VBRules::EApplyResult::Landed)
		{
			VB_LOG(Error, "시네마틱 복귀 후 효과를 적용하지 못했다: %s", *GetNameSafe(Definition.Get()));
		}
	}
	if (!bProcessing)
	{
		ReleaseRequestData();
	}
}

bool UVBCinematicDirectorSubsystem::PrepareHandoffContext(FVBRuleContext& Context) const
{
	APawn* Pawn = ActivePawn.Get();
	APlayerController* PC = ActiveController.Get();
	APlayerState* State = ActiveInstigator.Get();
	UWorld* World = GetWorld();
	AActor* Counterpart = ActiveCounterpart.Get();
	if (bStopping || !IsValid(World) || World->bIsTearingDown || World->GetNetMode() == NM_Client
		|| !IsValid(ActiveDefinition) || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed() || !Pawn->HasAuthority()
		|| !IsValid(PC) || PC->IsActorBeingDestroyed() || !PC->IsLocalController()
		|| Pawn->GetWorld() != World || PC->GetWorld() != World || Pawn->GetController() != PC || PC->GetPawn() != Pawn
		|| !IsValid(State) || State->IsActorBeingDestroyed() || State->GetWorld() != World
		|| Pawn->GetPlayerState() != State || PC->PlayerState != State
		|| (bHadCounterpart && (!IsValid(Counterpart) || Counterpart->IsActorBeingDestroyed() || Counterpart->GetWorld() != World)))
	{
		return false;
	}

	const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(Pawn);
	if (!Save || Save->GetGameStartContext().AttemptId != AttemptId)
	{
		return false;
	}

	Context = FVBRuleContext::Make(this, State);
	Context.Counterpart = Counterpart;
	return Context.CanApplyEffects();
}

void UVBCinematicDirectorSubsystem::ReleaseRequestData()
{
	const FGuid HUDToken = HUDPresentationToken;
	TWeakObjectPtr<UVBUIManagerSubsystem> UIOwner = HUDPresentationOwner;
	HUDPresentationToken.Invalidate();
	HUDPresentationOwner.Reset();
	if (UVBWeaponVisualComponent* Visuals = WeaponPresentationOwner.Get())
	{
		Visuals->EndPresentationHide(WeaponPresentationToken);
	}

	WeaponPresentationOwner.Reset();
	WeaponPresentationToken.Invalidate();
	CompletionEffects.Reset();
	HandoffEffects.Reset();
	ActiveDefinition = nullptr;
	ActivePawn.Reset();
	ActiveController.Reset();
	ActiveInstigator.Reset();
	ActiveCounterpart.Reset();
	AttemptId.Invalidate();
	bHadCounterpart = false;
	if (UVBUIManagerSubsystem* UI = UIOwner.Get())
	{
		UI->EndHUDHide(HUDToken);
	}
}

bool UVBCinematicDirectorSubsystem::AcquireHUDPresentation(APawn& Pawn, const UVBCinematicDefinition& Definition)
{
	if (!Definition.PlaybackSettings.bHideHud)
	{
		return true;
	}
	APlayerController* PC = Cast<APlayerController>(Pawn.GetController());
	UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(PC);
	const FGuid Token = UI && PC ? UI->BeginHUDHide(*PC) : FGuid();
	if (!Token.IsValid())
	{
		return false;
	}
	if (!IsContextCurrent())
	{
		UI->EndHUDHide(Token);
		return false;
	}
	HUDPresentationOwner = UI;
	HUDPresentationToken = Token;
	return true;
}

bool UVBCinematicDirectorSubsystem::AcquireWeaponPresentation(APawn& Pawn, const UVBCinematicDefinition& Definition)
{
	if (!Definition.bHideHeldWeaponVisuals)
	{
		return true;
	}

	UVBWeaponVisualComponent* Visuals = Pawn.FindComponentByClass<UVBWeaponVisualComponent>();
	if (!Visuals)
	{
		VB_LOG(Error, "Cinematic %s requires a weapon visual owner on %s", *GetNameSafe(&Definition), *Pawn.GetName());
		return false;
	}

	WeaponPresentationToken = Visuals->BeginPresentationHide();
	if (!WeaponPresentationToken.IsValid())
	{
		VB_LOG(Error, "Cinematic %s could not hold weapon visuals on %s", *GetNameSafe(&Definition), *Pawn.GetName());
		return false;
	}

	WeaponPresentationOwner = Visuals;
	return true;
}

bool UVBCinematicDirectorSubsystem::HasCompletedFor(const APawn* Pawn, const UVBCinematicDefinition* Definition) const
{
	if (bStopping || !IsValid(Pawn) || !IsValid(Definition) || Pawn->GetWorld() != GetWorld())
	{
		return false;
	}

	const FCompletionReceipt* Receipt = Receipts.Find(
		TWeakObjectPtr<UVBCinematicDefinition>(const_cast<UVBCinematicDefinition*>(Definition)));
	const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(Pawn);
	return Receipt && Receipt->Pawn.Get() == Pawn && Save && Save->GetGameStartContext().AttemptId == Receipt->AttemptId;
}

bool UVBCinematicDirectorSubsystem::SkipActive()
{
	return !bStopping && !bProcessing && SkipPlayback();
}

bool UVBCinematicDirectorSubsystem::SkipPlayback()
{
	return !bStopping && Playback.IsValid() && SessionId.IsValid() && !EndResult.IsSet() && Playback->Skip(SessionId);
}

bool UVBCinematicDirectorSubsystem::CancelActive()
{
	if (bStopping || bProcessing || !RequestId.IsValid())
	{
		return false;
	}

	if (!EndResult.IsSet() && Playback.IsValid() && SessionId.IsValid())
	{
		return Playback->Cancel(SessionId);
	}

	TStrongObjectPtr<ThisClass> KeepAlive(this);
	ON_SCOPE_EXIT { if (!RequestId.IsValid()) ReleaseRequestData(); };
	TGuardValue<bool> Processing(bProcessing, true);
	if (!EndResult.IsSet()) EndResult = FVBCinematicEndResult{
		SessionId, EVBCinematicEndType::Aborted, EVBCinematicEndReason::Cancelled};
	CloseRequest(false);
	FTSTicker::RemoveTicker(TickerHandle);
	TickerHandle.Reset();
	return true;
}
