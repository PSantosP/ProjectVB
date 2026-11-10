// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBScreenText.h"

#include "Inventory/VBItemDefinition.h"

FText VBScreenText::HeldCurrencyLine(const FText& Format, const UVBItemDefinition* Currency, int32 HeldCount)
{
	return FText::FormatNamed(Format, TEXT("Name"), Currency ? Currency->DisplayName : FText::GetEmpty(), TEXT("Count"), HeldCount);
}

FText VBScreenText::NotEnoughCurrency()
{
	return NSLOCTEXT("VBShopScreen", "NotEnoughCurrency", "값을 치를 수 없다");
}

FText VBScreenText::NoRoom()
{
	return NSLOCTEXT("VBShopScreen", "NoRoom", "더 가질 수 없다");
}
