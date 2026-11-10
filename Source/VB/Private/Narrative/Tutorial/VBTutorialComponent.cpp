// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Tutorial/VBTutorialComponent.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Narrative/Tutorial/VBTutorialDefinition.h"
#include "Player/VBPlayerStateLookup.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"

namespace
{
	FVBTutorialProgress FromSave(const FVBSavedTutorial& Saved)
	{
		FVBTutorialProgress Entry;
		Entry.TutorialId = Saved.TutorialId;
		Entry.CurrentStepId = Saved.CurrentStepId;
		Entry.EventCount = Saved.EventCount;
		Entry.bCompleted = Saved.bCompleted;
		Entry.UnlockedSteps = Saved.UnlockedSteps;
		return Entry;
	}

	FVBSavedTutorial ToSave(const FVBTutorialProgress& Entry)
	{
		FVBSavedTutorial Saved;
		Saved.TutorialId = Entry.TutorialId;
		Saved.CurrentStepId = Entry.CurrentStepId;
		Saved.EventCount = Entry.EventCount;
		Saved.bCompleted = Entry.bCompleted;
		Saved.UnlockedSteps = Entry.UnlockedSteps;
		return Saved;
	}
}

UVBTutorialComponent::UVBTutorialComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
}

UVBTutorialComponent* UVBTutorialComponent::FindFor(const AActor* Actor)
{
	return VBPlayerStateLookup::FindComponent<UVBTutorialComponent>(Actor);
}

APlayerState* UVBTutorialComponent::GetPlayerState() const
{
	return Cast<APlayerState>(GetOwner());
}

bool UVBTutorialComponent::HasWriteAuthority() const
{
	const APlayerState* Player = GetPlayerState();
	return Player && Player->HasAuthority() && GetWorld() && GetWorld()->GetNetMode() != NM_Client;
}

void UVBTutorialComponent::InitializeComponent()
{
	Super::InitializeComponent();
	if (HasWriteAuthority())
	{
		UVBSaveParticipantSubsystem::Register(this);
		SaveParticipants = GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>();
		if (UVBSaveParticipantSubsystem* Registry = SaveParticipants.Get())
		{
			SaveAppliedHandle = Registry->OnSaveApplied.AddUObject(this, &UVBTutorialComponent::HandleSaveApplied);
		}
	}

	LoadCatalog();
}

void UVBTutorialComponent::UninitializeComponent()
{
	UnbindSources();
	if (UVBSaveParticipantSubsystem* Registry = SaveParticipants.Get())
	{
		Registry->OnSaveApplied.Remove(SaveAppliedHandle);
	}

	SaveAppliedHandle.Reset();
	SaveParticipants.Reset();
	UVBSaveParticipantSubsystem::Unregister(this);
	InvalidateCatalogLoad();
	Super::UninitializeComponent();
}

void UVBTutorialComponent::BeginPlay()
{
	Super::BeginPlay();
	if (HasWriteAuthority())
	{
		BindSources();
		// 이야기 구독은 여기서 한 번만 - 묶는 즉시 첫 RefreshConditions 가 돈다. BindSources(평가마다 불림)에 두면 되부름이 끝나지 않는다.
		StoryBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &UVBTutorialComponent::RefreshConditions));
	}
}

void UVBTutorialComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bCatalogReady = false;
	bProgressReady = false;
	UnbindSources();
	InvalidateCatalogLoad();
	PendingEvents.Reset();
	PendingCompletionNotices.Reset();
	Super::EndPlay(EndPlayReason);
}

void UVBTutorialComponent::BindSources()
{
	if (!HasWriteAuthority())
	{
		return;
	}

	if (APlayerState* Player = GetPlayerState())
	{
		Player->OnPawnSet.AddUniqueDynamic(this, &UVBTutorialComponent::HandlePawnSet);
	}

	if (!EventHandle.IsValid())
	{
		if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
		{
			EventHandle = Events->OnGameEvent.AddUObject(this, &UVBTutorialComponent::HandleGameEvent);
		}
	}

	UVBQuestLogComponent* CurrentQuestLog = UVBQuestLogComponent::FindFor(GetOwner());
	if (QuestLog.Get() != CurrentQuestLog)
	{
		if (UVBQuestLogComponent* Previous = QuestLog.Get())
		{
			Previous->OnQuestLogChanged.Remove(QuestHandle);
		}

		QuestHandle.Reset();
		QuestLog = CurrentQuestLog;
		if (CurrentQuestLog)
		{
			QuestHandle = CurrentQuestLog->OnQuestLogChanged.AddUObject(this, &UVBTutorialComponent::RefreshConditions);
		}
	}

	UVBCinematicDirectorSubsystem* CurrentCinematic = GetWorld()->GetSubsystem<UVBCinematicDirectorSubsystem>();
	if (Cinematic.Get() != CurrentCinematic)
	{
		if (UVBCinematicDirectorSubsystem* Previous = Cinematic.Get())
		{
			Previous->OnHandoffEnded.Remove(CinematicHandle);
		}

		CinematicHandle.Reset();
		Cinematic = CurrentCinematic;
		if (CurrentCinematic)
		{
			CinematicHandle = CurrentCinematic->OnHandoffEnded.AddUObject(this, &UVBTutorialComponent::HandleCinematicEnded);
		}
	}
}

void UVBTutorialComponent::UnbindSources()
{
	UnbindMovementSource();
	if (APlayerState* Player = GetPlayerState())
	{
		Player->OnPawnSet.RemoveDynamic(this, &UVBTutorialComponent::HandlePawnSet);
	}

	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		Events->OnGameEvent.Remove(EventHandle);
	}

	StoryBinding.Disconnect();
	if (UVBQuestLogComponent* Previous = QuestLog.Get())
	{
		Previous->OnQuestLogChanged.Remove(QuestHandle);
	}

	if (UVBCinematicDirectorSubsystem* Previous = Cinematic.Get())
	{
		Previous->OnHandoffEnded.Remove(CinematicHandle);
	}

	QuestLog.Reset();
	Cinematic.Reset();
	EventHandle.Reset();
	QuestHandle.Reset();
	CinematicHandle.Reset();
}

void UVBTutorialComponent::UnbindMovementSource()
{
	if (ACharacter* Previous = MovementCharacter.Get())
	{
		Previous->OnCharacterMovementUpdated.RemoveDynamic(this, &UVBTutorialComponent::HandleMovementUpdated);
	}

	MovementCharacter.Reset();
}

void UVBTutorialComponent::RefreshMovementSource()
{
	ACharacter* Desired = nullptr;
	if (HasWriteAuthority() && IsReady())
	{
		for (const FVBTutorialProgress& Entry : Progress)
		{
			const UVBTutorialDefinition* Definition = ResolveDefinition(Entry.TutorialId);
			if (Definition && VBTutorial::NeedsMovementObservation(*Definition, Entry))
			{
				Desired = Cast<ACharacter>(GetPlayerState()->GetPawn());
				break;
			}
		}
	}

	if (Desired == MovementCharacter.Get())
	{
		return;
	}

	UnbindMovementSource();
	MovementCharacter = Desired;
	if (Desired)
	{
		Desired->OnCharacterMovementUpdated.AddUniqueDynamic(this, &UVBTutorialComponent::HandleMovementUpdated);
	}
}

void UVBTutorialComponent::HandlePawnSet(APlayerState* Player, APawn* NewPawn, APawn* OldPawn)
{
	if (Player == GetPlayerState())
	{
		RefreshMovementSource();
	}
}

void UVBTutorialComponent::HandleMovementUpdated(float DeltaSeconds, FVector OldLocation, FVector OldVelocity)
{
	APlayerState* Player = GetPlayerState();
	const ACharacter* Character = MovementCharacter.Get();
	if (!CanEvaluate() || !IsReady() || !Character || !Player || Player->GetPawn() != Character)
	{
		return;
	}

	const AController* Controller = Character->GetController();
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const bool bInputBlocked = !Controller || Controller->IsMoveInputIgnored();
	if (!Movement || !VBTutorial::IsControlledMovement(Character->GetActorLocation() - OldLocation,
		Movement->GetCurrentAcceleration(), bInputBlocked))
	{
		return;
	}

	FVBGameEvent Event;
	Event.EventTag = VBTutorial::MovementEventTag();
	Event.Instigator = Player;
	HandleGameEvent(Event);
}

uint32 UVBTutorialComponent::InvalidateCatalogLoad()
{
	const uint32 Generation = ++CatalogGeneration;
	if (CatalogLoadHandle)
	{
		CatalogLoadHandle->CancelHandle();
	}

	CatalogLoadHandle.Reset();
	return Generation;
}

void UVBTutorialComponent::LoadCatalog()
{
	bCatalogReady = false;
	bCatalogFailed = false;
	const uint32 Generation = InvalidateCatalogLoad();
	CatalogPaths.Reset();
	Definitions.Reset();
	DefinitionIndices.Reset();
	UAssetManager& Manager = UAssetManager::Get();
	TArray<FPrimaryAssetId> Ids;
	Manager.GetPrimaryAssetIdList(UVBTutorialDefinition::GetTutorialAssetType(), Ids);
	for (const FPrimaryAssetId& Id : Ids)
	{
		const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Id);
		if (!Path.IsValid())
		{
			bCatalogFailed = true;
			VB_LOG(Error, "Tutorial catalog contains an invalid asset path: %s", *Id.ToString());
			OnTutorialChanged.Broadcast();
			return;
		}

		CatalogPaths.AddUnique(Path);
	}

	if (CatalogPaths.IsEmpty())
	{
		FinishCatalogLoad(Generation);
		return;
	}

	CatalogLoadHandle = Manager.GetStreamableManager().RequestAsyncLoad(CatalogPaths,
		FStreamableDelegate::CreateUObject(this, &UVBTutorialComponent::FinishCatalogLoad, Generation),
		FStreamableManager::DefaultAsyncLoadPriority, false, true);
	if (CatalogLoadHandle)
	{
		CatalogLoadHandle->StartStalledHandle();
	}
	else
	{
		bCatalogFailed = true;
		VB_LOG(Error, "Tutorial catalog request failed");
		OnTutorialChanged.Broadcast();
	}
}

void UVBTutorialComponent::FinishCatalogLoad(uint32 ExpectedGeneration)
{
	if (CatalogGeneration != ExpectedGeneration || !IsRegistered())
	{
		return;
	}

	for (const FSoftObjectPath& Path : CatalogPaths)
	{
		UVBTutorialDefinition* Definition = Cast<UVBTutorialDefinition>(Path.ResolveObject());
		if (!Definition || !Definition->IsUsable() || DefinitionIndices.Contains(Definition->TutorialId))
		{
			bCatalogFailed = true;
			VB_LOG(Error, "Invalid or duplicate tutorial definition: %s", *Path.ToString());
			Definitions.Reset();
			DefinitionIndices.Reset();
			OnTutorialChanged.Broadcast();
			return;
		}

		DefinitionIndices.Add(Definition->TutorialId, Definitions.Add(Definition));
	}

	bCatalogReady = true;
	if (HasWriteAuthority())
	{
		ResolveSavedProgress();
		if (HasBegunPlay())
		{
			BindSources();
			RefreshConditions();
		}
	}

	OnTutorialChanged.Broadcast();
}

bool UVBTutorialComponent::RetryCatalog()
{
	if (!bCatalogFailed || !IsRegistered() || bProcessing)
	{
		return false;
	}

	LoadCatalog();
	OnTutorialChanged.Broadcast();
	return true;
}

const UVBTutorialDefinition* UVBTutorialComponent::ResolveDefinition(FGameplayTag TutorialId) const
{
	const int32* Index = DefinitionIndices.Find(TutorialId);
	return bCatalogReady && Index && Definitions.IsValidIndex(*Index) ? Definitions[*Index].Get() : nullptr;
}

const FVBTutorialProgress* UVBTutorialComponent::FindProgress(FGameplayTag TutorialId) const
{
	return Progress.FindByPredicate([TutorialId](const FVBTutorialProgress& Entry) { return Entry.TutorialId == TutorialId; });
}

const UVBTutorialDefinition* UVBTutorialComponent::GetHintDefinition() const
{
	if (!IsReady())
	{
		return nullptr;
	}

	TArray<const UVBTutorialDefinition*> Values;
	for (const UVBTutorialDefinition* Definition : Definitions)
	{
		Values.Add(Definition);
	}

	return VBTutorial::SelectHint(Values, Progress);
}

bool UVBTutorialComponent::CanEvaluate() const
{
	const UVBSaveParticipantSubsystem* Registry = SaveParticipants.Get();
	return HasWriteAuthority() && HasBegunPlay() && bCatalogReady && StoryBinding.GetStory()
		&& (!Registry || !Registry->IsApplyingSave());
}

bool UVBTutorialComponent::Start(const UVBTutorialDefinition* Definition)
{
	BindSources();
	if (!CanEvaluate() || bProcessing || !Definition || ResolveDefinition(Definition->TutorialId) != Definition
		|| FindProgress(Definition->TutorialId)
		|| UnresolvedSavedProgress.ContainsByPredicate(
			[Definition](const FVBSavedTutorial& Entry) { return Entry.TutorialId == Definition->TutorialId; }))
	{
		return false;
	}

	FVBTutorialProgress Entry;
	if (!VBTutorial::Start(*Definition, Entry))
	{
		return false;
	}

	Progress.Add(MoveTemp(Entry));
	RefreshConditions();
	GetOwner()->ForceNetUpdate();
	OnTutorialChanged.Broadcast();
	return true;
}

void UVBTutorialComponent::HandleGameEvent(const FVBGameEvent& Event)
{
	if (Event.Instigator && Event.Instigator != GetPlayerState())
	{
		return;
	}

	if (HasWriteAuthority() && HasBegunPlay() && !bCatalogReady)
	{
		const UVBSaveParticipantSubsystem* Registry = SaveParticipants.Get();
		if (Registry && Registry->IsApplyingSave())
		{
			return;
		}

		if (PendingEvents.Num() < 64)
		{
			PendingEvents.Add(Event);
		}
		else { VB_LOG(Warning, "Tutorial catalog not ready: event buffer is full"); }
		return;
	}

	Evaluate(&Event);
}

void UVBTutorialComponent::RefreshConditions()
{
	Evaluate(nullptr);
}

void UVBTutorialComponent::HandleSaveApplied()
{
	RefreshConditions();
}

void UVBTutorialComponent::HandleCinematicEnded(const FVBCinematicEndResult& Result, bool bSucceeded)
{
	RefreshConditions();
}

bool UVBTutorialComponent::EvaluateDefinition(const UVBTutorialDefinition& Definition, const FVBGameEvent* Event,
	const FVBRuleContext& Context, TArray<FText>& OutCompleted)
{
	FVBTutorialProgress* Entry = Progress.FindByPredicate([&Definition](const FVBTutorialProgress& Value)
	{
		return Value.TutorialId == Definition.TutorialId;
	});
	if (Entry)
	{
		const bool bWasCompleted = Entry->bCompleted;
		const bool bChanged = Event ? VBTutorial::ApplyEvent(Definition, *Event, Context, *Entry)
			: VBTutorial::RefreshConditions(Definition, Context, *Entry);
		if (!bWasCompleted && Entry->bCompleted)
		{
			OutCompleted.Add(Definition.Title);
		}

		return bChanged;
	}

	const bool bUnresolved = UnresolvedSavedProgress.ContainsByPredicate([&Definition](const FVBSavedTutorial& Saved)
	{
		return Saved.TutorialId == Definition.TutorialId;
	});
	FVBTutorialProgress Started;
	if (bUnresolved || !VBTutorial::CanActivate(Definition, Event, Context) || !VBTutorial::Start(Definition, Started))
	{
		return false;
	}

	Entry = &Progress[Progress.Add(MoveTemp(Started))];
	// 활성화 신호는 여기서 소비되고 첫 단계 진행으로 세지 않는다.
	VBTutorial::RefreshConditions(Definition, Context, *Entry);
	if (Entry->bCompleted)
	{
		OutCompleted.Add(Definition.Title);
	}

	return true;
}

bool UVBTutorialComponent::DrainEvaluations(const FVBGameEvent* Event, TArray<FText>& OutCompleted)
{
	// 평가 중 들어온 이벤트와 새로 고침은 큐에서 이어 처리한다. 서로 다시 부르는 조건이 끝나지 않을 때를 대비해 횟수를 막는다.
	constexpr int32 MaxEvaluationRounds = 64;
	TGuardValue<bool> Processing(bProcessing, true);
	TOptional<FVBGameEvent> Current;
	if (Event)
	{
		Current = *Event;
	}

	bool bChanged = false;
	for (int32 Round = 0; Round < MaxEvaluationRounds; ++Round)
	{
		bRefreshPending = false;
		const FVBRuleContext Context = FVBRuleContext::Make(this, GetPlayerState());
		const FVBGameEvent* CurrentEvent = Current.IsSet() ? &Current.GetValue() : nullptr;
		for (const UVBTutorialDefinition* Definition : Definitions)
		{
			bChanged |= EvaluateDefinition(*Definition, CurrentEvent, Context, OutCompleted);
		}

		if (Round == MaxEvaluationRounds - 1)
		{
			break;
		}

		if (!PendingEvents.IsEmpty())
		{
			Current = PendingEvents[0];
			PendingEvents.RemoveAt(0);
		}
		else if (bRefreshPending)
		{
			Current.Reset();
		}
		else
		{
			break;
		}
	}

	if (!PendingEvents.IsEmpty() || bRefreshPending)
	{
		VB_LOG(Warning, "Tutorial refresh exceeded %d nested evaluations", MaxEvaluationRounds);
		PendingEvents.Reset();
		bRefreshPending = false;
	}

	return bChanged;
}

void UVBTutorialComponent::Evaluate(const FVBGameEvent* Event)
{
	BindSources();
	if (!CanEvaluate())
	{
		return;
	}

	if (bProcessing)
	{
		if (Event)
		{
			PendingEvents.Add(*Event);
		}
		else
		{
			bRefreshPending = true;
		}

		return;
	}

	const bool bBecameReady = !bProgressReady;
	bProgressReady = true;
	TArray<FText> Completed;
	const bool bChanged = DrainEvaluations(Event, Completed);
	RefreshMovementSource();
	if (bChanged || bBecameReady)
	{
		GetOwner()->ForceNetUpdate();
		OnTutorialChanged.Broadcast();
	}

	if (!bSuppressNextEvaluationNotices)
	{
		for (const FText& Title : Completed)
		{
			ClientCompletionNotice(Title);
		}
	}

	bSuppressNextEvaluationNotices = false;
}

void UVBTutorialComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UVBTutorialComponent, Progress, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UVBTutorialComponent, bProgressReady, COND_OwnerOnly);
}

void UVBTutorialComponent::OnRep_Progress()
{
	OnTutorialChanged.Broadcast();
}

void UVBTutorialComponent::ClientCompletionNotice_Implementation(const FText& Title)
{
	PendingCompletionNotices.Add(Title);
	OnCompletionNoticesChanged.Broadcast();
}

TArray<FText> UVBTutorialComponent::TakeCompletionNotices()
{
	TArray<FText> Notices = MoveTemp(PendingCompletionNotices);
	PendingCompletionNotices.Reset();
	return Notices;
}

void UVBTutorialComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (!HasWriteAuthority() || Context.PlayerState != GetPlayerState())
	{
		return;
	}

	Save.Tutorials.Entries = UnresolvedSavedProgress;
	for (const FVBTutorialProgress& Entry : Progress)
	{
		Save.Tutorials.Entries.Add(ToSave(Entry));
	}
}

void UVBTutorialComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (!HasWriteAuthority() || Context.PlayerState != GetPlayerState())
	{
		return;
	}

	UnbindMovementSource();
	Progress.Reset();
	bProgressReady = false;
	UnresolvedSavedProgress = Save.Tutorials.Entries;
	PendingEvents.Reset();
	PendingCompletionNotices.Reset();
	bRefreshPending = false;
	bSuppressNextEvaluationNotices = true;
	ResolveSavedProgress();
	GetOwner()->ForceNetUpdate();
	OnTutorialChanged.Broadcast();
}

void UVBTutorialComponent::ResolveSavedProgress()
{
	if (!HasWriteAuthority() || !bCatalogReady)
	{
		return;
	}

	TMap<FGameplayTag, int32> Counts;
	for (const FVBSavedTutorial& Saved : UnresolvedSavedProgress)
	{
		++Counts.FindOrAdd(Saved.TutorialId);
	}

	TArray<FVBSavedTutorial> StillUnresolved;
	for (const FVBSavedTutorial& Saved : UnresolvedSavedProgress)
	{
		const UVBTutorialDefinition* Definition = ResolveDefinition(Saved.TutorialId);
		FVBTutorialProgress Entry = FromSave(Saved);
		if (Counts.FindRef(Saved.TutorialId) != 1 || FindProgress(Saved.TutorialId)
			|| !Definition || !VBTutorial::IsProgressCompatible(*Definition, Entry))
		{
			StillUnresolved.Add(Saved);
			VB_LOG(Warning, "Preserving unresolved tutorial save: %s", *Saved.TutorialId.ToString());
		}
		else { Progress.Add(MoveTemp(Entry)); }
	}

	UnresolvedSavedProgress = MoveTemp(StillUnresolved);
}
