// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FVBQuestLogEntry;

/** 퀘스트를 글로 푸는 곳 하나. HUD 목표 줄과 일지 화면이 같이 써서 모양이 갈라지지 않게 한다. */
namespace VBQuestText
{
	// 지금 단계의 목표들. 한 목표 한 줄, (n/m), (완료), (선택) 표시를 붙인다.
	VB_API FText DescribeObjectives(const FVBQuestLogEntry& Entry);

	// HUD 한 덩어리: 제목 / 단계 설명 / 목표들.
	VB_API FText DescribeTracker(const FVBQuestLogEntry& Entry);
}
