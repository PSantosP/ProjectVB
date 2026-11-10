// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBElevatorRules.generated.h"

class AVBElevator;

/**
 * 승강기를 보내는 효과. 층마다 놓인 호출 버튼(AVBToggleProp)의 완료 효과에 단다.
 * 전력 같은 조건은 버튼의 UseConditions 에 둔다 - 이 효과는 조건을 보지 않는다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="승강기 보내기"))
struct VB_API FVBEffect_SendElevator : public FVBEffect
{
	GENERATED_BODY()

	// 같은 레벨에 놓인 승강기. 이 월드에 올라와 있지 않으면 아무것도 안 한다(경고).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator")
	TSoftObjectPtr<AVBElevator> Elevator;

	// 참 = 위 층으로, 거짓 = 아래 층으로.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator")
	bool bUp = true;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
