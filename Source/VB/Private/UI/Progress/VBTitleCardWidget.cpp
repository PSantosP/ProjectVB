// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Progress/VBTitleCardWidget.h"

#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Narrative/Progress/VBChapterDefinition.h"
#include "Narrative/Progress/VBProgressCatalogSubsystem.h"
#include "Narrative/Progress/VBRegionDefinition.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Player/VBPlayerController.h"
#include "VB/VB.h"

void UVBTitleCardWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::Collapsed);
	// 구성될 때마다 다시 잇는다 - 해체 때 끊는다(NativeDestruct). 역할이 없으면 아무것도 안 듣는다.
	switch (Source)
	{
	case EVBTitleCardSource::Chapter:
		BindStory();
		break;
	case EVBTitleCardSource::Region:
		if (AVBPlayerController* PC = GetOwningPlayer<AVBPlayerController>(); PC && !RegionSource.IsValid())
		{
			RegionSource = PC;
			RegionEnteredHandle = PC->OnRegionEntered.AddUObject(this, &UVBTitleCardWidget::HandleRegionEntered);
		}
		break;
	case EVBTitleCardSource::None:
		break;
	}
}

void UVBTitleCardWidget::NativeDestruct()
{
	StoryBinding.Disconnect();
	if (AVBPlayerController* PC = RegionSource.Get())
	{
		PC->OnRegionEntered.Remove(RegionEnteredHandle);
	}

	RegionSource.Reset();
	RegionEnteredHandle.Reset();
	// 챕터 기준선은 남긴다(헤더 BaselineStory 주석).
	Super::NativeDestruct();
}

void UVBTitleCardWidget::BindStory()
{
	StoryBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &UVBTitleCardWidget::HandleStoryChanged));
}

void UVBTitleCardWidget::HandleStoryChanged()
{
	UVBStoryStateComponent* Story = StoryBinding.GetStory();
	if (!Story)
	{
		// GameState 는 왔는데 세계 사실이 없다 - 다른 GameState 클래스다. 카드가 영영 안 뜨므로 한 번 말한다.
		const UWorld* World = GetWorld();
		if (World && World->GetGameState() && !bWarnedNoStory)
		{
			bWarnedNoStory = true;
			VB_LOG(Warning, "제목 카드: GameState(%s)에 UVBStoryStateComponent 가 없다 - 챕터 카드가 안 뜬다", *GetNameSafe(World->GetGameState()));
		}

		return;
	}
	// 처음 보는 세계 사실이면 기준만 잡는다 - 이어하기로 들어온 챕터에 카드가 뜨지 않게.
	if (Story != BaselineStory.Get())
	{
		ChapterBaseline = VBChapterProgress::FBaseline();
		BaselineStory = Story;
	}

	const UVBProgressCatalogSubsystem* Catalog = UVBProgressCatalogSubsystem::Get(this);
	if (!Catalog)
	{
		return;
	}

	const UVBChapterDefinition* Current = Catalog->ResolveChapter(Story->GetState());
	if (const UVBChapterDefinition* Entered = VBChapterProgress::StepBaseline(ChapterBaseline, Current, Story->GetSaveReadSerial()))
	{
		ShowCard(Entered->Title, Entered->Subtitle, /*bLarge=*/true);
	}
}

void UVBTitleCardWidget::HandleRegionEntered(FGameplayTag RegionId, bool bFirstVisit)
{
	const UVBProgressCatalogSubsystem* Catalog = UVBProgressCatalogSubsystem::Get(this);
	const UVBRegionDefinition* Region = Catalog ? Catalog->FindRegion(RegionId) : nullptr;
	if (!Region)
	{
		VB_LOG(Warning, "지역 배너: 지역 %s 정의가 카탈로그에 없다 - 배너 없음", *RegionId.ToString());
		return;
	}

	ShowCard(Region->DisplayName, FText::GetEmpty(), bFirstVisit);
}

void UVBTitleCardWidget::ShowCard(const FText& Title, const FText& Subtitle, bool bLarge)
{
	const UWorld* World = GetWorld();
	if (!TxtTitle || !World)
	{
		return;
	}

	TxtTitle->SetText(Title);
	if (TxtSubtitle)
	{
		TxtSubtitle->SetText(Subtitle);
		TxtSubtitle->SetVisibility(Subtitle.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	// 글 칸만 줄인다 - 위젯 전체(HUD 가 화면 가득 깐다)를 줄이면 화면 가운데로 끌려가 위쪽 배너가 내려온다.
	const FVector2D Scale(bLarge ? 1.0f : SmallScale);
	TxtTitle->SetRenderScale(Scale);
	if (TxtSubtitle)
	{
		TxtSubtitle->SetRenderScale(Scale);
	}

	ShowSeconds = bLarge ? LargeSeconds : SmallSeconds;
	ShownAt = World->GetRealTimeSeconds();
	SetRenderOpacity(0.0f);
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UVBTitleCardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const UWorld* World = GetWorld();
	if (ShownAt < 0.0 || !World)
	{
		return;
	}

	const float Elapsed = static_cast<float>(World->GetRealTimeSeconds() - ShownAt);
	if (Elapsed >= ShowSeconds)
	{
		ShownAt = -1.0;
		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	const float Fade = FMath::Min(FadeSeconds, ShowSeconds * 0.5f);
	const float In = Fade > 0.0f ? Elapsed / Fade : 1.0f;
	const float Out = Fade > 0.0f ? (ShowSeconds - Elapsed) / Fade : 1.0f;
	SetRenderOpacity(FMath::Clamp(FMath::Min(In, Out), 0.0f, 1.0f));
}
