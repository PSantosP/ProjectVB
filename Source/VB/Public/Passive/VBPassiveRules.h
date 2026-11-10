// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBPassiveRules.generated.h"

/** 부른 플레이어에게 패시브를 준다. 귀속 아이템의 얻기 효과(FVBItemFragment_AcquireEffects)에 한 줄로 넣는다. */
USTRUCT(BlueprintType, meta=(DisplayName="패시브 주기"))
struct VB_API FVBEffect_GrantPassive : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Passive", meta=(Categories="Passive"))
	FGameplayTag PassiveTag;

	virtual bool CanApply(const FVBRuleContext& Context) const override;
	virtual void Apply(const FVBRuleContext& Context) const override;
};
