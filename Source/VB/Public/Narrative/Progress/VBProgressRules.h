// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBProgressRules.generated.h"

/**
 * 지역에 들어왔다. 영역 트리거(AVBAreaTrigger::Effects)에 꽂는다.
 * 서버가 VisitedFact 로 처음인지 보고(처음이면 켠다) 들어온 플레이어의 소유 클라에만 배너를 알린다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="지역 들어옴"))
struct VB_API FVBEffect_EnterRegion : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Progress", meta=(Categories="Map.Region"))
	FGameplayTag RegionId;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
