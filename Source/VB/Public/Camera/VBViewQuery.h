// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class APlayerController;

/** 컨트롤러 시야에 대한 질문. UI 표시기와 예고 큐가 같이 묻는다 - 큐가 UI 를 보지 않게 여기 둔다. */
namespace VBViewQuery
{
	// 경계 포함(0 <= X <= SizeX, 0 <= Y <= SizeY).
	VB_API bool IsInsidePixels(const FVector2D& Pixels, const FIntPoint& ViewportPixels);

	// 월드 위치가 이 컨트롤러의 화면 안에 있는가. 카메라 뒤(투영 실패)도 밖이다.
	//   OutPixels 는 투영이 됐을 때만 쓴다(픽셀 단위, DPI 배율 전).
	VB_API bool IsInsideViewport(const APlayerController* PC, const FVector& WorldLocation, FVector2D* OutPixels = nullptr);
}
