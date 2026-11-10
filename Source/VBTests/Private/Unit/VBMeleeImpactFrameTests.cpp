#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/GameplayCues/VBGC_MeleeImpact.h"
#include "CharacterCommon/VBBodyContact.h"
#include "Engine/HitResult.h"

namespace
{
	FHitResult MakeSweepHit(const FVector& ImpactPoint, const FVector& ImpactNormal, const FVector& TraceStart, const FVector& TraceEnd)
	{
		FHitResult Hit;
		Hit.bBlockingHit = true;
		Hit.ImpactPoint = ImpactPoint;
		Hit.ImpactNormal = ImpactNormal;
		Hit.TraceStart = TraceStart;
		Hit.TraceEnd = TraceEnd;
		return Hit;
	}
}

// 타격 이펙트의 좌표틀 - 이펙트는 +X = 맞은 면, +Z = 날이 지나간 방향으로 읽어서 이게 틀리면 모든 타격 이펙트가 엉뚱한 쪽으로 뿜는다.
// 순수 계산이라 ApplicationContextMask - EditorContext 로 두면 -game 실행에서 조용히 빠진다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeImpactFrameSweepTest, "VB.Unit.Combat.MeleeImpactFrame.FromSweep",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeImpactFrameSweepTest::RunTest(const FString& Parameters)
{
	// 정면(-X 쪽 공격자)을 오른쪽으로 벤다. 면 법선 -X, 날 이동 +Y.
	const FHitResult Hit = MakeSweepHit(FVector(0, 0, 100), FVector(-1, 0, 0), FVector(-10, -20, 100), FVector(-10, 20, 100));
	const FVBMeleeImpactFrame Frame = UVBGC_MeleeImpact::ResolveImpactFrame(&Hit, FVector::ZeroVector, FVector(-200, 0, 0), 5.0f);
	TestTrue(TEXT("HitResult 를 썼다"), Frame.bFromHitResult);
	TestTrue(TEXT("법선은 면 법선"), Frame.Normal.Equals(FVector(-1, 0, 0), 1e-4f));
	TestTrue(TEXT("날 방향은 TraceEnd - TraceStart"), Frame.SwingDirection.Equals(FVector(0, 1, 0), 1e-4f));
	TestTrue(TEXT("위치는 접점에서 법선 쪽으로 5cm"), Frame.Location.Equals(FVector(-5, 0, 100), 1e-3f));

	const FQuat Q = Frame.ToQuat();
	TestTrue(TEXT("회전 +X = 법선"), Q.GetAxisX().Equals(FVector(-1, 0, 0), 1e-4f));
	TestTrue(TEXT("회전 +Z = 날 방향"), Q.GetAxisZ().Equals(FVector(0, 1, 0), 1e-4f));

	// 비스듬히 내려 벤 날: 법선 성분은 회전축에서 빠지고 원본 방향은 보존된다.
	const FHitResult Diagonal = MakeSweepHit(FVector::ZeroVector, FVector(-1, 0, 0), FVector(0, 0, 0), FVector(-30, 40, 0));
	const FVBMeleeImpactFrame D = UVBGC_MeleeImpact::ResolveImpactFrame(&Diagonal, FVector::ZeroVector, FVector(-200, 0, 0), 0.0f);
	TestTrue(TEXT("원본 날 방향 보존"), D.SwingDirection.Equals(FVector(-0.6f, 0.8f, 0), 1e-4f));
	TestTrue(TEXT("회전축은 법선에 직교"), FMath::IsNearlyZero(FVector::DotProduct(D.SwingAxis, D.Normal), 1e-4f));
	TestTrue(TEXT("직교화된 축은 +Y"), D.SwingAxis.Equals(FVector(0, 1, 0), 1e-4f));
	return true;
}

// 날이 몸의 어느 쪽에 닿았나 - 겹침 판정은 면 법선이 날 이동의 반대로 올 수 있어서 바깥은 몸 중심축 -> 날 위치로 정한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeImpactBladeSideTest, "VB.Unit.Combat.MeleeImpactFrame.BladeSide",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeImpactBladeSideTest::RunTest(const FString& Parameters)
{
	const FVector Base(0, 0, 90); // 캡슐 중심(발 0 ~ 머리 180)
	const FVector Fallback(-1, 0, 0);
	TestTrue(TEXT("옆구리의 날 -> 수평 옆"),
		VBBodyContact::ComputeBladeSide(FVector(0, 40, 120), Base, 90.0f, Fallback).Equals(FVector(0, 1, 0), 1e-4f));
	TestTrue(TEXT("정면 가슴의 날 -> 정면"),
		VBBodyContact::ComputeBladeSide(FVector(-35, 0, 140), Base, 90.0f, Fallback).Equals(FVector(-1, 0, 0), 1e-4f));
	// 머리 위에서 내려친 날: 축 점이 머리 끝(180)으로 잘려 위쪽 성분이 생긴다.
	const FVector Over = VBBodyContact::ComputeBladeSide(FVector(-10, 0, 230), Base, 90.0f, Fallback);
	TestTrue(TEXT("머리 위의 날 -> 주로 위"), Over.Z > 0.95f && Over.IsNormalized());
	TestTrue(TEXT("날이 축 위면 대체 방향"), VBBodyContact::ComputeBladeSide(FVector(0, 0, 100), Base, 90.0f, Fallback).Equals(Fallback, 1e-4f));
	TestTrue(TEXT("대체 방향마저 0 이면 단위 벡터"),
		VBBodyContact::ComputeBladeSide(FVector(0, 0, 100), Base, 90.0f, FVector::ZeroVector).IsNormalized());
	return true;
}

// 대체 경로. 처음부터 겹친 스윕(법선 0), 날이 안 움직인 스윕, 찌르기, 위를 향한 면, HitResult 없는 출혈 폭발.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeImpactFrameFallbackTest, "VB.Unit.Combat.MeleeImpactFrame.Fallbacks",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeImpactFrameFallbackTest::RunTest(const FString& Parameters)
{
	const FVector Target(0, 0, 0);
	const FVector Source(-300, 0, 0);

	const FHitResult ZeroNormal = MakeSweepHit(FVector(0, 0, 90), FVector::ZeroVector, FVector(0, -10, 90), FVector(0, 10, 90));
	const FVBMeleeImpactFrame A = UVBGC_MeleeImpact::ResolveImpactFrame(&ZeroNormal, Target, Source, 0.0f);
	TestTrue(TEXT("법선 0 이면 맞은 쪽 -> 때린 쪽"), A.Normal.Equals(FVector(-1, 0, 0), 1e-4f));

	const FHitResult Still = MakeSweepHit(FVector(0, 0, 90), FVector(-1, 0, 0), FVector(0, 0, 90), FVector(0, 0, 90));
	const FVBMeleeImpactFrame B = UVBGC_MeleeImpact::ResolveImpactFrame(&Still, Target, Source, 0.0f);
	TestTrue(TEXT("날이 안 움직였으면 수평 옆 방향"), FMath::IsNearlyZero(B.SwingDirection.Z, 1e-4f) && B.SwingDirection.IsNormalized());
	TestTrue(TEXT("그 방향도 법선에 직교"), FMath::IsNearlyZero(FVector::DotProduct(B.SwingDirection, B.Normal), 1e-4f));

	const FHitResult Thrust = MakeSweepHit(FVector::ZeroVector, FVector(-1, 0, 0), FVector(-40, 0, 0), FVector(0, 0, 0));
	const FVBMeleeImpactFrame C = UVBGC_MeleeImpact::ResolveImpactFrame(&Thrust, Target, Source, 0.0f);
	TestTrue(TEXT("찌르기는 원본 방향이 들어온 방향"), C.SwingDirection.Equals(FVector(1, 0, 0), 1e-4f));
	TestTrue(TEXT("찌르기의 회전축은 위쪽"), C.SwingAxis.Equals(FVector(0, 0, 1), 1e-4f));
	TestFalse(TEXT("회전에 NaN 없음"), C.ToQuat().ContainsNaN());

	// 법선이 위를 향하면(누운 적의 배를 내려침) 위쪽 대체축도 평행이다 - 옆축으로 떨어져야 한다.
	const FHitResult Down = MakeSweepHit(FVector::ZeroVector, FVector(0, 0, 1), FVector(0, 0, 40), FVector(0, 0, 0));
	const FVBMeleeImpactFrame E = UVBGC_MeleeImpact::ResolveImpactFrame(&Down, Target, Source, 0.0f);
	TestTrue(TEXT("위로 향한 법선의 내려찍기도 직교 축"), FMath::IsNearlyZero(FVector::DotProduct(E.SwingAxis, E.Normal), 1e-4f)
		&& E.SwingAxis.IsNormalized());
	TestFalse(TEXT("그 회전에도 NaN 없음"), E.ToQuat().ContainsNaN());

	const FVBMeleeImpactFrame F = UVBGC_MeleeImpact::ResolveImpactFrame(nullptr, FVector(0, 0, 100), FVector(-300, 0, 100), 5.0f);
	TestFalse(TEXT("HitResult 없음 표시"), F.bFromHitResult);
	TestTrue(TEXT("HitResult 없으면 대상 위치에서 법선 쪽으로"), F.Location.Equals(FVector(-5, 0, 100), 1e-3f));

	const FVBMeleeImpactFrame G = UVBGC_MeleeImpact::ResolveImpactFrame(nullptr, Target, Target, 0.0f);
	TestTrue(TEXT("모든 정보가 없어도 단위 법선"), G.Normal.IsNormalized() && G.SwingAxis.IsNormalized());
	return true;
}
#endif
