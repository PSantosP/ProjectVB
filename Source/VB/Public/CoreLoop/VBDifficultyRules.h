// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UVBDifficultyConfig;
struct FVBDifficultyLevel;

// 난이도가 피해 하나에 곱할 값. 순수 함수라 월드 없이 시험한다. 쓰는 곳은 UVBDifficultyStateComponent::GetDamageScale 하나(피해 중심이 부름).
namespace VBDifficultyRules
{
	// 처형, 정화 피해(IsFinisherDamage)는 늘 1. 단계를 못 찾으면 기본 단계, 그것도 없으면 1.
	// 배수가 유한하지 않거나 0 이하이면 1(검증 없이 들어온 자산 대비).
	VB_API float ResolveDamageScale(const UVBDifficultyConfig* Config, FGameplayTag Level, bool bVictimIsPlayer,
		bool bFinisherDamage);

	// 0 보다 큰 유한한 수. 자산 검증과 판정이 같은 기준을 쓴다.
	VB_API bool IsValidScale(float Scale);

	// 들고 다닐 단계 태그로 정규화한다. 목록에 있으면 그대로, 아니면 기본 단계, 그것도 없으면 빈 태그(모르는 태그가 저장되지 않게).
	VB_API FGameplayTag NormalizeLevel(const UVBDifficultyConfig* Config, FGameplayTag Level);

	// 고르기 화면의 한 줄 설명을 두 배수에서 만든다. 예: "받는 피해 1.5배, 주는 피해 0.8배".
	VB_API FText DescribeLevel(const FVBDifficultyLevel& Level);
}
