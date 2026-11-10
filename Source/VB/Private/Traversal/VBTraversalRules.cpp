// Copyright VB. All Rights Reserved.

#include "Traversal/VBTraversalRules.h"

namespace VBTraversalRules
{
	float PlayRate::ResolveTraversalPlayRate(bool bFast, float WantInput, float FastUpRate, float FastDownRate)
	{
		// 0.2 는 사다리, 등반, 옆걸음이 이동 몽타주를 고르는 문턱과 같다 - 대기 중에는 배속이 의미가 없다.
		if (!bFast || FMath::Abs(WantInput) <= 0.2f)
		{
			return 1.0f;
		}

		const float Rate = WantInput > 0.0f ? FastUpRate : FastDownRate;
		// 데이터가 깨졌으면(비유한, 1 미만) 끈다. 상한 3 은 데이터 칸의 ClampMax 와 같다.
		return FMath::IsFinite(Rate) ? FMath::Clamp(Rate, 1.0f, 3.0f) : 1.0f;
	}

	bool Auto::HasAutoTraversalIntent(bool bSprinting, float InputForwardDot, float Speed2D, float MinForwardDot, float MinSpeed)
	{
		// 앞 입력이 없으면 내적이 0 이라 Shift 만 누르고 서 있는 경우는 여기서 걸러진다(MinForwardDot > 0 일 때).
		return bSprinting && FMath::IsFinite(InputForwardDot) && InputForwardDot >= MinForwardDot
			&& FMath::IsFinite(Speed2D) && Speed2D >= MinSpeed;
	}

	bool Auto::HoldsAutoTraversalInput(bool bSprinting, const FVector2D& RawMoveInput)
	{
		// 0.2 는 이동 몽타주를 고르는 문턱과 같다(입력 크기의 제곱 0.04). 그 아래는 누르고 있지 않은 것으로 본다.
		return bSprinting && RawMoveInput.SizeSquared() > 0.04;
	}

	bool Ladder::IsLadderStartPlausible(const FVector& RequesterLocation, const FVector& AxisNear, const FVector& FacingNormal,
		float MaxDistance)
	{
		if (RequesterLocation.ContainsNaN() || AxisNear.ContainsNaN() || FacingNormal.ContainsNaN() || !FMath::IsFinite(MaxDistance))
		{
			return false;
		}
		// 타는 면 = 바깥 법선 쪽. 0 은 면 위(옆)라 거절한다 - 뒤편에서 붙으면 캡슐이 사다리를 뚫고 앞으로 옮겨진다.
		return FVector::DistSquared(RequesterLocation, AxisNear) <= FMath::Square(FMath::Max(MaxDistance, 0.0f))
			&& FVector::DotProduct(RequesterLocation - AxisNear, FacingNormal) > 0.0;
	}

	bool Ladder::ShouldTryLadderTopOut(const bool bAuthority, const bool bEntryAligned, const float WantInput,
		const float HeightOnAxis, const float TopHeight, const float MinUpInput)
	{
		return bAuthority && bEntryAligned && WantInput >= MinUpInput && HeightOnAxis >= TopHeight;
	}

	FFloatInterval Ladder::ComputeLadderTopOutBand(const float TopZ, const float Skin, const float Tolerance)
	{
		// 꼭대기와 같은 높이의 발판(P06)이 바닥 선의 맞은 점 오차로 떨어지지 않게 아래로는 스킨만큼만 둔다.
		return FFloatInterval(TopZ - FMath::Max(Skin, 0.0f), TopZ + FMath::Max(Tolerance, 0.0f));
	}

	bool Climb::IsClimbTopOutFloorUsable(float CenterFloorNormalZ, float WalkableFloorZ, float CenterFloorZ, float CapsuleContactZ,
		const FFloatInterval& Band, float StepTolerance)
	{
		if (!FMath::IsFinite(CenterFloorNormalZ) || !FMath::IsFinite(WalkableFloorZ) || !FMath::IsFinite(CenterFloorZ) ||
			!FMath::IsFinite(CapsuleContactZ) || !FMath::IsFinite(Band.Min) || !FMath::IsFinite(Band.Max)
			|| !FMath::IsFinite(StepTolerance))
		{
			return false;
		}

		return CenterFloorNormalZ >= WalkableFloorZ
			&& CenterFloorZ >= Band.Min && CenterFloorZ <= Band.Max
			&& FMath::Abs(CapsuleContactZ - CenterFloorZ) <= FMath::Max(StepTolerance, 0.0f);
	}
}
