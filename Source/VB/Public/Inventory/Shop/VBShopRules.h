// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBShopRules.generated.h"

class UVBShopDefinition;
struct FVBPriceRule;

/** 사기 판정의 답. 화면은 이것으로 사기 버튼을 켜고 끄고 까닭을 보인다. */
UENUM(BlueprintType)
enum class EVBPurchaseResult : uint8
{
	Ok,
	// 수가 1 보다 작거나 단가가 없다 - 정상 화면에서는 나오지 않는 요청.
	InvalidRequest,
	// 줄의 조건이 거짓.
	Unavailable,
	OutOfStock,
	NotEnoughCurrency,
	// 아이템 상한에 걸려 다 넣을 수 없다. 돈만 빠지는 반쪽 거래를 막으려고 아예 안 판다.
	NoRoom,
};

/** 사기 판정에 드는 수 전부. 월드 없이 채워 서버와 화면이 같은 판정(VBShop::CheckPurchase)을 쓴다. */
struct FVBPurchaseQuote
{
	int32 Count = 1;
	int32 UnitPrice = 0;
	// 남은 재고. INDEX_NONE = 무한.
	int32 Remaining = INDEX_NONE;
	bool bAvailable = true;
	int32 CurrencyHeld = 0;
	// 지금 가진 수에서 Count 개를 넣으려 할 때 실제로 들어갈 수(UVBItemDefinition::ClampToMax).
	int32 AddableCount = 0;
};

/** 값과 사기 판정. */
namespace VBShop
{
	// 조건이 참인 규칙의 배율을 모두 곱한다. 빈 목록 = 1.
	VB_API float PriceMultiplier(const TArray<FVBPriceRule>& Rules, const FVBRuleContext& Context);

	// 기준 값 x 배율, 반올림, 최소 1. 기준 값이 없으면(0 이하) 0 = 팔 수 없다.
	VB_API int32 UnitPrice(int32 BasePrice, float Multiplier);

	VB_API EVBPurchaseResult CheckPurchase(const FVBPurchaseQuote& Quote);

#if WITH_EDITOR
	// 값 규칙 배열 검증(경제 설정, 상점이 같이 쓴다): 조건 칸, 배율 > 0.
	VB_API bool ValidatePriceRules(const TArray<FVBPriceRule>& Rules, const FText& Where, class FDataValidationContext& Context);
#endif
}

/** 상점을 연다(대화 효과). 문맥의 상대를 상인으로 전달하고, 상대가 없으면 상인 없이 연다. */
USTRUCT(BlueprintType, meta=(DisplayName="상점 열기"))
struct VB_API FVBEffect_OpenShop : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Shop")
	TObjectPtr<const UVBShopDefinition> Shop;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
