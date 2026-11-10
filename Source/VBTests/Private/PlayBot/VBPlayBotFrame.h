// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// play bot 프레임 시계 - 게임은 고정 60 Hz 로 돈다(playbot.sh 의 -UseFixedTimeStep -FPS=60, 시작 전 UVBPlayBotSubsystem 이 확인)
// 상수 집은 여기 하나다 - 파일마다 사본을 두면 unity build 에서 이름이 겹친다
namespace VBPlayBot
{
	inline constexpr double FrameMs = 1000.0 / 60.0;

	// 미래 시각을 그 시각을 담을 수 있는 첫 프레임으로 올림한다
	inline int64 MsToFrames(double Ms)
	{
		return static_cast<int64>(FMath::CeilToDouble(Ms / FrameMs - UE_KINDA_SMALL_NUMBER));
	}
}
