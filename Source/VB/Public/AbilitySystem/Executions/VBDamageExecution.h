// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "VBDamageExecution.generated.h"

/**
 * 피해 GE 계산 - 받는 쪽 피해를 계산하는 유일한 곳. 공격자 BaseDamage x 받는 피해 배수 x 난이도.
 * IncomingDamage 다음 IncomingPoiseDamage 순으로 낸다(체력 다음 경직 순서를 지키려고 모디파이어로 두지 않는다).
 * 무효화, 가드 감쇠는 받는 세트의 몫이다.
 */
UCLASS()
class VB_API UVBDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UVBDamageExecution();

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};
