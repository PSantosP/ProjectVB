// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UVBItemDefinition;

/** 메뉴 화면이 같이 쓰는 글 규칙(순수 함수). */
namespace VBScreenText
{
	// 못 하는 까닭 글. 고른 줄이 없거나 표에 없는 결과면 비운다.
	template <typename ResultType>
	FText ResultReason(const TMap<ResultType, FText>& Texts, ResultType Result, bool bHasSelection)
	{
		const FText* Reason = bHasSelection ? Texts.Find(Result) : nullptr;
		return Reason ? *Reason : FText::GetEmpty();
	}

	// 가진 돈 한 줄. Format 의 {Name} = 돈 아이템의 표시 이름, {Count} = 가진 수. 돈 아이템이 없으면 이름을 비운다.
	VB_API FText HeldCurrencyLine(const FText& Format, const UVBItemDefinition* Currency, int32 HeldCount);

	// 돈이 모자랄 때의 기본 글.
	VB_API FText NotEnoughCurrency();

	// 더 가질 자리가 없을 때의 기본 글.
	VB_API FText NoRoom();
}
