// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Dialogue/VBSubtitleWidget.h"

#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Narrative/Dialogue/VBSpeakerDefinition.h"
#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Settings/VBOptionsSettings.h"
#include "TimerManager.h"
#include "VB/VB.h"

void UVBSubtitleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::Collapsed);
	// 구성될 때마다 다시 잇는다 - 해체 때 BindTo(nullptr) 로 놓기 때문이다. 줄 컴포넌트는 PC 생성자 하위 객체라 기다릴 신호가 없다.
	//   소유자가 PC 가 아니면(다른 쓰임) 아무것도 안 한다. PC 인데 줄 컴포넌트가 없으면 자막이 조용히 죽으므로 경고한다.
	if (const APlayerController* PC = GetOwningPlayer())
	{
		UVBSpokenLineComponent* Lines = PC->FindComponentByClass<UVBSpokenLineComponent>();
		VB_CLOG(!Lines, Warning, "자막: 주인 PC 에 UVBSpokenLineComponent 가 없다 - 자막이 아무것도 안 띄운다");
		BindTo(Lines);
	}
}

void UVBSubtitleWidget::NativeDestruct()
{
	BindTo(nullptr);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
	}

	Super::NativeDestruct();
}

void UVBSubtitleWidget::BindTo(UVBSpokenLineComponent* InLines)
{
	if (UVBSpokenLineComponent* Old = BoundLines.Get())
	{
		Old->OnLineStarted.Remove(LineHandle);
	}

	LineHandle.Reset();
	BoundLines = InLines;
	if (InLines)
	{
		LineHandle = InLines->OnLineStarted.AddUObject(this, &UVBSubtitleWidget::HandleLine);
	}
}

void UVBSubtitleWidget::HandleLine(const FVBSpokenLine& Line, float Seconds)
{
	const FVBSettingsSaveData& Settings = UVBSaveGameSubsystem::GetSettingsData(GetOwningPlayer());
	if (!Settings.bSubtitlesEnabled)
	{
		HideLine();
		return;
	}

	ApplyTextSize(Settings.SubtitleSize);
	TxtLine->SetText(Line.Text);
	if (TxtSpeaker)
	{
		const bool bHasSpeaker = Settings.bShowSpeakerName && Line.Speaker && !Line.Speaker->DisplayName.IsEmpty();
		TxtSpeaker->SetText(bHasSpeaker ? Line.Speaker->DisplayName : FText::GetEmpty());
		TxtSpeaker->SetVisibility(bHasSpeaker ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}

	SetVisibility(ESlateVisibility::HitTestInvisible);

	// 새 줄은 지난 줄의 타이머를 덮는다 - 짧은 줄 뒤에 긴 줄이 오면 긴 줄이 끝까지 보인다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HideTimer, this, &UVBSubtitleWidget::HideLine, FMath::Max(Seconds, 0.1f), false);
	}
}

void UVBSubtitleWidget::HideLine()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UVBSubtitleWidget::ApplyTextSize(EVBSubtitleSize Size)
{
	// 설정에 없는 단계는 WBP 에 저작한 크기 그대로 둔다.
	const int32* FontSize = GetDefault<UVBOptionsSettings>()->SubtitleFontSizes.Find(Size);
	if (!FontSize)
	{
		return;
	}

	FSlateFontInfo Font = TxtLine->GetFont();
	Font.Size = *FontSize;
	TxtLine->SetFont(Font);
}
