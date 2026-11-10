// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Map/VBMapRules.h"

bool VBMap::IsValidBounds(const FVector2D& WorldMin, const FVector2D& WorldMax)
{
	return !WorldMin.ContainsNaN() && !WorldMax.ContainsNaN()
		&& FMath::IsFinite(WorldMin.X) && FMath::IsFinite(WorldMin.Y) && FMath::IsFinite(WorldMax.X) && FMath::IsFinite(WorldMax.Y)
		&& WorldMax.X > WorldMin.X && WorldMax.Y > WorldMin.Y;
}

FVector2D VBMap::WorldToUV(const FVector2D& WorldXY, const FVector2D& WorldMin, const FVector2D& WorldMax)
{
	if (!IsValidBounds(WorldMin, WorldMax) || WorldXY.ContainsNaN())
	{
		return FVector2D(-1.0, -1.0);
	}
	// 오른쪽 = +Y, 위 = +X(V 는 아래로 커지므로 X 가 클수록 V 가 작다).
	const double U = (WorldXY.Y - WorldMin.Y) / (WorldMax.Y - WorldMin.Y);
	const double V = 1.0 - (WorldXY.X - WorldMin.X) / (WorldMax.X - WorldMin.X);
	return FVector2D(U, V);
}

FVector2D VBMap::UVToWorld(const FVector2D& UV, const FVector2D& WorldMin, const FVector2D& WorldMax)
{
	if (!IsValidBounds(WorldMin, WorldMax) || UV.ContainsNaN())
	{
		return FVector2D::ZeroVector;
	}

	const double X = WorldMin.X + (1.0 - UV.Y) * (WorldMax.X - WorldMin.X);
	const double Y = WorldMin.Y + UV.X * (WorldMax.Y - WorldMin.Y);
	return FVector2D(X, Y);
}

bool VBMap::IsInsideUV(const FVector2D& UV)
{
	return UV.X >= 0.0 && UV.X <= 1.0 && UV.Y >= 0.0 && UV.Y <= 1.0;
}

FVector2D VBMap::MinimapOffset(const FVector2D& Center, const FVector2D& Point, float CameraYawDegrees, float WorldRadius)
{
	if (!(WorldRadius > 0.0f) || !FMath::IsFinite(CameraYawDegrees) || Center.ContainsNaN() || Point.ContainsNaN())
	{
		return FVector2D::ZeroVector;
	}

	const FVector2D Delta = Point - Center;
	const double Yaw = FMath::DegreesToRadians(static_cast<double>(CameraYawDegrees));
	const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D Right(-FMath::Sin(Yaw), FMath::Cos(Yaw));
	// 화면: 오른쪽 = 카메라 오른쪽, 위(-y) = 카메라 앞.
	return FVector2D(FVector2D::DotProduct(Delta, Right), -FVector2D::DotProduct(Delta, Forward)) / WorldRadius;
}

FVector2D VBMap::ClampToCircle(const FVector2D& Offset)
{
	if (Offset.ContainsNaN())
	{
		return FVector2D::ZeroVector;
	}

	const double Length = Offset.Size();
	return Length > 1.0 ? Offset / Length : Offset;
}

VBMap::FVBMinimapMaterialParams VBMap::BuildMinimapMaterialParams(const FVector2D& PawnXY,
	const FVector2D& WorldMin, const FVector2D& WorldMax, float MapYawDegrees, float RadiusCm)
{
	if (!IsValidBounds(WorldMin, WorldMax) || PawnXY.ContainsNaN()
		|| !FMath::IsFinite(MapYawDegrees) || !FMath::IsFinite(RadiusCm) || RadiusCm <= 0.0f)
	{
		return {};
	}

	const FVector2D Extents = WorldMax - WorldMin;
	if (Extents.ContainsNaN())
	{
		return {};
	}

	FVBMinimapMaterialParams Result;
	Result.CenterUV = WorldToUV(PawnXY, WorldMin, WorldMax);
	Result.RadiusOverExtents = FVector2D(RadiusCm / Extents.Y, RadiusCm / Extents.X);
	const double Yaw = FMath::DegreesToRadians(static_cast<double>(MapYawDegrees));
	Result.YawCosSin = FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw));
	if (Result.CenterUV.ContainsNaN() || Result.RadiusOverExtents.ContainsNaN()
		|| Result.RadiusOverExtents.X <= 0.0 || Result.RadiusOverExtents.Y <= 0.0)
	{
		return {};
	}

	Result.bValid = true;
	return Result;
}

bool VBMap::TrySampleMinimapUV(const FVBMinimapMaterialParams& Params,
	const FVector2D& NormalizedOffset, FVector2D& OutUV)
{
	if (!Params.bValid || Params.CenterUV.ContainsNaN() || Params.RadiusOverExtents.ContainsNaN()
		|| Params.YawCosSin.ContainsNaN() || NormalizedOffset.ContainsNaN()
		|| Params.RadiusOverExtents.X <= 0.0 || Params.RadiusOverExtents.Y <= 0.0)
	{
		return false;
	}

	const double C = Params.YawCosSin.X;
	const double S = Params.YawCosSin.Y;
	const FVector2D SampleUV = Params.CenterUV + FVector2D(
		Params.RadiusOverExtents.X * (C * NormalizedOffset.X - S * NormalizedOffset.Y),
		Params.RadiusOverExtents.Y * (S * NormalizedOffset.X + C * NormalizedOffset.Y));
	if (SampleUV.ContainsNaN())
	{
		return false;
	}

	OutUV = SampleUV;
	return true;
}

VBMap::FVBMinimapIconPlacement VBMap::PlaceMinimapIcon(const FVector2D& RawOffset,
	double LocalRadius, double BorderInset, double GlyphHalfDiagonal, EVBMinimapIconPolicy Policy)
{
	if (RawOffset.ContainsNaN() || !FMath::IsFinite(LocalRadius) || LocalRadius <= 0.0
		|| !FMath::IsFinite(BorderInset) || BorderInset < 0.0
		|| !FMath::IsFinite(GlyphHalfDiagonal) || GlyphHalfDiagonal < 0.0
		|| (Policy != EVBMinimapIconPolicy::ClampToRim && Policy != EVBMinimapIconPolicy::HideOutside))
	{
		return {};
	}

	FVBMinimapIconPlacement Result;
	Result.bValid = true;
	const double SafeRadius = LocalRadius - BorderInset - GlyphHalfDiagonal;
	if (SafeRadius <= 0.0)
	{
		return Result;
	}

	const double SafeRatio = SafeRadius / LocalRadius;
	// 정규화 전에 줄여 먼 유한 offset 이 Size() 에서 넘치지 않게 한다.
	const double MaxComponent = FMath::Max(FMath::Abs(RawOffset.X), FMath::Abs(RawOffset.Y));
	double ScaledLength = 0.0;
	if (MaxComponent > 0.0)
	{
		const FVector2D Scaled = RawOffset / MaxComponent;
		ScaledLength = Scaled.Size();
		Result.Direction = Scaled / ScaledLength;
	}

	const bool bOutside = MaxComponent > SafeRatio || MaxComponent * ScaledLength > SafeRatio;
	if (bOutside && Policy == EVBMinimapIconPolicy::HideOutside)
	{
		return Result;
	}

	Result.LocalOffset = bOutside ? ClampToCircle(Result.Direction) * SafeRadius : RawOffset * LocalRadius;
	if (Result.LocalOffset.ContainsNaN())
	{
		return {};
	}

	Result.bVisible = true;
	Result.bOnRim = bOutside;
	return Result;
}
