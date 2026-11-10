// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 한 번의 연속 누름만 세며 해제와 입력 취소는 누적 시간을 버린다.
class FVBCinematicSkipHold
{
public:
	void Press(double Now)
	{
		if (!StartedAt.IsSet() && FMath::IsFinite(Now))
		{
			StartedAt = Now;
			bConsumed = false;
		}
	}

	void Release()
	{
		StartedAt.Reset();
		bConsumed = false;
	}

	bool IsPressed() const
	{
		return StartedAt.IsSet();
	}

	bool Consume(double Now, float HoldSeconds, bool bStillHeld)
	{
		if (!bStillHeld || !FMath::IsFinite(Now) || !FMath::IsFinite(HoldSeconds)
			|| HoldSeconds < 0.f || (StartedAt.IsSet() && Now < StartedAt.GetValue()))
		{
			Release();
			return false;
		}

		if (!StartedAt.IsSet() || bConsumed || Now - StartedAt.GetValue() < HoldSeconds)
		{
			return false;
		}

		bConsumed = true;
		return true;
	}

private:
	TOptional<double> StartedAt;
	bool bConsumed = false;
};
