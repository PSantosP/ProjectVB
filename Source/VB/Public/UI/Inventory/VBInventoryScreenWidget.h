// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Inventory/VBInventoryView.h"
#include "Player/VBPlayerController.h" // FVBPlayerStateSetRegistration
#include "UI/VBListDetailScreenWidget.h"
#include "VBInventoryScreenWidget.generated.h"

class APlayerState;
class UButton;
class UImage;
class UInputAction;
class UPanelWidget;
class UTextBlock;
class UVBInventoryComponent;
class UVBListRowWidget;
class UVBItemDefinition;

/** 인벤토리 화면의 필터 탭 하나. 태그가 비면 전체다. */
USTRUCT()
struct FVBInventoryFilter
{
	GENERATED_BODY()

	// 아이템 ID 의 부모 태그(Item.Medical 이면 Item.Medical.Gauze 가 걸린다).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(Categories="Item"))
	FGameplayTag Tag;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText Label;
};

/** 장비 능력치 한 줄의 이름표. 퍼센트면 값에 100 을 곱해 % 로 보인다. */
USTRUCT()
struct FVBStatLabel
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText Label;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	bool bPercent = false;
};

/**
 * 인벤토리 화면. 인벤토리 변경을 듣고 다시 그리며, 버튼은 서버에 요청만 보낸다(최종 판단은 서버).
 * 닫기는 뒤로 가기 또는 같은 입력을 한 번 더. 켜질 때마다 처음부터 채운다.
 */
UCLASS(Abstract)
class VB_API UVBInventoryScreenWidget : public UVBListDetailScreenWidget
{
	GENERATED_BODY()

public:
#if WITH_EDITOR
	// 장비 칸 종류마다 SlotLabels 이름이 있는지 검사한다.
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeOnActivated() override;
	virtual void NativeOnDeactivated() override;
	virtual int32 BuildRows() override;
	virtual void RefreshDetail() override;
	virtual void Select(int32 RowIndex) override;
	// 고른 물건에 쓰는 순서: 사용, 장착, 읽기, 버리기(보이고 켜진 첫 것이 줄을 고른 뒤 초점이 된다).
	virtual TArray<UWidget*> GetDetailActions() const override;

	// 줄 옆 글(가진 수). {0} = 수.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText CountFormat = NSLOCTEXT("VBInventory", "EntryCount", "x{0}");

	// 떠 있는 동안 이 입력을 누르면 닫는다(여는 IA 와 같은 것).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TObjectPtr<UInputAction> CloseInputAction;

	// 고른 아이템을 퀵슬롯에 넣는 칸 버튼. 인벤토리 칸 수만큼 런타임에 짓는다. 비면 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBListRowWidget> QuickSlotButtonClass;

	// 칸 버튼 글. {0} = 칸 번호(1부터).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText QuickSlotLabelFormat = NSLOCTEXT("VBInventory", "QuickSlotLabel", "퀵슬롯 {0}");

	// 고른 아이템이 이미 든 칸의 옆 글(누르면 뺀다).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText QuickSlotRemoveText = NSLOCTEXT("VBInventory", "QuickSlotRemove", "빼기");

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemName;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemDescription;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnUse;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnRead;

	// 칸 버튼이 들어갈 자리(선택). 없으면 퀵슬롯을 못 넣는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> QuickSlotBox;

	// 필터 탭. 비면 전체를 보인다. 탭 버튼은 FilterBox 에 런타임으로 짓는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TArray<FVBInventoryFilter> Filters;

	// 필터 탭 버튼의 WBP. 비면 경고한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBListRowWidget> FilterButtonClass;

	// 지금 고른 필터 탭의 옆 글(나머지 탭은 비어 있다).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText ActiveFilterMark = NSLOCTEXT("VBInventory", "ActiveFilter", "보는 중");

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> FilterBox;

	// 누를 때마다 다음 정렬(획득 순 -> 이름 -> 수량 -> 종류). 글은 TxtSort 가 보인다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnSort;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtSort;

	// 고른 아이템 하나를 버린다(발밑에 떨어진다). 버릴 수 없거나 가진 것이 없으면 꺼진다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnDiscard;

	// 상세의 아이템 그림. 그림이 없는 아이템이면 접는다.
	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UImage> ImgItemIcon;

	// 장비 칸 줄의 WBP. EquipBox 에 UVBEquipmentComponent::SlotLayout 대로 런타임에 짓는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSubclassOf<UVBListRowWidget> EquipSlotRowClass;

	// 칸 종류 -> 칸 이름. 칸 종류마다 있어야 한다(IsDataValid).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(Categories="Equipment.Slot"))
	TMap<FGameplayTag, FText> SlotLabels;

	// 능력치 -> 이름표(Data.Stat.MaxHealth -> 최대 체력). 없으면 태그 이름이 보인다.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI", meta=(Categories="Data.Stat"))
	TMap<FGameplayTag, FVBStatLabel> StatLabels;

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText EmptySlotText = NSLOCTEXT("VBInventory", "EmptySlot", "비어 있음");

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText EquipText = NSLOCTEXT("VBInventory", "Equip", "장착");

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText UnequipText = NSLOCTEXT("VBInventory", "Unequip", "해제");

	// 능력치 한 줄. {0} = 이름표, {1} = 값, {2} = 같은 칸에 지금 입은 장비의 값.
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText StatLineFormat = NSLOCTEXT("VBInventory", "StatLine", "{0} {1}");

	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	FText StatCompareFormat = NSLOCTEXT("VBInventory", "StatCompare", "{0} {1} (지금 {2})");

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UPanelWidget> EquipBox;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UButton> BtnEquip;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtEquip;

	UPROPERTY(meta=(BindWidgetOptional))
	TObjectPtr<UTextBlock> TxtItemStats;

private:
	void RefreshActions(const UVBItemDefinition* Item);
	void RefreshEquipment();
	void HandleEquipSlotClicked(int32 SlotIndex);
	FText FormatStat(const FGameplayTag& Stat, float Value) const;
	FText BuildStatText(const UVBItemDefinition* Item) const;

	UFUNCTION()
	void HandleEquipClicked();

	TWeakObjectPtr<class UVBEquipmentComponent> Equipment;
	FDelegateHandle EquipmentChangedHandle;

	void RebuildFilterButtons();
	void HandleFilterClicked(int32 FilterIndex);
	FText SortLabel() const;

	UFUNCTION()
	void HandleSortClicked();

	UFUNCTION()
	void HandleDiscardClicked();

	// 저장하지 않는 화면 상태. 위젯 재사용으로 다시 열어도 남는다.
	int32 ActiveFilterIndex = 0;
	EVBInventorySort ActiveSort = EVBInventorySort::Acquired;

	// 인벤토리가 바뀌었다. 초점이 줄에 있을 때만 초점을 옮긴다 - 사용, 버리기 버튼의 초점은 지워지지 않으니 그대로 둔다.
	void HandleInventoryChanged();
	void RefreshQuickSlotButtons();
	void HandleQuickSlotClicked(int32 SlotIndex);
	void HandleCloseInput();
	void BindPlayerComponents(APlayerState* PlayerState);
	void UnbindPlayerComponents();

	UFUNCTION()
	void HandleUseClicked();

	UFUNCTION()
	void HandleReadClicked();

	FVBPlayerStateSetRegistration PlayerStateSet;
	TWeakObjectPtr<UVBInventoryComponent> Inventory;
	TWeakObjectPtr<const UVBItemDefinition> Selected;
	// 줄 번호 -> 아이템(줄 위젯은 번호만 알린다).
	TArray<TWeakObjectPtr<const UVBItemDefinition>> RowItems;
	FDelegateHandle InventoryChangedHandle;
	FDelegateHandle QuickSlotsChangedHandle;
};
