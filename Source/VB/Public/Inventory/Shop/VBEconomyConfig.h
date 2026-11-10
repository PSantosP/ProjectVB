// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Inventory/Shop/VBShopDefinition.h"
#include "VBEconomyConfig.generated.h"

class UVBItemDefinition;

/** 경제의 전역 데이터: 무엇이 돈인가, 모든 상점에 걸리는 값 규칙. UVBEconomySettings 가 가리키는 하나뿐인 자산. */
UCLASS(BlueprintType, Const)
class VB_API UVBEconomyConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 돈으로 쓰는 아이템. 돈도 아이템이라 조건, 주기, 저장을 그대로 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Economy")
	TObjectPtr<const UVBItemDefinition> Currency;

	// 모든 상점에 거는 값 규칙(예: 평판 50 이상이면 0.8). 상점의 규칙과 함께 곱한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Economy")
	TArray<FVBPriceRule> PriceRules;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
