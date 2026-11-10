// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Inventory/VBInventoryExchangeRules.h"

#include "Inventory/VBItemDefinition.h"

EVBExchangeRowCheck VBInventoryExchangeRules::CheckRow(const FVBExchangeRow& Row, int32 Held)
{
	if (Held < Row.Cost)
	{
		return EVBExchangeRowCheck::NotEnough;
	}

	if (Row.Gain <= 0)
	{
		return EVBExchangeRowCheck::Ok;
	}

	if (Row.Gain > MAX_int32 - (Held - Row.Cost) || Row.Item->ClampToMax(Held - Row.Cost, Row.Gain) != Row.Gain)
	{
		return EVBExchangeRowCheck::NoRoom;
	}

	return EVBExchangeRowCheck::Ok;
}
