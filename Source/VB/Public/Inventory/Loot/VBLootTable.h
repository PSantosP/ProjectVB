// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "StructUtils/InstancedStruct.h"
#include "VBLootTable.generated.h"

class UVBItemDefinition;

/** 보상표의 한 줄. 줄마다 따로 굴린다. */
USTRUCT(BlueprintType)
struct VB_API FVBLootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Loot")
	TObjectPtr<const UVBItemDefinition> Item;

	// 나오면 이 범위에서 개수를 고른다(양끝 포함).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Loot", meta=(ClampMin="1"))
	int32 MinCount = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Loot", meta=(ClampMin="1"))
	int32 MaxCount = 1;

	// 나올 확률. 1 = 늘. 0 은 저작 실수로 본다(IsDataValid).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Loot", meta=(ClampMin="0.0", ClampMax="1.0"))
	float Chance = 1.0f;

	// 전부 참일 때만 굴린다(예: 그 퀘스트를 진행 중일 때만 나오는 열쇠). 비면 늘 굴린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Loot",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;
};

/** 보상표 - 무엇이 나오나. 상자, 적이 FVBEffect_GiveLoot 로 쓰고, 굴리기는 VBLoot::Roll 하나다. */
UCLASS(BlueprintType, Const)
class VB_API UVBLootTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Loot")
	TArray<FVBLootEntry> Entries;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
