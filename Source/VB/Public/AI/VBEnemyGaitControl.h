// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UVBEnemyCombatComponent;
enum class EVBGait : uint8;

// 전투 중 적의 걷기와 뛰기를 정한다. 목표까지 남은 거리는 전투 컴포넌트가 답하는 역할, 슬롯, 시작 거리로 잰다.
namespace VBEnemyGaitControl
{
	// 남은 거리로 걸음을 고른다. 걷는 중이면 진입 거리, 뛰는 중이면 더 짧은 이탈 거리를 봐 경계에서 걸음이 떨지 않는다.
	VB_API EVBGait ChooseGait(double RemainingDistance, bool bRunningNow, float RunEnterDistance, float RunExitDistance);

	// 소유 적의 걸음을 갱신한다. 대상이 없으면 걷는다. 적이 아닌 소유자에는 아무것도 안 한다.
	VB_API void Update(const UVBEnemyCombatComponent& Combat);
}
