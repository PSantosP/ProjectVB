// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonInputTypeEnum.h"
#include "GameplayTagContainer.h"
#include "VBTutorialTypes.generated.h"

class UVBTutorialDefinition;
struct FVBTutorialStep;
struct FVBGameEvent;
struct FVBRuleContext;

/**
 * 튜토리얼 하나의 런타임 진행(복제 값, UVBTutorialComponent 가 관리). 저장 형식은 FVBSavedTutorial 로 따로 둔다.
 * UnlockedSteps = 서버가 다시 보기를 허용한 단계(실제로 그려졌는지가 아니다).
 */
USTRUCT(BlueprintType)
struct VB_API FVBTutorialProgress
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="VB|Tutorial")
	FGameplayTag TutorialId;

	UPROPERTY(BlueprintReadOnly, Category="VB|Tutorial")
	FGameplayTag CurrentStepId;

	UPROPERTY(BlueprintReadOnly, Category="VB|Tutorial")
	int32 EventCount = 0;

	UPROPERTY(BlueprintReadOnly, Category="VB|Tutorial")
	bool bCompleted = false;

	UPROPERTY(BlueprintReadOnly, Category="VB|Tutorial")
	TArray<FGameplayTag> UnlockedSteps;
};

// 받은 값만 변환하는 함수들. 권한과 알림은 컴포넌트가 맡는다.
namespace VBTutorial
{
	VB_API FGameplayTag MovementEventTag();
	VB_API bool IsControlledMovement(const FVector& Displacement, const FVector& Acceleration, bool bInputBlocked);
	VB_API bool NeedsMovementObservation(const UVBTutorialDefinition& Definition, const FVBTutorialProgress& Progress);
	VB_API bool CanActivate(const UVBTutorialDefinition& Definition, const FVBGameEvent* Event,
		const FVBRuleContext& Context);
	VB_API bool Start(const UVBTutorialDefinition& Definition, FVBTutorialProgress& Progress);
	VB_API bool IsProgressCompatible(const UVBTutorialDefinition& Definition, const FVBTutorialProgress& Progress);
	VB_API bool ApplyEvent(const UVBTutorialDefinition& Definition, const FVBGameEvent& Event,
		const FVBRuleContext& Context, FVBTutorialProgress& Progress);
	VB_API bool RefreshConditions(const UVBTutorialDefinition& Definition, const FVBRuleContext& Context,
		FVBTutorialProgress& Progress);
	// 이 입력 장치에서 보일 글: DeviceBodies 에 있으면 그 글, 없거나 장치를 모르면 Body.
	VB_API const FText& BodyFor(const FVBTutorialStep& Step, TOptional<ECommonInputType> InputType);
	VB_API const FVBTutorialStep* GetHint(const UVBTutorialDefinition& Definition, const FVBTutorialProgress& Progress);
	VB_API const UVBTutorialDefinition* SelectHint(const TArray<const UVBTutorialDefinition*>& Definitions,
		const TArray<FVBTutorialProgress>& Progress);
	VB_API TArray<int32> BuildReviewStepIndices(const UVBTutorialDefinition& Definition,
		const FVBTutorialProgress& Progress);
}
