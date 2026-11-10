// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Inventory/VBInventoryScreenWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Input/CommonUIInputTypes.h"
#include "Engine/Texture2D.h"
#include "Inventory/Equipment/VBEquipmentComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBQuickSlotRules.h"
#include "Narrative/Collection/VBCollectionEntryDefinition.h"
#include "UI/VBListRowWidget.h"
#include "VB/VB.h"
#include "GameplayTagsManager.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	void ApplyAction(UButton* Button, bool bShow, bool bEnable)
	{
		if (Button)
		{
			Button->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			Button->SetIsEnabled(bEnable);
		}
	}
}

void UVBInventoryScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnUse)
	{
		BtnUse->OnClicked.AddUniqueDynamic(this, &UVBInventoryScreenWidget::HandleUseClicked);
	}

	if (BtnSort)
	{
		BtnSort->OnClicked.AddUniqueDynamic(this, &UVBInventoryScreenWidget::HandleSortClicked);
	}

	if (BtnRead)
	{
		BtnRead->OnClicked.AddUniqueDynamic(this, &UVBInventoryScreenWidget::HandleReadClicked);
	}

	if (BtnDiscard)
	{
		BtnDiscard->OnClicked.AddUniqueDynamic(this, &UVBInventoryScreenWidget::HandleDiscardClicked);
	}

	if (BtnEquip)
	{
		BtnEquip->OnClicked.AddUniqueDynamic(this, &UVBInventoryScreenWidget::HandleEquipClicked);
	}

	if (CloseInputAction)
	{
		RegisterUIActionBinding(FBindUIActionArgs(CloseInputAction, /*bShouldDisplayInActionBar=*/false,
			FSimpleDelegate::CreateUObject(this, &UVBInventoryScreenWidget::HandleCloseInput)));
	}
}

void UVBInventoryScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	Selected.Reset();
	ActiveFilterIndex = Filters.IsValidIndex(ActiveFilterIndex) ? ActiveFilterIndex : 0;
	RebuildFilterButtons();
	if (TxtSort)
	{
		TxtSort->SetText(SortLabel());
	}
	// 인벤토리, 장비는 PlayerState 에 있다. 원격 클라는 늦게 올 수 있고, 이미 있으면 여기서 바로 묶고 그린다.
	AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer());
	if (PC)
	{
		PlayerStateSet.Register(*PC, FVBOnPlayerStateSet::FDelegate::CreateUObject(this, &UVBInventoryScreenWidget::BindPlayerComponents));
	}
	else
	{
		VB_LOG(Warning, "인벤토리 화면: 주인이 AVBPlayerController 가 아니다 - PlayerState 를 기다릴 수 없다");
	}

	if (!PC || !PC->PlayerState)
	{
		Rebuild();
	}
}

void UVBInventoryScreenWidget::NativeOnDeactivated()
{
	PlayerStateSet.Unregister();
	UnbindPlayerComponents();
	Super::NativeOnDeactivated();
}

void UVBInventoryScreenWidget::BindPlayerComponents(APlayerState* /*PlayerState*/)
{
	UnbindPlayerComponents();
	Inventory = UVBInventoryComponent::FindFor(GetOwningPlayer());
	if (UVBInventoryComponent* Owned = Inventory.Get())
	{
		InventoryChangedHandle = Owned->OnInventoryChanged.AddUObject(this, &UVBInventoryScreenWidget::HandleInventoryChanged);
		QuickSlotsChangedHandle = Owned->OnQuickSlotsChanged.AddUObject(this, &UVBInventoryScreenWidget::RefreshQuickSlotButtons);
	}
	else
	{
		VB_LOG(Warning, "인벤토리 화면: PlayerState 는 있는데 인벤토리가 없다(PlayerState 에 UVBInventoryComponent)");
	}

	Equipment = UVBEquipmentComponent::FindFor(GetOwningPlayer());
	if (UVBEquipmentComponent* Worn = Equipment.Get())
	{
		EquipmentChangedHandle = Worn->OnEquipmentChanged.AddUObject(this, &UVBInventoryScreenWidget::RefreshDetail);
	}

	Rebuild();
}

void UVBInventoryScreenWidget::HandleInventoryChanged()
{
	// 줄을 지우기 전에 봐야 한다. 지운 뒤에는 초점이 어디 있었는지 알 수 없다.
	if (EntryBox && EntryBox->HasFocusedDescendants())
	{
		Rebuild();
		return;
	}

	RebuildRows();
}

void UVBInventoryScreenWidget::UnbindPlayerComponents()
{
	if (UVBInventoryComponent* Owned = Inventory.Get())
	{
		Owned->OnInventoryChanged.Remove(InventoryChangedHandle);
		Owned->OnQuickSlotsChanged.Remove(QuickSlotsChangedHandle);
	}

	if (UVBEquipmentComponent* Worn = Equipment.Get())
	{
		Worn->OnEquipmentChanged.Remove(EquipmentChangedHandle);
	}

	InventoryChangedHandle.Reset();
	QuickSlotsChangedHandle.Reset();
	EquipmentChangedHandle.Reset();
	Inventory.Reset();
	Equipment.Reset();
}

int32 UVBInventoryScreenWidget::BuildRows()
{
	RowItems.Reset();
	const UVBInventoryComponent* Owned = Inventory.Get();
	const TArray<FVBInventoryEntry> NoEntries;
	const TArray<FVBInventoryEntry>& Entries = Owned ? Owned->GetEntries() : NoEntries;
	const FGameplayTag Filter = Filters.IsValidIndex(ActiveFilterIndex) ? Filters[ActiveFilterIndex].Tag : FGameplayTag();
	for (const UVBItemDefinition* Item : VBInventoryView::BuildRows(Entries, Filter, ActiveSort))
	{
		if (UVBListRowWidget* Row = AddRow(RowItems.Add(Item), Item->DisplayName, FText::Format(CountFormat, Owned->GetCount(Item))))
		{
			Row->SetIcon(Item->Icon);
		}
	}

	// 고른 것을 다 써서 사라졌거나 필터 밖이면 첫 줄로 옮긴다. 빈 설명이 남지 않게 한다.
	KeepSelectionOrFirst(RowItems, Selected, nullptr);
	return RowItems.Num();
}

void UVBInventoryScreenWidget::Select(int32 RowIndex)
{
	if (RowItems.IsValidIndex(RowIndex))
	{
		Selected = RowItems[RowIndex];
		RefreshDetail();
	}
}

void UVBInventoryScreenWidget::HandleReadClicked()
{
	const UVBItemDefinition* Item = Selected.Get();
	const UVBInventoryComponent* Owned = Inventory.Get();
	const FGameplayTag Fact = VBCollection::GetReadingFact(Item);
	if (IsActivated() && Owned && Owned->GetCount(Item) > 0 && Fact.IsValid())
	{
		if (AVBPlayerController* PC = Cast<AVBPlayerController>(GetOwningPlayer()))
		{
			PC->RequestCollectionScreen(Fact);
		}
	}
}

void UVBInventoryScreenWidget::RefreshActions(const UVBItemDefinition* Item)
{
	const FVBInventoryActions Actions = VBInventoryView::DescribeActions(Item, Inventory.Get(), Equipment.Get());
	ApplyAction(BtnRead, Actions.bShowRead, Actions.bEnableRead);
	ApplyAction(BtnUse, Actions.bShowUse, Actions.bEnableUse);
	ApplyAction(BtnDiscard, Actions.bShowDiscard, Actions.bEnableDiscard);
	if (BtnEquip)
	{
		BtnEquip->SetVisibility(Actions.bShowEquip ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		if (TxtEquip)
		{
			TxtEquip->SetText(Actions.bEquipped ? UnequipText : EquipText);
		}
	}
}

TArray<UWidget*> UVBInventoryScreenWidget::GetDetailActions() const
{
	return { BtnUse, BtnEquip, BtnRead, BtnDiscard };
}

void UVBInventoryScreenWidget::RefreshDetail()
{
	const UVBItemDefinition* Item = Selected.Get();
	RefreshActions(Item);
	if (TxtItemName)
	{
		TxtItemName->SetText(Item ? Item->DisplayName : FText::GetEmpty());
	}

	if (TxtItemDescription)
	{
		TxtItemDescription->SetText(Item ? Item->Description : FText::GetEmpty());
	}

	if (TxtItemStats)
	{
		const FText Stats = BuildStatText(Item);
		TxtItemStats->SetText(Stats);
		TxtItemStats->SetVisibility(Stats.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	RefreshEquipment();
	if (ImgItemIcon)
	{
		const bool bHasIcon = Item && !Item->Icon.IsNull();
		ImgItemIcon->SetVisibility(bHasIcon ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (bHasIcon)
		{
			ImgItemIcon->SetBrushFromSoftTexture(Item->Icon);
		}
	}

	RefreshQuickSlotButtons();
}

void UVBInventoryScreenWidget::RebuildFilterButtons()
{
	if (!FilterBox)
	{
		return;
	}

	FilterBox->SetVisibility(Filters.Num() > 0 ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (Filters.Num() == 0)
	{
		return;
	}

	if (!FilterButtonClass)
	{
		VB_LOG(Warning, "인벤토리 화면: FilterButtonClass 미지정 - 필터 탭을 그릴 수 없다(WBP 클래스 기본값)");
		return;
	}
	// 탭 수가 같으면 있는 버튼의 글만 바꾼다 - 새로 지으면 게임패드 초점이 누른 버튼과 함께 사라진다(퀵슬롯 칸 버튼과 같은 판단).
	if (FilterBox->GetChildrenCount() != Filters.Num())
	{
		FilterBox->ClearChildren();
		for (int32 Index = 0; Index < Filters.Num(); ++Index)
		{
			UVBListRowWidget* Button = CreateWidget<UVBListRowWidget>(this, FilterButtonClass);
			Button->OnRowClicked.BindUObject(this, &UVBInventoryScreenWidget::HandleFilterClicked);
			FilterBox->AddChild(Button);
		}
	}

	for (int32 Index = 0; Index < Filters.Num(); ++Index)
	{
		if (UVBListRowWidget* Button = Cast<UVBListRowWidget>(FilterBox->GetChildAt(Index)))
		{
			Button->Setup(Index, Filters[Index].Label, Index == ActiveFilterIndex ? ActiveFilterMark : FText::GetEmpty(),
				/*bEnabled=*/true);
		}
	}
}

void UVBInventoryScreenWidget::HandleFilterClicked(int32 FilterIndex)
{
	if (Filters.IsValidIndex(FilterIndex) && FilterIndex != ActiveFilterIndex)
	{
		ActiveFilterIndex = FilterIndex;
		RebuildFilterButtons();
		// 초점은 누른 탭에 그대로 둔다.
		RebuildRows();
	}
}

FText UVBInventoryScreenWidget::SortLabel() const
{
	switch (ActiveSort)
	{
	case EVBInventorySort::Name: return NSLOCTEXT("VBInventory", "SortName", "정렬: 이름");
	case EVBInventorySort::Count: return NSLOCTEXT("VBInventory", "SortCount", "정렬: 수량");
	case EVBInventorySort::Kind: return NSLOCTEXT("VBInventory", "SortKind", "정렬: 종류");
	case EVBInventorySort::Acquired: break;
	}

	return NSLOCTEXT("VBInventory", "SortAcquired", "정렬: 얻은 순서");
}

void UVBInventoryScreenWidget::HandleSortClicked()
{
	ActiveSort = VBInventoryView::Next(ActiveSort);
	if (TxtSort)
	{
		TxtSort->SetText(SortLabel());
	}

	// 초점은 누른 정렬 버튼에 그대로 둔다.
	RebuildRows();
}

void UVBInventoryScreenWidget::RefreshEquipment()
{
	const UVBEquipmentComponent* Worn = Equipment.Get();
	if (!EquipBox || !Worn)
	{
		return;
	}

	if (!EquipSlotRowClass)
	{
		VB_LOG(Warning, "인벤토리 화면: EquipSlotRowClass 미지정 - 장비 칸 줄을 그릴 수 없다(WBP 클래스 기본값)");
		return;
	}

	const TArray<FGameplayTag>& Layout = Worn->GetSlotLayout();
	// 칸 수가 같으면 있는 줄의 글만 바꾼다 - 게임패드 초점을 지킨다(퀵슬롯 칸 버튼과 같은 판단).
	if (EquipBox->GetChildrenCount() != Layout.Num())
	{
		EquipBox->ClearChildren();
		for (int32 Index = 0; Index < Layout.Num(); ++Index)
		{
			UVBListRowWidget* Row = CreateWidget<UVBListRowWidget>(this, EquipSlotRowClass);
			Row->OnRowClicked.BindUObject(this, &UVBInventoryScreenWidget::HandleEquipSlotClicked);
			EquipBox->AddChild(Row);
		}
	}

	for (int32 Index = 0; Index < Layout.Num(); ++Index)
	{
		UVBListRowWidget* Row = Cast<UVBListRowWidget>(EquipBox->GetChildAt(Index));
		if (!Row)
		{
			continue;
		}

		const FText* SlotName = SlotLabels.Find(Layout[Index]);
		if (!SlotName || SlotName->IsEmpty())
		{
			VB_LOG(Warning, "인벤토리 화면: 장비 칸 %s 의 이름이 SlotLabels 에 없다 - 태그 글을 그대로 보인다(WBP 클래스 기본값)", *Layout[Index].ToString());
		}

		const UVBItemDefinition* InSlot = Worn->GetEquipped(Index);
		Row->Setup(Index, SlotName ? *SlotName : FText::FromName(Layout[Index].GetTagName()), InSlot ? InSlot->DisplayName : EmptySlotText,
			/*bEnabled=*/InSlot != nullptr);
		Row->SetIcon(InSlot ? InSlot->Icon : TSoftObjectPtr<UTexture2D>());
	}
}

void UVBInventoryScreenWidget::HandleEquipSlotClicked(int32 SlotIndex)
{
	// 칸을 누르면 그 칸의 장비를 고른다 - 상세에서 '해제' 할 수 있게. 벗기를 한 번에 하지 않는 이유 = 잘못 누른 한 번에 벗겨지지 않게.
	if (const UVBEquipmentComponent* Worn = Equipment.Get(); Worn && Worn->GetEquipped(SlotIndex))
	{
		Selected = Worn->GetEquipped(SlotIndex);
		RefreshDetail();
	}
}

void UVBInventoryScreenWidget::HandleEquipClicked()
{
	UVBEquipmentComponent* Worn = Equipment.Get();
	const UVBItemDefinition* Item = Selected.Get();
	if (!Worn || !Item)
	{
		return;
	}

	const int32 WornSlot = Worn->FindSlotOf(Item);
	if (WornSlot != INDEX_NONE)
	{
		Worn->RequestUnequip(WornSlot);
	}
	else
	{
		Worn->RequestEquip(Item);
	}
}

FText UVBInventoryScreenWidget::FormatStat(const FGameplayTag& Stat, float Value) const
{
	const FVBStatLabel* Label = StatLabels.Find(Stat);
	FNumberFormattingOptions Options;
	Options.AlwaysSign = true;
	Options.MaximumFractionalDigits = 2;
	const bool bPercent = Label && Label->bPercent;
	const FText Number = bPercent
		? FText::Format(NSLOCTEXT("VBInventory", "PercentValue", "{0}%"), FText::AsNumber(Value * 100.0f, &Options))
		: FText::AsNumber(Value, &Options);
	return Number;
}

FText UVBInventoryScreenWidget::BuildStatText(const UVBItemDefinition* Item) const
{
	const FVBItemFragment_Equippable* Equippable = Item ? Item->FindFragment<FVBItemFragment_Equippable>() : nullptr;
	if (!Equippable)
	{
		return FText::GetEmpty();
	}
	// 견줄 장비 = 같은 종류 칸에 지금 입은 다른 장비(첫 칸). 이 장비를 이미 입었거나 그 칸이 비면 견주지 않는다.
	const UVBItemDefinition* Current = nullptr;
	if (const UVBEquipmentComponent* Worn = Equipment.Get(); Worn && Worn->FindSlotOf(Item) == INDEX_NONE)
	{
		const TArray<FGameplayTag>& Layout = Worn->GetSlotLayout();
		for (int32 Index = 0; Index < Layout.Num() && !Current; ++Index)
		{
			Current = Layout[Index].MatchesTagExact(Equippable->SlotType) ? Worn->GetEquipped(Index) : nullptr;
		}
	}

	const FVBItemFragment_Equippable* CurrentStats = Current ? Current->FindFragment<FVBItemFragment_Equippable>() : nullptr;
	TArray<FText> Lines;
	for (const TPair<FGameplayTag, float>& Stat : Equippable->Stats)
	{
		const FVBStatLabel* Label = StatLabels.Find(Stat.Key);
		const FText Name = Label ? Label->Label : FText::FromName(Stat.Key.GetTagName());
		const FText Line = CurrentStats
			? FText::Format(StatCompareFormat, Name, FormatStat(Stat.Key, Stat.Value),
				FormatStat(Stat.Key, CurrentStats->Stats.FindRef(Stat.Key)))
			: FText::Format(StatLineFormat, Name, FormatStat(Stat.Key, Stat.Value));
		Lines.Add(Line);
	}

	return FText::Join(INVTEXT("\n"), Lines);
}

void UVBInventoryScreenWidget::HandleDiscardClicked()
{
	if (UVBInventoryComponent* Owned = Inventory.Get())
	{
		// 한 번에 하나. 목록은 OnInventoryChanged 로 다시 그려진다.
		Owned->RequestDiscard(Selected.Get(), 1);
	}
}

void UVBInventoryScreenWidget::RefreshQuickSlotButtons()
{
	if (!QuickSlotBox)
	{
		return;
	}

	const UVBItemDefinition* Item = Selected.Get();
	const UVBInventoryComponent* Owned = Inventory.Get();
	// 넣을 수 없는 아이템이면 칸 버튼을 접는다 - 서버가 거절할 것을 누르게 두지 않는다(결정은 여전히 서버의 VBQuickSlot::Assign).
	const bool bAssignable = Item && Owned && VBQuickSlot::CanAssign(Item);
	QuickSlotBox->SetVisibility(bAssignable ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (!bAssignable)
	{
		return;
	}

	if (!QuickSlotButtonClass)
	{
		VB_LOG(Warning, "인벤토리 화면: QuickSlotButtonClass 미지정 - 퀵슬롯 칸 버튼을 그릴 수 없다(WBP 클래스 기본값)");
		return;
	}

	const TArray<TObjectPtr<const UVBItemDefinition>>& Slots = Owned->GetQuickSlots();
	// 칸 수가 같으면 있는 버튼의 글만 바꾼다 - 새로 지으면 게임패드 초점이 누른 버튼과 함께 사라진다.
	if (QuickSlotBox->GetChildrenCount() != Slots.Num())
	{
		QuickSlotBox->ClearChildren();
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			UVBListRowWidget* Button = CreateWidget<UVBListRowWidget>(this, QuickSlotButtonClass);
			Button->OnRowClicked.BindUObject(this, &UVBInventoryScreenWidget::HandleQuickSlotClicked);
			QuickSlotBox->AddChild(Button);
		}
	}

	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		UVBListRowWidget* Button = Cast<UVBListRowWidget>(QuickSlotBox->GetChildAt(Index));
		if (!Button)
		{
			continue;
		}

		const UVBItemDefinition* InSlot = Slots[Index].Get();
		const FText Detail = InSlot == Item ? QuickSlotRemoveText : (InSlot ? InSlot->DisplayName : FText::GetEmpty());
		Button->Setup(Index, FText::Format(QuickSlotLabelFormat, Index + 1), Detail, /*bEnabled=*/true);
	}
}

void UVBInventoryScreenWidget::HandleQuickSlotClicked(int32 SlotIndex)
{
	UVBInventoryComponent* Owned = Inventory.Get();
	const UVBItemDefinition* Item = Selected.Get();
	if (!Owned || !Item)
	{
		return;
	}
	// 고른 아이템이 이미 그 칸에 있으면 뺀다(옆 글 '빼기'), 아니면 넣는다 - 다른 칸에 있던 같은 아이템은 서버가 옮긴다.
	Owned->RequestAssignQuickSlot(SlotIndex, Owned->GetQuickSlotItem(SlotIndex) == Item ? nullptr : Item);
}

void UVBInventoryScreenWidget::HandleCloseInput()
{
	DeactivateWidget();
}

void UVBInventoryScreenWidget::HandleUseClicked()
{
	UVBInventoryComponent* Owned = Inventory.Get();
	const UVBItemDefinition* Item = Selected.Get();
	if (!Owned)
	{
		return;
	}
	// 캐릭터 동작이 있는 아이템이면 화면을 닫고 동작을 본다.
	//   바로 쓰는 아이템은 닫지 않는다 - 볼 동작이 없고, 여러 개를 이어 쓰는 흐름을 끊지 않는다.
	const FVBItemFragment_Usable* Usable = Item ? Item->FindFragment<FVBItemFragment_Usable>() : nullptr;
	const bool bHasMotion = Usable && Usable->UseMontage;
	Owned->RequestUseItem(Item);
	if (bHasMotion)
	{
		DeactivateWidget();
	}
}

#if WITH_EDITOR
EDataValidationResult UVBInventoryScreenWidget::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// 칸 종류는 Equipment.Slot 아래 태그 전부다(배치 UVBEquipmentComponent::SlotLayout 는 그중 고른 순서일 뿐) - 하나라도 이름이 없으면 그 칸은 태그 글로 보인다.
	//   Equipment.Slot 이 등록돼 있지 않으면 RequestGameplayTag 가 여기서 오류를 낸다 - 빈 목록으로 통과하지 않는다.
	FGameplayTagContainer SlotKinds =
		UGameplayTagsManager::Get().RequestGameplayTagChildren(FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot")));
	for (const FGameplayTag& SlotKind : SlotKinds)
	{
		const FText* Label = SlotLabels.Find(SlotKind);
		if (!Label || Label->IsEmpty())
		{
			Context.AddError(FText::Format(NSLOCTEXT("VBInventoryScreen", "MissingSlotLabel",
				"SlotLabels 에 장비 칸 {0} 의 이름이 없다 - 장비 화면에 태그 글이 그대로 보인다."), FText::FromName(SlotKind.GetTagName())));
			Result = EDataValidationResult::Invalid;
		}
	}

	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif
