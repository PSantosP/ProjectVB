// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Settings/VBOptionsScreenWidget.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "CoreLoop/VBDifficultyConfig.h"
#include "CoreLoop/VBDifficultySettings.h"
#include "CoreLoop/VBDifficultyStateComponent.h"
#include "Engine/World.h"
#include "Input/CommonUIInputTypes.h"
#include "Settings/VBOptionsSettings.h"
#include "Settings/VBOptionsSubsystem.h"
#include "UI/Settings/VBOptionRowWidget.h"
#include "UI/VBListRowWidget.h"
#include "UI/VBUIManagerSubsystem.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBOptions"

UVBOptionsScreenWidget::UVBOptionsScreenWidget()
{
	// 닫기(BtnClose)와 뒤로 가기는 같은 길 - 적용 안 한 변경이 있으면 먼저 묻는다(NativeOnHandleBackAction).
	bIsBackHandler = true;
	bRequireCloseButton = true;
	InputMode = EVBScreenInputMode::Menu;
	DiscardPrompt = LOCTEXT("DiscardPrompt", "적용하지 않은 변경이 있습니다. 버리고 나갈까요?");
	KeepVideoFormat = LOCTEXT("KeepVideo", "이 화면을 유지할까요? {Seconds}초 뒤 되돌립니다.");
}

#undef LOCTEXT_NAMESPACE

void UVBOptionsScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BtnApply->OnClicked.AddDynamic(this, &UVBOptionsScreenWidget::HandleApply);
	BtnReset->OnClicked.AddDynamic(this, &UVBOptionsScreenWidget::HandleReset);

	if (PreviousTabInputAction && NextTabInputAction)
	{
		RegisterUIActionBinding(FBindUIActionArgs(PreviousTabInputAction, /*bShouldDisplayInActionBar=*/false,
			FSimpleDelegate::CreateUObject(this, &UVBOptionsScreenWidget::StepTab, -1)));
		RegisterUIActionBinding(FBindUIActionArgs(NextTabInputAction, /*bShouldDisplayInActionBar=*/false,
			FSimpleDelegate::CreateUObject(this, &UVBOptionsScreenWidget::StepTab, 1)));
	}
	else
	{
		VB_LOG(Warning, "옵션 화면: PreviousTabInputAction/NextTabInputAction 미지정 - 패드로 탭을 못 넘긴다(WBP 클래스 기본값)");
	}

	if (!TabWidgetClass || !RowWidgetClass)
	{
		VB_LOG(Warning, "옵션 화면: TabWidgetClass/RowWidgetClass 미지정 - 탭이나 줄을 그릴 수 없다(WBP 클래스 기본값)");
		return;
	}

	TabBox->ClearChildren();
	for (int32 TabIndex = 0; TabIndex < static_cast<int32>(EVBOptionTab::Count); ++TabIndex)
	{
		UVBListRowWidget* Tab = CreateWidget<UVBListRowWidget>(this, TabWidgetClass);
		Tab->Setup(TabIndex, VBOptionCatalog::TabLabel(static_cast<EVBOptionTab>(TabIndex)), FText::GetEmpty(), /*bEnabled=*/true);
		Tab->OnRowClicked.BindUObject(this, &UVBOptionsScreenWidget::SelectTab);
		TabBox->AddChild(Tab);
	}
}

void UVBOptionsScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	// 화면은 컨테이너가 재사용한다 - 켤 때마다 지금 값에서 다시 시작한다.
	if (UVBOptionsSubsystem* Options = GetOptions())
	{
		Committed = Options->MakeDraft();
		Draft = Committed;
	}

	RefreshTabVisibility();
	SelectTab(static_cast<int32>(CurrentTab));
}

FVBOptionCatalogContext UVBOptionsScreenWidget::MakeCatalogContext() const
{
	FVBOptionCatalogContext Context;
	Context.Settings = GetDefault<UVBOptionsSettings>();
	if (UVBOptionsSubsystem* Options = GetOptions())
	{
		Context.Resolutions = Options->GetSelectableResolutions();
		Context.ResolutionPresets = Options->GetResolutionPresets();
	}
	// 적용된 값 기준 - 프리셋이 아니면 사용자 지정 줄이 생긴다(자동 감지가 71, 87% 같은 값을 정할 때도).
	Context.CurrentResolutionQuality = Committed.Machine.ResolutionQuality;
	Context.MonitorRefreshRate = FPlatformMisc::GetMaxRefreshRate();
	Context.Cultures = VBCulture::GetSelectable();
	// 난이도: 살아 있는 게임 쪽 난이도 상태 컴포넌트가 있을 때만(메인 메뉴에는 없다). 원격 클라는 지금 단계 하나만 - 서버도 따로 거절한다.
	const UVBDifficultyStateComponent* Difficulty = UVBDifficultyStateComponent::Get(this);
	const UVBDifficultyConfig* Levels = UVBDifficultySettings::LoadConfig( /*bWarnIfMissing=*/false);
	if (Difficulty && Levels)
	{
		Context.bDifficultyHostOnly = GetWorld() && GetWorld()->GetNetMode() == NM_Client;
		for (const FVBDifficultyLevel& Level : Levels->Levels)
		{
			if (!Context.bDifficultyHostOnly || Level.Tag == Difficulty->GetLevel())
			{
				Context.Difficulties.Add({Level.Tag, Level.DisplayName});
			}
		}
	}

	return Context;
}

void UVBOptionsScreenWidget::RefreshTabVisibility()
{
	const FVBOptionCatalogContext Context = MakeCatalogContext();
	for (int32 TabIndex = 0; TabIndex < TabBox->GetChildrenCount() && TabIndex < static_cast<int32>(EVBOptionTab::Count); ++TabIndex)
	{
		const bool bHasRows = !VBOptionCatalog::BuildTab(static_cast<EVBOptionTab>(TabIndex), Context).IsEmpty();
		TabBox->GetChildAt(TabIndex)->SetVisibility(bHasRows ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (!bHasRows && CurrentTab == static_cast<EVBOptionTab>(TabIndex))
		{
			CurrentTab = EVBOptionTab::Display;
		}
	}
}

void UVBOptionsScreenWidget::NativeOnDeactivated()
{
	ConfirmAsk.Cancel(this);
	Super::NativeOnDeactivated();
}

void UVBOptionsScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (Confirm != EConfirm::KeepVideo)
	{
		return;
	}
	// UVBOptionsSubsystem 은 질문이 대기 중이어도 미리보기 시간을 잰다 - 서브시스템이 되돌렸으면 질문을 거둔다.
	const UVBOptionsSubsystem* Options = GetOptions();
	if (!Options || !Options->HasPendingVideoMode())
	{
		ConfirmAsk.Cancel(this);
		return;
	}

	const int32 Seconds = FMath::CeilToInt(Options->GetPendingVideoSecondsLeft());
	ConfirmAsk.SetBody(this, FText::FormatNamed(KeepVideoFormat, TEXT("Seconds"), FText::AsNumber(Seconds)));
}

bool UVBOptionsScreenWidget::NativeOnHandleBackAction()
{
	HandleBack();
	return true;
}

UWidget* UVBOptionsScreenWidget::NativeGetDesiredFocusTarget() const
{
	return Rows.IsEmpty() ? static_cast<UWidget*>(BtnApply) : Rows[0]->GetFocusTarget();
}

void UVBOptionsScreenWidget::SelectTab(int32 TabIndex)
{
	if (IsAsking() || TabIndex < 0 || TabIndex >= static_cast<int32>(EVBOptionTab::Count))
	{
		return;
	}

	CurrentTab = static_cast<EVBOptionTab>(TabIndex);
	RebuildRows();
}

void UVBOptionsScreenWidget::StepTab(int32 Step)
{
	if (IsAsking())
	{
		return;
	}

	TArray<bool> Visible;
	for (int32 TabIndex = 0; TabIndex < TabBox->GetChildrenCount(); ++TabIndex)
	{
		Visible.Add(TabBox->GetChildAt(TabIndex)->GetVisibility() != ESlateVisibility::Collapsed);
	}

	const int32 Next = VBOptionCatalog::StepVisibleTab(static_cast<int32>(CurrentTab), Step, Visible);
	if (Next == INDEX_NONE)
	{
		return;
	}

	SelectTab(Next);
	// 행을 새로 만들어 포커스를 가진 행이 사라졌다 - 새 탭의 첫 행으로 옮긴다.
	if (UWidget* Target = NativeGetDesiredFocusTarget())
	{
		Target->SetFocus();
	}
}

void UVBOptionsScreenWidget::RebuildRows()
{
	Entries = VBOptionCatalog::BuildTab(CurrentTab, MakeCatalogContext());

	EntryBox->ClearChildren();
	Rows.Reset();
	if (!RowWidgetClass)
	{
		return;
	}

	for (int32 RowIndex = 0; RowIndex < Entries.Num(); ++RowIndex)
	{
		UVBOptionRowWidget* Row = CreateWidget<UVBOptionRowWidget>(this, RowWidgetClass);
		Row->Setup(RowIndex, Entries[RowIndex]);
		Row->OnValueChanged.BindUObject(this, &UVBOptionsScreenWidget::HandleRowValueChanged);
		Row->OnAction.BindUObject(this, &UVBOptionsScreenWidget::HandleRowAction);
		EntryBox->AddChild(Row);
		Rows.Add(Row);
	}

	RefreshRows();
}

void UVBOptionsScreenWidget::RefreshRows()
{
	for (int32 RowIndex = 0; RowIndex < Rows.Num(); ++RowIndex)
	{
		if (Entries[RowIndex].Get)
		{
			Rows[RowIndex]->SetValue(Entries[RowIndex].Get(Draft));
		}
	}

	RefreshButtons();
}

void UVBOptionsScreenWidget::RefreshButtons()
{
	BtnApply->SetIsEnabled(!(Draft == Committed));
}

void UVBOptionsScreenWidget::HandleRowValueChanged(int32 RowIndex, float Value)
{
	if (IsAsking() || !Entries.IsValidIndex(RowIndex) || !Entries[RowIndex].Set)
	{
		return;
	}

	Entries[RowIndex].Set(Draft, Value);
	// 한 줄이 다른 줄을 바꿀 수 있다(전체 품질 <-> 그룹 품질) - 모든 줄을 초안에서 다시 읽는다.
	RefreshRows();
}

void UVBOptionsScreenWidget::HandleRowAction(int32 RowIndex)
{
	if (IsAsking() || !Entries.IsValidIndex(RowIndex))
	{
		return;
	}

	const FName Action = Entries[RowIndex].Id;
	if (Action == VBOptionCatalog::KeyBindings)
	{
		if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(this))
		{
			UI->PushScreen(VBGameplayTags::UI_Screen_KeyBindings);
		}
	}
	else if (Action == VBOptionCatalog::AutoDetect)
	{
		if (UVBOptionsSubsystem* Options = GetOptions())
		{
			// 벤치마크는 그래픽 단계를 바로 건다. 적용 안 한 다른 고침은 초안에 남긴다.
			Options->RunAutoDetect();
			const FVBMachineSettings Detected = Options->MakeDraft().Machine;
			Committed.Machine = Detected;
			Draft.Machine.Quality = Detected.Quality;
			// 벤치마크는 렌더 해상도 배율도 정한다(BaseScalability PerfIndexValues_ResolutionQuality). 초안에 안 옮기면 적용이 감지 전 값으로 되돌린다.
			Draft.Machine.ResolutionQuality = Detected.ResolutionQuality;
			// 감지 값이 프리셋이 아닐 수 있어 선택지부터 다시 짓는다.
			RebuildRows();
		}
	}
}

void UVBOptionsScreenWidget::HandleApply()
{
	UVBOptionsSubsystem* Options = GetOptions();
	if (IsAsking() || !Options)
	{
		return;
	}

	const bool bVideoPreview = Options->Commit(Draft);
	Committed = Options->MakeDraft();
	Draft = Committed;
	RefreshRows();
	if (bVideoPreview)
	{
		AskKeepVideo();
	}
}

void UVBOptionsScreenWidget::HandleReset()
{
	UVBOptionsSubsystem* Options = GetOptions();
	if (IsAsking() || !Options)
	{
		return;
	}
	// 지금 탭의 줄만 기본값으로 - 줄마다 기본값 한 벌에서 읽어 초안에 쓴다.
	const FVBOptionsDraft Defaults = Options->MakeDefaultDraft();
	for (const FVBOptionEntry& Entry : Entries)
	{
		if (Entry.Get && Entry.Set)
		{
			Entry.Set(Draft, Entry.Get(Defaults));
		}
	}

	RefreshRows();
}

void UVBOptionsScreenWidget::HandleBack()
{
	if (IsAsking())
	{
		return;
	}

	if (Draft == Committed)
	{
		Leave();
	}
	else
	{
		AskDiscard();
	}
}

bool UVBOptionsScreenWidget::IsAsking() const
{
	// Ask 는 핸들이 정해지기 전에 동기적으로 알릴 수 있어 Confirm 도 함께 본다.
	return Confirm != EConfirm::None || ConfirmAsk.IsPending(this);
}

void UVBOptionsScreenWidget::AskDiscard()
{
	FVBConfirmationRequest Request;
	Request.Body = DiscardPrompt;
	Confirm = EConfirm::Discard;
	// 질문을 못 띄우면 고친 값을 조용히 버리거나 화면을 떠나지 않는다.
	if (!ConfirmAsk.Ask(this, Request, FVBConfirmationResultDelegate::CreateUObject(this, &UVBOptionsScreenWidget::HandleDiscardAnswer)))
	{
		Confirm = EConfirm::None;
	}
}

void UVBOptionsScreenWidget::AskKeepVideo()
{
	const UVBOptionsSubsystem* Options = GetOptions();
	FVBConfirmationRequest Request;
	Request.Body = FText::FormatNamed(KeepVideoFormat, TEXT("Seconds"),
		FText::AsNumber(Options ? FMath::CeilToInt(Options->GetPendingVideoSecondsLeft()) : 0));
	Confirm = EConfirm::KeepVideo;
	if (!ConfirmAsk.Ask(this, Request, FVBConfirmationResultDelegate::CreateUObject(this, &UVBOptionsScreenWidget::HandleKeepVideoAnswer)))
	{
		// 실패한 질문으로는 비디오 변경을 유지할 수 없다 - 동기 Failed 가 이미 되돌렸을 수도 있다.
		Confirm = EConfirm::None;
		RevertVideoPreview();
	}
}

void UVBOptionsScreenWidget::HandleDiscardAnswer(EVBConfirmationResult Result)
{
	Confirm = EConfirm::None;
	// Confirmed 만 버리고 떠난다 - Declined/Cancelled/Failed 는 화면을 그대로 둔다.
	if (Result == EVBConfirmationResult::Confirmed)
	{
		Leave();
	}
}

void UVBOptionsScreenWidget::HandleKeepVideoAnswer(EVBConfirmationResult Result)
{
	Confirm = EConfirm::None;
	if (Result == EVBConfirmationResult::Confirmed)
	{
		if (UVBOptionsSubsystem* Options = GetOptions())
		{
			Options->KeepVideoMode();
		}

		return;
	}
	// 확인이 아닌 결과는 모두 되돌린다 - 화면이 꺼진 뒤 거둬진 질문도 포함.
	RevertVideoPreview();
}

void UVBOptionsScreenWidget::RevertVideoPreview()
{
	UVBOptionsSubsystem* Options = GetOptions();
	if (!Options)
	{
		return;
	}

	Options->RevertVideoMode();
	if (IsActivated())
	{
		Committed = Options->MakeDraft();
		Draft = Committed;
		RefreshRows();
	}
}

void UVBOptionsScreenWidget::Leave()
{
	DeactivateWidget();
}

UVBOptionsSubsystem* UVBOptionsScreenWidget::GetOptions() const
{
	return UVBOptionsSubsystem::Get(GetOwningLocalPlayer());
}
