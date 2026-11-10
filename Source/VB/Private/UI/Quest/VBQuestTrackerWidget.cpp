// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Quest/VBQuestTrackerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Quest/VBQuestText.h"
#include "VB/VB.h"

void UVBQuestTrackerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		TrackerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("TrackerText"));
		TrackerText->SetJustification(ETextJustify::Right);
		WidgetTree->RootWidget = TrackerText;
	}
	// 블루프린트 자식이 자기 트리를 가져오면 글 칸을 안 짓는다 - 그러면 목표 줄이 영영 비므로 말한다.
	VB_CLOG(!TrackerText, Warning, "퀘스트 목표 줄: %s 가 자기 위젯 트리를 가져와 글 칸이 없다 - 목표 줄이 안 뜬다", *GetNameSafe(GetClass()));
	SetVisibility(ESlateVisibility::Collapsed);
}

void UVBQuestTrackerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 구성될 때마다 다시 잇는다(해체 때 푼다).
	PlayerStateSet.FollowComponent(this, QuestLog, &UVBQuestTrackerWidget::BindQuestLog);
}

void UVBQuestTrackerWidget::NativeDestruct()
{
	PlayerStateSet.Stop();
	BindQuestLog(nullptr);
	Super::NativeDestruct();
}

void UVBQuestTrackerWidget::BindQuestLog(UVBQuestLogComponent* Component)
{
	if (UVBQuestLogComponent* Old = QuestLog.Get())
	{
		Old->OnQuestLogChanged.Remove(QuestLogChangedHandle);
		Old->OnQuestNotice.Remove(QuestNoticeHandle);
	}

	QuestLogChangedHandle.Reset();
	QuestNoticeHandle.Reset();
	QuestLog = Component;
	if (Component)
	{
		QuestLogChangedHandle = Component->OnQuestLogChanged.AddUObject(this, &UVBQuestTrackerWidget::Refresh);
		QuestNoticeHandle = Component->OnQuestNotice.AddWeakLambda(this,
			[this](const UVBQuestDefinition* /*Quest*/, EVBQuestNotice /*Kind*/)
			{
				Refresh();
			});
	}

	Refresh();
}

void UVBQuestTrackerWidget::Refresh()
{
	const UVBQuestLogComponent* Log = QuestLog.Get();
	const FVBQuestLogEntry* Tracked = Log ? Log->GetTrackedEntry() : nullptr;
	const FText Text = Tracked ? VBQuestText::DescribeTracker(*Tracked) : FText::GetEmpty();
	if (TrackerText)
	{
		TrackerText->SetText(Text);
	}

	SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}
