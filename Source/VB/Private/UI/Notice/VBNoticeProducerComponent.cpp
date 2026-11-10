// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Notice/VBNoticeProducerComponent.h"
#include "UI/Notice/VBNoticeSubsystem.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Data/VBAbilityFailureMessageData.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Interaction/VBInteractorComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Tutorial/VBTutorialComponent.h"
#include "Player/VBPlayerController.h"
#include "Progression/VBProgressionComponent.h"
#include "Progression/VBSkillTreeDefinition.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "VB/VB.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UVBNoticeProducerComponent::UVBNoticeProducerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SaveCompletedText = NSLOCTEXT("VBHUD", "SaveCompleted", "저장됨");
	ItemAcquiredFormat = NSLOCTEXT("VBHUD", "ItemAcquired", "{0} x{1} 획득");
	QuestStartedFormat = NSLOCTEXT("VBHUD", "QuestStarted", "새 퀘스트: {0}");
	QuestAdvancedFormat = NSLOCTEXT("VBHUD", "QuestAdvanced", "퀘스트 갱신: {0}");
	QuestCompletedFormat = NSLOCTEXT("VBHUD", "QuestCompleted", "퀘스트 완료: {0}");
	LevelUpFormat = NSLOCTEXT("VBHUD", "LevelUp", "레벨 {0} - 스킬 포인트를 얻었다");
	SkillUnlockedFormat = NSLOCTEXT("VBHUD", "SkillUnlocked", "해금: {0}");
	TutorialCompletedFormat = NSLOCTEXT("VBHUD", "TutorialCompleted", "안내 완료: {0}");
}

void UVBNoticeProducerComponent::BeginPlay()
{
	Super::BeginPlay();
	AVBPlayerController* PC = GetOwner<AVBPlayerController>();
	// 원격 PC(리슨 서버가 든 남의 PC)는 화면이 없다 - 여기서 만들면 호스트 화면에 남의 알림이 뜬다.
	if (!PC || !PC->IsLocalController())
	{
		return;
	}

	if (!SaveCompletedText.IsEmpty())
	{
		if (UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this))
		{
			Save->OnGameSaveCompleted.AddUniqueDynamic(this, &UVBNoticeProducerComponent::HandleGameSaveCompleted);
			bSaveBound = true;
		}
	}

	PC->OnPossessedPawnChanged.AddUniqueDynamic(this, &UVBNoticeProducerComponent::HandlePossessedPawnChanged);
	BindPawn(PC->GetPawn());
	// 이미 PlayerState 가 있으면 바로 한 번 불린다.
	PlayerStateSetHandle = PC->CallAndRegister_OnPlayerStateSet(
		FVBOnPlayerStateSet::FDelegate::CreateUObject(this, &UVBNoticeProducerComponent::HandlePlayerStateSet));
}

void UVBNoticeProducerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AVBPlayerController* PC = GetOwner<AVBPlayerController>())
	{
		PC->UnregisterOnPlayerStateSet(PlayerStateSetHandle);
		PC->OnPossessedPawnChanged.RemoveDynamic(this, &UVBNoticeProducerComponent::HandlePossessedPawnChanged);
	}

	PlayerStateSetHandle.Reset();
	if (bSaveBound)
	{
		if (UVBSaveGameSubsystem* Save = UVBSaveGameSubsystem::Get(this))
		{
			Save->OnGameSaveCompleted.RemoveDynamic(this, &UVBNoticeProducerComponent::HandleGameSaveCompleted);
		}

		bSaveBound = false;
	}

	UnbindPawn();
	UnbindPlayerStateParts();
	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
EDataValidationResult UVBNoticeProducerComponent::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// 표가 없으면 실행 중에는 첫 실패 때 경고 한 줄뿐이다 - 저작 시점에 막는다. 표 자체의 행 검사는 그 자산이 한다.
	if (!AbilityFailureMessages)
	{
		Context.AddError(NSLOCTEXT("VBNotice", "MissingFailureMessages",
			"NoticeProducer 에 AbilityFailureMessages 가 비어 있다 - 능력 실패 알림이 하나도 뜨지 않는다."));
		Result = EDataValidationResult::Invalid;
	}

	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

UVBNoticeSubsystem* UVBNoticeProducerComponent::GetNoticeSink() const
{
	return UVBNoticeSubsystem::Get(GetOwner<APlayerController>());
}

void UVBNoticeProducerComponent::Push(FVBNoticeRequest&& Request) const
{
	if (UVBNoticeSubsystem* Notices = GetNoticeSink())
	{
		Notices->Push(MoveTemp(Request));
	}
}

void UVBNoticeProducerComponent::HandlePlayerStateSet(APlayerState* PlayerState)
{
	BindPlayerStateParts(PlayerState);
}

void UVBNoticeProducerComponent::BindPlayerStateParts(APlayerState* PlayerState)
{
	if (PlayerState == BoundPlayerState.Get() && PlayerState)
	{
		return;
	}

	UnbindPlayerStateParts();
	if (!PlayerState)
	{
		return;
	}

	BoundPlayerState = PlayerState;
	if (UVBAbilitySystemComponent* ASC =
		Cast<UVBAbilitySystemComponent>(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerState)))
	{
		BoundFailureASC = ASC;
		AbilityFailureHandle = ASC->OnPlayerAbilityFailure.AddUObject(this, &UVBNoticeProducerComponent::HandlePlayerAbilityFailure);
	}
	// 부품 넷은 PlayerState 의 생성자 하위 객체라 PlayerState 가 정해지면 함께 있다 - 기다리는 경로가 없다.
	if (UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(PlayerState); Inventory && !ItemAcquiredFormat.IsEmpty())
	{
		BoundInventory = Inventory;
		ItemAcquiredHandle = Inventory->OnItemAcquired.AddUObject(this, &UVBNoticeProducerComponent::HandleItemAcquired);
	}

	if (UVBQuestLogComponent* QuestLog = UVBQuestLogComponent::FindFor(PlayerState))
	{
		BoundQuestLog = QuestLog;
		QuestNoticeHandle = QuestLog->OnQuestNotice.AddUObject(this, &UVBNoticeProducerComponent::HandleQuestNotice);
	}

	if (UVBProgressionComponent* Progression = UVBProgressionComponent::FindFor(PlayerState))
	{
		BoundProgression = Progression;
		ProgressionChangedHandle = Progression->OnProgressionChanged.AddUObject(this,
			&UVBNoticeProducerComponent::HandleProgressionChanged);
		// 묶는 순간의 레벨, 해금은 기준만 잡는다(처음 본 값은 알리지 않는다).
		HandleProgressionChanged();
	}

	if (UVBTutorialComponent* Tutorial = UVBTutorialComponent::FindFor(PlayerState))
	{
		BoundTutorial = Tutorial;
		TutorialNoticeHandle = Tutorial->OnCompletionNoticesChanged.AddUObject(this,
			&UVBNoticeProducerComponent::HandleTutorialCompletionNotices);
		// 묶기 전에 쌓여 있던 완료도 지금 처리한다 - 컴포넌트가 늦은 소비자를 위해 보관하던 것.
		HandleTutorialCompletionNotices();
	}
}

void UVBNoticeProducerComponent::UnbindPlayerStateParts()
{
	if (UVBAbilitySystemComponent* ASC = BoundFailureASC.Get())
	{
		ASC->OnPlayerAbilityFailure.Remove(AbilityFailureHandle);
	}

	BoundFailureASC.Reset();
	AbilityFailureHandle.Reset();
	if (UVBInventoryComponent* Inventory = BoundInventory.Get())
	{
		Inventory->OnItemAcquired.Remove(ItemAcquiredHandle);
	}

	if (UVBQuestLogComponent* QuestLog = BoundQuestLog.Get())
	{
		QuestLog->OnQuestNotice.Remove(QuestNoticeHandle);
	}

	if (UVBProgressionComponent* Progression = BoundProgression.Get())
	{
		Progression->OnProgressionChanged.Remove(ProgressionChangedHandle);
	}
	// 완료 큐는 비우지 않는다 - 다음에 묶는 소비자가 가져간다.
	if (UVBTutorialComponent* Tutorial = BoundTutorial.Get())
	{
		Tutorial->OnCompletionNoticesChanged.Remove(TutorialNoticeHandle);
	}

	BoundPlayerState.Reset();
	BoundInventory.Reset();
	BoundQuestLog.Reset();
	BoundProgression.Reset();
	BoundTutorial.Reset();
	ItemAcquiredHandle.Reset();
	QuestNoticeHandle.Reset();
	ProgressionChangedHandle.Reset();
	TutorialNoticeHandle.Reset();
	NoticeBaseline = VBGrowth::FNoticeBaseline();
}

void UVBNoticeProducerComponent::HandlePlayerAbilityFailure(const FVBAbilityFailureReport& Report)
{
	const APlayerController* PC = GetOwner<APlayerController>();
	const UVBAbilitySystemComponent* ASC = BoundFailureASC.Get();
	if (!PC || !PC->IsLocalController() || !PC->GetLocalPlayer() || PC->PlayerState != BoundPlayerState.Get()
		|| !ASC || !ASC->AbilityActorInfo.IsValid() || ASC->AbilityActorInfo->PlayerController.Get() != PC
		|| !Report.Avatar.IsValid() || Report.Avatar.Get() != ASC->GetAvatarActor_Direct()
		|| Report.Avatar.Get() != PC->GetPawn())
	{
		return;
	}

	const FVBAbilityFailureMessageRow* Row = AbilityFailureMessages ? AbilityFailureMessages->FindRow(Report.FailureTags) : nullptr;
	if (!Row)
	{
		if (!bWarnedFailureMessages)
		{
			VB_LOG(Warning, "Ability failure notice data is missing or invalid on %s", *GetNameSafe(PC));
			bWarnedFailureMessages = true;
		}

		return;
	}
	// 조용히 둔 사유는 데이터의 결정이다 - 화면에 아무것도 내지 않는다.
	if (Row->bSilent)
	{
		return;
	}

	Push(FVBNoticeRequest::FromText(EVBNoticeCategory::Refusal, Row->Message));
}

void UVBNoticeProducerComponent::HandlePossessedPawnChanged(APawn* /*OldPawn*/, APawn* NewPawn)
{
	BindPawn(NewPawn);
}

void UVBNoticeProducerComponent::BindPawn(APawn* Pawn)
{
	UVBInteractorComponent* Interactor = Pawn ? Pawn->FindComponentByClass<UVBInteractorComponent>() : nullptr;
	if (Interactor == BoundInteractor.Get() && Interactor)
	{
		return;
	}

	UnbindPawn();
	if (!Interactor)
	{
		return;
	}

	BoundInteractor = Interactor;
	InteractionRefusedHandle = Interactor->OnInteractionRefused.AddUObject(this, &UVBNoticeProducerComponent::HandleInteractionRefused);
}

void UVBNoticeProducerComponent::UnbindPawn()
{
	if (UVBInteractorComponent* Interactor = BoundInteractor.Get())
	{
		Interactor->OnInteractionRefused.Remove(InteractionRefusedHandle);
	}

	BoundInteractor.Reset();
	InteractionRefusedHandle.Reset();
}

void UVBNoticeProducerComponent::HandleGameSaveCompleted(int32 /*SlotIndex*/, bool bSuccess)
{
	if (bSuccess)
	{
		Push(FVBNoticeRequest::FromText(EVBNoticeCategory::System, SaveCompletedText, TEXT("Save")));
	}
}

void UVBNoticeProducerComponent::HandleItemAcquired(const UVBItemDefinition* Item, int32 Count)
{
	if (!Item)
	{
		return;
	}
	// 같은 아이템을 연달아 얻으면 한 줄에 합친다("붕대 x3 획득"). 글은 합칠 때마다 다시 만들므로 문구와 이름을 값으로 담는다 - this 를 잡지 않는다.
	FVBNoticeRequest Request;
	Request.Category = EVBNoticeCategory::Item;
	Request.MergeKey = Item->GetFName();
	Request.Count = Count;
	Request.MakeText = [Format = ItemAcquiredFormat, Name = Item->DisplayName](int32 Total) { return FText::Format(Format, Name, Total); };
	Push(MoveTemp(Request));
}

void UVBNoticeProducerComponent::HandleQuestNotice(const UVBQuestDefinition* Quest, EVBQuestNotice Kind)
{
	const FText& Format = Kind == EVBQuestNotice::Started ? QuestStartedFormat
		: Kind == EVBQuestNotice::Completed ? QuestCompletedFormat : QuestAdvancedFormat;
	if (Quest && !Format.IsEmpty())
	{
		Push(FVBNoticeRequest::FromText(EVBNoticeCategory::Quest, FText::Format(Format, Quest->Title)));
	}
}

void UVBNoticeProducerComponent::HandleProgressionChanged()
{
	const UVBProgressionComponent* Progression = BoundProgression.Get();
	if (!Progression)
	{
		return;
	}
	// 무엇을 알릴지는 성장 규칙 한 곳이 정한다 - 처음 본 값과 세이브 읽기로 바뀐 값은 기준만 잡고, 그 뒤의 오름, 해금만 알린다.
	//   한 번에 여러 레벨이 오르면 한 줄(최신 레벨)로 합친다.
	const VBGrowth::FNotices Notices = VBGrowth::StepNoticeBaseline(NoticeBaseline, Progression->GetLevel(),
		Progression->GetUnlocked(), Progression->GetSaveReadSerial());
	if (Notices.LevelUpTo > 0 && !LevelUpFormat.IsEmpty())
	{
		Push(FVBNoticeRequest::FromText(EVBNoticeCategory::Progression, FText::Format(LevelUpFormat, Notices.LevelUpTo), TEXT("LevelUp")));
	}

	if (SkillUnlockedFormat.IsEmpty())
	{
		return;
	}

	const UVBSkillTreeDefinition* Tree = Progression->GetSkillTree();
	for (const FGameplayTag& NodeId : Notices.NewlyUnlocked)
	{
		const FVBSkillNode* Node = Tree ? Tree->FindNode(NodeId) : nullptr;
		const FText Name = Node && !Node->DisplayName.IsEmpty() ? Node->DisplayName : FText::FromName(NodeId.GetTagName());
		Push(FVBNoticeRequest::FromText(EVBNoticeCategory::Progression, FText::Format(SkillUnlockedFormat, Name)));
	}
}

void UVBNoticeProducerComponent::HandleTutorialCompletionNotices()
{
	UVBTutorialComponent* Tutorial = BoundTutorial.Get();
	// 넣을 곳이나 문구가 없으면 큐를 그대로 둔다 - 꺼내는 순간 큐가 비므로(단일 소비자) 버리게 된다. 다음 신호나 다시 묶을 때 가져간다.
	UVBNoticeSubsystem* Notices = GetNoticeSink();
	if (!Tutorial || !Notices || TutorialCompletedFormat.IsEmpty())
	{
		return;
	}

	for (const FText& Title : Tutorial->TakeCompletionNotices())
	{
		Notices->PushText(EVBNoticeCategory::System, FText::Format(TutorialCompletedFormat, Title));
	}
}

void UVBNoticeProducerComponent::HandleInteractionRefused(const FText& RefusedText)
{
	Push(FVBNoticeRequest::FromText(EVBNoticeCategory::Refusal, RefusedText, TEXT("InteractionRefused")));
}
