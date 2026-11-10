#include "Narrative/Ending/VBEndingDirectorSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Campaign/VBCampaignStateComponent.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "Narrative/Ending/VBEndingRules.h"
#include "Narrative/Ending/VBEndingSettings.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "VB/VB.h"

bool UVBEndingDirectorSubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UVBEndingDirectorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->GetNetMode() != NM_Client;
}

void UVBEndingDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bStopping = false;
	Cinematic = Collection.InitializeDependency<UVBCinematicDirectorSubsystem>();
	if (Cinematic.IsValid())
	{
		CinematicHandle = Cinematic->OnHandoffEnded.AddUObject(this, &ThisClass::HandleCinematicEnded);
	}
}

void UVBEndingDirectorSubsystem::Deinitialize()
{
	Stop();
	Super::Deinitialize();
}

void UVBEndingDirectorSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	Stop();
	Super::OnWorldEndPlay(InWorld);
}

bool UVBEndingDirectorSubsystem::CanWrite() const
{
	const UWorld* World = GetWorld();
	return !bStopping && IsValid(World) && !World->bIsTearingDown && World->GetNetMode() != NM_Client;
}

bool UVBEndingDirectorSubsystem::IsContextCurrent() const
{
	const APlayerState* Instigator = ActiveInstigator.Get();
	if (!CanWrite() || !IsValid(Instigator) || Instigator->IsActorBeingDestroyed()
		|| Instigator->GetWorld() != GetWorld() || !Instigator->HasAuthority()
		|| !Campaign.IsValid() || !Story.IsValid()
		|| Campaign.Get() != UVBCampaignStateComponent::Get(this)
		|| Story.Get() != UVBStoryStateComponent::Get(this)
		|| Campaign->GetState().RunId != RunId || Story->GetSaveReadSerial() != StoryReadSerial)
	{
		return false;
	}

	const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this);
	return (Save ? Save->GetGameStartContext().AttemptId : FGuid()) == SaveAttemptId;
}

bool UVBEndingDirectorSubsystem::BeginFinale(APlayerState* Instigator)
{
	if (!CanWrite() || Phase == EPhase::Offered || Phase == EPhase::Playing
		|| !IsValid(Instigator) || Instigator->IsActorBeingDestroyed()
		|| Instigator->GetWorld() != GetWorld() || !Instigator->HasAuthority())
	{
		return false;
	}

	ResetAttempt();
	UVBCampaignStateComponent* CurrentCampaign = UVBCampaignStateComponent::Get(this);
	UVBStoryStateComponent* CurrentStory = UVBStoryStateComponent::Get(this);
	if (!CurrentCampaign || !CurrentStory || !CurrentCampaign->GetState().RunId.IsValid())
	{
		VB_LOG(Warning, "Finale refused: campaign or story state unavailable");
		return false;
	}

	const UVBEndingCatalog* Catalog = UVBEndingSettings::LoadCatalog();
	const TArray<const FVBEndingDefinition*> Group = VBEndingRules::ResolveGroup(Catalog, FVBRuleContext::Make(this, Instigator));
	if (Group.IsEmpty())
	{
		return false;
	}

	for (const FVBEndingDefinition* Row : Group)
	{
		FrozenGroup.Add(*Row);
	}

	ActiveInstigator = Instigator;
	Campaign = CurrentCampaign;
	Story = CurrentStory;
	RunId = CurrentCampaign->GetState().RunId;
	StoryReadSerial = CurrentStory->GetSaveReadSerial();
	const UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this);
	SaveAttemptId = Save ? Save->GetGameStartContext().AttemptId : FGuid();
	AttemptId = FGuid::NewGuid();
	Phase = EPhase::Offered;
	return true;
}

bool UVBEndingDirectorSubsystem::ChooseEnding(FGuid Attempt, FGameplayTag Ending)
{
	if (Phase != EPhase::Offered || !Attempt.IsValid() || Attempt != AttemptId)
	{
		return false;
	}

	if (!IsContextCurrent())
	{
		ResetAttempt();
		return false;
	}

	TArray<const FVBEndingDefinition*> Group;
	for (const FVBEndingDefinition& Row : FrozenGroup)
	{
		Group.Add(&Row);
	}

	if (!VBEndingRules::IsInGroup(Group, Ending))
	{
		return false;
	}

	SelectedIndex = FrozenGroup.IndexOfByPredicate([Ending](const FVBEndingDefinition& Row) { return Row.Ending == Ending; });
	Phase = EPhase::Playing;
	const FVBEndingDefinition& Selected = FrozenGroup[SelectedIndex];
	if (!Selected.Cinematic)
	{
		return CompleteAttempt();
	}

	if (!Cinematic.IsValid())
	{
		Phase = EPhase::Offered;
		return false;
	}

	const FGuid ChoosingAttempt = AttemptId;
	const FVBGameCinematicStartResult Start = Cinematic->RequestPlay(
		Selected.Cinematic, ActiveInstigator.Get(), Selected.PlayerBindingTag, {});
	// 안전 provider 가 받는 도중 동기적으로 피날레를 취소하거나 월드를 끝낼 수 있어 다시 확인한다.
	if (AttemptId != ChoosingAttempt || Phase != EPhase::Playing || !IsContextCurrent())
	{
		if (Start.Status == EVBGameCinematicStartStatus::Accepted && Cinematic.IsValid())
		{
			Cinematic->CancelActive();
		}

		ResetAttempt();
		return false;
	}

	if (Start.Status != EVBGameCinematicStartStatus::Accepted)
	{
		Phase = EPhase::Offered;
		VB_LOG(Warning, "Ending cinematic refused: %d; no clear recorded", static_cast<int32>(Start.Status));
		return false;
	}

	PlaybackSessionId = Start.Core.SessionId;
	return true;
}

bool UVBEndingDirectorSubsystem::CompleteAttempt()
{
	if (Phase != EPhase::Playing || !IsContextCurrent() || !FrozenGroup.IsValidIndex(SelectedIndex))
	{
		ResetAttempt();
		return false;
	}

	const FVBClearSummary Summary = VBEndingRules::MakeClearSummary(FrozenGroup[SelectedIndex], Story->GetState());
	// 기록 전에 닫는다 - 중복 수신이 클리어 id 를 하나 더 만들지 못하게.
	Phase = EPhase::Completed;
	PlaybackSessionId.Invalidate();
	if (!Campaign->RecordClear(Summary))
	{
		ResetAttempt();
		return false;
	}

	return true;
}

void UVBEndingDirectorSubsystem::HandleCinematicEnded(const FVBCinematicEndResult& Result, bool bSucceeded)
{
	if (Phase != EPhase::Playing || !PlaybackSessionId.IsValid() || Result.SessionId != PlaybackSessionId)
	{
		return;
	}

	if (!bSucceeded || (Result.Type != EVBCinematicEndType::Completed && Result.Type != EVBCinematicEndType::Skipped))
	{
		ResetAttempt();
		return;
	}
	// 이 이벤트는 gameplay 복원과 시네마틱 세이브 보류 해제 뒤에 온다.
	CompleteAttempt();
}

bool UVBEndingDirectorSubsystem::CancelFinale(FGuid Attempt)
{
	if (!CanWrite() || !Attempt.IsValid() || Attempt != AttemptId
		|| (Phase != EPhase::Offered && Phase != EPhase::Playing))
	{
		return false;
	}

	const bool bCancelPlayback = PlaybackSessionId.IsValid();
	ResetAttempt();
	if (bCancelPlayback && Cinematic.IsValid())
	{
		Cinematic->CancelActive();
	}

	return true;
}

void UVBEndingDirectorSubsystem::ResetAttempt()
{
	Phase = EPhase::Idle;
	AttemptId.Invalidate();
	PlaybackSessionId.Invalidate();
	FrozenGroup.Reset();
	SelectedIndex = INDEX_NONE;
	ActiveInstigator.Reset();
	Campaign.Reset();
	Story.Reset();
	RunId.Invalidate();
	SaveAttemptId.Invalidate();
}

void UVBEndingDirectorSubsystem::Stop()
{
	if (bStopping)
	{
		return;
	}

	bStopping = true;
	const bool bCancelPlayback = PlaybackSessionId.IsValid();
	ResetAttempt();
	if (Cinematic.IsValid())
	{
		Cinematic->OnHandoffEnded.Remove(CinematicHandle);
		if (bCancelPlayback)
		{
			Cinematic->CancelActive();
		}
	}

	Cinematic.Reset();
}
