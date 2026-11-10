// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBWorldIndicatorProjection.h"

#include "Blueprint/WidgetLayoutLibrary.h"
#include "Camera/VBViewQuery.h"
#include "GameFramework/PlayerController.h"
#include "UI/VBScreenEdge.h"

namespace VBWorldIndicatorProjection
{
	FVBIndicatorView MakeView(const APlayerController& PC, const FVector2D& LocalSize)
	{
		FVBIndicatorView View;
		View.PC = &PC;
		PC.GetPlayerViewPoint(View.CameraLocation, View.CameraRotation);
		View.ViewportScale = UWidgetLayoutLibrary::GetViewportScale(&PC);
		PC.GetViewportSize(View.ViewportPixels.X, View.ViewportPixels.Y);
		View.LocalSize = LocalSize;
		return View;
	}

	FVBIndicatorPlacement Classify(const FVBIndicatorView& View, bool bProjected, const FVector2D& Pixels,
		const FVector2D& CompassDir, EVBIndicatorScreenPolicy Policy, float EdgeMargin)
	{
		auto OnEdge = [&View, &CompassDir, EdgeMargin]()
		{
			FVBIndicatorPlacement Edge;
			Edge.bVisible = true;
			Edge.bEdge = true;
			Edge.Position = VBScreenEdge::PointOnInsetRect(View.LocalSize, EdgeMargin, CompassDir);
			Edge.ArrowAngle = VBScreenEdge::ArrowAngleDegrees(CompassDir);
			return Edge;
		};
		const bool bInside = bProjected && VBViewQuery::IsInsidePixels(Pixels, View.ViewportPixels);

		FVBIndicatorPlacement Result;
		switch (Policy)
		{
		case EVBIndicatorScreenPolicy::FollowWhenProjected:
			// 배율 가드를 두지 않는다 - 지금 프롬프트 동작 그대로다(가드를 넣으면 동작 결정이다, 설계 11.2).
			Result.bVisible = bProjected;
			Result.Position = bProjected ? Pixels / View.ViewportScale : FVector2D::ZeroVector;
			return Result;
		case EVBIndicatorScreenPolicy::ClampToEdge:
			if (bInside && View.ViewportScale > KINDA_SMALL_NUMBER)
			{
				Result.bVisible = true;
				Result.Position = Pixels / View.ViewportScale;
				return Result;
			}

			return OnEdge();
		case EVBIndicatorScreenPolicy::EdgeOnly:
			return bInside ? Result : OnEdge();
		case EVBIndicatorScreenPolicy::InsideOnly:
			if (bInside && View.ViewportScale > KINDA_SMALL_NUMBER)
			{
				Result.bVisible = true;
				Result.Position = Pixels / View.ViewportScale;
			}

			return Result;
		}

		return Result;
	}

	FVBIndicatorPlacement Place(const FVBIndicatorView& View, const FVector& WorldLocation,
		EVBIndicatorScreenPolicy Policy, float EdgeMargin)
	{
		if (!View.PC)
		{
			return FVBIndicatorPlacement();
		}

		FVector2D Pixels = FVector2D::ZeroVector;
		const bool bProjected = View.PC->ProjectWorldLocationToScreen(WorldLocation, Pixels, /*bPlayerViewportRelative=*/false);
		// 가장자리를 안 쓰는 정책(프롬프트, 화면 안만)은 나침반을 셈하지 않는다.
		const bool bUsesEdge = Policy != EVBIndicatorScreenPolicy::FollowWhenProjected && Policy != EVBIndicatorScreenPolicy::InsideOnly;
		const FVector2D CompassDir = !bUsesEdge
			? FVector2D::ZeroVector
			: VBScreenEdge::CompassDirection(View.CameraLocation, View.CameraRotation, WorldLocation);
		return Classify(View, bProjected, Pixels, CompassDir, Policy, EdgeMargin);
	}
}
