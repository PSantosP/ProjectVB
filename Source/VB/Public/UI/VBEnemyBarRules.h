// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;

// 근처 적 체력 바 후보 하나. 거리, 화면 안, 죽음은 위젯이 이번 프레임 시야로 채운다.
struct FVBNearbyBarCandidate
{
	AActor* Actor = nullptr;
	float DistanceSquared = 0.0f;
	bool bOnScreen = false;
	bool bDead = false;
};

namespace VBEnemyBars
{
	// 반경 안, 화면 안, 살아 있음, 제외 대상(락온 대상 = 제 바가 있다, 싸우는 보스 = 보스 바)이 아님 중 가까운 순 Max 개.
	VB_API TArray<AActor*> PickNearby(TArrayView<const FVBNearbyBarCandidate> Candidates, float Radius, int32 Max,
		const AActor* ExcludeA, const AActor* ExcludeB);
}
