// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBDifficultyPickerWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "CoreLoop/VBDifficultyConfig.h"
#include "CoreLoop/VBDifficultyRules.h"
#include "CoreLoop/VBDifficultySettings.h"
#include "Player/VBMenuPlayerController.h"
#include "UI/VBListRowWidget.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBDifficultyPicker"

void UVBDifficultyPickerWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BtnConfirm->OnClicked.AddDynamic(this, &UVBDifficultyPickerWidget::HandleConfirm);
}

void UVBDifficultyPickerWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	// 재사용되는 화면이라 켤 때마다 상태를 처음부터 채운다.
	bSubmitted = false;
	Request.Reset();
	if (AVBMenuPlayerController* MenuPC = Cast<AVBMenuPlayerController>(GetOwningPlayer()))
	{
		Request = MenuPC->ConsumePendingNewGame();
	}

	VB_CLOG(!Request.IsSet(), Warning, "난이도 고르기: 새 게임 요청 없이 열렸다 - «확인» 을 끈다(여는 길은 AVBMenuPlayerController::RequestNewGame)");
	Rebuild();
	// 줄이 없어도 켠다 - 빈 태그로 기본 단계에 시작한다.
	BtnConfirm->SetIsEnabled(Request.IsSet());
	RequestRefreshFocus();
}

void UVBDifficultyPickerWidget::Rebuild()
{
	Levels.Reset();
	Names.Reset();
	Descriptions.Reset();
	Rows.Reset();
	ChoiceBox->ClearChildren();
	Selected = INDEX_NONE;

	const UVBDifficultyConfig* Config = UVBDifficultySettings::LoadConfig();
	if (!Config || !ChoiceWidgetClass)
	{
		VB_LOG(Warning, "난이도 고르기: %s - 줄을 그릴 수 없다, 기본 단계로만 시작한다",
			!Config ? TEXT("난이도 설정 자산이 없다") : TEXT("ChoiceWidgetClass 미지정(WBP 클래스 기본값)"));
		return;
	}

	const FGameplayTag Default = VBDifficultyRules::NormalizeLevel(Config, FGameplayTag());
	for (const FVBDifficultyLevel& Level : Config->Levels)
	{
		const int32 Index = Levels.Add(Level.Tag);
		Names.Add(Level.DisplayName);
		Descriptions.Add(VBDifficultyRules::DescribeLevel(Level));
		UVBListRowWidget* Row = CreateWidget<UVBListRowWidget>(this, ChoiceWidgetClass);
		Row->Setup(Index, Level.DisplayName, Descriptions.Last(), /*bEnabled=*/true);
		Row->OnRowClicked.BindUObject(this, &UVBDifficultyPickerWidget::Select);
		ChoiceBox->AddChild(Row);
		Rows.Add(Row);
		if (Level.Tag == Default)
		{
			Selected = Index;
		}
	}

	Select(Levels.IsValidIndex(Selected) ? Selected : 0);
}

void UVBDifficultyPickerWidget::Select(int32 Index)
{
	if (!Levels.IsValidIndex(Index))
	{
		return;
	}

	Selected = Index;
	if (TxtSelected)
	{
		TxtSelected->SetText(FText::Format(LOCTEXT("Selected", "{0} - {1}"), Names[Index], Descriptions[Index]));
	}
}

void UVBDifficultyPickerWidget::HandleConfirm()
{
	if (bSubmitted || !Request.IsSet())
	{
		return;
	}

	AVBMenuPlayerController* MenuPC = Cast<AVBMenuPlayerController>(GetOwningPlayer());
	if (!MenuPC)
	{
		VB_LOG(Warning, "난이도 고르기: 주인이 메뉴 PC 가 아니다 - 시작하지 않는다");
		return;
	}

	bSubmitted = true;
	// 거절되면 다시 누를 수 있게 푼다. 줄이 없으면 빈 태그 = 기본 단계.
	const FGameplayTag Difficulty = Levels.IsValidIndex(Selected) ? Levels[Selected] : FGameplayTag();
	bSubmitted = MenuPC->StartNewGameNow(Request.GetValue(), Difficulty);
}

UWidget* UVBDifficultyPickerWidget::NativeGetDesiredFocusTarget() const
{
	if (Rows.IsValidIndex(Selected) && Rows[Selected])
	{
		return Rows[Selected]->GetButton();
	}

	return BtnConfirm;
}

#undef LOCTEXT_NAMESPACE
