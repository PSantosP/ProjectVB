// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Narrative/Story/VBGameEventSubsystem.h"   // FVBGameEvent - 처리 중에 온 사건을 값으로 쌓는다
#include "Narrative/Story/VBStoryStateBinding.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "SaveSystem/VBSaveGameTypes.h"   // FVBSavedQuest - 못 찾은 저장 줄을 값으로 지킨다
#include "VBQuestLogComponent.generated.h"

class APlayerState;
class UVBQuestDefinition;
class UVBQuestLogComponent;
struct FVBRuleContext;

UENUM(BlueprintType)
enum class EVBQuestStatus : uint8
{
	// 일지에 없는 상태. 조건이 아직 안 받은 퀘스트를 물을 때만 쓰인다.
	NotStarted,
	Active,
	Completed,
};

/** 일지의 한 줄(퀘스트당 한 줄). */
USTRUCT()
struct FVBQuestLogEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UVBQuestDefinition> Definition = nullptr;

	UPROPERTY()
	EVBQuestStatus Status = EVBQuestStatus::Active;

	UPROPERTY()
	int32 StageIndex = 0;

	// 지금 단계의 목표별 진행(목표와 같은 순서). 단계가 넘어가면 0 부터 다시.
	UPROPERTY()
	TArray<int32> Progress;

	// 마지막으로 시작, 진행된 순번(서버가 올림). HUD 가 따라가는 퀘스트를 고를 때 가장 큰 것을 쓴다.
	UPROPERTY()
	int32 RecentOrder = 0;

	// 확인을 기다리는 목표 번호(없으면 INDEX_NONE). 저장하지 않는다 - 불러온 뒤에는 확인한 것으로 친다.
	UPROPERTY()
	int32 PendingAckObjective = INDEX_NONE;

	void PostReplicatedAdd(const struct FVBQuestLogList& List);
	void PostReplicatedChange(const struct FVBQuestLogList& List);
	void PreReplicatedRemove(const struct FVBQuestLogList& List);
};

USTRUCT()
struct FVBQuestLogList : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FVBQuestLogEntry> Entries;

	UPROPERTY(NotReplicated)
	TObjectPtr<UVBQuestLogComponent> Owner = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FVBQuestLogEntry, FVBQuestLogList>(Entries, DeltaParms, *this);
	}
};

template <>
struct TStructOpsTypeTraits<FVBQuestLogList> : public TStructOpsTypeTraitsBase2<FVBQuestLogList>
{
	enum { WithNetDeltaSerializer = true };
};

UENUM()
enum class EVBQuestNotice : uint8
{
	Started,
	StageAdvanced,
	Completed,
};

DECLARE_MULTICAST_DELEGATE(FVBOnQuestLogChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnQuestNotice, const UVBQuestDefinition*, EVBQuestNotice);

/**
 * 퀘스트 진행을 관리한다. AVBPlayerState 에 붙고 쓰기는 서버만 한다. 시작은 StartQuest, 진행은 사건 구독 한 경로뿐이다.
 * 이 플레이어나 세계가 일으킨 사건만 센다. 처리 중에 온 사건은 줄을 세웠다가 끝난 뒤 처리한다(재진입 방지).
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBQuestLogComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBQuestLogComponent();

	// 폰, 컨트롤러, PlayerState 어느 쪽을 줘도 그 플레이어의 일지. 없으면 nullptr.
	static UVBQuestLogComponent* FindFor(const AActor* Actor);

	EVBQuestStatus GetStatus(const UVBQuestDefinition* Quest) const;
	// 진행 중이 아니면 INDEX_NONE.
	int32 GetStageIndex(const UVBQuestDefinition* Quest) const;
	const TArray<FVBQuestLogEntry>& GetEntries() const { return List.Entries; }

	// 진행 중 퀘스트 중 가장 최근에 시작, 진행된 것(HUD 목표 줄). 없으면 nullptr.
	const FVBQuestLogEntry* GetTrackedEntry() const;

	// 서버에서만. 이미 일지에 있으면(진행, 완료) 다시 시작하지 않고 false.
	bool StartQuest(const UVBQuestDefinition* Quest);

	// 서버: 사건 하나를 센다. 사건 방송을 듣는 자리이고, 시험이 직접 부르기도 한다. 처리 구간 안에서 오면 줄만 선다(RunProcessingSection).
	void HandleGameEvent(const FVBGameEvent& Event);

	FVBOnQuestLogChanged OnQuestLogChanged;
	FVBOnQuestNotice OnQuestNotice;

	//~ IVBSaveParticipant - 퀘스트 구역(UVBSaveGame::Quests). 문맥의 PlayerState 가 소유자일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

	// 저장 줄 -> 일지 줄. 되찾지 못한 줄은 OutUnresolved 로 보내 다음 저장에 다시 쓴다.
	static void ResolveSavedQuests(const TArray<FVBSavedQuest>& SavedQuests,
		TFunctionRef<const UVBQuestDefinition*(FGameplayTag)> Resolve, TArray<FVBQuestLogEntry>& OutEntries,
		TArray<FVBSavedQuest>& OutUnresolved, int32& InOutNextRecentOrder);

	// 복제 콜백(FVBQuestLogEntry)이 부른다.
	void HandleReplicatedChange() { OnQuestLogChanged.Broadcast(); }

protected:
	virtual void InitializeComponent() override;
	virtual void UninitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	FVBQuestLogList List;

	UFUNCTION(Client, Unreliable)
	void ClientQuestNotice(const UVBQuestDefinition* Quest, EVBQuestNotice Notice);

	// 한 줄의 지금 단계를 사건 하나로 센다(Event 가 null 이면 상태 목표만 다시 본다). 바뀌었으면 참.
	bool UpdateProgress(FVBQuestLogEntry& Entry, const FVBGameEvent* Event, const FVBRuleContext& Context);

	// 현재 단계가 다 찼으면 효과를 적용하고 다음 단계로 전환한다(새 단계가 이미 차 있으면 이어서). 마지막이면 보상, 완료.
	void TryAdvance(FVBQuestLogEntry& Entry, const FVBRuleContext& Context);

	// 단계 시작 순간의 판정. 이미 찬 목표가 확인을 기다려야 하면 대기만 세운다. 바뀌었으면 참.
	// 사실 변경, 세이브 적용 뒤 재평가는 이것을 쓰지 않는다(불러오자마자 대화가 뜨면 안 된다).
	bool EvaluateStageEntry(FVBQuestLogEntry& Entry, const FVBRuleContext& Context);

	// 사건이 대기 중인 목표의 확인(대화 확인 노드 도착)이면 대기를 풀고 참. 다른 일은 하지 않는다.
	bool TryAcknowledge(FVBQuestLogEntry& Entry, const FVBGameEvent& Event);

	// 처리가 끝난 뒤 한 번: 대기 중인 확인 대화를 연다(대화, 전투 중이면 다음 기회에).
	// 줄 참조를 들고 열지 않는다 - 대화 효과가 다른 퀘스트를 열면 일지 배열이 다시 잡힌다.
	void OfferPendingAcknowledgements();

	// 지금 단계에 맞게 진행 칸을 0 으로 다시 만든다.
	static void ResetProgress(FVBQuestLogEntry& Entry);

	// 이름이 ProcessEvent 면 UObject::ProcessEvent(RPC 생성 코드가 부른다)를 가린다 - 그래서 GameEvent 를 붙인다.
	void ProcessGameEvent(const FVBGameEvent& Event);
	void HandleStoryChanged();
	// 세이브 적용이 끝났다. 적용 중에 사실이 바뀌었으면 그때 미룬 판단을 한 번 한다.
	void HandleSaveApplied();
	void Announce(const UVBQuestDefinition* Quest, EVBQuestNotice Notice, FGameplayTag EventTag);
	void Touch(FVBQuestLogEntry& Entry);

	APlayerState* GetOwningPlayerState() const;
	bool HasWriteAuthority(const TCHAR* What) const;
	FVBQuestLogEntry* FindEntry(const UVBQuestDefinition* Quest);
	const FVBQuestLogEntry* FindEntry(const UVBQuestDefinition* Quest) const;

	FDelegateHandle GameEventHandle;
	FVBStoryStateBinding StoryBinding;
	FDelegateHandle SaveAppliedHandle;
	int32 NextRecentOrder = 1;

	// 세이브 적용 중에 사실이 바뀌어 판단을 미뤘다(HandleStoryChanged).
	bool bStoryChangedWhileApplyingSave = false;

	// 확인 대화를 열 PC, 대화 컴포넌트가 없다고 이미 말했다(한 번만).
	bool bWarnedNoAckDialogue = false;

	// 되찾지 못한 저장 줄. 다음 저장에 그대로 다시 쓴다(ReadSave).
	TArray<FVBSavedQuest> UnresolvedSavedQuests;

	// 처리 구간 하나: Body 동안의 사건은 줄을 서고, 끝에서 상한까지 처리한 뒤 확인 대화를 내민다(넘친 것은 경고 후 버림).
	// 중첩 호출은 Body 만 돌고 비우기는 바깥 구간이 한다. bProcessing 은 여기서만 바뀐다.
	void RunProcessingSection(TFunctionRef<void()> Body);
	static constexpr int32 MaxChainedEventsPerSection = 64; // 퀘스트끼리 서로를 여닫는 저작 고리를 끊는 상한

	// 처리 중 재진입 막기 - 규칙은 RunProcessingSection 주석.
	bool bProcessing = false;
	TArray<FVBGameEvent> PendingEvents;
};
