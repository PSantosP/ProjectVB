// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"
#include "Data/VBAttackConfig.h"

namespace VBEnemyVariantRulesTest
{
	FVBEnemyVariantFit MakeFit(const TArray<float>& Lunges, const TArray<float>& Needed, float Ratio)
	{
		FVBEnemyVariantFit Fit;
		Fit.Allowed.Init(true, Lunges.Num());
		Fit.Lunges = Lunges;
		Fit.Needed = Needed;
		Fit.Ratio = Ratio;
		return Fit;
	}
}

// 적 변형의 좌우 필터. 대조: 전부 허용 복구를 빼면 no variant on that side 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyVariantSideTest, "VB.Unit.Melee.EnemyVariant.Side",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyVariantSideTest::RunTest(const FString& Parameters)
{
	using namespace VBMeleeAttackRules;
	TestTrue(TEXT("+Y is right"), ResolveTargetSide(1.0f) == EVBAttackSide::Right);
	TestTrue(TEXT("dead ahead counts as right"), ResolveTargetSide(0.0f) == EVBAttackSide::Right);
	TestTrue(TEXT("-Y is left"), ResolveTargetSide(-1.0f) == EVBAttackSide::Left);

	bool bNoneOnSide = true;
	const TArray<EVBAttackSide> Unsided = {EVBAttackSide::Any, EVBAttackSide::Any};
	const TArray<bool> AllAllowed = FilterVariantsBySide(Unsided, EVBAttackSide::Left, bNoneOnSide);
	TestTrue(TEXT("no sided variant: every variant stays"), AllAllowed[0] && AllAllowed[1]);
	TestFalse(TEXT("no sided variant: nothing to warn about"), bNoneOnSide);

	const TArray<EVBAttackSide> Mixed = {EVBAttackSide::Left, EVBAttackSide::Right, EVBAttackSide::Any};
	const TArray<bool> OnRight = FilterVariantsBySide(Mixed, EVBAttackSide::Right, bNoneOnSide);
	TestTrue(TEXT("other side is dropped, same side and Any stay"), !OnRight[0] && OnRight[1] && OnRight[2]);
	TestFalse(TEXT("a variant remains"), bNoneOnSide);

	const TArray<EVBAttackSide> OnlyLeft = {EVBAttackSide::Left, EVBAttackSide::Left};
	const TArray<bool> Fallback = FilterVariantsBySide(OnlyLeft, EVBAttackSide::Right, bNoneOnSide);
	TestTrue(TEXT("no variant on that side: everything is allowed"), Fallback[0] && Fallback[1]);
	TestTrue(TEXT("no variant on that side: reported to the caller"), bNoneOnSide);
	return true;
}

// 계획된 변형의 판정. 대조: 대역 검사를 빼면 stale plan 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyVariantPlanTest, "VB.Unit.Melee.EnemyVariant.Plan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyVariantPlanTest::RunTest(const FString& Parameters)
{
	using namespace VBMeleeAttackRules;
	FVBEnemyVariantFit Fit = VBEnemyVariantRulesTest::MakeFit({200.0f, 100.0f, 200.0f}, {200.0f, 400.0f, 200.0f}, 1.5f);
	Fit.Allowed[2] = false;
	TestTrue(TEXT("no plan"), JudgePlannedVariant(Fit, INDEX_NONE) == EVBPlannedVariant::Unusable);
	TestTrue(TEXT("plan past the list"), JudgePlannedVariant(Fit, 3) == EVBPlannedVariant::Unusable);
	TestTrue(TEXT("plan on the wrong side"), JudgePlannedVariant(Fit, 2) == EVBPlannedVariant::Unusable);
	TestTrue(TEXT("plan in band"), JudgePlannedVariant(Fit, 0) == EVBPlannedVariant::InBand);
	TestTrue(TEXT("stale plan"), JudgePlannedVariant(Fit, 1) == EVBPlannedVariant::OutOfBand);
	return true;
}

// 고르기: 대역 안에서 전진이 가장 긴 것, 없으면 덜 틀린 것. 대조: 위쪽 경계를 빼면 standing clip 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyVariantPickTest, "VB.Unit.Melee.EnemyVariant.Pick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyVariantPickTest::RunTest(const FString& Parameters)
{
	using namespace VBMeleeAttackRules;
	bool bInBand = false;
	FVBEnemyVariantFit Fit = VBEnemyVariantRulesTest::MakeFit({100.0f, 200.0f, 300.0f}, {150.0f, 150.0f, 150.0f}, 2.0f);
	TestEqual(TEXT("longest lunge in band"), PickBestEnemyVariant(Fit, bInBand), 2);
	TestTrue(TEXT("in band"), bInBand);
	Fit.Allowed[2] = false;
	TestEqual(TEXT("the side filter removes a candidate"), PickBestEnemyVariant(Fit, bInBand), 1);

	const FVBEnemyVariantFit Far = VBEnemyVariantRulesTest::MakeFit({10.0f, 100.0f}, {500.0f, 500.0f}, 1.2f);
	TestEqual(TEXT("least distorted when nothing fits"), PickBestEnemyVariant(Far, bInBand), 1);
	TestFalse(TEXT("out of band"), bInBand);

	const FVBEnemyVariantFit Standing = VBEnemyVariantRulesTest::MakeFit({0.0f}, {300.0f}, 1.2f);
	TestFalse(TEXT("a standing clip is not in band far away"), Standing.InBand(0));

	FVBEnemyVariantFit Empty = VBEnemyVariantRulesTest::MakeFit({100.0f}, {100.0f}, 1.0f);
	Empty.Allowed[0] = false;
	TestEqual(TEXT("no candidate at all"), PickBestEnemyVariant(Empty, bInBand), static_cast<int32>(INDEX_NONE));
	return true;
}
#endif
