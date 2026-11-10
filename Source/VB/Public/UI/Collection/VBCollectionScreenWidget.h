#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "UI/VBScreenWidget.h"
#include "VBCollectionScreenWidget.generated.h"

class UImage;
class UButton;
class APlayerController;
class UPanelWidget;
class UScrollBox;
class UTextBlock;
class UVBCollectionEntryDefinition;
class UVBListRowWidget;
class UVBStoryStateComponent;
struct FStreamableHandle;

USTRUCT()
struct FVBCollectionFilter
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, Category="VB|Collection", meta=(Categories="Story.Collection"))
	FGameplayTag Tag;
	UPROPERTY(EditDefaultsOnly, Category="VB|Collection")
	FText Label;
};

/**
 * 도감 화면(UI.Screen.Collection). 발견 사실(Story.Collection.*)이 켜진 항목만 거르개별로 늘어놓고, 고른 항목의 제목, 본문, 그림을 보인다.
 * 복제된 이야기 상태를 범위 구독으로 읽는다(GameState 가 늦게 와도).
 */
UCLASS(Abstract)
class VB_API UVBCollectionScreenWidget : public UVBScreenWidget
{
	GENERATED_BODY()

public:
	UVBCollectionScreenWidget();
	void SelectEntry(FGameplayTag Fact);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual void NativeDestruct() override;
	virtual UWidget* NativeGetDesiredFocusTarget() const override;
	virtual bool ProvidesNativeFocusTarget() const override { return true; }

	UPROPERTY(EditDefaultsOnly, Category="VB|Collection")
	TSubclassOf<UVBListRowWidget> EntryWidgetClass;
	UPROPERTY(EditDefaultsOnly, Category="VB|Collection")
	TSubclassOf<UVBListRowWidget> FilterWidgetClass;
	UPROPERTY(EditDefaultsOnly, Category="VB|Collection")
	TArray<FVBCollectionFilter> Filters;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> EntryBox;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UPanelWidget> FilterBox;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtStatus;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtTitle;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UTextBlock> TxtBody;
	UPROPERTY(meta=(BindWidget))
	TObjectPtr<UScrollBox> BodyScroll;
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgIllustration;
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnRetry;

private:
	void Unbind();
	UFUNCTION()
	void HandleRetryClicked();
	void HandleStoryChanged();
	void HandleSelectionPending();
	void LoadDefinitions();
	void FinishLoad(uint32 ExpectedGeneration, TWeakObjectPtr<APlayerController> ExpectedOwner);
	void Rebuild();
	void RebuildFilters();
	void HandleFilterClicked(int32 Index);
	void HandleRowClicked(int32 Index);
	void RefreshDetail();
	void SetStatus(const FText& Text);
	bool IsCurrent(uint32 ExpectedGeneration, TWeakObjectPtr<APlayerController> ExpectedOwner) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UVBCollectionEntryDefinition>> Definitions;
	TArray<FSoftObjectPath> DefinitionPaths;
	TArray<const UVBCollectionEntryDefinition*> Rows;
	FVBStoryStateBinding StoryBinding;
	TWeakObjectPtr<APlayerController> BoundOwner;
	FDelegateHandle SelectionPendingHandle;
	TSharedPtr<FStreamableHandle> LoadHandle;
	FGameplayTag SelectedFact;
	FGameplayTag ShownFact;
	FGameplayTag RequestedFact;
	int32 ActiveFilterIndex = 0;
	uint32 Generation = 0;
	bool bDefinitionsReady = false;
	bool bLoadFailed = false;
};
