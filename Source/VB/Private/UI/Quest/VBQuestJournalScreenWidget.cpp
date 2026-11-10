// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Quest/VBQuestJournalScreenWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Input/CommonUIInputTypes.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Quest/VBQuestText.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBQuestJournal"

void UVBQuestJournalScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (CloseInputAction)
	{
		// 메뉴 화면이라 바인딩 기본 모드(Menu)가 맞다 - HUD 의 여는 바인딩만 Game 이다.
		RegisterUIActionBinding(FBindUIActionArgs(CloseInputAction, /*bShouldDisplayInActionBar=*/false,
			FSimpleDelegate::CreateUObject(this, &UVBQuestJournalScreenWidget::HandleCloseInput)));
	}
}

void UVBQuestJournalScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	Selected.Reset();
	// 일지는 PlayerState 에 있다. 원격 클라는 늦게 올 수 있고, 이미 있으면 여기서 바로 묶고 그린다.
	AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer());
	if (PC)
	{
		PlayerStateSet.Register(*PC, FVBOnPlayerStateSet::FDelegate::CreateUObject(this, &UVBQuestJournalScreenWidget::BindQuestLog));
	}
	else
	{
		VB_LOG(Warning, "퀘스트 일지 화면: 주인이 AVBPlayerController 가 아니다 - PlayerState 를 기다릴 수 없다");
	}

	if (!PC || !PC->PlayerState)
	{
		Rebuild();
	}
}

void UVBQuestJournalScreenWidget::NativeOnDeactivated()
{
	PlayerStateSet.Unregister();
	QuestLog.Reset();
	Super::NativeOnDeactivated();
}

void UVBQuestJournalScreenWidget::BindQuestLog(APlayerState* /*PlayerState*/)
{
	UVBQuestLogComponent* Found = UVBQuestLogComponent::FindFor(GetOwningPlayer());
	QuestLog.Bind(Found, &UVBQuestLogComponent::OnQuestLogChanged, this, &UVBQuestJournalScreenWidget::Rebuild);
	VB_CLOG(!Found, Warning, "퀘스트 일지 화면: PlayerState 는 있는데 퀘스트 일지가 없다(PlayerState 에 UVBQuestLogComponent)");
	Rebuild();
}

int32 UVBQuestJournalScreenWidget::BuildRows()
{
	RowQuests.Reset();
	const UVBQuestLogComponent* Log = QuestLog.Get();
	// 진행 중을 위에(최근 것부터), 끝난 것을 아래에.
	TArray<const FVBQuestLogEntry*> Rows;
	if (Log)
	{
		for (const FVBQuestLogEntry& Entry : Log->GetEntries())
		{
			if (Entry.Definition)
			{
				Rows.Add(&Entry);
			}
		}
	}

	Rows.Sort([](const FVBQuestLogEntry& A, const FVBQuestLogEntry& B)
	{
		const bool bActiveA = A.Status == EVBQuestStatus::Active;
		const bool bActiveB = B.Status == EVBQuestStatus::Active;
		return bActiveA != bActiveB ? bActiveA : A.RecentOrder > B.RecentOrder;
	});
	for (const FVBQuestLogEntry* Entry : Rows)
	{
		AddRow(RowQuests.Add(Entry->Definition.Get()), Entry->Definition->Title,
			Entry->Status == EVBQuestStatus::Completed ? CompletedDetail : FText::GetEmpty());
	}

	KeepSelectionOrFirst(RowQuests, Selected, nullptr);
	return RowQuests.Num();
}

void UVBQuestJournalScreenWidget::Select(int32 RowIndex)
{
	if (RowQuests.IsValidIndex(RowIndex))
	{
		Selected = RowQuests[RowIndex];
		RefreshDetail();
	}
}

void UVBQuestJournalScreenWidget::RefreshDetail()
{
	const UVBQuestDefinition* Quest = Selected.Get();
	const UVBQuestLogComponent* Log = QuestLog.Get();
	const FVBQuestLogEntry* Entry = nullptr;
	if (Log && Quest)
	{
		Entry = Log->GetEntries().FindByPredicate([Quest](const FVBQuestLogEntry& E) { return E.Definition == Quest; });
	}

	if (TxtQuestTitle)
	{
		TxtQuestTitle->SetText(Quest ? Quest->Title : FText::GetEmpty());
	}

	if (!TxtQuestDetail)
	{
		return;
	}

	if (!Quest || !Entry)
	{
		TxtQuestDetail->SetText(FText::GetEmpty());
		return;
	}
	// 요약 / 지난 단계(완료) / 지금 단계와 목표. 끝난 퀘스트는 단계 전부를 지난 것으로 적는다.
	TArray<FText> Lines;
	Lines.Add(Quest->Summary);
	const int32 DoneStages = Entry->Status == EVBQuestStatus::Completed ? Quest->Stages.Num() : Entry->StageIndex;
	for (int32 StageIndex = 0; StageIndex < DoneStages && Quest->Stages.IsValidIndex(StageIndex); ++StageIndex)
	{
		Lines.Add(FText::Format(LOCTEXT("DoneStage", "{0} (완료)"), Quest->Stages[StageIndex].Description));
	}

	if (Entry->Status == EVBQuestStatus::Active && Quest->Stages.IsValidIndex(Entry->StageIndex))
	{
		Lines.Add(Quest->Stages[Entry->StageIndex].Description);
		Lines.Add(VBQuestText::DescribeObjectives(*Entry));
	}

	TxtQuestDetail->SetText(FText::Join(FText::FromString(TEXT("\n\n")), Lines));
}

void UVBQuestJournalScreenWidget::HandleCloseInput()
{
	DeactivateWidget();
}

#undef LOCTEXT_NAMESPACE
