#include "Player/VBPlayerReadinessComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"

#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBPawnExtensionComponent.h"
#include "Engine/World.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "UI/VBUIManagerSubsystem.h"

namespace
{
	uint32 Bit(EVBPlayerPreparationReason Reason) { return static_cast<uint32>(Reason); }
}

UVBPlayerReadinessComponent::UVBPlayerReadinessComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

EVBPlayerPreparationState UVBPlayerReadinessComponent::EvaluateReasons(uint32 Blocked, uint32 Unavailable)
{
	return Unavailable ? EVBPlayerPreparationState::Unavailable
		: Blocked ? EVBPlayerPreparationState::Blocked : EVBPlayerPreparationState::Ready;
}

FVBPlayerPreparationSnapshot UVBPlayerReadinessComponent::QueryForPawn(const APawn* Pawn)
{
	if (IsValid(Pawn))
	{
		if (const UVBPlayerReadinessComponent* Component = Pawn->FindComponentByClass<UVBPlayerReadinessComponent>())
		{
			return Component->Query();
		}
	}

	FVBPlayerPreparationSnapshot Result;
	Result.UnavailableReasons = Bit(EVBPlayerPreparationReason::Pawn);
	return Result;
}

FVBPlayerPreparationSnapshot UVBPlayerReadinessComponent::Query() const
{
	FVBPlayerPreparationSnapshot Result;
	Result.InitializationId = InitializationId;
	Result.Revision = Revision;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const UWorld* World = GetWorld();
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	AddActorReasons(Pawn, World, PC, Result);
	AddInitializationReasons(Result);
	AddSaveReasons(Pawn, World, Result);
	AddRestoreReasons(Pawn, Result);
	AddAbilitySystemReasons(Pawn, Result);
	AddUIReasons(PC, Result);
	AddHoldReasons(Result);
	Result.State = EvaluateReasons(Result.BlockedReasons, Result.UnavailableReasons);
	return Result;
}

void UVBPlayerReadinessComponent::AddActorReasons(const APawn* Pawn, const UWorld* World, const APlayerController* PC,
	FVBPlayerPreparationSnapshot& Result) const
{
	if (bEnding || !IsRegistered() || !IsValid(Pawn) || Pawn->IsActorBeingDestroyed())
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::Pawn);
	}

	if (!IsValid(World) || World->bIsTearingDown)
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::World);
	}

	if (!IsValid(PC) || PC->GetPawn() != Pawn)
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::Controller);
	}

	if (!Pawn || !Pawn->HasAuthority() || !PC || !PC->IsLocalController())
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::Authority);
	}
}

void UVBPlayerReadinessComponent::AddInitializationReasons(FVBPlayerPreparationSnapshot& Result) const
{
	if (!InitializationId.IsValid() || StartIntent == EVBGameStartIntent::Unknown)
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::Initialization);
	}
	else if (!bInitializationEnded)
	{
		Result.BlockedReasons |= Bit(EVBPlayerPreparationReason::Initialization);
	}
	else if (StartIntent == EVBGameStartIntent::Continue && !bContinueDispatched)
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::Initialization);
	}
}

void UVBPlayerReadinessComponent::AddSaveReasons(const APawn* Pawn, const UWorld* World, FVBPlayerPreparationSnapshot& Result) const
{
	const UVBSaveGameSubsystem* CurrentSave = IsValid(Pawn) ? UVBSaveGameSubsystem::Get(Pawn) : nullptr;
	if (!CurrentSave || CurrentSave->GetGameStartContext().AttemptId != InitializationId)
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::SaveSource);
	}

	const UVBSaveParticipantSubsystem* CurrentRegistry = World ? World->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr;
	if (!CurrentRegistry)
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::SaveSource);
	}
	else if (CurrentRegistry->IsApplyingSave())
	{
		Result.BlockedReasons |= Bit(EVBPlayerPreparationReason::SaveApplication);
	}
}

void UVBPlayerReadinessComponent::AddRestoreReasons(const APawn* Pawn, FVBPlayerPreparationSnapshot& Result) const
{
	const UVBPlayerRestoreComponent* CurrentRestore = IsValid(Pawn) ? Pawn->FindComponentByClass<UVBPlayerRestoreComponent>() : nullptr;
	if (!CurrentRestore || !CurrentRestore->IsRegistered())
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::RestoreSource);
	}
	else
	{
		const FVBPlayerRestoreReceipt& Receipt = CurrentRestore->GetRestoreReceipt();
		if (CurrentRestore->IsRestoring())
		{
			Result.BlockedReasons |= Bit(EVBPlayerPreparationReason::Restore);
		}

		if (Receipt.InitializationId != InitializationId)
		{
			Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::RestoreSource);
		}
		else if (Receipt.Outcome == EVBPlayerRestoreOutcome::InProgress)
		{
			Result.BlockedReasons |= Bit(EVBPlayerPreparationReason::Restore);
		}
		else if (bInitializationEnded && !Receipt.IsSafeForPreparation())
		{
			Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::Restore);
		}
	}
}

void UVBPlayerReadinessComponent::AddAbilitySystemReasons(const APawn* Pawn, FVBPlayerPreparationSnapshot& Result)
{
	const UAbilitySystemComponent* ASC = IsValid(Pawn)
		? UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<APawn*>(Pawn)) : nullptr;
	if (!IsValid(ASC) || ASC->GetAvatarActor() != Pawn)
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::AbilitySystem);
	}
	else
	{
		if (ASC->HasMatchingGameplayTag(VBGameplayTags::State_Interacting))
		{
			Result.BlockedReasons |= Bit(EVBPlayerPreparationReason::Interaction);
		}

		if (UVBAbilitySystemStatics::IsDead(ASC))
		{
			Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::Dead);
		}
	}
}

void UVBPlayerReadinessComponent::AddUIReasons(const APlayerController* PC, FVBPlayerPreparationSnapshot& Result)
{
	const UVBUIManagerSubsystem* CurrentUI = IsValid(PC) ? UVBUIManagerSubsystem::Get(PC) : nullptr;
	const FVBUIInputAvailability Input = CurrentUI ? CurrentUI->QueryInputAvailability(PC) : FVBUIInputAvailability();
	if (!Input.bAvailable)
	{
		Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::UISource);
	}
	else if (Input.bBlocked)
	{
		Result.BlockedReasons |= Bit(EVBPlayerPreparationReason::UI);
	}
}

void UVBPlayerReadinessComponent::AddHoldReasons(FVBPlayerPreparationSnapshot& Result) const
{
	for (const auto& Pair : Holds)
	{
		if (!Pair.Value.Owner.IsValid())
		{
			Result.UnavailableReasons |= Bit(EVBPlayerPreparationReason::HoldOwner);
		}
		else
		{
			Result.BlockedReasons |= Bit(EVBPlayerPreparationReason::ExternalHold);
		}
	}
}

void UVBPlayerReadinessComponent::OnRegister()
{
	Super::OnRegister();
	bEnding = false;
	BoundPawn = Cast<APawn>(GetOwner());
	if (BoundPawn.IsValid())
	{
		BoundPawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &ThisClass::HandleControllerChanged);
	}

	RefreshBindings();
	PawnExtension = UVBPawnExtensionComponent::FindFor(GetOwner());
	if (PawnExtension.IsValid())
	{
		AbilitySystemUninitializedHandle = PawnExtension->RegisterOnAbilitySystemUninitialized(
			FVBAbilitySystemUninitializedDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemUninitialized));
		AbilitySystemInitializedHandle = PawnExtension->RegisterAndCallOnAbilitySystemInitialized(
			FVBAbilitySystemInitializedDelegate::FDelegate::CreateUObject(this, &ThisClass::HandleAbilitySystemInitialized));
	}
}

void UVBPlayerReadinessComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshBindings();
	NotifyChanged();
}

void UVBPlayerReadinessComponent::OnUnregister()
{
	bEnding = true;
	DisconnectProviders();
	if (PawnExtension.IsValid())
	{
		PawnExtension->UnregisterAbilitySystemDelegates(AbilitySystemInitializedHandle, AbilitySystemUninitializedHandle);
	}

	PawnExtension.Reset();
	if (BoundPawn.IsValid())
	{
		BoundPawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &ThisClass::HandleControllerChanged);
	}

	BoundPawn.Reset();
	InitializationId.Invalidate();
	Holds.Reset();
	NotifyChanged();
	Super::OnUnregister();
}

void UVBPlayerReadinessComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEnding = true;
	InitializationId.Invalidate();
	DisconnectProviders();
	NotifyChanged();
	Super::EndPlay(EndPlayReason);
}

void UVBPlayerReadinessComponent::HandleControllerChanged(APawn* Pawn, AController* OldController, AController* NewController)
{
	if (Pawn != GetOwner())
	{
		return;
	}

	if (OldController && OldController != NewController)
	{
		InitializationId.Invalidate();
		bInitializationEnded = false;
	}

	RefreshBindings();
	NotifyChanged();
}

void UVBPlayerReadinessComponent::RefreshBindings()
{
	DisconnectProviders();
	APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!Pawn || !World || !World->IsGameWorld() || World->bIsTearingDown)
	{
		return;
	}

	AbilitySystem = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Pawn);
	if (AbilitySystem.IsValid())
	{
		InteractionHandle = AbilitySystem->RegisterGameplayTagEvent(VBGameplayTags::State_Interacting, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::HandleTagChanged);
		DeadHandle = AbilitySystem->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved)
			.AddUObject(this, &ThisClass::HandleTagChanged);
	}

	Restore = Pawn->FindComponentByClass<UVBPlayerRestoreComponent>();
	if (Restore.IsValid())
	{
		RestoreHandle = Restore->OnRestoreStateChanged.AddUObject(this, &ThisClass::NotifyChanged);
	}

	UI = UVBUIManagerSubsystem::Get(Pawn);
	if (UI.IsValid())
	{
		UIHandle = UI->OnInputAvailabilityChanged.AddUObject(this, &ThisClass::NotifyChanged);
	}

	Registry = World->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (Registry.IsValid())
	{
		SaveAppliedHandle = Registry->OnSaveApplied.AddUObject(this, &ThisClass::NotifyChanged);
	}

	Save = UVBSaveGameSubsystem::Get(Pawn);
	if (Save.IsValid())
	{
		StartContextHandle = Save->OnGameStartContextChanged.AddUObject(this, &ThisClass::NotifyChanged);
	}
}

void UVBPlayerReadinessComponent::DisconnectProviders()
{
	++ProviderEpoch;
	FTSTicker::RemoveTicker(TagNotificationHandle);
	TagNotificationHandle.Reset();
	bTagNotificationPending = false;
	ReleaseAbilitySystem();
	if (Restore.IsValid())
	{
		Restore->OnRestoreStateChanged.Remove(RestoreHandle);
	}

	if (UI.IsValid())
	{
		UI->OnInputAvailabilityChanged.Remove(UIHandle);
	}

	if (Registry.IsValid())
	{
		Registry->OnSaveApplied.Remove(SaveAppliedHandle);
	}

	if (Save.IsValid())
	{
		Save->OnGameStartContextChanged.Remove(StartContextHandle);
	}

	Restore.Reset(); UI.Reset(); Registry.Reset(); Save.Reset();
}

void UVBPlayerReadinessComponent::ReleaseAbilitySystem()
{
	if (AbilitySystem.IsValid())
	{
		AbilitySystem->RegisterGameplayTagEvent(VBGameplayTags::State_Interacting,
			EGameplayTagEventType::NewOrRemoved).Remove(InteractionHandle);
		AbilitySystem->RegisterGameplayTagEvent(VBGameplayTags::State_Dead, EGameplayTagEventType::NewOrRemoved).Remove(DeadHandle);
	}

	AbilitySystem.Reset();
}

void UVBPlayerReadinessComponent::HandleAbilitySystemInitialized(UVBAbilitySystemComponent&)
{
	if (bEnding)
	{
		return;
	}

	RefreshBindings();
	NotifyChanged();
}

void UVBPlayerReadinessComponent::HandleAbilitySystemUninitialized(UVBAbilitySystemComponent&, bool)
{
	// 구독만 거둔다. 상태 다시 읽기는 뒤따르는 컨트롤러 변경 통지(Super::UnPossessed)가 한다 - 지금은 아직 이어져 있다.
	//   끝나는 중(EndPlay 에서 온 해제)이면 구독은 거두되 알리지 않는다 - 다른 알림 경로와 같은 bEnding 규칙.
	ReleaseAbilitySystem();
	if (!bEnding)
	{
		NotifyChanged();
	}
}

void UVBPlayerReadinessComponent::BeginInitialization(FGuid Id, EVBGameStartIntent Intent)
{
	if (bEnding || !IsRegistered())
	{
		return;
	}

	if (Id.IsValid() && Id == InitializationId && Intent == StartIntent)
	{
		return;
	}

	InitializationId = Id;
	StartIntent = Intent;
	bInitializationEnded = false;
	bContinueDispatched = false;
	Holds.Reset();
	RefreshBindings();
	const TWeakObjectPtr<UVBPlayerRestoreComponent> ArmedRestore = Restore;
	if (ArmedRestore.IsValid())
	{
		ArmedRestore->ArmInitialization(Id);
		if (Id == InitializationId && ArmedRestore.IsValid() && ArmedRestore == Restore && Intent == EVBGameStartIntent::NewGame)
		{
			ArmedRestore->MarkRestoreNotRequired(Id);
		}
	}

	NotifyChanged();
}

void UVBPlayerReadinessComponent::CompleteInitialization(FGuid Id, bool bDispatched)
{
	if (bEnding || !Id.IsValid() || Id != InitializationId || bInitializationEnded)
	{
		return;
	}

	bContinueDispatched = bDispatched;
	bInitializationEnded = true;
	NotifyChanged();
}

FVBPlayerPreparationHoldToken UVBPlayerReadinessComponent::AcquirePreparationHold(UObject* Owner, FName Reason)
{
	if (bEnding || !IsValid(Owner) || Reason.IsNone() || !InitializationId.IsValid()) return {};
	FVBPlayerPreparationHoldToken Token{FGuid::NewGuid(), InitializationId};
	Holds.Add(Token.Id, {Owner, Reason});
	NotifyChanged();
	return Token;
}

bool UVBPlayerReadinessComponent::CanFinishPreparationHold(const FVBPlayerPreparationHoldToken& Token) const
{
	const FPreparationHold* Hold = Holds.Find(Token.Id);
	if (bEnding || Token.InitializationId != InitializationId || Holds.Num() != 1 || !Hold || !Hold->Owner.IsValid())
	{
		return false;
	}

	const FVBPlayerPreparationSnapshot Snapshot = Query();
	return Snapshot.UnavailableReasons == 0
		&& (Snapshot.BlockedReasons & ~Bit(EVBPlayerPreparationReason::ExternalHold)) == 0;
}

void UVBPlayerReadinessComponent::ReleasePreparationHold(const FVBPlayerPreparationHoldToken& Token)
{
	if (Token.InitializationId == InitializationId && Holds.Remove(Token.Id) > 0)
	{
		NotifyChanged();
	}
}

void UVBPlayerReadinessComponent::HandleTagChanged(FGameplayTag Tag, int32 Count)
{
	NotifyChanged();
	if (bEnding || !IsRegistered())
	{
		return;
	}
	// 태그를 관리하는 쪽이 같은 호출 스택 뒤쪽에서 controller 입력 잠금을 풀 수 있다
	bTagNotificationPending = true;
	if (!TagNotificationHandle.IsValid())
	{
		TagNotificationHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &ThisClass::NotifyAfterTagChange));
	}
}

bool UVBPlayerReadinessComponent::NotifyAfterTagChange(float DeltaSeconds)
{
	const uint32 Epoch = ProviderEpoch;
	bTagNotificationPending = false;
	if (!bEnding && IsRegistered())
	{
		NotifyChanged();
	}

	if (Epoch != ProviderEpoch)
	{
		return false;
	}

	if (bTagNotificationPending && !bEnding)
	{
		return true;
	}

	TagNotificationHandle.Reset();
	return false;
}

void UVBPlayerReadinessComponent::NotifyChanged() { ++Revision; OnPreparationChanged.Broadcast(); }
