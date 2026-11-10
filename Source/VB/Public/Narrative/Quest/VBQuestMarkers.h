// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UVBQuestDefinition;

/** 퀘스트가 현재 가리키는 마커 고르기. 순수 판정이라 시험한다. */
namespace VBQuestMarkers
{
	// 단계 StageIndex 에서 아직 안 찬 목표(선택 목표 포함)의 MarkerTag 를 Out 에 더한다. 진행 배열이 짧으면 빠진 칸은 0 으로 본다.
	//   단계가 범위 밖(완료 뒤 등)이거나 정의가 없으면 아무것도 안 더한다.
	VB_API void CollectActiveMarkerTags(const UVBQuestDefinition* Quest, int32 StageIndex, TConstArrayView<int32> Progress,
		FGameplayTagContainer& Out);
}
