// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/Shop/VBShopRules.h"
#include "Player/VBPlayerController.h" // FVBPlayerStateSetRegistration
#include "UI/VBStationScreenWidget.h"
#include "VBShopScreenWidget.generated.h"

class APlayerState;
class UButton;
class UTextBlock;
class UVBInventoryComponent;
struct FVBShopViewEntry;

/** 상점 화면. 서버가 보낸 뷰와 인벤토리 변경만 구독해 그리고, 사기 버튼은 요청만 보낸다. */
UCLASS(Abstract)
class VB_API UVBShopScreenWidget : public UVBStationScreenWidget
{
	GENERATED_BODY()

public:
	UVBShopScreenWidget(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual UVBStationSessionComponent* FindSession() const override;
	virtual int32 BuildRows() override;
	virtual void RefreshDetail() override;
	virtual void Select(int32 RowIndex) override;

	// 줄 옆 글 - 재고 무한일 때. {0} = 단가.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText PriceFormat;

	// 줄 옆 글 - 재고가 정해졌을 때. {0} = 단가, {1} = 남은 수.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText LimitedPriceFormat;

	// 사기를 못 하는 까닭(Ok 는 비운다). 빠진 것은 까닭 칸을 비운다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TMap<EVBPurchaseResult, FText> ResultTexts;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtShopName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemDescription;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtStatus;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnBuy;

private:
	const FVBShopViewEntry* FindSelected() const;
	void BindInventory(APlayerState* PlayerState);

	UFUNCTION()
	void HandleBuyClicked();

	FVBPlayerStateSetRegistration PlayerStateSet;
	TVBComponentSubscription<UVBInventoryComponent> Inventory;
	// 고른 줄의 상점 정의 번호(보기를 다시 받아도 유지된다).
	int32 SelectedEntryIndex = INDEX_NONE;
	// 목록 줄 번호 -> 상점 정의 번호.
	TArray<int32> RowEntryIndices;
};
