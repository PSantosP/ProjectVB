// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"
#include "Inventory/VBItemAmount.h"
#include "VBCraftingStationDefinition.generated.h"

/** 제작법 하나: 재료와 돈을 내고 결과를 받는다. 실패는 없다. */
USTRUCT(BlueprintType)
struct VB_API FVBCraftRecipe
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Crafting")
	FVBItemAmount Output;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Crafting")
	TArray<FVBItemAmount> Inputs;

	// 돈(경제 설정의 돈 아이템). 0 = 재료만.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Crafting", meta=(ClampMin="0"))
	int32 Gold = 0;

	// 전부 참일 때만 만들 수 있다(예: 설계도를 얻은 사실). 비면 늘.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Crafting",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 조건이 거짓일 때 잠긴 채 보이나(상점 줄과 같은 칸). 끄면 목록에서 빠진다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Crafting")
	bool bShowWhenUnavailable = true;
};

/**
 * 제작대 하나의 정의(대장장이, 작업대마다 하나, 실행 중 불변). 여는 길은 FVBEffect_OpenCrafting 하나다.
 * 강화 단계는 강화 컴포넌트가, 비용 계산은 VBCrafting 함수가 든다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBCraftingStationDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Crafting")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Crafting")
	TArray<FVBCraftRecipe> Recipes;

	// 이 제작대에서 강화를 할 수 있나. 끄면 강화 탭이 숨고 서버도 거절한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Crafting")
	bool bAllowEnhancement = true;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
