// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class APlayerController;

/**
 * 월드 표시기의 월드 -> 화면 자리 정책. 갈래마다 화면 판정이 달라 정책 이름으로 드러낸다.
 * 가장자리 수학은 VBScreenEdge, 화면 안 판정은 VBViewQuery 가 맡는다.
 */
enum class EVBIndicatorScreenPolicy : uint8
{
	// 투영이 되면 그 자리(화면 사각형 검사 없음), 카메라 뒤면 숨김 - 상호작용 프롬프트.
	FollowWhenProjected,
	// 화면 안이고 DPI 배율이 유효하면 그 자리, 나머지(뒤, 밖, 배율 무효)는 가장자리 + 화살표 - 목표 마커.
	ClampToEdge,
	// 화면 안이면 숨김, 밖이면 가장자리 - 화면 밖 위협 화살표. 배율은 판정에 안 들어간다.
	EdgeOnly,
	// 화면 안이고 배율이 유효할 때만 그 자리, 나머지는 숨김(가장자리 없음) - 락온 대상 체력 바.
	InsideOnly,
};

// 한 프레임의 카메라, 크기, 배율. 투영 자체는 호출마다 엔진을 탄다.
struct FVBIndicatorView
{
	const APlayerController* PC = nullptr;
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	// 픽셀 -> 위젯 단위(UWidgetLayoutLibrary::GetViewportScale).
	float ViewportScale = 0.0f;
	FIntPoint ViewportPixels = FIntPoint::ZeroValue;
	// 표시가 놓이는 전체 화면 캔버스의 로컬 크기.
	FVector2D LocalSize = FVector2D::ZeroVector;
};

struct FVBIndicatorPlacement
{
	bool bVisible = false;
	// 가장자리에 놓고 화살표를 켜는가. 실제 화면 밖 여부와 다를 수 있다(ClampToEdge 의 배율 무효).
	bool bEdge = false;
	FVector2D Position = FVector2D::ZeroVector;
	// bEdge 일 때만 뜻이 있다(VBScreenEdge::ArrowAngleDegrees).
	float ArrowAngle = 0.0f;
};

namespace VBWorldIndicatorProjection
{
	VB_API FVBIndicatorView MakeView(const APlayerController& PC, const FVector2D& LocalSize);

	// 투영 결과와 나침반 방향으로 정책대로 자리를 정하는 순수 판정(PC 없이 시험한다).
	VB_API FVBIndicatorPlacement Classify(const FVBIndicatorView& View, bool bProjected, const FVector2D& Pixels,
		const FVector2D& CompassDir, EVBIndicatorScreenPolicy Policy, float EdgeMargin);

	// View.PC 로 투영해 Classify 한다. PC 가 없으면 숨김.
	VB_API FVBIndicatorPlacement Place(const FVBIndicatorView& View, const FVector& WorldLocation,
		EVBIndicatorScreenPolicy Policy, float EdgeMargin);
}
