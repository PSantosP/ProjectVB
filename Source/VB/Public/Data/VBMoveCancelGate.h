// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 새로 움직였는지 판정. 이동 입력이 한 번 멎은 뒤 다시 들어올 때만 참이다(달려와 누른 입력으로 바로 취소되지 않게).
 * 상호작용 이동 취소와 공격 회복 끊기(UVBAttackConfig::bMoveCancelsRecoveryInWindow)가 같이 쓴다.
 */
struct FVBMoveCancelGate
{
	// 검사 주기(초). 이보다 짧게 톡 친 입력은 놓칠 수 있다 - 취소하려고 움직이는 입력은 이보다 길다.
	static constexpr float Interval = 0.05f;

	// 이번 검사 때 이동 입력이 있는가. 새로 움직였으면 참.
	bool Step(bool bHasMoveInput)
	{
		if (!bHasMoveInput)
		{
			bArmed = true;
			return false;
		}

		return bArmed;
	}

	void Reset() { bArmed = false; }

	// 판정용 이동 입력. 상호작용이 캡슐 방향을 제어하는 동안엔 가속이 0 이라 원시 스틱 입력도 본다.
	static bool HasMoveInput(const FVector& Acceleration, const FVector2D& RawStick)
	{
		return !Acceleration.IsNearlyZero(0.1f) || !RawStick.IsNearlyZero(0.1f);
	}

private:
	bool bArmed = false;
};
