// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Templates/SubclassOf.h"
#include "VBPlayerRules.generated.h"

class UGameplayEffect;

/**
 * 플레이어의 GAS(ASC, 평판)를 읽고 쓰는 서사 규칙. 서사 핵심이 GAS 를 몰라도 되게 게임 층(Player/)에 둔다.
 */

/** 부른 플레이어의 평판이 값과 비교해 맞나. 읽기만 한다 - 평판은 GAS 속성이 관리한다. */
USTRUCT(BlueprintType, meta=(DisplayName="평판"))
struct VB_API FVBCondition_Reputation : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	EVBRuleCompare Compare = EVBRuleCompare::AtLeast;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	float Value = 0.0f;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
};

/**
 * 부른 플레이어에게 GE + SetByCaller 를 건다. 평판 변경도 이 길이다(평판 전용 효과 타입을 두지 않는다).
 */
USTRUCT(BlueprintType, meta=(DisplayName="GE 걸기"))
struct VB_API FVBEffect_ApplyGameplayEffect : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	TSubclassOf<UGameplayEffect> Effect;

	// SetByCaller 태그 -> 값. 예: Data.ReputationChange -> 10.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(Categories="Data"))
	TMap<FGameplayTag, float> SetByCallerMagnitudes;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/**
 * 부른 플레이어의 지금 상태를 저장하되, 불러올 때 돌아올 자리는 RestoreAt 이다.
 * RestoreAt 이 비었거나 못 찾으면 지금 자리로 저장하고 경고한다. 저장 실패는 HUD 가 알린다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="체크포인트 저장"))
struct VB_API FVBEffect_SaveCheckpoint : public FVBEffect
{
	GENERATED_BODY()

	// 돌아올 위치를 표시하는 액터(같은 맵에 놓인 것, 보통 TargetPoint). 위치와 야우만 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	TSoftObjectPtr<AActor> RestoreAt;

	virtual void Apply(const FVBRuleContext& Context) const override;
};

/**
 * 부른 플레이어를 표시 액터 위치로 페이드하며 옮긴다(실행은 UVBPlayerRelocationComponent). 지금 옮길 수 없으면 아무것도 안 한다.
 * 도착 효과는 실제로 도착했을 때만 한 번 적용한다.
 */
USTRUCT(BlueprintType, meta=(DisplayName="자리로 옮기기"))
struct VB_API FVBEffect_MoveToPoint : public FVBEffect
{
	GENERATED_BODY()

	// 도착 위치를 표시하는 액터(같은 맵의 액터). 위치와 야우만 쓴다. 월드 파티션 맵이면 늘 로드되는 액터여야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	TSoftObjectPtr<AActor> Destination;

	// 어두워지는 시간(초). 이 동안 폰을 그 자리에 붙든다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(ClampMin="0"))
	float FadeOutSeconds = 0.2f;

	// 도착 뒤 밝아지는 시간(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(ClampMin="0"))
	float FadeInSeconds = 0.4f;

	// 실제로 도착했을 때만 한 번 적용하는 효과.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnArrivedEffects;

	virtual void Apply(const FVBRuleContext& Context) const override;
};
