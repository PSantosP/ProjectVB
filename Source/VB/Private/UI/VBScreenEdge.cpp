// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBScreenEdge.h"

namespace VBScreenEdge
{
	FVector2D CompassDirection(const FVector& CameraLocation, const FRotator& CameraRotation, const FVector& Target)
	{
		const FVector Local = CameraRotation.UnrotateVector(Target - CameraLocation);
		FVector2D Dir(Local.Y, -Local.X); // 화면 x = 오른쪽, 화면 y = 아래
		if (!Dir.Normalize())
		{
			Dir = FVector2D(0.0f, 1.0f);
		}

		return Dir;
	}

	FVector2D PointOnInsetRect(const FVector2D& ViewSize, float Margin, const FVector2D& Dir)
	{
		const FVector2D Center = ViewSize * 0.5f;
		const FVector2D Half = (Center - FVector2D(Margin, Margin)).ComponentMax(FVector2D::ZeroVector);
		const double Tx = FMath::Abs(Dir.X) > KINDA_SMALL_NUMBER ? Half.X / FMath::Abs(Dir.X) : TNumericLimits<double>::Max();
		const double Ty = FMath::Abs(Dir.Y) > KINDA_SMALL_NUMBER ? Half.Y / FMath::Abs(Dir.Y) : TNumericLimits<double>::Max();
		const double T = FMath::Min(Tx, Ty);
		// 두 축이 다 0 인 방향(정규화 실패한 입력)이면 중심에 둔다 - 끝없이 먼 점을 내보내지 않는다.
		return T < TNumericLimits<double>::Max() ? Center + Dir * T : Center;
	}

	float ArrowAngleDegrees(const FVector2D& Dir)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)) + 90.0f;
	}
}
