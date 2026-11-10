#pragma once

#include "CoreMinimal.h"

namespace VBEnvironmentCameraPolicy
{
	struct FCandidate
	{
		FVector Position = FVector::ZeroVector;
		double Score = 0;
		bool bSafe = false;
	};
	/** 무효 후보는 점수와 상관없이 못 이긴다. 유효한 후보는 분명히 더 나은 후보가 올 때까지 유지한다 */
	VB_API int32 SelectCandidate(TConstArrayView<FCandidate> Candidates, int32 Previous, double SwitchMargin);
	VB_API double RecoveryAlpha(double DeltaTime, double HalfLife);
	/** Lyra PreventCameraPenetration 의 안전 위치. 시선에 가장 가까운 capsule 점에서 PushIn 만큼 안쪽 */
	VB_API FVector CapsuleSafeLocation(const FVector& Center, const FVector& Up, double HalfHeight, double CapsuleRadius,
		double PushIn, const FVector& ViewLocation, const FVector& ViewDirection);
	/** 원하는 anchor-카메라 거리에 대한 비율. 안쪽으로 줄어드는 변화는 바로 적용한다 */
	struct VB_API FRecovery
	{
		double Fraction = 1;
		double ClearTime = 0;
		void Reset() { Fraction = 1; ClearTime = 0; }
		double Update(double SafeFraction, double DeltaTime, double HalfLife, double HoldSeconds, bool bHasClearance);
	};
}
