// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBItemAmount.generated.h"

class UVBItemDefinition;

// 아이템 하나와 개수 - 강화 비용, 제작 재료, 결과처럼 무엇을 몇 개인지 저작하는 줄.
//   저장 줄(FVBSavedItem)은 이름표를 든다. 이것은 자산을 직접 가리키는 저작용이다.
USTRUCT(BlueprintType)
struct VB_API FVBItemAmount
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Item")
	TObjectPtr<const UVBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Item", meta=(ClampMin="1"))
	int32 Count = 1;
};
