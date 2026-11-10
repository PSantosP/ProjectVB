// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Tutorial/VBTutorialScreenWidget.h"

#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Narrative/Tutorial/VBTutorialComponent.h"
#include "Narrative/Tutorial/VBTutorialDefinition.h"
#include "Player/VBPlayerController.h"
#include "UI/VBInputKeyText.h"
#include "UI/VBListRowWidget.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBTutorialUI"

UVBTutorialScreenWidget::UVBTutorialScreenWidget()
{
	InputMode = EVBScreenInputMode::Menu;
	bPausesGame = true;
	bIsBackHandler = true;
	bRequireCloseButton = true;
}

void UVBTutorialScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnRetry)
	{
		BtnRetry->OnClicked.AddUniqueDynamic(this, &UVBTutorialScreenWidget::HandleRetryClick);
	}

	if (TxtTitle)
	{
		TxtTitle->SetAutoWrapText(true);
	}

	if (TxtBody)
	{
		TxtBody->SetAutoWrapText(true);
	}

	if (TxtStatus)
	{
		TxtStatus->SetAutoWrapText(true);
	}
}

void UVBTutorialScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	Unbind();
	BoundOwner = GetOwningPlayer();
	AVBPlayerController* PC = Cast<AVBPlayerController>(BoundOwner.Get());
	if (PC)
	{
		RequestedTutorialId = PC->ConsumePendingTutorialId();
		// 이미 켜진 화면에 새 선택이 오면 여기로 온다.
		SelectionPendingHandle = PC->OnScreenSelectionPending().AddUObject(this, &UVBTutorialScreenWidget::HandleSelectionPending);
		// 튜토리얼 부품은 PlayerState 에 있다 - 원격 클라는 PlayerState 가 늦게 올 수 있어 정해질 때마다 다시 묶는다(지금 있으면 바로).
		PlayerStateSetHandle = PC->CallAndRegister_OnPlayerStateSet(
			FVBOnPlayerStateSet::FDelegate::CreateUObject(this, &UVBTutorialScreenWidget::RefreshBinding));
	}
	else
	{
		VB_LOG(Warning, "튜토리얼 화면: 주인이 AVBPlayerController 가 아니다 - PlayerState 를 기다릴 수 없다");
	}

	if (!PC || !PC->PlayerState)
	{
		RefreshBinding(nullptr);
	}

	RefreshKey();
	// 키 글은 입력 장치, 키 설정을 따라 바뀐다 - 힌트 위젯과 같이 폴링한다(이벤트로 바꾸는 것은 키 이름 장치 출처 통일과 함께, 따로).
	if (IsCurrent(Generation, BoundOwner))
	{
		KeyPollHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(
			this, &UVBTutorialScreenWidget::PollKey, Generation, BoundOwner), 0.25f);
	}
}

void UVBTutorialScreenWidget::HandleSelectionPending()
{
	AVBPlayerController* PC = Cast<AVBPlayerController>(BoundOwner.Get());
	const FGameplayTag Id = PC ? PC->ConsumePendingTutorialId() : FGameplayTag();
	if (Id.IsValid())
	{
		SelectTutorial(Id);
	}
}

void UVBTutorialScreenWidget::NativeOnDeactivated()
{
	Unbind();
	Super::NativeOnDeactivated();
}

void UVBTutorialScreenWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UVBTutorialScreenWidget::Unbind()
{
	++Generation;
	if (UVBTutorialComponent* Component = Tutorial.Get())
	{
		Component->OnTutorialChanged.Remove(ChangedHandle);
	}

	Tutorial.Reset();
	ChangedHandle.Reset();
	FTSTicker::RemoveTicker(KeyPollHandle);
	KeyPollHandle.Reset();
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(BoundOwner.Get()))
	{
		PC->UnregisterOnPlayerStateSet(PlayerStateSetHandle);
		PC->OnScreenSelectionPending().Remove(SelectionPendingHandle);
	}

	PlayerStateSetHandle.Reset();
	SelectionPendingHandle.Reset();
	BoundOwner.Reset();
	Rows.Reset();
	RequestedTutorialId = {};
	SelectedTutorialId = {};
	SelectedStepId = {};
	ShownStep = FVBTutorialStep();
	if (EntryBox)
	{
		EntryBox->ClearChildren();
	}

	RefreshDetail();
	SetStatus(FText::GetEmpty());
}

bool UVBTutorialScreenWidget::IsCurrent(uint32 ExpectedGeneration, TWeakObjectPtr<APlayerController> ExpectedOwner) const
{
	return IsActivated() && Generation == ExpectedGeneration && ExpectedOwner.IsValid()
		&& BoundOwner == ExpectedOwner && GetOwningPlayer() == ExpectedOwner.Get();
}

void UVBTutorialScreenWidget::RefreshBinding(APlayerState* /*PlayerState*/)
{
	if (!IsCurrent(Generation, BoundOwner))
	{
		Unbind();
		return;
	}

	UVBTutorialComponent* Current = UVBTutorialComponent::FindFor(BoundOwner.Get());
	if (Current != Tutorial.Get() || (Current && !ChangedHandle.IsValid()) || (!Current && ChangedHandle.IsValid()))
	{
		if (UVBTutorialComponent* Previous = Tutorial.Get())
		{
			Previous->OnTutorialChanged.Remove(ChangedHandle);
		}

		ChangedHandle.Reset();
		Tutorial = Current;
		if (Current)
		{
			ChangedHandle = Current->OnTutorialChanged.AddUObject(this, &UVBTutorialScreenWidget::Rebuild);
		}

		Rebuild();
	}
	else if (!Current) { SetStatus(LOCTEXT("Loading", "\ubd88\ub7ec\uc624\ub294 \uc911")); }
}

bool UVBTutorialScreenWidget::PollKey(float /*DeltaSeconds*/, uint32 ExpectedGeneration, TWeakObjectPtr<APlayerController> ExpectedOwner)
{
	if (!IsCurrent(ExpectedGeneration, ExpectedOwner))
	{
		if (Generation == ExpectedGeneration)
		{
			Unbind();
		}

		return false;
	}

	RefreshKey();
	return true;
}

void UVBTutorialScreenWidget::SetStatus(const FText& Text)
{
	if (BtnRetry)
	{
		const UVBTutorialComponent* Component = Tutorial.Get();
		const bool bCanRetry = IsCurrent(Generation, BoundOwner) && Component && Component->HasCatalogFailed();
		BtnRetry->SetVisibility(bCanRetry ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		BtnRetry->SetIsEnabled(bCanRetry);
	}

	if (TxtStatus)
	{
		TxtStatus->SetText(Text);
		TxtStatus->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UVBTutorialScreenWidget::Rebuild()
{
	if (!IsCurrent(Generation, BoundOwner))
	{
		return;
	}

	Rows.Reset();
	if (EntryBox)
	{
		EntryBox->ClearChildren();
	}

	const UVBTutorialComponent* Component = Tutorial.Get();
	if (Component && Component->HasCatalogFailed())
	{
		SetStatus(LOCTEXT("Failed", "\uc548\ub0b4\ub97c \ubd88\ub7ec\uc624\uc9c0 \ubabb\ud588\uc2b5\ub2c8\ub2e4"));
		RefreshDetail();
		RequestRefreshFocus();
		return;
	}

	if (!Component || !Component->IsReady())
	{
		SetStatus(LOCTEXT("Loading", "\ubd88\ub7ec\uc624\ub294 \uc911"));
		RefreshDetail();
		RequestRefreshFocus();
		return;
	}

	TArray<const UVBTutorialDefinition*> Definitions;
	for (const UVBTutorialDefinition* Definition : Component->GetDefinitions())
	{
		if (Definition)
		{
			Definitions.Add(Definition);
		}
	}

	Definitions.Sort([](const UVBTutorialDefinition& A, const UVBTutorialDefinition& B)
	{
		return A.SortOrder != B.SortOrder ? A.SortOrder < B.SortOrder
			: A.TutorialId.ToString() < B.TutorialId.ToString();
	});
	for (const UVBTutorialDefinition* Definition : Definitions)
	{
		const FVBTutorialProgress* Progress = Component->FindProgress(Definition->TutorialId);
		if (!Progress)
		{
			continue;
		}

		for (int32 StepIndex : VBTutorial::BuildReviewStepIndices(*Definition, *Progress))
		{
			Rows.Add({Definition->TutorialId, Definition->Steps[StepIndex].StepId});
		}
	}

	const int32 RequestedIndex = Rows.IndexOfByPredicate([this](const FReviewRow& Row)
	{
		return Row.TutorialId == RequestedTutorialId;
	});
	const bool bRequestedUnavailable = RequestedTutorialId.IsValid() && RequestedIndex == INDEX_NONE;
	if (RequestedIndex != INDEX_NONE)
	{
		SelectedTutorialId = Rows[RequestedIndex].TutorialId;
		SelectedStepId = Rows[RequestedIndex].StepId;
		RequestedTutorialId = {};
	}

	if (!Rows.ContainsByPredicate([this](const FReviewRow& Row)
	{
		return Row.TutorialId == SelectedTutorialId && Row.StepId == SelectedStepId;
	}))
	{
		SelectedTutorialId = !Rows.IsEmpty() ? Rows[0].TutorialId : FGameplayTag();
		SelectedStepId = !Rows.IsEmpty() ? Rows[0].StepId : FGameplayTag();
	}

	bool bFailed = !EntryBox || !ListRowWidgetClass;
	for (int32 Index = 0; EntryBox && ListRowWidgetClass && Index < Rows.Num(); ++Index)
	{
		UVBListRowWidget* RowWidget = CreateWidget<UVBListRowWidget>(this, ListRowWidgetClass);
		if (!RowWidget)
		{
			bFailed = true;
			break;
		}

		const UVBTutorialDefinition* Definition = Component->ResolveDefinition(Rows[Index].TutorialId);
		const int32 StepIndex = Definition ? Definition->FindStepIndex(Rows[Index].StepId) : INDEX_NONE;
		if (StepIndex == INDEX_NONE)
		{
			bFailed = true;
			break;
		}

		RowWidget->Setup(Index, Definition->Steps[StepIndex].Title, Definition->Title, true);
		RowWidget->SetIcon(Definition->Icon);
		RowWidget->OnRowClicked.BindUObject(this, &UVBTutorialScreenWidget::HandleRowClicked);
		EntryBox->AddChild(RowWidget);
	}

	if (bFailed)
	{
		SetStatus(LOCTEXT("Failed", "\uc548\ub0b4\ub97c \ubd88\ub7ec\uc624\uc9c0 \ubabb\ud588\uc2b5\ub2c8\ub2e4"));
	}
	else if (bRequestedUnavailable)
	{
		SetStatus(LOCTEXT("Unavailable", "\uc544\uc9c1 \uc5f4\ub9ac\uc9c0 \uc54a\uc740 \uc548\ub0b4\uc785\ub2c8\ub2e4"));
	}
	else if (Rows.IsEmpty())
	{
		SetStatus(LOCTEXT("Empty", "\uc5f4\ub9b0 \uc548\ub0b4 \uc5c6\uc74c"));
	}
	else
	{
		SetStatus(FText::GetEmpty());
	}

	RefreshDetail();
	RequestRefreshFocus();
}

void UVBTutorialScreenWidget::HandleRowClicked(int32 Index)
{
	if (!IsCurrent(Generation, BoundOwner) || !Rows.IsValidIndex(Index))
	{
		return;
	}

	RequestedTutorialId = {};
	SelectedTutorialId = Rows[Index].TutorialId;
	SelectedStepId = Rows[Index].StepId;
	RefreshDetail();
	SetStatus(FText::GetEmpty());
}

void UVBTutorialScreenWidget::SelectTutorial(FGameplayTag TutorialId)
{
	if (!IsCurrent(Generation, BoundOwner))
	{
		return;
	}

	RequestedTutorialId = TutorialId;
	Rebuild();
}

void UVBTutorialScreenWidget::HandleRetryClick()
{
	if (!IsCurrent(Generation, BoundOwner))
	{
		return;
	}

	UVBTutorialComponent* Component = Tutorial.Get();
	if (!Component || Component != UVBTutorialComponent::FindFor(BoundOwner.Get()) || !Component->HasCatalogFailed())
	{
		return;
	}

	Component->RetryCatalog();
}

void UVBTutorialScreenWidget::RefreshDetail()
{
	const UVBTutorialComponent* Component = Tutorial.Get();
	const bool bHasRow = Rows.ContainsByPredicate([this](const FReviewRow& Row)
	{
		return Row.TutorialId == SelectedTutorialId && Row.StepId == SelectedStepId;
	});
	const UVBTutorialDefinition* Definition = Component && Component->IsReady() && bHasRow
		? Component->ResolveDefinition(SelectedTutorialId) : nullptr;
	const int32 StepIndex = Definition ? Definition->FindStepIndex(SelectedStepId) : INDEX_NONE;
	const FVBTutorialStep* Step = StepIndex != INDEX_NONE ? &Definition->Steps[StepIndex] : nullptr;
	const FGameplayTag NewShownTutorialId = Step ? SelectedTutorialId : FGameplayTag();
	const FGameplayTag NewShownStepId = Step ? SelectedStepId : FGameplayTag();
	const bool bChanged = ShownTutorialId != NewShownTutorialId || ShownStepId != NewShownStepId;
	ShownTutorialId = NewShownTutorialId;
	ShownStepId = NewShownStepId;
	ShownStep = Step ? *Step : FVBTutorialStep();
	if (TxtTitle)
	{
		TxtTitle->SetText(ShownStep.Title);
	}

	if (BodyScroll && bChanged)
	{
		BodyScroll->ScrollToStart();
	}

	RefreshKey();
}

void UVBTutorialScreenWidget::RefreshKey()
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (TxtBody)
	{
		TxtBody->SetText(VBTutorial::BodyFor(ShownStep, VBInputKeyText::CurrentInputType(LocalPlayer)));
	}

	if (TxtKey || ImgKey)
	{
		VBInputKeyText::ShowKey(TxtKey, ImgKey, VBInputKeyText::ForCurrentDevice(LocalPlayer, ShownStep.InputMappingName),
			VBInputKeyText::IconForMapping(LocalPlayer, ShownStep.InputMappingName));
	}
}

UWidget* UVBTutorialScreenWidget::NativeGetDesiredFocusTarget() const
{
	if (Tutorial.IsValid() && Tutorial->HasCatalogFailed() && BtnRetry)
	{
		return BtnRetry.Get();
	}

	if (BtnRetry && BtnRetry->GetVisibility() == ESlateVisibility::Visible && BtnRetry->GetIsEnabled())
	{
		return BtnRetry.Get();
	}

	for (int32 Index = 0; EntryBox && Index < EntryBox->GetChildrenCount(); ++Index)
	{
		if (Rows.IsValidIndex(Index) && Rows[Index].TutorialId == SelectedTutorialId && Rows[Index].StepId == SelectedStepId)
		{
			if (const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(Index)))
			{
				return Row->GetButton();
			}
		}
	}

	return BtnClose ? BtnClose.Get() : Super::NativeGetDesiredFocusTarget();
}

#undef LOCTEXT_NAMESPACE
