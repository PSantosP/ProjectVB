// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h"

/**
 * 플레이어가 걷는 이유들(토글, 아날로그, 스마트 오브젝트 덮어쓰기)과 그것으로 정해지는 걸음. 덮어쓰기가 이기고, 없으면 하나라도 걷기면 걷는다.
 * 서버가 이유가 바뀔 때마다 Resolve() 를 복제 칸 Gait 에 쓴다. 결과와 이유를 따로 들어야 덮어쓰기가 끝날 때 다른 이유가 지워지지 않는다.
 */
struct FVBWalkReasons
{
	// 스틱 크기가 아날로그 걷기인가(0 = 손 뗌, 문턱 이상 = 달리기). 키보드는 1 이라 걷지 않는다.
	static bool IsAnalogWalk(float StickMagnitude, float WalkRunThreshold)
	{
		return StickMagnitude > UE_KINDA_SMALL_NUMBER && StickMagnitude < WalkRunThreshold;
	}

	void ToggleWalk() { bWalkToggled = !bWalkToggled; }
	// 질주를 누르면 걷기 토글이 풀린다 - 질주가 끝나도 걷기로 돌아가지 않는다.
	//   아날로그 걷기는 손이 정하므로 그대로 둔다.
	void ClearToggleForSprint() { bWalkToggled = false; }
	void SetAnalogWalk(bool bWalk) { bAnalogWalk = bWalk; }
	void SetSmartObjectOverride(EVBGait OverrideGait) { SmartObjectOverride = OverrideGait; }
	void ClearSmartObjectOverride() { SmartObjectOverride.Reset(); }

	EVBGait Resolve() const
	{
		if (SmartObjectOverride.IsSet())
		{
			return SmartObjectOverride.GetValue();
		}

		return (bWalkToggled || bAnalogWalk) ? EVBGait::Walk : EVBGait::Run;
	}

private:
	bool bWalkToggled = false;
	bool bAnalogWalk = false;
	TOptional<EVBGait> SmartObjectOverride;
};
