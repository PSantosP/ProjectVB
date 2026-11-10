#pragma once
#include "CoreMinimal.h"

// 입력/선택의 순수 정책. 네트워크나 viewport 없이 경계값을 검증한다.
namespace VBTargetLockPolicy
{
// 마우스 시점 입력으로는 락온을 풀지 않는다. 풀리는 길 = 락 키, 뒤로 질주, 대상 사망, 거리, 가림.
// 땅 평면에서 From 이 To 를 보는 yaw(도). 두 점이 겹치거나 NaN 이면 CurrentYaw 를 유지한다.
inline float FacingYaw(const FVector& From, const FVector& To, float CurrentYaw)
{
	const FVector Flat(To.X - From.X, To.Y - From.Y, 0.0);
	return (Flat.ContainsNaN() || Flat.IsNearlyZero()) ? CurrentYaw : static_cast<float>(Flat.Rotation().Yaw);
}
inline float AcquisitionRange(float MaxDistance, float SearchRadius)
{
	return FMath::IsFinite(MaxDistance) && FMath::IsFinite(SearchRadius) ? FMath::Max(0.0f, FMath::Min(MaxDistance, SearchRadius)) : 0.0f;
}
inline bool IsInAcquisitionCone(const FVector& ViewForward, const FVector& ToTarget, float HalfAngle)
{
	return !ViewForward.ContainsNaN() && !ToTarget.ContainsNaN() && FMath::IsFinite(HalfAngle) &&
		FVector::DotProduct(ViewForward.GetSafeNormal(),
			ToTarget.GetSafeNormal()) >= FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(HalfAngle, 0.0f, 89.0f)));
}
inline bool IsRetreatSprint(bool bSprinting, bool bGrounded, const FVector& Acceleration, const FVector& ToTarget, float DotThreshold)
{
	return bSprinting && bGrounded && !Acceleration.ContainsNaN() && !ToTarget.ContainsNaN() &&
		!Acceleration.IsNearlyZero() && !ToTarget.IsNearlyZero() &&
		FVector::DotProduct(Acceleration.GetSafeNormal2D(), ToTarget.GetSafeNormal2D()) <= -FMath::Clamp(DotThreshold, 0.01f, 1.0f);
}
// 명중 연타와 직접 시점 입력은 보정 시간을 연장하지 않는다.
struct FHitRecenterWindow
{
	double LastHitTime = -DBL_MAX;
	double LastInputTime = -DBL_MAX;
	double StartTime = 0.0;
	double EndTime = 0.0;
	bool bActive = false;
	void Cancel() { bActive = false; }
	void RecordInput(double Now) { LastInputTime = Now; Cancel(); }
	bool RegisterHit(double Now, float QuietSeconds, float InputGraceSeconds)
	{
		const bool bQuiet = Now - LastHitTime >= FMath::Max(0.0f, QuietSeconds);
		LastHitTime = Now;
		return !bActive && bQuiet && Now - LastInputTime >= FMath::Max(0.0f, InputGraceSeconds);
	}

	void Start(double Now, float Duration)
	{
		StartTime = Now;
		EndTime = Now + FMath::Max(0.0f, Duration);
		bActive = EndTime > StartTime;
	}

	double GetAlpha(double Now) const
	{
		if (EndTime <= StartTime)
		{
			return 1.0;
		}

		const double T = FMath::Clamp((Now - StartTime) / (EndTime - StartTime), 0.0, 1.0);
		return T * T * (3.0 - 2.0 * T);
	}
};
inline bool IsHitNotificationFresh(double Now, double HitTime, float MaxAge)
{
	// 작은 시각 동기화 오차는 허용하되 크게 어긋난 미래/과거 알림은 버린다.
	return FMath::IsFinite(Now) && FMath::IsFinite(HitTime) && FMath::Abs(Now - HitTime) <= FMath::Max(0.0f, MaxAge);
}
inline bool IsInRecenterSafeFrame(const FVector2D& Screen, const FVector2D& HalfExtent)
{
	return !Screen.ContainsNaN() && FMath::Abs(Screen.X - 0.5) <= FMath::Clamp(HalfExtent.X, 0.0, 0.5) &&
		FMath::Abs(Screen.Y - 0.5) <= FMath::Clamp(HalfExtent.Y, 0.0, 0.5);
}
struct FStickLatch
{
	int32 HeldDirection = 0;
	void Reset() { HeldDirection = 0; }
	int32 Update(float Axis, float PressThreshold, float ReleaseThreshold)
	{
		if (!FMath::IsFinite(Axis))
		{
			return 0;
		}

		const float Release = FMath::Clamp(ReleaseThreshold, 0.0f, 0.99f);
		const float Press = FMath::Clamp(PressThreshold, Release + 0.01f, 1.0f);
		if (FMath::Abs(Axis) <= Release)
		{
			Reset();
			return 0;
		}

		const int32 Direction = Axis > 0.0f ? 1 : -1;
		if (FMath::Abs(Axis) >= Press && HeldDirection != Direction)
		{
			HeldDirection = Direction;
			return Direction;
		}

		return 0;
	}
};
inline bool IsWithinDistance(double DistanceSquared, float Limit)
{
	return FMath::IsFinite(DistanceSquared) && Limit >= 0.0f && DistanceSquared <= FMath::Square(double(Limit));
}
inline double ScreenScore(const FVector2D& Origin, const FVector2D& Candidate, float VerticalWeight)
{
	const FVector2D Delta = Candidate - Origin;
	return FMath::Square(Delta.X) + FMath::Square(Delta.Y * FMath::Max(0.0f, VerticalWeight));
}
inline bool IsOnSide(const FVector2D& Origin, const FVector2D& Candidate, float Direction, float MinHorizontalDelta)
{
	return (Candidate.X - Origin.X) * FMath::Sign(Direction) > FMath::Max(0.0f, MinHorizontalDelta);
}

// 화면 대상 고르기의 조건. Direction 이 0 이면 첫 락온, 아니면 그 쪽으로 옆 전환이다. bRetarget 은 처치 뒤 다음 대상을 고를 때다.
struct FScreenTargetRules
{
	FVector2D Origin = FVector2D(0.5, 0.5);
	float Direction = 0.0f;
	bool bRetarget = false;
	float ConeHalfAngle = 0.0f;
	float MinSideDelta = 0.0f;
	float VerticalWeight = 0.0f;
	float DistanceWeight = 0.0f;
	float Range = 0.0f;
};

// 옆 전환이면 true. 0 근처의 축 값은 첫 락온으로 친다.
inline bool IsSideSwitch(float Direction)
{
	return !FMath::IsNearlyZero(Direction);
}

// 시야 트레이스 전에 후보를 거른다. 첫 락온은 가운데 원뿔, 옆 전환은 그 방향, 처치 뒤 다음 대상은 화면 안이면 된다.
inline bool PassesScreenFilter(const FScreenTargetRules& Rules, const FVector& ViewForward, const FVector& ToAim, const FVector2D& Screen)
{
	if (!Rules.bRetarget && !IsSideSwitch(Rules.Direction) && !IsInAcquisitionCone(ViewForward, ToAim, Rules.ConeHalfAngle))
	{
		return false;
	}

	return !IsSideSwitch(Rules.Direction) || IsOnSide(Rules.Origin, Screen, Rules.Direction, Rules.MinSideDelta);
}

// 화면 대상의 점수. 낮을수록 좋다. 첫 락온에만 거리 점수를 더한다.
inline double ScreenTargetScore(const FScreenTargetRules& Rules, const FVector2D& Screen, double DistanceSquared)
{
	const double ScreenPart = ScreenScore(Rules.Origin, Screen, Rules.VerticalWeight);
	if (IsSideSwitch(Rules.Direction))
	{
		return ScreenPart;
	}

	return ScreenPart + FMath::Max(0.0f, Rules.DistanceWeight) * DistanceSquared / FMath::Square(double(Rules.Range));
}

enum class EScreenTargetOrder : uint8
{
	Worse,
	Better,
	// 점수와 거리가 모두 같다. 부르는 쪽이 이름으로 정한다.
	SameDistance,
};

// 후보를 지금까지의 최선과 비교한다. 점수가 같으면 가까운 쪽이 앞선다.
inline EScreenTargetOrder CompareScreenTarget(double Score, double Distance, double BestScore, double BestDistance)
{
	const bool bTie = FMath::IsNearlyEqual(Score, BestScore, 1.e-6);
	if (Score < BestScore - 1.e-6 || (bTie && Distance < BestDistance))
	{
		return EScreenTargetOrder::Better;
	}

	return bTie && Distance == BestDistance ? EScreenTargetOrder::SameDistance : EScreenTargetOrder::Worse;
}

// 소프트 런지 후보의 점수. 높을수록 좋다. Dot 은 정면과 후보 방향의 내적이다.
inline float SoftLungeScore(float Dot, float Distance, float Radius, float DistanceWeight)
{
	return Dot + (1.0f - Distance / Radius) * DistanceWeight;
}

// 락온 중 방향 공격 전환. 아래 기본값은 테스트용이고 실제 값은 UVBTargetLockConfig 가 채운다.
// 후보 하나의 사실(땅 평면 방향, 수평 거리, 높이 차, 자격). 모으는 쪽이 채운다.
struct FDirectionalCandidate
{
	FVector2D GroundDirection = FVector2D::ZeroVector;
	float Distance2D = 0.0f;
	float VerticalDelta = 0.0f;
	bool bEligible = false;
};
struct FDirectionalParams
{
	float MinInputMagnitude = 0.35f;
	float CandidateHalfAngle = 45.0f;
	float CurrentKeepHalfAngle = 20.0f;
	float AngleAdvantage = 10.0f;
	float AngleTieBand = 3.0f;
	float MaxDistance = 600.0f;
	float MaxVerticalDelta = 150.0f;
};
// 이동 입력(X = 오른쪽, Y = 앞)을 카메라 yaw 로 돌린 월드 XY. 길이는 1 로 자른다.
inline FVector2D WorldMoveIntent(const FVector2D& RawMove, float ControlYawDegrees)
{
	if (RawMove.ContainsNaN() || !FMath::IsFinite(ControlYawDegrees))
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D Clamped = RawMove.GetClampedToMaxSize(1.0f);
	const float Yaw = FMath::DegreesToRadians(ControlYawDegrees);
	const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D Right(-FMath::Sin(Yaw), FMath::Cos(Yaw));
	return Forward * Clamped.Y + Right * Clamped.X;
}
// 두 단위 벡터 사이 각(도). 0 벡터면 180(어느 쪽도 아님).
inline float AngleBetweenDegrees(const FVector2D& A, const FVector2D& B)
{
	if (A.IsNearlyZero() || B.IsNearlyZero())
	{
		return 180.0f;
	}

	return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0f, 1.0f)));
}
// 입력 방향(월드 XY)으로 다음 대상을 고른다. INDEX_NONE = 지금 대상 유지.
//   가장 작은 각에서 AngleTieBand 안의 후보 중 가장 가까운 것 - 방향이 거리보다 앞선다.
inline int32 PickDirectionalTarget(const FVector2D& Input, const FVector2D& CurrentDirection, bool bHasCurrent,
	TArrayView<const FDirectionalCandidate> Candidates, const FDirectionalParams& Params)
{
	if (Input.ContainsNaN() || Input.Size() < FMath::Clamp(Params.MinInputMagnitude, 0.0f, 1.0f) || Input.IsNearlyZero())
	{
		return INDEX_NONE;
	}

	const FVector2D Intent = Input.GetSafeNormal();
	const float CurrentAngle = bHasCurrent ? AngleBetweenDegrees(Intent, CurrentDirection) : 180.0f;
	if (bHasCurrent && CurrentAngle <= FMath::Max(0.0f, Params.CurrentKeepHalfAngle))
	{
		return INDEX_NONE;
	}

	TArray<float, TInlineAllocator<16>> Angles;
	Angles.Init(-1.0f, Candidates.Num());
	float MinAngle = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		const FDirectionalCandidate& C = Candidates[Index];
		if (!C.bEligible || !FMath::IsFinite(C.Distance2D) || C.Distance2D > Params.MaxDistance
			|| FMath::Abs(C.VerticalDelta) > Params.MaxVerticalDelta)
		{
			continue;
		}

		const float Angle = AngleBetweenDegrees(Intent, C.GroundDirection);
		if (Angle > Params.CandidateHalfAngle || (bHasCurrent && Angle + FMath::Max(0.0f, Params.AngleAdvantage) > CurrentAngle))
		{
			continue;
		}

		Angles[Index] = Angle;
		MinAngle = FMath::Min(MinAngle, Angle);
	}

	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		if (Angles[Index] < 0.0f || Angles[Index] > MinAngle + FMath::Max(0.0f, Params.AngleTieBand))
		{
			continue;
		}

		if (Best == INDEX_NONE || Candidates[Index].Distance2D < Candidates[Best].Distance2D)
		{
			Best = Index;
		}
	}

	return Best;
}
}
