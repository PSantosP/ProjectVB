#include "UI/Collection/VBCollectionScreenWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "Narrative/Collection/VBCollectionEntryDefinition.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Engine/World.h"
#include "Player/VBPlayerController.h"
#include "UI/VBListRowWidget.h"

#define LOCTEXT_NAMESPACE "VBCollection"

UVBCollectionScreenWidget::UVBCollectionScreenWidget()
{
	InputMode = EVBScreenInputMode::Menu;
	bPausesGame = true;
	bIsBackHandler = true;
	bRequireCloseButton = true;
}

void UVBCollectionScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnRetry)
	{
		BtnRetry->OnClicked.AddUniqueDynamic(this, &UVBCollectionScreenWidget::HandleRetryClicked);
	}

	RebuildFilters();
}

void UVBCollectionScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	Unbind();
	BoundOwner = GetOwningPlayer();
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		RequestedFact = PC->ConsumePendingCollectionEntry();
		// 이미 켜진 화면에 새 선택이 오면 여기로 온다.
		SelectionPendingHandle = PC->OnScreenSelectionPending().AddUObject(this, &UVBCollectionScreenWidget::HandleSelectionPending);
	}

	if (RequestedFact.IsValid())
	{
		ActiveFilterIndex = INDEX_NONE;
	}

	RebuildFilters();
	StoryBinding.Refresh(this, GetWorld(), FSimpleDelegate::CreateUObject(this, &UVBCollectionScreenWidget::HandleStoryChanged));
	LoadDefinitions();
}

void UVBCollectionScreenWidget::NativeOnDeactivated()
{
	Unbind();
	Super::NativeOnDeactivated();
}

void UVBCollectionScreenWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UVBCollectionScreenWidget::Unbind()
{
	++Generation;
	if (AVBPlayerController* PC = Cast<AVBPlayerController>(BoundOwner.Get()))
	{
		PC->OnScreenSelectionPending().Remove(SelectionPendingHandle);
	}

	SelectionPendingHandle.Reset();
	StoryBinding.Disconnect();
	if (LoadHandle)
	{
		LoadHandle->CancelHandle();
	}

	LoadHandle.Reset();
	Definitions.Reset();
	DefinitionPaths.Reset();
	Rows.Reset();
	if (EntryBox)
	{
		EntryBox->ClearChildren();
	}

	if (ImgIllustration)
	{
		ImgIllustration->SetBrushFromTexture(nullptr);
	}

	ShownFact = {};
	bDefinitionsReady = false;
	bLoadFailed = false;
	RequestedFact = {};
	BoundOwner.Reset();
}

bool UVBCollectionScreenWidget::IsCurrent(uint32 ExpectedGeneration, TWeakObjectPtr<APlayerController> ExpectedOwner) const
{
	return IsActivated() && Generation == ExpectedGeneration && ExpectedOwner.IsValid()
		&& BoundOwner == ExpectedOwner && GetOwningPlayer() == ExpectedOwner.Get();
}

void UVBCollectionScreenWidget::HandleStoryChanged()
{
	if (!IsCurrent(Generation, BoundOwner))
	{
		Unbind();
		RefreshDetail();
		return;
	}

	Rebuild();
}

void UVBCollectionScreenWidget::LoadDefinitions()
{
	if (BtnRetry)
	{
		BtnRetry->SetIsEnabled(false);
	}

	UAssetManager& Manager = UAssetManager::Get();
	TArray<FPrimaryAssetId> Ids;
	Manager.GetPrimaryAssetIdList(UVBCollectionEntryDefinition::GetCollectionAssetType(), Ids);
	for (const FPrimaryAssetId& Id : Ids)
	{
		const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Id);
		if (Path.IsValid())
		{
			DefinitionPaths.Add(Path);
		}
		else { bLoadFailed = true; }
	}

	Rebuild();
	if (DefinitionPaths.IsEmpty())
	{
		bDefinitionsReady = true;
		Rebuild();
		return;
	}
	// 멤버가 핸들을 쥘 때까지 완료를 미룬다 - 캐시가 이미 데워져 바로 끝나는 경우도 포함한다.
	LoadHandle = Manager.GetStreamableManager().RequestAsyncLoad(DefinitionPaths,
		FStreamableDelegate::CreateUObject(this, &UVBCollectionScreenWidget::FinishLoad, Generation, BoundOwner),
		FStreamableManager::DefaultAsyncLoadPriority, false, true);
	if (LoadHandle)
	{
		LoadHandle->StartStalledHandle();
	}
	else
	{
		bLoadFailed = true;
		bDefinitionsReady = true;
		Rebuild();
	}
}

void UVBCollectionScreenWidget::FinishLoad(uint32 ExpectedGeneration, TWeakObjectPtr<APlayerController> ExpectedOwner)
{
	if (!IsCurrent(ExpectedGeneration, ExpectedOwner))
	{
		return;
	}

	Definitions.Reset();
	for (const FSoftObjectPath& Path : DefinitionPaths)
	{
		UVBCollectionEntryDefinition* Entry = Cast<UVBCollectionEntryDefinition>(Path.ResolveObject());
		if (Entry && VBCollection::IsEntryFact(Entry->DiscoveryFact))
		{
			Definitions.Add(Entry);
		}
		else { bLoadFailed = true; }
	}

	bDefinitionsReady = true;
	Rebuild();
}

void UVBCollectionScreenWidget::HandleSelectionPending()
{
	AVBPlayerController* PC = Cast<AVBPlayerController>(BoundOwner.Get());
	const FGameplayTag Entry = PC ? PC->ConsumePendingCollectionEntry() : FGameplayTag();
	if (Entry.IsValid())
	{
		SelectEntry(Entry);
	}
}

void UVBCollectionScreenWidget::SelectEntry(FGameplayTag Fact)
{
	RequestedFact = Fact;
	ActiveFilterIndex = INDEX_NONE;
	RebuildFilters();
	Rebuild();
}

void UVBCollectionScreenWidget::RebuildFilters()
{
	if (!FilterBox)
	{
		return;
	}

	if (!FilterWidgetClass)
	{
		FilterBox->ClearChildren();
		return;
	}

	if (FilterBox->GetChildrenCount() != Filters.Num())
	{
		FilterBox->ClearChildren();
		for (int32 Index = 0; Index < Filters.Num(); ++Index)
		{
			UVBListRowWidget* Row = CreateWidget<UVBListRowWidget>(this, FilterWidgetClass);
			if (!Row)
			{
				return;
			}

			Row->OnRowClicked.BindUObject(this, &UVBCollectionScreenWidget::HandleFilterClicked);
			FilterBox->AddChild(Row);
		}
	}

	for (int32 Index = 0; Index < Filters.Num(); ++Index)
	{
		if (UVBListRowWidget* Row = Cast<UVBListRowWidget>(FilterBox->GetChildAt(Index)))
		{
			Row->Setup(Index, Filters[Index].Label,
				Index == ActiveFilterIndex ? LOCTEXT("Active", "\ubcf4\ub294 \uc911") : FText::GetEmpty(), true);
		}
	}
}

void UVBCollectionScreenWidget::HandleFilterClicked(int32 Index)
{
	if (!IsActivated() || !Filters.IsValidIndex(Index))
	{
		return;
	}

	RequestedFact = {};
	ActiveFilterIndex = Index;
	RebuildFilters();
	Rebuild();
}

void UVBCollectionScreenWidget::HandleRowClicked(int32 Index)
{
	if (!IsActivated() || !Rows.IsValidIndex(Index))
	{
		return;
	}

	RequestedFact = {};
	SelectedFact = Rows[Index]->DiscoveryFact;
	Rebuild();
}

void UVBCollectionScreenWidget::SetStatus(const FText& Text)
{
	if (TxtStatus)
	{
		TxtStatus->SetText(Text);
		TxtStatus->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	if (BtnRetry)
	{
		BtnRetry->SetIsEnabled(bDefinitionsReady);
		BtnRetry->SetVisibility(bLoadFailed || (bDefinitionsReady && !Text.IsEmpty())
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UVBCollectionScreenWidget::HandleRetryClicked()
{
	if (!IsCurrent(Generation, BoundOwner))
	{
		return;
	}

	++Generation;
	if (LoadHandle)
	{
		LoadHandle->CancelHandle();
	}

	LoadHandle.Reset();
	Definitions.Reset();
	DefinitionPaths.Reset();
	bDefinitionsReady = false;
	bLoadFailed = false;
	LoadDefinitions();
}

void UVBCollectionScreenWidget::RefreshDetail()
{
	const UVBCollectionEntryDefinition* Selected = nullptr;
	for (const UVBCollectionEntryDefinition* Row : Rows)
	{
		if (Row->DiscoveryFact == SelectedFact)
		{
			Selected = Row;
			break;
		}
	}

	if (TxtTitle)
	{
		TxtTitle->SetText(Selected ? Selected->Title : FText::GetEmpty());
	}

	if (TxtBody)
	{
		TxtBody->SetText(Selected ? Selected->Body : FText::GetEmpty());
	}

	const FGameplayTag NewShownFact = Selected ? Selected->DiscoveryFact : FGameplayTag();
	const bool bChanged = ShownFact != NewShownFact;
	ShownFact = NewShownFact;
	if (bChanged && BodyScroll)
	{
		BodyScroll->ScrollToStart();
	}

	if (bChanged && ImgIllustration)
	{
		// 먼저 비워서 새 soft 이미지를 읽는 동안 이전 문서가 보이지 않게 한다.
		ImgIllustration->SetBrushFromTexture(nullptr);
		const bool bHasImage = Selected && !Selected->Illustration.IsNull();
		ImgIllustration->SetVisibility(bHasImage ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bHasImage)
		{
			ImgIllustration->SetBrushFromSoftTexture(Selected->Illustration);
		}
	}
}

UWidget* UVBCollectionScreenWidget::NativeGetDesiredFocusTarget() const
{
	if (EntryBox)
	{
		for (int32 Index = 0; Index < EntryBox->GetChildrenCount(); ++Index)
		{
			if (const UVBListRowWidget* Row = Cast<UVBListRowWidget>(EntryBox->GetChildAt(Index)))
			{
				if (Rows.IsValidIndex(Index) && Rows[Index]->DiscoveryFact == SelectedFact)
				{
					return Row->GetButton();
				}
			}
		}
	}

	return BtnClose ? BtnClose.Get() : Super::NativeGetDesiredFocusTarget();
}

void UVBCollectionScreenWidget::Rebuild()
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

	const UVBStoryStateComponent* Story = StoryBinding.GetStory();
	if (!bDefinitionsReady || !Story)
	{
		SetStatus(LOCTEXT("Loading", "\ubd88\ub7ec\uc624\ub294 \uc911"));
		RefreshDetail();
		return;
	}

	TArray<const UVBCollectionEntryDefinition*> Loaded;
	for (const UVBCollectionEntryDefinition* Definition : Definitions)
	{
		Loaded.Add(Definition);
	}

	const FGameplayTag Filter = Filters.IsValidIndex(ActiveFilterIndex) ? Filters[ActiveFilterIndex].Tag : FGameplayTag();
	Rows = VBCollection::BuildRows(Loaded, Story->GetState(), Filter);
	bool bMissingDefinition = false;
	const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("Story.Collection"), false);
	for (const FGameplayTag Fact : Story->GetState().Facts)
	{
		if (Root.IsValid() && Fact.MatchesTag(Root) && VBCollection::IsEntryFact(Fact)
			&& !Loaded.ContainsByPredicate([Fact](const UVBCollectionEntryDefinition* Entry) { return Entry->DiscoveryFact == Fact; }))
		{
			bMissingDefinition = true;
		}
	}

	const bool bRequestedUnavailable = RequestedFact.IsValid() && !Story->HasFact(RequestedFact);
	const bool bRequestedMissing = RequestedFact.IsValid() && !Loaded.ContainsByPredicate(
		[this](const UVBCollectionEntryDefinition* Entry) { return Entry->DiscoveryFact == RequestedFact; });
	if (RequestedFact.IsValid()) { SelectedFact = {}; }
	if (RequestedFact.IsValid() && !bRequestedUnavailable && !bRequestedMissing)
	{
		SelectedFact = RequestedFact;
		RequestedFact = {};
	}

	if (!Rows.ContainsByPredicate([this](const UVBCollectionEntryDefinition* Entry) { return Entry->DiscoveryFact == SelectedFact; }))
	{
		SelectedFact = !Rows.IsEmpty() && !RequestedFact.IsValid() ? Rows[0]->DiscoveryFact : FGameplayTag();
	}

	for (int32 Index = 0; EntryBox && EntryWidgetClass && Index < Rows.Num(); ++Index)
	{
		UVBListRowWidget* Row = CreateWidget<UVBListRowWidget>(this, EntryWidgetClass);
		if (!Row)
		{
			bLoadFailed = true;
			break;
		}

		Row->Setup(Index, Rows[Index]->Title, FText::GetEmpty(), true);
		Row->SetIcon(Rows[Index]->Icon);
		Row->OnRowClicked.BindUObject(this, &UVBCollectionScreenWidget::HandleRowClicked);
		EntryBox->AddChild(Row);
	}

	if (bLoadFailed || bMissingDefinition || bRequestedMissing || !EntryWidgetClass || !EntryBox)
	{
		SetStatus(LOCTEXT("Failed", "\uae30\ub85d\uc744 \ubd88\ub7ec\uc624\uc9c0 \ubabb\ud588\uc2b5\ub2c8\ub2e4"));
	}
	else if (bRequestedUnavailable) { SetStatus(LOCTEXT("Waiting", "\ubc1c\uacac \uae30\ub85d\uc744 \uae30\ub2e4\ub9ac\ub294 \uc911")); }
	else if (Rows.IsEmpty()) { SetStatus(LOCTEXT("Empty", "\ubc1c\uacac\ud55c \uae30\ub85d \uc5c6\uc74c")); }
	else { SetStatus(FText::GetEmpty()); }
	RefreshDetail();
	RequestRefreshFocus();
}

#undef LOCTEXT_NAMESPACE
