// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

class UVBChapterDefinition;
class UVBRegionDefinition;
struct FVBStoryState;

/** 챕터, 지역의 순수 판정. 월드, 화면을 모른다 - 슬롯 메타(세이브 객체)와 HUD(복제된 사실)가 같이 쓴다. */
namespace VBChapterProgress
{
	// ReachedFact 가 켜진 챕터 중 SortOrder 가 가장 큰 것. 같으면 ChapterId 이름이 큰 쪽(결정적). 없으면 nullptr.
	VB_API const UVBChapterDefinition* ResolveCurrent(TConstArrayView<TObjectPtr<UVBChapterDefinition>> Chapters,
		const FVBStoryState& Story);

	// 열림 사실이 비었거나 켜졌으면 열린 지역이다.
	VB_API bool IsRegionUnlocked(const UVBRegionDefinition& Region, const FVBStoryState& Story);

	struct FBaseline
	{
		FGameplayTag LastChapter;
		int32 SaveReadSerial = 0;
		bool bSeen = false;
	};
	// 제목 카드를 띄울 챕터. 처음 본 갱신과 세이브를 읽어 바뀐 갱신(SaveReadSerial 이 바뀜)은 기준만 잡고 nullptr -
	//   불러온 챕터를 '새로 들어옴' 으로 읽지 않게(성장 알림 VBGrowth::StepNoticeBaseline 과 같은 모양). 그 밖에는 챕터가 바뀌었을 때만.
	VB_API const UVBChapterDefinition* StepBaseline(FBaseline& Baseline, const UVBChapterDefinition* Current, int32 SaveReadSerial);
}
