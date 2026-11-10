// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBCampaignRules.generated.h"

/**
 * 앞 회차를 읽는 조건. 이번 회차 사실은 FVBCondition_Fact 가 본다 - 둘은 다른 저장소다.
 * 앞 회차 기록을 바꾸는 효과는 두지 않는다. 회차를 아직 모르면(클라 복제 전) bNegate 와 무관하게 거짓이다.
 */

/** 앞 회차 결말이 이 이름표(또는 그 아래)인가. Ending 뿌리면 결말 무관하게 앞 회차가 있는지. 1회차는 거짓. */
USTRUCT(BlueprintType, meta=(DisplayName="앞 회차 결말"))
struct VB_API FVBCondition_PreviousEnding : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Campaign", meta=(Categories="Ending"))
	FGameplayTag Ending;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Campaign")
	bool bNegate = false;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

/** 앞 회차 결말이 기억할 것으로 남긴 사실 중에 이것이 있나(정확히 같은 이름표). 1회차는 거짓(bNegate 면 참). */
USTRUCT(BlueprintType, meta=(DisplayName="앞 회차 사실"))
struct VB_API FVBCondition_PreviousFact : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Campaign", meta=(Categories="Story"))
	FGameplayTag Fact;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Campaign")
	bool bNegate = false;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};
