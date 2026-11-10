// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/Shop/VBShopComponent.h"

#include "GameFramework/Controller.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/Shop/VBEconomyConfig.h"
#include "Inventory/Shop/VBEconomySettings.h"
#include "Inventory/Shop/VBShopDefinition.h"
#include "Inventory/Shop/VBShopStateSubsystem.h"
#include "VB/VB.h"

const FVBShopEntry* FVBShopView::GetEntry(const FVBShopViewEntry& ViewEntry) const
{
	return Shop && Shop->Entries.IsValidIndex(ViewEntry.EntryIndex) ? &Shop->Entries[ViewEntry.EntryIndex] : nullptr;
}

UVBShopComponent::UVBShopComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 서버 -> 소유 클라이언트 RPC 를 쓰려면 컴포넌트가 복제돼야 한다. 복제하는 속성은 없다(대화 컴포넌트와 같다).
	SetIsReplicatedByDefault(true);
}

bool UVBShopComponent::OpenShop(const UVBShopDefinition* Shop, AActor* Merchant)
{
	return OpenSession(Shop, Merchant);
}

void UVBShopComponent::CloseShop()
{
	CloseSession();
}

void UVBShopComponent::RequestBuy(const UVBItemDefinition* Item, int32 Count)
{
	if (View.IsActive())
	{
		ServerBuy(View.Shop, Item, Count);
	}
}

void UVBShopComponent::RequestClose()
{
	if (View.IsActive())
	{
		ServerClose();
	}
}

EVBPurchaseResult UVBShopComponent::PreviewPurchase(const FVBShopViewEntry& ViewEntry, int32 Count) const
{
	const FVBShopEntry* Entry = View.GetEntry(ViewEntry);
	const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	const UVBItemDefinition* Currency = GetCurrency();
	if (!Entry || !Entry->Item || !Inventory || !Currency)
	{
		return EVBPurchaseResult::InvalidRequest;
	}

	FVBPurchaseQuote Quote;
	Quote.Count = Count;
	Quote.UnitPrice = ViewEntry.UnitPrice;
	Quote.Remaining = ViewEntry.Remaining;
	Quote.bAvailable = ViewEntry.bAvailable;
	Quote.CurrencyHeld = Inventory->GetCount(Currency);
	Quote.AddableCount = Entry->Item->ClampToMax(Inventory->GetCount(Entry->Item), Count);
	return VBShop::CheckPurchase(Quote);
}

const UVBItemDefinition* UVBShopComponent::GetCurrency()
{
	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy();
	return Economy ? Economy->Currency.Get() : nullptr;
}

bool UVBShopComponent::CanOpenSession(const UObject* Definition) const
{
	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy();
	if (!Economy || !Economy->Currency)
	{
		// 돈을 모르면 값도 거래도 판정할 수 없다. 열어 두면 사기 버튼이 전부 꺼진 상점이 되므로 아예 열지 않는다.
		VB_LOG(Warning, "상점 열기 거절(%s): 경제 설정에 돈 아이템이 없다", *GetNameSafe(Definition));
		return false;
	}

	return true;
}

void UVBShopComponent::SendSessionClosed()
{
	ClientCloseShop();
}

const UVBShopDefinition* UVBShopComponent::GetServerShop() const
{
	return Cast<UVBShopDefinition>(GetSessionDefinition());
}

void UVBShopComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	View = FVBShopView();
	Super::EndPlay(EndPlayReason);
}

void UVBShopComponent::ServerBuy_Implementation(const UVBShopDefinition* Shop, const UVBItemDefinition* Item, int32 Count)
{
	if (!ValidateSessionRequest(Shop, TEXT("사기")))
	{
		return;
	}

	const FVBShopEntry* Entry = GetServerShop()->FindEntry(Item);
	if (!Entry)
	{
		VB_LOG(Warning, "상점 사기 거절: %s 에 %s 줄이 없다", *GetNameSafe(GetServerShop()), *GetNameSafe(Item));
		return;
	}

	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy();
	UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(GetOwner());
	UVBShopStateSubsystem* State = UVBShopStateSubsystem::Get(this);
	if (!Economy || !Economy->Currency || !Inventory || !State)
	{
		// 판 수를 셀 곳이 없으면 재고 한정 줄이 무한이 된다 - 모르는 상태를 통과로 떨어뜨리지 않는다.
		VB_LOG(Warning, "상점 사기 거절: %s", !Inventory ? TEXT("이 플레이어의 인벤토리가 없다")
			: !State ? TEXT("재고 서브시스템이 없다") : TEXT("경제 설정에 돈 아이템이 없다"));
		return;
	}

	const UVBItemDefinition* Currency = Economy->Currency;
	const FVBRuleContext Context = MakeRuleContext();

	// 화면이 켠 버튼을 믿지 않는다 - 조건, 값, 재고, 돈, 자리를 여기서 다시 본다.
	FVBPurchaseQuote Quote;
	Quote.Count = Count;
	Quote.UnitPrice = VBShop::UnitPrice(Entry->GetBasePrice(), ComputeMultiplier(*Economy, Context));
	Quote.Remaining = Entry->GetRemaining(State->GetSold(GetServerShop()->ShopId, Item->ItemId));
	Quote.bAvailable = VBRules::EvaluateAll(Entry->Conditions, Context);
	Quote.CurrencyHeld = Inventory->GetCount(Currency);
	Quote.AddableCount = Item->ClampToMax(Inventory->GetCount(Item), Count);
	const EVBPurchaseResult Result = VBShop::CheckPurchase(Quote);
	if (Result != EVBPurchaseResult::Ok)
	{
		VB_LOG(Log, "상점 사기 거절: %s x%d - %s", *GetNameSafe(Item), Count, *UEnum::GetValueAsString(Result));
		// 화면이 본 값이 낡았을 수 있다(그 사이 재고, 조건이 바뀜) - 지금 값으로 다시 그리게 한다.
		SendView();
		return;
	}

	// 바꾸기. 위 판정이 돈(가진 수 >= 합)과 자리(넣을 수 == Count)를 확인했으므로 둘 다 실패하지 않아야 한다.
	const int32 Total = Quote.UnitPrice * Count;
	if (!Inventory->RemoveItem(Currency, Total))
	{
		VB_LOG(Error, "상점 사기: 판정은 통과했는데 돈 %d 를 빼지 못했다 - 거래를 하지 않는다", Total);
		return;
	}

	const int32 Added = Inventory->AddItem(Item, Count);
	if (Added < Count)
	{
		// 판정과 넣기 사이에 상한이 바뀐 경우뿐이다. 못 넣은 만큼 돈을 돌려준다 - 돈만 빠진 거래를 남기지 않는다.
		VB_LOG(Error, "상점 사기: %s 를 %d 개 중 %d 개만 넣었다 - 나머지 값 %d 를 돌려준다",
			*GetNameSafe(Item), Count, Added, (Count - Added) * Quote.UnitPrice);
		Inventory->AddItem(Currency, (Count - Added) * Quote.UnitPrice);
	}

	if (Added > 0)
	{
		State->AddSold(Shop->ShopId, Item->ItemId, Added);
	}

	VB_LOG(Log, "상점 사기: %s x%d, 단가 %d (%s)", *GetNameSafe(Item), Added, Quote.UnitPrice, *GetNameSafe(Shop));
	SendView();
}

void UVBShopComponent::ServerClose_Implementation()
{
	CloseShop();
}

void UVBShopComponent::ClientShowShop_Implementation(const FVBShopView& InView)
{
	View = InView;
	if (!View.IsActive())
	{
		VB_LOG(Warning, "상점 표시: 상점이 빈 보기를 받았다 - 빈 화면이 된다");
	}
	// 화면을 여는 것은 이 컴포넌트가 아니라 PC 다(AVBPlayerController 가 이 알림을 듣는다).
	OnViewChanged.Broadcast();
}

void UVBShopComponent::ClientCloseShop_Implementation()
{
	View = FVBShopView();
	OnViewChanged.Broadcast();
}

void UVBShopComponent::SendView()
{
	const UVBEconomyConfig* Economy = UVBEconomySettings::LoadEconomy();
	const UVBShopStateSubsystem* State = UVBShopStateSubsystem::Get(this);
	if (!GetServerShop() || !Economy || !State)
	{
		VB_LOG(Warning, "상점 보기: 상점·경제 설정·재고 중 없는 것이 있다 - 거래를 닫는다");
		CloseShop();
		return;
	}

	const FVBRuleContext Context = MakeRuleContext();
	const float Multiplier = ComputeMultiplier(*Economy, Context);

	FVBShopView NewView;
	NewView.Shop = GetServerShop();
	for (int32 Index = 0; Index < GetServerShop()->Entries.Num(); ++Index)
	{
		const FVBShopEntry& Entry = GetServerShop()->Entries[Index];
		if (!Entry.Item)
		{
			continue;
		}

		const bool bAvailable = VBRules::EvaluateAll(Entry.Conditions, Context);
		if (!bAvailable && !Entry.bShowWhenUnavailable)
		{
			continue;
		}

		FVBShopViewEntry& ViewEntry = NewView.Entries.AddDefaulted_GetRef();
		ViewEntry.EntryIndex = Index;
		ViewEntry.UnitPrice = VBShop::UnitPrice(Entry.GetBasePrice(), Multiplier);
		ViewEntry.Remaining = Entry.GetRemaining(State->GetSold(GetServerShop()->ShopId, Entry.Item->ItemId));
		ViewEntry.bAvailable = bAvailable;
	}

	ClientShowShop(NewView);
}

float UVBShopComponent::ComputeMultiplier(const UVBEconomyConfig& Economy, const FVBRuleContext& Context) const
{
	return VBShop::PriceMultiplier(Economy.PriceRules,
		Context) * (GetServerShop() ? VBShop::PriceMultiplier(GetServerShop()->PriceRules, Context) : 1.0f);
}

