// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBEncounterRules.generated.h"

/**
 * 조우 문(UVBEncounterGateComponent)을 움직이는 규칙. 규칙 핵심(VBRuleTypes)은 적을 몰라 적 갈래에 둔다.
 * 대상은 꼬리표로 찾는다: 이 월드에서 EncounterTags 가 질의와 겹치는 조우 문 전부.
 */

/** 꼬리표가 맞는 조우를 보류하거나 푼다. 시네마틱 재생이 장면 시작, 끝 효과로 건다. */
USTRUCT(BlueprintType, meta=(DisplayName="조우 보류"))
struct VB_API FVBEffect_HoldEncounter : public FVBEffect
{
	GENERATED_BODY()

	// 이 꼬리표 중 하나라도 가진 조우 문에 건다. 비면 아무것도 안 한다(월드 전체를 멈추지 않는다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter")
	FGameplayTagContainer EncounterTags;

	// 참 = 보류, 거짓 = 풀기.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Encounter")
	bool bHold = true;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
