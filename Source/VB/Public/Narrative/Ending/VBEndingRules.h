// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UVBEndingCatalog;
struct FVBClearSummary;
struct FVBEndingDefinition;
struct FVBRuleContext;
struct FVBStoryState;

/** 결말 판정(상태 없음). 한 일이 고를 수 있는 결말 무리를 정하고 플레이어가 그 안에서 고른다. */
namespace VBEndingRules
{
	// 조건이 참인 결말을 목록 순서대로. 하나도 없으면 빈 배열이고 경고하므로 결말 장면에서 한 번만 부른다.
	VB_API TArray<const FVBEndingDefinition*> ResolveGroup(const UVBEndingCatalog* Catalog, const FVBRuleContext& Context);

	// 고른 결말이 그 무리 안에 있나. 클라가 보낸 선택은 서버가 이것으로 다시 본다.
	VB_API bool IsInGroup(const TArray<const FVBEndingDefinition*>& Group, FGameplayTag Ending);

	// 끝낸 결말 한 번의 요약. 다음 회차에는 RememberFacts 중 켜진 것만 가고, ClearId 는 매번 새로 만든다.
	// 이름표 없는 결말이면 빈 요약. 무리 확인(IsInGroup)은 부른 쪽 몫이다.
	VB_API FVBClearSummary MakeClearSummary(const FVBEndingDefinition& Ending, const FVBStoryState& Facts);
}
