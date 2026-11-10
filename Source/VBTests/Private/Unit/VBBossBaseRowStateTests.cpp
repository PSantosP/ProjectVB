// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Data/VBBossConfig.h"
#include "Enemy/VBBossAttackRules.h"
#include "Enemy/VBBossBaseRowState.h"

// 고른 행의 수명: 페이즈마다 따로, Reset 은 행만 잊고 쿨다운과 피격 기록은 남긴다.
//   대조: Reset 이 쿨다운까지 비우면 cooldown survives 줄이, FindHeldRow 의 페이즈 비교를 빼면 other phase 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBossRowStateTest, "VB.Unit.Boss.AttackRows.RowState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBBossRowStateTest::RunTest(const FString& Parameters)
{
	FVBBossAttackRow Behind;
	Behind.Situation = EVBBossAttackSituation::TargetBehind;
	Behind.CooldownSeconds = 30.0f;
	const TArray<FVBBossAttackRow> Rows = {Behind};
	const auto Always = [](const FVBBossAttackRow&) { return true; };
	FVBBossSituationFacts Facts;
	FVBBossRowState State;

	TestEqual(TEXT("no situation: nothing chosen"), State.Choose(Rows, 0, Facts, 10.0, Always, 0.5f), static_cast<int32>(INDEX_NONE));
	TestNull(TEXT("nothing held"), State.FindHeldRow(Rows, 0));

	Facts.bTargetBehind = true;
	TestEqual(TEXT("the situation row is chosen"), State.Choose(Rows, 0, Facts, 10.0, Always, 0.5f), 0);
	TestNotNull(TEXT("the row is held for its phase"), State.FindHeldRow(Rows, 0));
	TestNull(TEXT("other phase does not see the row"), State.FindHeldRow(Rows, 1));
	State.StartHeldRowCooldown(Rows, 1, 10.0);
	State.Reset();
	TestNull(TEXT("reset forgets the row"), State.FindHeldRow(Rows, 0));
	TestEqual(TEXT("a cooldown started from the wrong phase never landed"), State.Choose(Rows, 0, Facts, 10.0, Always, 0.5f), 0);

	State.StartHeldRowCooldown(Rows, 0, 10.0);
	State.Reset();
	TestEqual(TEXT("cooldown survives the reset"), State.Choose(Rows, 0, Facts, 39.0, Always, 0.5f), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("cooldown is per phase"), State.Choose(Rows, 1, Facts, 39.0, Always, 0.5f), 0);
	TestEqual(TEXT("cooldown ends"), State.Choose(Rows, 0, Facts, 40.0, Always, 0.5f), 0);

	TestFalse(TEXT("no damage: no burst"), State.IsAfterHitBurst(5.0, 2.0, 2));
	State.RecordDamage(4.0, 2.0);
	TestFalse(TEXT("one hit is below the burst"), State.IsAfterHitBurst(4.5, 2.0, 2));
	State.RecordDamage(5.0, 2.0);
	TestTrue(TEXT("two hits in the window"), State.IsAfterHitBurst(5.0, 2.0, 2));
	TestFalse(TEXT("the first hit left the window"), State.IsAfterHitBurst(6.5, 2.0, 2));
	State.Reset();
	TestTrue(TEXT("reset keeps the damage record"), State.IsAfterHitBurst(5.0, 2.0, 2));
	return true;
}
#endif
