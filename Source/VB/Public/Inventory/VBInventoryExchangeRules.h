// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Inventory/VBItemAmount.h"

class UVBItemDefinition;

// 교환의 한 줄: 한 아이템의 내는 수와 받는 수. 호출 중에만 쓰는 값이고 가진 수의 정본은 UVBInventoryComponent 다.
struct FVBExchangeRow
{
	const UVBItemDefinition* Item = nullptr;
	int32 Cost = 0;
	int32 Gain = 0;
};

// 한 줄의 수량 판정. 효과 사전 확인은 재진입을 받는 실행 문맥이라 UVBInventoryComponent 가 한다.
enum class EVBExchangeRowCheck : uint8
{
	Ok,
	NotEnough,
	NoRoom,
};

// 교환의 수량 계산. 월드, ASC, 델리게이트, 복제를 모르는 값 함수다.
namespace VBInventoryExchangeRules
{
	// 내는 줄이나 받는 줄을 처음 나온 순서대로 아이템별로 합친다. 틀린 줄이나 넘침이면 false.
	template <typename RowArrayType>
	bool MergeAmounts(TConstArrayView<FVBItemAmount> Amounts, bool bCost, RowArrayType& Rows)
	{
		using FRow = typename RowArrayType::ElementType;
		for (const FVBItemAmount& Amount : Amounts)
		{
			if (!Amount.Item || Amount.Count <= 0)
			{
				return false;
			}

			FRow* Row = Rows.FindByPredicate([&Amount](const FRow& R) { return R.Item == Amount.Item; });
			if (!Row)
			{
				Row = &Rows.AddDefaulted_GetRef();
				Row->Item = Amount.Item;
			}

			int32& Sum = bCost ? Row->Cost : Row->Gain;
			if (Amount.Count > MAX_int32 - Sum)
			{
				return false;
			}

			Sum += Amount.Count;
		}

		return true;
	}

	// 가진 수가 Held 일 때 이 줄을 낼 수 있고 받을 자리가 있는지 본다.
	VB_API EVBExchangeRowCheck CheckRow(const FVBExchangeRow& Row, int32 Held);

	// 교환 뒤의 가진 수.
	inline int32 CountAfter(const FVBExchangeRow& Row, int32 Held)
	{
		return Held - Row.Cost + Row.Gain;
	}
}
