// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommonInputTypeEnum.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBTutorialDefinition.generated.h"

class UTexture2D;

/**
 * 튜토리얼의 한 단계. StepId 는 저장 열쇠라 순서를 바꿔도 옛 슬롯이 맞는 단계를 찾는다.
 * 장치별 글은 DeviceBodies(VBTutorial::BodyFor 가 고름), 완료 판정은 Completion 목표 하나가 한다.
 */
USTRUCT(BlueprintType)
struct VB_API FVBTutorialStep
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial", meta=(Categories="Tutorial"))
	FGameplayTag StepId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial")
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial", meta=(MultiLine=true))
	FText Body;

	// 지금 입력 장치가 이 표에 있으면 Body 대신 그 글을 보인다. 없는 장치는 Body. 빈 글은 자산 검증이 거절한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial", meta=(MultiLine=true))
	TMap<ECommonInputType, FText> DeviceBodies;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial")
	FName InputMappingName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial",
		meta=(BaseStruct="/Script/VB.VBQuestObjective", ExcludeBaseStruct))
	FInstancedStruct Completion;
};

/**
 * 튜토리얼 정의(자산 ID = TutorialId). 내용의 기준 데이터이고 화면은 읽기만 한다.
 * 시작 = ActivationEvent(+ActivationTargetQuery) + ActivationConditions 가 모두 참일 때. 둘 다 비면 자동 시작하지 않는다.
 * 여러 개가 진행 중이면 HintPriority -> SortOrder -> ID 순으로 힌트 하나를 고른다(VBTutorial::SelectHint).
 */
UCLASS(BlueprintType, Const)
class VB_API UVBTutorialDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetTutorialAssetType();
	static FPrimaryAssetId MakeTutorialAssetId(FGameplayTag InTutorialId);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial", meta=(Categories="Tutorial"))
	FGameplayTag TutorialId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial")
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial", meta=(MultiLine=true))
	FText Summary;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial")
	int32 SortOrder = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial")
	int32 HintPriority = 0;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial", meta=(Categories="Event"))
	FGameplayTag ActivationEvent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial")
	FGameplayTagQuery ActivationTargetQuery;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> ActivationConditions;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Tutorial")
	TArray<FVBTutorialStep> Steps;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	bool IsUsable() const;
	int32 FindStepIndex(FGameplayTag StepId) const;
	static bool HasRefreshSignal(const UScriptStruct* ConditionType);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;

private:
	// 튜토리얼 태그, 제목과 요약, 단계 유무를 본다.
	void ValidateIdentity(class FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 조건 목록의 구조체 형과 필수 칸, 갱신 신호를 본다.
	void ValidateConditions(const TArray<FInstancedStruct>& Conditions, const FText& Where,
		class FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 단계 하나의 태그, 글, 기기별 글, 완료 조건을 본다.
	void ValidateStep(const FVBTutorialStep& Step, int32 Index, TSet<FGameplayTag>& StepIds,
		class FDataValidationContext& Context, EDataValidationResult& Result) const;
	// 단계의 완료 목표를 본다.
	void ValidateStepCompletion(const FVBTutorialStep& Step, const FText& Where,
		class FDataValidationContext& Context, EDataValidationResult& Result) const;
#endif
};
