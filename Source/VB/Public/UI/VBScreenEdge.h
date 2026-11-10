// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** 화면 밖 대상을 가장자리에서 가리키는 순수 수학. 위협 화살표, 목표 마커가 같이 쓴다. */
namespace VBScreenEdge
{
	// 카메라 기준 나침반 방향: 앞 = 화면 위(-y), 오른쪽 = +x. 높이는 무시하고, 바로 위, 아래면 (0, 1).
	VB_API FVector2D CompassDirection(const FVector& CameraLocation, const FRotator& CameraRotation, const FVector& Target);

	// 화면 중심에서 Dir 방향으로 여백 사각형과 만나는 점(ViewSize 단위).
	VB_API FVector2D PointOnInsetRect(const FVector2D& ViewSize, float Margin, const FVector2D& Dir);

	// 위를 가리키는 그림을 Dir 로 돌리는 각(도).
	VB_API float ArrowAngleDegrees(const FVector2D& Dir);
}
