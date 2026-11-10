// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AI/VBBTDecorator_LiveCondition.h"
#include "VBBTDecorator_HasAttackToken.generated.h"

/**
 * 이 적이 지금 어택 토큰을 보유하고 있는가. 주기 캐시인 블랙보드가 아니라 전투 컴포넌트에 직접 묻는다.
 * 관찰형이라 걷는 도중 토큰을 회수당하면 그 자리에서 분기를 끊는다.
 */
UCLASS()
class VB_API UVBBTDecorator_HasAttackToken : public UVBBTDecorator_LiveCondition
{
	GENERATED_BODY()

public:
	UVBBTDecorator_HasAttackToken();

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
	virtual FString GetStaticDescription() const override;
};
