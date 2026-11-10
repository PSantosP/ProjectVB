// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 지도 좌표의 규칙. 월드, 뷰포트 없이 시험한다.
 * 지도 그림 방향: 위 = 월드 +X, 오른쪽 = 월드 +Y. UV 는 U 오른쪽, V 아래로 커진다(0..1).
 */
namespace VBMap
{
	// 경계(월드 XY)가 쓸 수 있나: 유한하고 두 축 모두 넓이가 있다.
	VB_API bool IsValidBounds(const FVector2D& WorldMin, const FVector2D& WorldMax);

	// 월드 XY -> UV. 경계가 틀리면 (-1, -1) - 지도 밖으로 읽혀 그려지지 않는다.
	VB_API FVector2D WorldToUV(const FVector2D& WorldXY, const FVector2D& WorldMin, const FVector2D& WorldMax);

	// UV -> 월드 XY. WorldToUV 의 반대. 경계가 틀리면 (0, 0).
	VB_API FVector2D UVToWorld(const FVector2D& UV, const FVector2D& WorldMin, const FVector2D& WorldMax);

	VB_API bool IsInsideUV(const FVector2D& UV);

	// 미니맵 위 자리. 가운데 = Center(플레이어), 위 = 카메라 앞(플레이어 기준 회전).
	//   돌려주는 값은 반지름 1 기준(-1..1, x 오른쪽, y 아래). WorldRadius = 미니맵 가장자리까지의 월드 거리(cm). 1 을 넘으면 테두리 밖이다.
	VB_API FVector2D MinimapOffset(const FVector2D& Center, const FVector2D& Point, float CameraYawDegrees, float WorldRadius);

	// 테두리 밖의 자리를 테두리 위로 당긴다(목표가 멀어도 방향은 보이게). 길이 1 이하는 그대로.
	VB_API FVector2D ClampToCircle(const FVector2D& Offset);

	struct FVBMinimapMaterialParams
	{
		bool bValid = false;
		FVector2D CenterUV = FVector2D::ZeroVector;
		FVector2D RadiusOverExtents = FVector2D::ZeroVector;
		FVector2D YawCosSin = FVector2D::ZeroVector;
	};

	// RadiusOverExtents = (Radius / 월드 Y extent, Radius / 월드 X extent).
	// CenterUV 는 clamp 하지 않는다 - 맵 경계 밖에서도 Pawn 이 가운데에 있게.
	VB_API FVBMinimapMaterialParams BuildMinimapMaterialParams(const FVector2D& PawnXY,
		const FVector2D& WorldMin, const FVector2D& WorldMax, float MapYawDegrees, float RadiusCm);

	// material 역변환의 CPU 기준 구현 - NormalizedOffset 은 반경 단위의 오른쪽, 아래 방향.
	// OutUV 는 true 일 때만 의미가 있고, [0, 1] 밖 UV 는 지형이 없는 곳의 정상 샘플이다.
	VB_API bool TrySampleMinimapUV(const FVBMinimapMaterialParams& Params,
		const FVector2D& NormalizedOffset, FVector2D& OutUV);

	enum class EVBMinimapIconPolicy : uint8
	{
		ClampToRim,
		HideOutside
	};

	struct FVBMinimapIconPlacement
	{
		bool bValid = false;
		bool bVisible = false;
		bool bOnRim = false;
		FVector2D LocalOffset = FVector2D::ZeroVector;
		FVector2D Direction = FVector2D::ZeroVector;
	};

	// LocalOffset 은 맵 중심 기준 UI 단위, Direction 은 오른쪽, 아래 단위 벡터(중심에서는 0).
	// 안전 반경이 0 이하면 유효한 아이콘도 숨기고, 잘못된 입력은 bValid=false 가 된다.
	VB_API FVBMinimapIconPlacement PlaceMinimapIcon(const FVector2D& RawOffset,
		double LocalRadius, double BorderInset, double GlyphHalfDiagonal, EVBMinimapIconPolicy Policy);
}
