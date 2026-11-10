// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBGrowthConfig.generated.h"

class UGameplayEffect;
class UVBSkillTreeDefinition;

/** 성장 설정(전역 DataAsset). 레벨은 누적 경험치에서 이 표로 센다 - 레벨, 포인트는 저장하지 않는다. */
UCLASS(BlueprintType, Const)
class VB_API UVBGrowthConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 칸 i = 레벨 i+1 -> i+2 필요 경험치. 최대 레벨 = 칸 수 + 1.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Growth", meta=(ClampMin="1"))
	TArray<int32> XPToNextLevel;

	// 레벨이 하나 오를 때마다 받는 스킬 포인트.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Growth", meta=(ClampMin="1"))
	int32 PointsPerLevel = 1;

	// 주인공의 스킬 트리.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Growth")
	TObjectPtr<const UVBSkillTreeDefinition> SkillTree;

	// 연 칸의 능력치 합을 싣는 무한 효과(능력치마다 SetByCaller 덧셈). 장비와 같은 GE.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Growth")
	TSubclassOf<UGameplayEffect> StatBonusEffect;

	int32 GetMaxLevel() const { return XPToNextLevel.Num() + 1; }

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
