// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Texture2D.h"
#include "Map/VBMapDefinition.h"
#include "Map/VBMapRules.h"
#include "Map/VBMapSettings.h"
#include <limits>

// 지도 좌표 - 위 = 월드 +X, 오른쪽 = 월드 +Y, UV 는 아래로 커진다. 미니맵은 카메라 앞이 위.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMapUVTest, "VB.Unit.Map.WorldToUV",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMapUVTest::RunTest(const FString& Parameters)
{
	const FVector2D Min(-1000.0, -2000.0);
	const FVector2D Max(1000.0, 2000.0);
	TestTrue(TEXT("centre is (0.5, 0.5)"), VBMap::WorldToUV(FVector2D(0.0, 0.0), Min, Max).Equals(FVector2D(0.5, 0.5), 1e-6));
	TestTrue(TEXT("north edge (max X) is the top (V = 0)"),
		FMath::IsNearlyEqual(VBMap::WorldToUV(FVector2D(1000.0, 0.0), Min, Max).Y, 0.0, 1e-6));
	TestTrue(TEXT("east edge (max Y) is the right (U = 1)"),
		FMath::IsNearlyEqual(VBMap::WorldToUV(FVector2D(0.0, 2000.0), Min, Max).X, 1.0, 1e-6));
	TestTrue(TEXT("south-west corner is bottom-left (0, 1)"), VBMap::WorldToUV(Min, Min, Max).Equals(FVector2D(0.0, 1.0), 1e-6));
	const FVector2D Point(321.0, -1234.0);
	TestTrue(TEXT("round trip"), VBMap::UVToWorld(VBMap::WorldToUV(Point, Min, Max), Min, Max).Equals(Point, 1e-6));
	TestFalse(TEXT("outside the map is outside the UV square"), VBMap::IsInsideUV(VBMap::WorldToUV(FVector2D(5000.0, 0.0), Min, Max)));
	TestTrue(TEXT("inside"), VBMap::IsInsideUV(VBMap::WorldToUV(Point, Min, Max)));
	TestFalse(TEXT("flat bounds are invalid"), VBMap::IsValidBounds(FVector2D(0.0, 0.0), FVector2D(10.0, 0.0)));
	TestFalse(TEXT("invalid bounds read as outside"), VBMap::IsInsideUV(VBMap::WorldToUV(Point, Max, Min)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMapMinimapTest, "VB.Unit.Map.MinimapOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMapMinimapTest::RunTest(const FString& Parameters)
{
	const FVector2D Center(100.0, 100.0);
	// 카메라가 +X 를 보면(yaw 0): 앞 = 위, +Y = 오른쪽.
	TestTrue(TEXT("yaw 0: ahead is up"),
		VBMap::MinimapOffset(Center, Center + FVector2D(500.0, 0.0), 0.0f, 1000.0f).Equals(FVector2D(0.0, -0.5), 1e-6));
	TestTrue(TEXT("yaw 0: +Y is right"),
		VBMap::MinimapOffset(Center, Center + FVector2D(0.0, 1000.0), 0.0f, 1000.0f).Equals(FVector2D(1.0, 0.0), 1e-6));
	// 카메라가 +Y 를 보면(yaw 90): +Y 가 위, +X 는 왼쪽 - 지도가 플레이어 기준으로 돈다.
	TestTrue(TEXT("yaw 90: +Y is up"),
		VBMap::MinimapOffset(Center, Center + FVector2D(0.0, 1000.0), 90.0f, 1000.0f).Equals(FVector2D(0.0, -1.0), 1e-6));
	TestTrue(TEXT("yaw 90: +X is left"),
		VBMap::MinimapOffset(Center, Center + FVector2D(1000.0, 0.0), 90.0f, 1000.0f).Equals(FVector2D(-1.0, 0.0), 1e-6));
	TestTrue(TEXT("zero radius gives the centre"), VBMap::MinimapOffset(Center, Center + FVector2D(10.0, 0.0), 0.0f, 0.0f).IsZero());
	// 테두리 밖은 테두리 위로(방향 유지), 안쪽은 그대로.
	TestTrue(TEXT("far point clamps to the rim"), VBMap::ClampToCircle(FVector2D(3.0, 4.0)).Equals(FVector2D(0.6, 0.8), 1e-6));
	TestTrue(TEXT("inside stays"), VBMap::ClampToCircle(FVector2D(0.3, -0.2)).Equals(FVector2D(0.3, -0.2), 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMapMaterialProjectionTest, "VB.Unit.Map.MinimapMaterialProjection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMapMaterialProjectionTest::RunTest(const FString& Parameters)
{
	const FVector2D Min(-1700.0, -4300.0);
	const FVector2D Bounds[] = { FVector2D(2300.0, -300.0), FVector2D(2300.0, 7700.0) };
	const FVector2D Centers[] = { FVector2D(213.0, -987.0), FVector2D(4000.0, -5000.0) };
	const FVector2D Deltas[] = { FVector2D::ZeroVector, FVector2D(311.0, -729.0),
		FVector2D(-914.0, 123.0), FVector2D(0.0, 450.0), FVector2D(3100.0, 1700.0) };
	const float Yaws[] = { 0.0f, 90.0f, 180.0f, 270.0f, -45.0f, 37.0f };
	const float Radii[] = { 625.0f, 1733.0f };
	for (const FVector2D& Max : Bounds)
	{
		for (const FVector2D& Center : Centers)
		{
			for (const float Yaw : Yaws)
			{
				for (const float Radius : Radii)
				{
					const auto Params = VBMap::BuildMinimapMaterialParams(Center, Min, Max, Yaw, Radius);
					TestTrue(TEXT("finite view parameters are valid, including outside-map centers"), Params.bValid);
					TestTrue(TEXT("center UV is not clamped"), Params.CenterUV.Equals(VBMap::WorldToUV(Center, Min, Max), 1e-9));
					TestTrue(TEXT("radius scales follow texture axes"), Params.RadiusOverExtents.Equals(
						FVector2D(Radius / (Max.Y - Min.Y), Radius / (Max.X - Min.X)), 1e-9));
					for (const FVector2D& Delta : Deltas)
					{
						const FVector2D Point = Center + Delta;
						FVector2D Sample;
						TestTrue(TEXT("inverse accepts the projected offset"), VBMap::TrySampleMinimapUV(Params,
							VBMap::MinimapOffset(Center, Point, Yaw, Radius), Sample));
						TestTrue(TEXT("material sample agrees with world projection"),
							Sample.Equals(VBMap::WorldToUV(Point, Min, Max), 1e-9));
					}
				}
			}
		}
	}

	const auto Params = VBMap::BuildMinimapMaterialParams(FVector2D(300.0, -100.0),
		FVector2D(-1000.0, -2000.0), FVector2D(1000.0, 2000.0), 0.0f, 500.0f);
	TestTrue(TEXT("yaw zero is cosine one, sine zero"), Params.YawCosSin.Equals(FVector2D(1.0, 0.0), 1e-9));
	FVector2D Sample;
	TestTrue(TEXT("right/down sample is usable"), VBMap::TrySampleMinimapUV(Params, FVector2D(1.0, 1.0), Sample));
	TestTrue(TEXT("right/down increases U and V with independent extent scales"), Sample.Equals(FVector2D(0.6, 0.6), 1e-9));
	FVector2D InPlace(1.0, 1.0);
	TestTrue(TEXT("sampling can reuse its input vector"), VBMap::TrySampleMinimapUV(Params, InPlace, InPlace)
		&& InPlace.Equals(Sample, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMapGlyphPlacementTest, "VB.Unit.Map.MinimapGlyphPlacement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMapGlyphPlacementTest::RunTest(const FString& Parameters)
{
	using VBMap::EVBMinimapIconPolicy;
	const auto Objective = VBMap::PlaceMinimapIcon(FVector2D(3.0, 4.0), 100.0, 5.0, 10.0, EVBMinimapIconPolicy::ClampToRim);
	TestTrue(TEXT("distant objective is valid, visible and clamped"), Objective.bValid && Objective.bVisible && Objective.bOnRim);
	TestTrue(TEXT("whole glyph fits inside the inset rim"), Objective.LocalOffset.Equals(FVector2D(51.0, 68.0), 1e-9));
	TestTrue(TEXT("rim direction preserves the raw direction"), Objective.Direction.Equals(FVector2D(0.6, 0.8), 1e-9));
	const auto SavePoint = VBMap::PlaceMinimapIcon(FVector2D(0.9, 0.0), 100.0, 5.0, 10.0, EVBMinimapIconPolicy::HideOutside);
	TestTrue(TEXT("save point beyond glyph-safe radius is valid but hidden"), SavePoint.bValid && !SavePoint.bVisible && !SavePoint.bOnRim);
	for (const auto Policy : { EVBMinimapIconPolicy::ClampToRim, EVBMinimapIconPolicy::HideOutside })
	{
		const auto Inside = VBMap::PlaceMinimapIcon(FVector2D(0.3, -0.2), 100.0, 5.0, 10.0, Policy);
		TestTrue(TEXT("inside positions retain their scale"), Inside.bValid && Inside.bVisible && !Inside.bOnRim
			&& Inside.LocalOffset.Equals(FVector2D(30.0, -20.0), 1e-9));
		const auto Edge = VBMap::PlaceMinimapIcon(FVector2D(0.85, 0.0), 100.0, 5.0, 10.0, Policy);
		TestTrue(TEXT("exact safe boundary remains visible without a rim arrow"), Edge.bValid && Edge.bVisible && !Edge.bOnRim
			&& Edge.LocalOffset.Equals(FVector2D(85.0, 0.0), 1e-9));
		const auto Center = VBMap::PlaceMinimapIcon(FVector2D::ZeroVector, 100.0, 0.0, 0.0, Policy);
		TestTrue(TEXT("center has no direction"), Center.bValid && Center.bVisible && !Center.bOnRim
			&& Center.LocalOffset.IsZero() && Center.Direction.IsZero());
		TestFalse(TEXT("zero safe radius hides"), VBMap::PlaceMinimapIcon(FVector2D::ZeroVector, 10.0, 5.0, 5.0, Policy).bVisible);
		const auto NoRoom = VBMap::PlaceMinimapIcon(FVector2D(1.0, 0.0), 10.0, 8.0, 5.0, Policy);
		TestTrue(TEXT("negative safe radius is a valid hidden result"), NoRoom.bValid && !NoRoom.bVisible);
	}

	const double Huge = std::numeric_limits<double>::max();
	const auto Distant = VBMap::PlaceMinimapIcon(FVector2D(Huge, Huge), 100.0, 5.0, 10.0, EVBMinimapIconPolicy::ClampToRim);
	TestTrue(TEXT("large finite offsets preserve rim placement without overflow"), Distant.bValid && Distant.bVisible && Distant.bOnRim
		&& FMath::IsNearlyEqual(Distant.LocalOffset.Size(), 85.0, 1e-9)
		&& Distant.Direction.Equals(FVector2D(1.0, 1.0).GetSafeNormal(), 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMapMinimapInvalidTest, "VB.Unit.Map.MinimapInvalidInputs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMapMinimapInvalidTest::RunTest(const FString& Parameters)
{
	const FVector2D Min(-1000.0, -2000.0), Max(1000.0, 2000.0);
	const double NaN = std::numeric_limits<double>::quiet_NaN();
	const double Inf = std::numeric_limits<double>::infinity();
	const float FloatInf = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("zero world radius invalid"), VBMap::BuildMinimapMaterialParams(Min, Min, Max, 0.0f, 0.0f).bValid);
	TestFalse(TEXT("negative world radius invalid"), VBMap::BuildMinimapMaterialParams(Min, Min, Max, 0.0f, -1.0f).bValid);
	TestFalse(TEXT("infinite world radius invalid"), VBMap::BuildMinimapMaterialParams(Min, Min, Max, 0.0f, FloatInf).bValid);
	TestFalse(TEXT("infinite yaw invalid"), VBMap::BuildMinimapMaterialParams(Min, Min, Max, FloatInf, 100.0f).bValid);
	TestFalse(TEXT("nonfinite pawn invalid"), VBMap::BuildMinimapMaterialParams(FVector2D(NaN, 0.0), Min, Max, 0.0f, 100.0f).bValid);
	TestFalse(TEXT("flat bounds invalid"), VBMap::BuildMinimapMaterialParams(Min, Min, Min, 0.0f, 100.0f).bValid);
	TestFalse(TEXT("nonfinite bounds invalid"), VBMap::BuildMinimapMaterialParams(Min, Min, FVector2D(Inf, 0.0), 0.0f, 100.0f).bValid);
	FVector2D Sample(1.0, 1.0);
	TestFalse(TEXT("default params cannot produce a false center sample"), VBMap::TrySampleMinimapUV({}, FVector2D::ZeroVector, Sample));
	auto Params = VBMap::BuildMinimapMaterialParams(Min, Min, Max, 0.0f, 100.0f);
	TestFalse(TEXT("nonfinite sample invalid"), VBMap::TrySampleMinimapUV(Params, FVector2D(0.0, Inf), Sample));
	Params.YawCosSin.X = NaN;
	TestFalse(TEXT("corrupt material params invalid"), VBMap::TrySampleMinimapUV(Params, FVector2D::ZeroVector, Sample));
	using VBMap::EVBMinimapIconPolicy;
	for (const double Invalid : { NaN, Inf, -Inf })
	{
		for (const auto Policy : { EVBMinimapIconPolicy::ClampToRim, EVBMinimapIconPolicy::HideOutside })
		{
			const auto BadOffset = VBMap::PlaceMinimapIcon(FVector2D(Invalid, 0.0), 100.0, 5.0, 10.0, Policy);
			TestTrue(TEXT("nonfinite icon is explicitly invalid and hidden"), !BadOffset.bValid && !BadOffset.bVisible);
			TestFalse(TEXT("nonfinite local radius invalid"), VBMap::PlaceMinimapIcon(Min, Invalid, 5.0, 10.0, Policy).bValid);
			TestFalse(TEXT("nonfinite border invalid"), VBMap::PlaceMinimapIcon(Min, 100.0, Invalid, 10.0, Policy).bValid);
			TestFalse(TEXT("nonfinite glyph extent invalid"), VBMap::PlaceMinimapIcon(Min, 100.0, 5.0, Invalid, Policy).bValid);
		}
	}

	TestFalse(TEXT("zero local radius invalid"), VBMap::PlaceMinimapIcon(Min, 0.0, 0.0, 0.0, EVBMinimapIconPolicy::ClampToRim).bValid);
	TestFalse(TEXT("negative border invalid"), VBMap::PlaceMinimapIcon(Min, 100.0, -1.0, 0.0, EVBMinimapIconPolicy::ClampToRim).bValid);
	TestFalse(TEXT("negative glyph extent invalid"),
		VBMap::PlaceMinimapIcon(Min, 100.0, 0.0, -1.0, EVBMinimapIconPolicy::ClampToRim).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMapSettingsTest, "VB.Unit.Map.SettingsLookup",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMapSettingsTest::RunTest(const FString& Parameters)
{
	// 설정 객체를 새로 만들어 본다(프로젝트 설정을 건드리지 않는다).
	UVBMapSettings* Settings = NewObject<UVBMapSettings>();
	UVBMapDefinition* Usable = NewObject<UVBMapDefinition>();
	Usable->Texture = NewObject<UTexture2D>();
	Usable->WorldMin = FVector2D(-100.0, -100.0);
	Usable->WorldMax = FVector2D(100.0, 100.0);
	UVBMapDefinition* NoTexture = NewObject<UVBMapDefinition>();
	NoTexture->WorldMin = Usable->WorldMin;
	NoTexture->WorldMax = Usable->WorldMax;
	FVBMapEntry& A = Settings->Maps.AddDefaulted_GetRef();
	A.World = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Test/MapA.MapA")));
	A.Definition = Usable;
	FVBMapEntry& B = Settings->Maps.AddDefaulted_GetRef();
	B.World = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/Test/MapB.MapB")));
	B.Definition = NoTexture;
	TestTrue(TEXT("listed map with a usable definition"), Settings->FindForPackage(TEXT("/Game/Test/MapA")) == Usable);
	TestNull(TEXT("a definition without a texture is not used"), Settings->FindForPackage(TEXT("/Game/Test/MapB")));
	TestNull(TEXT("an unlisted map has no map"), Settings->FindForPackage(TEXT("/Game/Test/MapC")));
	return true;
}
#endif
