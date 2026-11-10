// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Progress/VBProgressionLineWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Progression/VBProgressionComponent.h"
#include "VB/VB.h"

UVBProgressionLineWidget::UVBProgressionLineWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LineFormat = NSLOCTEXT("VBHUD", "ProgressionLine", "Lv {0}   {1} / {2}");
	MaxFormat = NSLOCTEXT("VBHUD", "ProgressionMax", "Lv {0}");
}

void UVBProgressionLineWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Box = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ProgressionBox"));
		LineText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ProgressionText"));
		LineBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ProgressionBar"));
		Box->AddChildToVerticalBox(LineText);
		USizeBox* BarBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ProgressionBarSize"));
		BarBox->SetWidthOverride(BarSize.X);
		BarBox->SetHeightOverride(BarSize.Y);
		BarBox->AddChild(LineBar);
		Box->AddChildToVerticalBox(BarBox);
		WidgetTree->RootWidget = Box;
	}
	// 블루프린트 자식이 자기 트리를 가져오면 글, 막대를 안 짓는다 - 그러면 레벨 줄이 영영 비므로 말한다.
	VB_CLOG(!LineText || !LineBar, Warning, "레벨 줄: %s 가 자기 위젯 트리를 가져와 글·막대가 없다 - 레벨 줄이 안 그려진다", *GetNameSafe(GetClass()));
	// 성장을 묶기 전까지는 접어 둔다 - 묶을 때 펼친다.
	SetVisibility(ESlateVisibility::Collapsed);
}

void UVBProgressionLineWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 구성될 때마다 다시 잇는다(해체 때 푼다).
	PlayerStateSet.FollowComponent(this, Progression, &UVBProgressionLineWidget::BindProgression);
}

void UVBProgressionLineWidget::NativeDestruct()
{
	PlayerStateSet.Stop();
	BindProgression(nullptr);
	Super::NativeDestruct();
}

void UVBProgressionLineWidget::BindProgression(UVBProgressionComponent* Component)
{
	Progression.Bind(Component, &UVBProgressionComponent::OnProgressionChanged, this, &UVBProgressionLineWidget::Refresh);
	SetVisibility(Component ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Refresh();
}

void UVBProgressionLineWidget::Refresh()
{
	const UVBProgressionComponent* Source = Progression.Get();
	if (!Source)
	{
		return;
	}

	const int32 Level = Source->GetLevel();
	const bool bMax = Level >= Source->GetMaxLevel();
	if (LineText)
	{
		LineText->SetText(bMax ? FText::Format(MaxFormat, Level)
			: FText::Format(LineFormat, Level, Source->GetXPIntoLevel(), Source->GetXPForNextLevel()));
	}

	if (LineBar)
	{
		const int32 Needed = Source->GetXPForNextLevel();
		LineBar->SetPercent(bMax || Needed <= 0 ? 1.0f : static_cast<float>(Source->GetXPIntoLevel()) / Needed);
	}
}
