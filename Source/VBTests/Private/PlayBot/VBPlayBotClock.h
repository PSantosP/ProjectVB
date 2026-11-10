// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"

// 실행 시계를 고정 프레임과 실제 경과 시간으로 구분한다.
enum class EVBPlayBotClockMode : uint8 { FixedFrames, NativeSeconds };

// 엔진 시간을 바꾸지 않고 단조 경과 시간과 입력 tick 간격을 기록한다.
class FVBPlayBotClock
{
public:
	bool Advance(double AbsoluteSeconds)
	{
		if (!FMath::IsFinite(AbsoluteSeconds) || AbsoluteSeconds < 0.0 || (bStarted && AbsoluteSeconds < Last)) { return false; }
		if (!bStarted) { Origin = AbsoluteSeconds; bStarted = true; Last = AbsoluteSeconds; }
		DeltaMs = (AbsoluteSeconds - Last) * 1000.0;
		Last = AbsoluteSeconds;
		return true;
	}
	double Seconds() const { return Last - Origin; }
	double DeltaMilliseconds() const { return DeltaMs; }
	double StartSeconds() const { return Origin; }
private:
	double Origin = 0.0;
	double Last = 0.0;
	double DeltaMs = 0.0;
	bool bStarted = false;
};
