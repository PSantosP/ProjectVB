// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Quest/VBQuestLogComponent.h"

#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/Dialogue/VBDialogueAsset.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Player/VBPlayerStateLookup.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "VB/VB.h"
#include "CoreLoop/VBServerWrite.h"

namespace
{
	const FVBQuestStage* GetStage(const FVBQuestLogEntry& Entry)
	{
		return Entry.Definition && Entry.Definition->Stages.IsValidIndex(Entry.StageIndex)
			? &Entry.Definition->Stages[Entry.StageIndex] : nullptr;
	}

	bool IsStageComplete(const FVBQuestLogEntry& Entry, const FVBQuestStage& Stage)
	{
		// 이미 찬 목표가 플레이어의 확인을 기다린다 - 확인 전에는 다음으로 진행하지 않는다.
		if (Entry.PendingAckObjective != INDEX_NONE)
		{
			return false;
		}

		for (int32 Index = 0; Index < Stage.Objectives.Num(); ++Index)
		{
			const FVBQuestObjective* Objective = Stage.Objectives[Index].GetPtr<FVBQuestObjective>();
			if (!Objective || Objective->bOptional)
			{
				continue;
			}

			if (!Entry.Progress.IsValidIndex(Index) || Entry.Progress[Index] < Objective->GetRequiredCount())
			{
				return false;
			}
		}

		return true;
	}

	// 확인을 기다리는 목표. 대기가 없거나 단계가 바뀌어 번호가 어긋났으면 nullptr.
	const FVBQuestObjective* GetPendingObjective(const FVBQuestLogEntry& Entry)
	{
		const FVBQuestStage* Stage = GetStage(Entry);
		return Stage && Stage->Objectives.IsValidIndex(Entry.PendingAckObjective) && Entry.Progress.IsValidIndex(Entry.PendingAckObjective)
			? Stage->Objectives[Entry.PendingAckObjective].GetPtr<FVBQuestObjective>() : nullptr;
	}
}

// ---------------------------------------------------------------- 복제 콜백

void FVBQuestLogEntry::PostReplicatedAdd(const FVBQuestLogList& InList)
{
	if (InList.Owner)
	{
		InList.Owner->HandleReplicatedChange();
	}
}

void FVBQuestLogEntry::PostReplicatedChange(const FVBQuestLogList& InList)
{
	if (InList.Owner)
	{
		InList.Owner->HandleReplicatedChange();
	}
}

void FVBQuestLogEntry::PreReplicatedRemove(const FVBQuestLogList& InList)
{
	if (InList.Owner)
	{
		InList.Owner->HandleReplicatedChange();
	}
}

// ---------------------------------------------------------------- 컴포넌트

UVBQuestLogComponent::UVBQuestLogComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsInitializeComponent = true;
	SetIsReplicatedByDefault(true);
	List.Owner = this;
}

UVBQuestLogComponent* UVBQuestLogComponent::FindFor(const AActor* Actor)
{
	// 폰, 컨트롤러, PlayerState 어느 쪽에서든 - 인벤토리, 장비, 성장과 같은 찾기.
	return VBPlayerStateLookup::FindComponent<UVBQuestLogComponent>(Actor);
}

EVBQuestStatus UVBQuestLogComponent::GetStatus(const UVBQuestDefinition* Quest) const
{
	const FVBQuestLogEntry* Entry = FindEntry(Quest);
	return Entry ? Entry->Status : EVBQuestStatus::NotStarted;
}

int32 UVBQuestLogComponent::GetStageIndex(const UVBQuestDefinition* Quest) const
{
	const FVBQuestLogEntry* Entry = FindEntry(Quest);
	return Entry && Entry->Status == EVBQuestStatus::Active ? Entry->StageIndex : INDEX_NONE;
}

const FVBQuestLogEntry* UVBQuestLogComponent::GetTrackedEntry() const
{
	const FVBQuestLogEntry* Best = nullptr;
	for (const FVBQuestLogEntry& Entry : List.Entries)
	{
		if (Entry.Definition && Entry.Status == EVBQuestStatus::Active && (!Best || Entry.RecentOrder > Best->RecentOrder))
		{
			Best = &Entry;
		}
	}

	return Best;
}

bool UVBQuestLogComponent::StartQuest(const UVBQuestDefinition* Quest)
{
	if (!Quest || !HasWriteAuthority(TEXT("StartQuest")))
	{
		return false;
	}

	if (Quest->Stages.IsEmpty())
	{
		VB_LOG(Warning, "퀘스트 %s: 단계가 없어 시작하지 않는다", *GetNameSafe(Quest));
		return false;
	}

	if (FindEntry(Quest))
	{
		VB_LOG(Log, "퀘스트 %s: 이미 일지에 있다(%s) - 다시 시작하지 않는다", *GetNameSafe(Quest),
			GetStatus(Quest) == EVBQuestStatus::Completed ? TEXT("완료") : TEXT("진행 중"));
		return false;
	}
	// 시작하며 일으킨 사건(Event.Quest.Started 등)은 줄을 서고 구간 끝에서 처리된다. 단계 효과가 연 시작이면 바깥 구간이 처리한다.
	RunProcessingSection([this, Quest]()
	{
		{
			FVBQuestLogEntry& Entry = List.Entries.AddDefaulted_GetRef();
			Entry.Definition = Quest;
			Entry.Status = EVBQuestStatus::Active;
			Entry.StageIndex = 0;
			ResetProgress(Entry);
			Touch(Entry);
			List.MarkItemDirty(Entry);
		}

		VB_LOG(Log, "퀘스트 시작: %s", *GetNameSafe(Quest));
		Announce(Quest, EVBQuestNotice::Started, VBGameplayTags::Event_Quest_Started);

		// 첫 단계의 상태 목표가 이미 차 있을 수 있다(이미 들고 있는 아이템).
		const FVBRuleContext Context = FVBRuleContext::Make(this, GetOwningPlayerState());
		if (FVBQuestLogEntry* Entry = FindEntry(Quest); Entry && EvaluateStageEntry(*Entry, Context))
		{
			List.MarkItemDirty(*Entry);
		}

		if (FVBQuestLogEntry* Entry = FindEntry(Quest))
		{
			TryAdvance(*Entry, Context);
		}
	});
	OnQuestLogChanged.Broadcast();
	return true;
}

void UVBQuestLogComponent::RunProcessingSection(TFunctionRef<void()> Body)
{
	if (bProcessing)
	{
		Body(); // 중첩 - 줄 비우기와 확인 대화는 바깥 구간이 한다
		return;
	}

	{
		TGuardValue<bool> Processing(bProcessing, true);
		Body();
		int32 Drained = 0;
		while (!PendingEvents.IsEmpty() && Drained++ < MaxChainedEventsPerSection)
		{
			const FVBGameEvent Next = PendingEvents[0];
			PendingEvents.RemoveAt(0);
			ProcessGameEvent(Next);
		}

		if (!PendingEvents.IsEmpty())
		{
			VB_LOG(Warning, "퀘스트 일지: 사건이 %d번 넘게 꼬리를 물었다 - 남은 %d 개를 버린다(퀘스트끼리 고리를 이룬 저작을 본다)",
				MaxChainedEventsPerSection, PendingEvents.Num());
			PendingEvents.Reset();
		}
	}

	OfferPendingAcknowledgements();
}

void UVBQuestLogComponent::HandleGameEvent(const FVBGameEvent& Event)
{
	if (GetOwner() && !GetOwner()->HasAuthority())
	{
		return;
	}
	// 다른 플레이어가 일으킨 일은 내 퀘스트가 세지 않는다. 일으킨 사람이 없으면(세계 사건) 센다.
	if (Event.Instigator && Event.Instigator != GetOwningPlayerState())
	{
		return;
	}

	if (bProcessing)
	{
		PendingEvents.Add(Event);
		return;
	}
	// 단계 효과, 보상이 일으킨 사건은 줄을 서고 구간 끝에서 처리된다.
	RunProcessingSection([this, &Event]() { ProcessGameEvent(Event); });
}

void UVBQuestLogComponent::ProcessGameEvent(const FVBGameEvent& Event)
{
	const FVBRuleContext Context = FVBRuleContext::Make(this, GetOwningPlayerState());
	// 정의로 돈다 - 단계 효과가 다른 퀘스트를 시작하면(StartQuest) 배열이 자라 줄의 주소가 바뀐다.
	TArray<const UVBQuestDefinition*, TInlineAllocator<8>> ActiveQuests;
	for (const FVBQuestLogEntry& Entry : List.Entries)
	{
		if (Entry.Definition && Entry.Status == EVBQuestStatus::Active)
		{
			ActiveQuests.Add(Entry.Definition);
		}
	}

	bool bChanged = false;
	for (const UVBQuestDefinition* Quest : ActiveQuests)
	{
		FVBQuestLogEntry* Entry = FindEntry(Quest);
		if (!Entry || Entry->Status != EVBQuestStatus::Active)
		{
			continue;
		}

		if (TryAcknowledge(*Entry, Event))
		{
			List.MarkItemDirty(*Entry);
			bChanged = true;
		}

		if (UpdateProgress(*Entry, &Event, Context))
		{
			List.MarkItemDirty(*Entry);
			bChanged = true;
		}

		if (FVBQuestLogEntry* Again = FindEntry(Quest))
		{
			const int32 StageBefore = Again->StageIndex;
			const EVBQuestStatus StatusBefore = Again->Status;
			TryAdvance(*Again, Context);
			const FVBQuestLogEntry* After = FindEntry(Quest);
			bChanged |= After && (After->StageIndex != StageBefore || After->Status != StatusBefore);
		}
	}

	if (bChanged)
	{
		OnQuestLogChanged.Broadcast();
	}
}

bool UVBQuestLogComponent::EvaluateStageEntry(FVBQuestLogEntry& Entry, const FVBRuleContext& Context)
{
	bool bChanged = UpdateProgress(Entry, nullptr, Context);
	const FVBQuestStage* Stage = GetStage(Entry);
	if (!Stage || Entry.Status != EVBQuestStatus::Active)
	{
		return bChanged;
	}

	Entry.PendingAckObjective = INDEX_NONE;
	for (int32 Index = 0; Index < Stage->Objectives.Num(); ++Index)
	{
		const FVBQuestObjective* Objective = Stage->Objectives[Index].GetPtr<FVBQuestObjective>();
		// 선택 목표는 단계를 붙잡지 않는다 - 기다릴 이유도 없다.
		if (!Objective || Objective->bOptional || !Entry.Progress.IsValidIndex(Index)
			|| Entry.Progress[Index] < Objective->GetRequiredCount())
		{
			continue;
		}

		if (!Objective->WaitsForAcknowledgement())
		{
			// 반만 채운 저작(대화만, 태그만)은 기다리지 않는다 - 확인할 길이 없어 단계가 영영 멈춘다.
			VB_CLOG(Objective->AlreadyMetDialogue || Objective->AlreadyMetAckTag.IsValid(), Warning,
				"퀘스트 %s 단계 %d 목표 %d: 이미 찬 목표의 확인 대화·확인 태그 중 하나가 비었다 - 확인 없이 넘긴다",
				*GetNameSafe(Entry.Definition), Entry.StageIndex, Index);
			continue;
		}

		Entry.PendingAckObjective = Index;
		VB_LOG(Log, "퀘스트 %s 단계 %d 목표 %d: 시작할 때 이미 찼다 - 플레이어 확인(%s)을 기다린다",
			*GetNameSafe(Entry.Definition), Entry.StageIndex, Index, *GetNameSafe(Objective->AlreadyMetDialogue));
		bChanged = true;
		break;
	}

	return bChanged;
}

bool UVBQuestLogComponent::TryAcknowledge(FVBQuestLogEntry& Entry, const FVBGameEvent& Event)
{
	const FVBQuestObjective* Objective = GetPendingObjective(Entry);
	if (!Objective || Event.EventTag != VBGameplayTags::Event_Dialogue_NodeReached
		|| !Event.TargetTags.HasTagExact(Objective->AlreadyMetAckTag))
	{
		return false;
	}

	VB_LOG(Log, "퀘스트 %s 단계 %d: 플레이어가 확인했다 - 단계를 넘긴다", *GetNameSafe(Entry.Definition), Entry.StageIndex);
	Entry.PendingAckObjective = INDEX_NONE;
	return true;
}

void UVBQuestLogComponent::OfferPendingAcknowledgements()
{
	// 일지를 다 고친 뒤에만 부른다. 대화의 시작 노드 효과가 다른 퀘스트를 열면 일지 배열이 다시 잡히므로, 대화를 연 뒤에는 줄을 만지지 않는다.
	const UVBDialogueAsset* ToOffer = nullptr;
	bool bCleared = false;
	for (FVBQuestLogEntry& Entry : List.Entries)
	{
		const FVBQuestObjective* Objective = GetPendingObjective(Entry);
		if (!Objective)
		{
			continue;
		}

		if (Entry.Progress[Entry.PendingAckObjective] < Objective->GetRequiredCount())
		{
			// 기다리는 사이 다시 안 찼다(아이템을 버림) - "이미 가지고 있네" 가 틀린 말이 된다. 대기를 풀고 보통 목표로 돌아간다.
			Entry.PendingAckObjective = INDEX_NONE;
			List.MarkItemDirty(Entry);
			bCleared = true;
			continue;
		}

		ToOffer = ToOffer ? ToOffer : Objective->AlreadyMetDialogue.Get();
	}

	if (bCleared)
	{
		OnQuestLogChanged.Broadcast();
	}

	const APlayerState* PlayerState = GetOwningPlayerState();
	if (!ToOffer || !PlayerState)
	{
		// 소유 PlayerState 가 없는 일지는 단위 시험뿐이다 - 대화를 열 Pawn 이 없다.
		return;
	}

	UVBDialogueComponent* Dialogue = UVBDialogueComponent::FindFor(PlayerState->GetPlayerController());
	if (!Dialogue)
	{
		VB_CLOG(!bWarnedNoAckDialogue, Warning, "퀘스트 일지: 확인을 기다리는데 %s 의 PC·대화 컴포넌트가 없다 - 확인 대화를 못 연다", *GetNameSafe(PlayerState));
		bWarnedNoAckDialogue = true;
		return;
	}
	// 다른 대화 중, 전투 중이면 묻지 않는다(거절 로그를 사건마다 남기지 않으려고) - 다음 사건 때 다시 본다.
	if (Dialogue->IsInDialogue() || Dialogue->IsBlockedByCombat(ToOffer))
	{
		return;
	}
	// 화자는 그 플레이어의 Pawn - 혼잣말이다.
	Dialogue->StartDialogue(ToOffer, PlayerState->GetPawn());
}

bool UVBQuestLogComponent::UpdateProgress(FVBQuestLogEntry& Entry, const FVBGameEvent* Event, const FVBRuleContext& Context)
{
	const FVBQuestStage* Stage = GetStage(Entry);
	if (!Stage || Entry.Status != EVBQuestStatus::Active)
	{
		return false;
	}

	if (Entry.Progress.Num() != Stage->Objectives.Num())
	{
		ResetProgress(Entry);
	}

	bool bChanged = false;
	for (int32 Index = 0; Index < Stage->Objectives.Num(); ++Index)
	{
		const FVBQuestObjective* Objective = Stage->Objectives[Index].GetPtr<FVBQuestObjective>();
		if (!Objective)
		{
			continue;
		}

		const int32 Required = Objective->GetRequiredCount();
		const int32 Old = Entry.Progress[Index];
		int32 New = Old;
		if (Objective->IsStateBased())
		{
			New = Objective->Evaluate(Context) ? Required : 0;
		}
		else if (Event)
		{
			New = FMath::Min(Required, Old + Objective->CountEvent(*Event));
		}

		if (New != Old)
		{
			Entry.Progress[Index] = New;
			bChanged = true;
			VB_LOG(Log, "퀘스트 %s 단계 %d 목표 %d: %d/%d", *GetNameSafe(Entry.Definition), Entry.StageIndex, Index, New, Required);
		}
	}

	return bChanged;
}

void UVBQuestLogComponent::TryAdvance(FVBQuestLogEntry& InEntry, const FVBRuleContext& Context)
{
	const UVBQuestDefinition* Quest = InEntry.Definition;
	// 새 단계가 시작하자마자 차 있으면 이어서 다음 단계로 진행한다. 단계 수만큼이 상한이다.
	for (int32 Guard = 0; Quest && Guard <= Quest->Stages.Num(); ++Guard)
	{
		FVBQuestLogEntry* Entry = FindEntry(Quest);
		const FVBQuestStage* Stage = Entry ? GetStage(*Entry) : nullptr;
		if (!Entry || Entry->Status != EVBQuestStatus::Active || !Stage || !IsStageComplete(*Entry, *Stage))
		{
			return;
		}

		const int32 Finished = Entry->StageIndex;
		const bool bLast = Finished + 1 >= Quest->Stages.Num();
		// 효과보다 상태를 먼저 전환한다 - 효과가 일으킨 이벤트(대기열에 들어간다)가 끝난 단계가 아니라 다음 단계에 세이게.
		if (bLast)
		{
			Entry->Status = EVBQuestStatus::Completed;
			Entry->Progress.Reset();
		}
		else
		{
			Entry->StageIndex = Finished + 1;
			ResetProgress(*Entry);
		}

		Touch(*Entry);
		List.MarkItemDirty(*Entry);
		VBRules::ApplyAll(Quest->Stages[Finished].OnCompleteEffects, Context);
		if (bLast)
		{
			VBRules::ApplyAll(Quest->Rewards, Context);
			VB_LOG(Log, "퀘스트 완료: %s", *GetNameSafe(Quest));
			Announce(Quest, EVBQuestNotice::Completed, VBGameplayTags::Event_Quest_Completed);
			return;
		}

		VB_LOG(Log, "퀘스트 %s: 단계 %d -> %d", *GetNameSafe(Quest), Finished, Finished + 1);
		Announce(Quest, EVBQuestNotice::StageAdvanced, VBGameplayTags::Event_Quest_StageAdvanced);
		// 새 단계의 상태 목표를 본다 - 이미 차 있을 수 있다.
		if (FVBQuestLogEntry* Next = FindEntry(Quest); Next && EvaluateStageEntry(*Next, Context))
		{
			List.MarkItemDirty(*Next);
		}
	}
}

void UVBQuestLogComponent::ResetProgress(FVBQuestLogEntry& Entry)
{
	const FVBQuestStage* Stage = GetStage(Entry);
	Entry.Progress.Init(0, Stage ? Stage->Objectives.Num() : 0);
}

void UVBQuestLogComponent::HandleStoryChanged()
{
	// 사실이 바뀌면 조건 목표가 바뀔 수 있다. 상태 목표만 다시 본다(UpdateProgress 에 사건 없이) - 사건 목표는 안 센다.
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}

	if (!StoryBinding.GetStory())
	{
		return;
	}

	const UVBSaveParticipantSubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr;
	if (Registry && Registry->IsApplyingSave())
	{
		// 세이브 적용 중 - 인벤토리 같은 다른 참여자가 아직 안 읽혔을 수 있다. 지금 단계를 진행하면 낡은 값으로 판단하고,
		//   그때 준 보상은 뒤이은 인벤토리 읽기가 덮어 사라진다(검증 W3). 적용이 끝나면 한 번 본다(HandleSaveApplied).
		bStoryChangedWhileApplyingSave = true;
		return;
	}

	if (bProcessing)
	{
		// 처리 중의 사실 변경은 단계 효과가 낸 것이다 - 그 퀘스트는 TryAdvance 가 효과 뒤에 새 단계의 상태 목표를 다시 본다.
		//   다른 퀘스트의 조건 목표는 다음 사건 때 다시 본다(한 박자 늦을 수 있다 - 기록만).
		return;
	}

	bool bChanged = false;
	RunProcessingSection([this, &bChanged]()
	{
		const FVBRuleContext Context = FVBRuleContext::Make(this, GetOwningPlayerState());
		TArray<const UVBQuestDefinition*, TInlineAllocator<8>> ActiveQuests;
		for (const FVBQuestLogEntry& Entry : List.Entries)
		{
			if (Entry.Definition && Entry.Status == EVBQuestStatus::Active)
			{
				ActiveQuests.Add(Entry.Definition);
			}
		}

		for (const UVBQuestDefinition* Quest : ActiveQuests)
		{
			if (FVBQuestLogEntry* Entry = FindEntry(Quest); Entry && UpdateProgress(*Entry, nullptr, Context))
			{
				List.MarkItemDirty(*Entry);
				bChanged = true;
			}

			if (FVBQuestLogEntry* Entry = FindEntry(Quest))
			{
				TryAdvance(*Entry, Context);
			}
		}
	});
	if (bChanged)
	{
		OnQuestLogChanged.Broadcast();
	}
}

void UVBQuestLogComponent::Announce(const UVBQuestDefinition* Quest, EVBQuestNotice Notice, FGameplayTag EventTag)
{
	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		FVBGameEvent Event;
		Event.EventTag = EventTag;
		Event.TargetTags.AddTag(Quest->QuestId);
		Event.Instigator = GetOwningPlayerState();
		Events->Broadcast(Event);
	}
	// 소유 클라이언트의 화면 알림. 리슨 서버 호스트면 이 자리에서 바로 돈다.
	ClientQuestNotice(Quest, Notice);
}

void UVBQuestLogComponent::Touch(FVBQuestLogEntry& Entry)
{
	Entry.RecentOrder = NextRecentOrder++;
}

void UVBQuestLogComponent::ClientQuestNotice_Implementation(const UVBQuestDefinition* Quest, EVBQuestNotice Notice)
{
	OnQuestNotice.Broadcast(Quest, Notice);
}

// ---------------------------------------------------------------- 저장

void UVBQuestLogComponent::WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner())
	{
		return;
	}

	Save.Quests.Quests.Reset();
	for (const FVBQuestLogEntry& Entry : List.Entries)
	{
		if (!Entry.Definition)
		{
			continue;
		}

		FVBSavedQuest& Saved = Save.Quests.Quests.AddDefaulted_GetRef();
		Saved.QuestId = Entry.Definition->QuestId;
		Saved.bCompleted = Entry.Status == EVBQuestStatus::Completed;
		Saved.StageIndex = Entry.StageIndex;
		Saved.Progress = Entry.Progress;
	}
	// 읽을 때 못 찾은 줄을 그대로 다시 쓴다 - 버리면 퀘스트 자산, QuestId 를 바꾼 한 번이 영영 진행을 지운다(검증 W2).
	Save.Quests.Quests.Append(UnresolvedSavedQuests);
}

void UVBQuestLogComponent::ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context)
{
	if (Context.PlayerState != GetOwner() || !HasWriteAuthority(TEXT("ReadSave")))
	{
		return;
	}

	ResolveSavedQuests(Save.Quests.Quests, [](FGameplayTag QuestId)
	{
		// 저장은 태그다 - 에셋 매니저가 /Game/VB/Narrative/Quest 를 훑어 둔 목록에서 되찾는다(UVBQuestDefinition 주석).
		const FSoftObjectPath Path = UAssetManager::Get().GetPrimaryAssetPath(UVBQuestDefinition::MakeQuestAssetId(QuestId));
		return Cast<UVBQuestDefinition>(Path.TryLoad());
	}, List.Entries, UnresolvedSavedQuests, NextRecentOrder);
	List.MarkArrayDirty();
	VB_LOG(Log, "퀘스트 로드: %d 줄(못 찾음 %d)", List.Entries.Num(), UnresolvedSavedQuests.Num());
	OnQuestLogChanged.Broadcast();
}

void UVBQuestLogComponent::ResolveSavedQuests(const TArray<FVBSavedQuest>& SavedQuests,
	TFunctionRef<const UVBQuestDefinition*(FGameplayTag)> Resolve, TArray<FVBQuestLogEntry>& OutEntries,
	TArray<FVBSavedQuest>& OutUnresolved, int32& InOutNextRecentOrder)
{
	OutEntries.Reset();
	OutUnresolved.Reset();
	for (const FVBSavedQuest& Saved : SavedQuests)
	{
		const UVBQuestDefinition* Quest = Resolve(Saved.QuestId);
		if (!Quest || Quest->Stages.IsEmpty())
		{
			// 버리지 않고 지킨다 - 다음 저장에 그대로 다시 쓴다(WriteSave). 자산, QuestId 를 되살리면 진행이 돌아온다.
			VB_LOG(Warning, "퀘스트 로드: %s 를 되찾지 못했다(퀘스트 자산·QuestId·에셋 매니저 설정을 본다) - 저장 줄은 지킨다", *Saved.QuestId.ToString());
			OutUnresolved.Add(Saved);
			continue;
		}

		FVBQuestLogEntry& Entry = OutEntries.AddDefaulted_GetRef();
		Entry.Definition = Quest;
		Entry.Status = Saved.bCompleted ? EVBQuestStatus::Completed : EVBQuestStatus::Active;
		Entry.StageIndex = Saved.StageIndex;
		if (Entry.Status == EVBQuestStatus::Active && !Quest->Stages.IsValidIndex(Entry.StageIndex))
		{
			// 저장 뒤에 퀘스트 단계가 줄었다. 마지막 단계로 둔다 - 일지에서 사라지는 것보다 덜 나쁘다.
			VB_LOG(Warning, "퀘스트 로드: %s 단계 %d 가 없다(단계 %d 개) - 마지막 단계로 둔다", *GetNameSafe(Quest), Saved.StageIndex, Quest->Stages.Num());
			Entry.StageIndex = Quest->Stages.Num() - 1;
		}

		Entry.Progress = Saved.Progress;
		if (Entry.Status == EVBQuestStatus::Active && Entry.Progress.Num() != Quest->Stages[Entry.StageIndex].Objectives.Num())
		{
			VB_LOG(Warning, "퀘스트 로드: %s 단계 %d 의 목표 수가 바뀌었다 - 진행을 0 부터 다시 센다", *GetNameSafe(Quest), Entry.StageIndex);
			ResetProgress(Entry);
		}

		Entry.RecentOrder = InOutNextRecentOrder++;
	}
}

// ---------------------------------------------------------------- 생명 주기

void UVBQuestLogComponent::InitializeComponent()
{
	Super::InitializeComponent();
	List.Owner = this;
	// 등록은 BeginPlay 가 아니라 여기 - Continue 적용이 월드 BeginPlay 보다 먼저 올 수 있다(IVBSaveParticipant 주석).
	UVBSaveParticipantSubsystem::Register(this);
}

void UVBQuestLogComponent::UninitializeComponent()
{
	UVBSaveParticipantSubsystem::Unregister(this);
	Super::UninitializeComponent();
}

void UVBQuestLogComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!VBServerWrite::HasOwnerAuthority(*this))
	{
		return;
	}
	// 사건을 듣는 것은 서버뿐이다(사건 방송도 서버뿐).
	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		GameEventHandle = Events->OnGameEvent.AddUObject(this, &UVBQuestLogComponent::HandleGameEvent);
	}
	else
	{
		VB_LOG(Warning, "퀘스트 일지: 사건 방송을 못 찾았다 - 퀘스트가 진행되지 않는다");
	}

	if (UVBSaveParticipantSubsystem* Registry = GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>())
	{
		SaveAppliedHandle = Registry->OnSaveApplied.AddUObject(this, &UVBQuestLogComponent::HandleSaveApplied);
	}
	// Refresh 가 바로 판정할 수 있으니 저장 완료 listener 가 먼저 있어야 한다.
	StoryBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &UVBQuestLogComponent::HandleStoryChanged));
}

void UVBQuestLogComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StoryBinding.Disconnect();
	if (UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(this))
	{
		Events->OnGameEvent.Remove(GameEventHandle);
	}

	if (UVBSaveParticipantSubsystem* Registry = GetWorld() ? GetWorld()->GetSubsystem<UVBSaveParticipantSubsystem>() : nullptr)
	{
		Registry->OnSaveApplied.Remove(SaveAppliedHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UVBQuestLogComponent::HandleSaveApplied()
{
	if (bStoryChangedWhileApplyingSave)
	{
		bStoryChangedWhileApplyingSave = false;
		HandleStoryChanged();
	}
}

void UVBQuestLogComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 일지는 그 플레이어만 안다(설계 3.3절).
	DOREPLIFETIME_CONDITION(UVBQuestLogComponent, List, COND_OwnerOnly);
}

APlayerState* UVBQuestLogComponent::GetOwningPlayerState() const
{
	return Cast<APlayerState>(GetOwner());
}

bool UVBQuestLogComponent::HasWriteAuthority(const TCHAR* What) const
{
	return VBServerWrite::IsAllowed(*this, TEXT("퀘스트 일지"), What);
}

FVBQuestLogEntry* UVBQuestLogComponent::FindEntry(const UVBQuestDefinition* Quest)
{
	return Quest ? List.Entries.FindByPredicate([Quest](const FVBQuestLogEntry& E) { return E.Definition == Quest; }) : nullptr;
}

const FVBQuestLogEntry* UVBQuestLogComponent::FindEntry(const UVBQuestDefinition* Quest) const
{
	return Quest ? List.Entries.FindByPredicate([Quest](const FVBQuestLogEntry& E) { return E.Definition == Quest; }) : nullptr;
}
