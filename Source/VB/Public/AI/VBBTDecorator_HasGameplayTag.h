// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AI/VBBTDecorator_LiveCondition.h"
#include "VBBTDecorator_HasGameplayTag.generated.h"

/**
 * 이 폰의 ASC 가 지정한 태그를 갖고 있는가. 태그는 자산에서 고른다.
 * 관찰형이라 경직처럼 분기 도중에 켜지는 상태도 그 자리에서 분기를 끊는다.
 */
UCLASS()
class VB_API UVBBTDecorator_HasGameplayTag : public UVBBTDecorator_LiveCondition
{
	GENERATED_BODY()

public:
	UVBBTDecorator_HasGameplayTag();

protected:
	// 물어볼 태그(자식 태그 포함). 비어 있으면 항상 false.
	UPROPERTY(EditAnywhere, Category="VB|AI")
	FGameplayTag QueryTag;

	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;
};
