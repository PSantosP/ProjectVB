// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Shop/VBShopScreenWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Inventory/Shop/VBShopDefinition.h"
#include "UI/VBScreenText.h"
#include "VB/VB.h"

#define LOCTEXT_NAMESPACE "VBShopScreen"

UVBShopScreenWidget::UVBShopScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PriceFormat = LOCTEXT("Price", "{0}");
	LimitedPriceFormat = LOCTEXT("LimitedPrice", "{0} (남은 {1})");
	ResultTexts.Add(EVBPurchaseResult::Unavailable, LOCTEXT("Unavailable", "아직 살 수 없다"));
	ResultTexts.Add(EVBPurchaseResult::OutOfStock, LOCTEXT("OutOfStock", "다 팔렸다"));
	ResultTexts.Add(EVBPurchaseResult::NotEnoughCurrency, VBScreenText::NotEnoughCurrency());
	ResultTexts.Add(EVBPurchaseResult::NoRoom, VBScreenText::NoRoom());
}

void UVBShopScreenWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (BtnBuy)
	{
		BtnBuy->OnClicked.AddUniqueDynamic(this, &UVBShopScreenWidget::HandleBuyClicked);
	}
}

void UVBShopScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	SelectedEntryIndex = INDEX_NONE;
	Rebuild();
	// 상점은 PC 에 있지만 가진 돈은 PlayerState 의 인벤토리다. 원격 클라는 PlayerState 가 늦게 올 수 있다.
	if (AVBPlayerController* VBPC = Cast<AVBPlayerController>(GetOwningPlayer()))
	{
		PlayerStateSet.Register(*VBPC, FVBOnPlayerStateSet::FDelegate::CreateUObject(this, &UVBShopScreenWidget::BindInventory));
	}
	else
	{
		VB_LOG(Warning, "상점 화면: 주인이 AVBPlayerController 가 아니다 - PlayerState 를 기다릴 수 없다");
	}
}

void UVBShopScreenWidget::NativeOnDeactivated()
{
	PlayerStateSet.Unregister();
	Inventory.Reset();
	Super::NativeOnDeactivated();
}

UVBStationSessionComponent* UVBShopScreenWidget::FindSession() const
{
	const APlayerController* PC = GetOwningPlayer();
	return PC ? PC->FindComponentByClass<UVBShopComponent>() : nullptr;
}

void UVBShopScreenWidget::BindInventory(APlayerState* /*PlayerState*/)
{
	// 가진 돈, 가진 수가 바뀌면 사기 버튼과 돈 글만 다시 본다.
	Inventory.Bind(UVBInventoryComponent::FindFor(GetOwningPlayer()), &UVBInventoryComponent::OnInventoryChanged,
		this, &UVBShopScreenWidget::RefreshDetail);
	RefreshDetail();
}

int32 UVBShopScreenWidget::BuildRows()
{
	RowEntryIndices.Reset();
	const UVBShopComponent* Shop = GetSession<UVBShopComponent>();
	const FVBShopView* View = Shop ? &Shop->GetView() : nullptr;
	if (TxtShopName)
	{
		TxtShopName->SetText(View && View->Shop ? View->Shop->DisplayName : FText::GetEmpty());
	}

	const int32 EntryCount = View ? View->Entries.Num() : 0;
	for (int32 Index = 0; Index < EntryCount; ++Index)
	{
		const FVBShopViewEntry& ViewEntry = View->Entries[Index];
		const FVBShopEntry* Entry = View->GetEntry(ViewEntry);
		if (!Entry || !Entry->Item)
		{
			continue;
		}

		const FText Detail = ViewEntry.Remaining == INDEX_NONE
			? FText::Format(PriceFormat, ViewEntry.UnitPrice)
			: FText::Format(LimitedPriceFormat, ViewEntry.UnitPrice, ViewEntry.Remaining);
		AddRow(RowEntryIndices.Add(ViewEntry.EntryIndex), Entry->Item->DisplayName, Detail);
	}

	KeepSelectionOrFirst(RowEntryIndices, SelectedEntryIndex, INDEX_NONE);
	return RowEntryIndices.Num();
}

void UVBShopScreenWidget::Select(int32 RowIndex)
{
	if (RowEntryIndices.IsValidIndex(RowIndex))
	{
		SelectedEntryIndex = RowEntryIndices[RowIndex];
		RefreshDetail();
	}
}

const FVBShopViewEntry* UVBShopScreenWidget::FindSelected() const
{
	const UVBShopComponent* Shop = GetSession<UVBShopComponent>();
	if (!Shop || SelectedEntryIndex == INDEX_NONE)
	{
		return nullptr;
	}

	const int32 Selected = SelectedEntryIndex;
	return Shop->GetView().Entries.FindByPredicate([Selected](const FVBShopViewEntry& E) { return E.EntryIndex == Selected; });
}

void UVBShopScreenWidget::RefreshDetail()
{
	const UVBShopComponent* Shop = GetSession<UVBShopComponent>();
	const FVBShopViewEntry* ViewEntry = FindSelected();
	const FVBShopEntry* Entry = Shop && ViewEntry ? Shop->GetView().GetEntry(*ViewEntry) : nullptr;
	const UVBItemDefinition* Item = Entry ? Entry->Item.Get() : nullptr;
	if (TxtItemName)
	{
		TxtItemName->SetText(Item ? Item->DisplayName : FText::GetEmpty());
	}

	if (TxtItemDescription)
	{
		TxtItemDescription->SetText(Item ? Item->Description : FText::GetEmpty());
	}

	RefreshCurrency(Inventory.Get());
	// 버튼은 서버와 같은 판정으로 켠다(표시용). 고른 줄이 없으면 꺼진다.
	const EVBPurchaseResult Result = Shop && ViewEntry ? Shop->PreviewPurchase(*ViewEntry, /*Count=*/1) : EVBPurchaseResult::InvalidRequest;
	if (BtnBuy)
	{
		BtnBuy->SetIsEnabled(Result == EVBPurchaseResult::Ok);
	}

	if (TxtStatus)
	{
		TxtStatus->SetText(VBScreenText::ResultReason(ResultTexts, Result, ViewEntry != nullptr));
	}
}

void UVBShopScreenWidget::HandleBuyClicked()
{
	UVBShopComponent* Shop = GetSession<UVBShopComponent>();
	const FVBShopViewEntry* ViewEntry = FindSelected();
	const FVBShopEntry* Entry = Shop && ViewEntry ? Shop->GetView().GetEntry(*ViewEntry) : nullptr;
	if (Entry && Entry->Item)
	{
		Shop->RequestBuy(Entry->Item, 1);
	}
}

#undef LOCTEXT_NAMESPACE
