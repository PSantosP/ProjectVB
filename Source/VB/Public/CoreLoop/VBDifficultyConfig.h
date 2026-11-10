// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBDifficultyConfig.generated.h"

// 난이도 한 단계. 숫자는 받는 피해, 주는 피해 배수 둘뿐이다.
USTRUCT(BlueprintType)
struct FVBDifficultyLevel
{
	GENERATED_BODY()

	// 단계 이름표(Difficulty.*). 순서 번호가 아니라서 단계를 끼워 넣어도 옛 저장이 안 밀린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Difficulty", meta=(Categories="Difficulty"))
	FGameplayTag Tag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Difficulty")
	FText DisplayName;

	// 플레이어가 받는 피해에 곱한다. 1 = 지금 게임 그대로. 0 보다 큰 유한한 수(IsDataValid).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Difficulty", meta=(ClampMin="0.01"))
	float PlayerDamageTakenScale = 1.0f;

	// 적이 받는 피해(= 플레이어가 주는 피해)에 곱한다. 체력 대신 이쪽이라 도중에 바꿔도 체력 막대가 튀지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Difficulty", meta=(ClampMin="0.01"))
	float EnemyDamageTakenScale = 1.0f;
};

/**
 * 난이도 단계 목록. 프로젝트에 하나(UVBDifficultySettings 가 가리킨다).
 * 처형, 정화 피해에는 어느 단계도 걸리지 않는다(VBDifficultyRules).
 */
UCLASS(BlueprintType)
class VB_API UVBDifficultyConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 화면에 보이는 순서 그대로.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Difficulty", meta=(TitleProperty="Tag"))
	TArray<FVBDifficultyLevel> Levels;

	// 새 게임의 첫 선택이자, 저장에 단계가 없거나 목록에 없을 때 쓰는 단계. 옛 저장이 보통이 되려면 Difficulty.Normal 이어야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Difficulty", meta=(Categories="Difficulty"))
	FGameplayTag DefaultLevel;

	// Tag 의 단계. 없으면 nullptr.
	const FVBDifficultyLevel* FindLevel(FGameplayTag Tag) const;
	// Tag 가 없거나 목록에 없으면 기본 단계. 기본 단계도 없으면 nullptr.
	const FVBDifficultyLevel* FindLevelOrDefault(FGameplayTag Tag) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
