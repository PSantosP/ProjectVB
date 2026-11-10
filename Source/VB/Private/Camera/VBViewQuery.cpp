// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Camera/VBViewQuery.h"

#include "GameFramework/PlayerController.h"

namespace VBViewQuery
{
	bool IsInsidePixels(const FVector2D& Pixels, const FIntPoint& ViewportPixels)
	{
		return Pixels.X >= 0.0f && Pixels.Y >= 0.0f
			&& Pixels.X <= static_cast<float>(ViewportPixels.X) && Pixels.Y <= static_cast<float>(ViewportPixels.Y);
	}

	bool IsInsideViewport(const APlayerController* PC, const FVector& WorldLocation, FVector2D* OutPixels)
	{
		if (!PC)
		{
			return false;
		}
		// 엔진 투영은 카메라 뒤에서만 false 를 돌려준다. 앞이지만 화면 사각형 밖인 점은 true 로 오므로 사각형 검사를 더한다.
		FVector2D Pixels = FVector2D::ZeroVector;
		if (!PC->ProjectWorldLocationToScreen(WorldLocation, Pixels, /*bPlayerViewportRelative=*/false))
		{
			return false;
		}

		if (OutPixels)
		{
			*OutPixels = Pixels;
		}

		FIntPoint Size = FIntPoint::ZeroValue;
		PC->GetViewportSize(Size.X, Size.Y);
		return IsInsidePixels(Pixels, Size);
	}
}
