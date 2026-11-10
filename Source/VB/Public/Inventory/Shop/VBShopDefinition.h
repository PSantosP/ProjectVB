// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBShopDefinition.generated.h"

class UVBItemDefinition;

/** 값 규칙 하나: 조건이 전부 참이면 배율을 곱한다. 참인 규칙(전역, 상점)은 모두 곱한다. */
USTRUCT(BlueprintType)
struct VB_API FVBPriceRule
{
	GENERATED_BODY()

	// 전부 참일 때만 배율이 걸린다. 비면 늘 걸린다(상점 전체 할증 같은 것).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 0.8 = 20% 할인, 1.25 = 25% 할증.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop", meta=(ClampMin="0.01"))
	float Multiplier = 1.0f;
};

/** 상점의 한 줄. 아이템이 재고의 열쇠라 같은 아이템은 한 줄뿐이다. */
USTRUCT(BlueprintType)
struct VB_API FVBShopEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop")
	TObjectPtr<const UVBItemDefinition> Item;

	// 0 = 아이템의 가격 조각(FVBItemFragment_Price)을 쓴다. 이 상점만 기준 값이 다를 때만 적는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop", meta=(ClampMin="0"))
	int32 PriceOverride = 0;

	// 팔 수 있는 총수. -1 = 무한. 남은 수 = 이것 - 판 수(UVBShopStateSubsystem).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop", meta=(ClampMin="-1"))
	int32 Stock = -1;

	// 전부 참일 때만 살 수 있다. 비면 늘 살 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 조건이 거짓일 때 잠긴 채 보이나(대화 선택지와 같은 칸). 끄면 목록에서 빠진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop")
	bool bShowWhenUnavailable = false;

	// 값 규칙을 걸기 전의 값. 0 = 값이 없다(팔 수 없는 줄 - IsDataValid 가 잡는다).
	int32 GetBasePrice() const;

	// 판 수가 Sold 일 때 남은 수. 무한이면 INDEX_NONE(-1).
	int32 GetRemaining(int32 Sold) const;
};

/**
 * 상점 하나의 정의(실행 중 불변). 여는 길은 대화의 FVBEffect_OpenShop 하나다.
 * 판 수는 UVBShopStateSubsystem 이, 값 계산은 VBShop 함수가 든다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBShopDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	// 저장이 이 상점을 부르는 이름. 자산마다 하나, 겹치면 판 수가 섞인다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop", meta=(Categories="Shop"))
	FGameplayTag ShopId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop")
	TArray<FVBShopEntry> Entries;

	// 이 상점에만 거는 값 규칙. 경제 설정의 전역 규칙과 함께 곱한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Shop")
	TArray<FVBPriceRule> PriceRules;

	// 그 아이템의 줄(없으면 nullptr).
	const FVBShopEntry* FindEntry(const UVBItemDefinition* Item) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
