// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBStationScreenWidget.h"

#include "Components/TextBlock.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBStationSessionComponent.h"
#include "UI/VBScreenText.h"
#include "VB/VB.h"

UVBStationScreenWidget::UVBStationScreenWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 뒤로 가기를 닫기 요청으로 돌린다. 끄면 뒤로 가기로 세션을 벗어날 길이 없다.
	bIsBackHandler = true;
	bRequireCloseButton = true;
	CurrencyFormat = NSLOCTEXT("VBShopScreen", "Currency", "{Name} {Count}");
}

void UVBStationScreenWidget::NativeOnActivated()
{
	Super::NativeOnActivated();
	UVBStationSessionComponent* Found = FindSession();
	Session.Bind(Found, &UVBStationSessionComponent::OnViewChanged, this, &ThisClass::Rebuild);
	VB_CLOG(!Found, Warning, "%s: 이 플레이어의 세션 컴포넌트를 PlayerController 에서 찾지 못했다", *GetClass()->GetName());
}

void UVBStationScreenWidget::NativeOnDeactivated()
{
	Session.Reset();
	Super::NativeOnDeactivated();
}

bool UVBStationScreenWidget::NativeOnHandleBackAction()
{
	// 이 값은 CommonUI 의 입력 바인딩만 본다. BtnClose 는 HandleBackAction 으로 바로 들어온다.
	if (!bIsBackHandler)
	{
		return false;
	}

	if (UVBStationSessionComponent* Owned = Session.Get())
	{
		Owned->RequestClose();
	}
	else
	{
		// 세션이 없으면 화면만이라도 내려 입력이 갇히지 않게 한다.
		DeactivateWidget();
	}

	return true;
}

void UVBStationScreenWidget::RefreshCurrency(const UVBInventoryComponent* Inventory)
{
	if (TxtCurrency)
	{
		const UVBItemDefinition* Currency = UVBShopComponent::GetCurrency();
		TxtCurrency->SetText(VBScreenText::HeldCurrencyLine(CurrencyFormat, Currency, Inventory && Currency ? Inventory->GetCount(Currency) : 0));
	}
}
