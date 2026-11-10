// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/Crafting/VBCraftingRules.h"
#include "Inventory/Enhancement/VBEnhancementRules.h"
#include "Player/VBPlayerController.h" // FVBPlayerStateSetRegistration
#include "UI/VBStationScreenWidget.h"
#include "VBCraftingScreenWidget.generated.h"

class APlayerState;
class UButton;
class UTextBlock;
class UVBEnhancementComponent;
class UVBInventoryComponent;

/**
 * 제작 화면(UI.Screen.Crafting). 탭 둘: 제작 / 강화.
 * 표시만 한다: 버튼은 서버와 같은 판정(CheckCraft, VBEnhancement::Check)으로 켜고 끈다.
 */
UCLASS(Abstract)
class VB_API UVBCraftingScreenWidget : public UVBStationScreenWidget
{
	GENERATED_BODY()

public:
	UVBCraftingScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual UVBStationSessionComponent* FindSession() const override;
	virtual int32 BuildRows() override;
	virtual void RefreshDetail() override;
	virtual void Select(int32 RowIndex) override;

	// 비용 한 줄. {Name} = 아이템 이름, {Held} = 가진 수, {Need} = 필요한 수.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText CostLineFormat;

	// 강화 목록 줄 옆 글. {0} = 지금 단계.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText LevelFormat;

	// 강화 상세. {Current}, {Next} = 단계, {Scale} = 다음 단계의 배율(예: 1.08).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText NextLevelFormat;

	// 제작 상세의 결과 줄. {Name} = 결과 이름, {Count} = 개수.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText OutputFormat;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText CraftActionText;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText EnhanceActionText;

	// 못 하는 까닭(Ok 는 비운다). 빠진 것은 까닭 칸을 비운다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TMap<EVBCraftResult, FText> CraftResultTexts;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TMap<EVBEnhanceResult, FText> EnhanceResultTexts;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtStationName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnTabCraft;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnTabEnhance;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemDescription;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtCost;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtStatus;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnAction;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtAction;

private:
	enum class ETab : uint8 { Craft, Enhance };

	void SetTab(ETab NewTab);
	void RebuildCraftRows();
	void RebuildEnhanceRows();
	void RefreshCraftDetail();
	void RefreshEnhanceDetail();
	FText FormatCosts(const TArray<FVBItemAmount>& Costs) const;
	void BindPlayerState(APlayerState* PlayerState);
	void HandleLevelChanged(const FVBEnhanceTarget& Target);

	UFUNCTION()
	void HandleActionClicked();
	UFUNCTION()
	void HandleCraftTabClicked();
	UFUNCTION()
	void HandleEnhanceTabClicked();

	ETab Tab = ETab::Craft;
	FVBPlayerStateSetRegistration PlayerStateSet;
	TVBComponentSubscription<UVBInventoryComponent> Inventory;
	TVBComponentSubscription<UVBEnhancementComponent> Enhancement;

	// 제작 탭: 목록 줄 번호 -> 제작법 번호. 선택은 번호로 들어 다시 그려도 유지된다.
	TArray<int32> RowRecipeIndices;
	int32 SelectedRecipeIndex = INDEX_NONE;

	// 강화 탭: 목록 줄 번호 -> 대상. 선택은 대상 값으로 든다.
	TArray<FVBEnhanceTarget> RowTargets;
	FVBEnhanceTarget SelectedTarget;
};
