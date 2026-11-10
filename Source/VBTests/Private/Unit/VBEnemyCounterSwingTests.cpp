#include "Misc/AutomationTest.h"
#include "AI/VBArmoredCounterTracker.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBEnemyConfig.h"

// 슈퍼아머 반격 근거를 판정이 아니라 타 단위로 센다 - 판정이 많은 연속 타격 한 타가 문턱을 혼자 채우지 않게.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyCounterSwingTest, "VB.Unit.EnemyCombat.CounterCountsSwings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBEnemyCounterSwingTest::RunTest(const FString& Parameters)
{
	// 능력 인스턴스 자리에 아무 UObject 나 둔다 - 세는 규칙은 같은 객체인지만 본다.
	const UObject* AbilityA = NewObject<UVBAttackConfig>();
	const UObject* AbilityB = NewObject<UVBAttackConfig>();
	auto Make = [](double Time, const UObject* Source, int32 Swing)
	{
		FVBCounterHit Hit;
		Hit.Time = Time;
		Hit.Source = Source;
		Hit.Swing = Swing;
		return Hit;
	};
	const double Window = 2.0;

	TArray<FVBCounterHit> SameSwing = {Make(1.0, AbilityA, 4), Make(1.05, AbilityA, 4), Make(1.1, AbilityA, 4)};
	TestEqual(TEXT("Three hits of one swing count once"), FVBArmoredCounterTracker::CountSwings(SameSwing, 1.2, Window), 1);

	TArray<FVBCounterHit> TwoSwings = {Make(1.0, AbilityA, 4), Make(1.5, AbilityA, 5)};
	TestEqual(TEXT("Different swings of one ability count separately"),
		FVBArmoredCounterTracker::CountSwings(TwoSwings, 1.6, Window), 2);

	TArray<FVBCounterHit> TwoAttackers = {Make(1.0, AbilityA, 4), Make(1.0, AbilityB, 4)};
	TestEqual(TEXT("Same swing number from another ability is another swing"),
		FVBArmoredCounterTracker::CountSwings(TwoAttackers, 1.1, Window), 2);

	TArray<FVBCounterHit> Unkeyed = {Make(1.0, nullptr, INDEX_NONE), Make(1.0, nullptr, INDEX_NONE)};
	TestEqual(TEXT("Hits without a swing count one each (old behaviour)"),
		FVBArmoredCounterTracker::CountSwings(Unkeyed, 1.1, Window), 2);

	TArray<FVBCounterHit> Expired = {Make(0.0, AbilityA, 1), Make(2.5, AbilityA, 2)};
	TestEqual(TEXT("Hits outside the window are not counted"), FVBArmoredCounterTracker::CountSwings(Expired, 3.0, Window), 1);

	// 창 밖으로 나간 첫 판정이 같은 타의 둘째 판정을 이미 본 것으로 지우면 안 된다.
	TArray<FVBCounterHit> SplitByWindow = {Make(0.0, AbilityA, 7), Make(2.1, AbilityA, 7)};
	TestEqual(TEXT("An expired hit does not hide a live hit of the same swing"),
		FVBArmoredCounterTracker::CountSwings(SplitByWindow, 2.2, Window), 1);
	return true;
}

// 문턱을 넘긴 반격을 다음 공격까지 잡아 둔다 - 안 잡으면 공격을 고를 때 첫 타가 창 밖으로 나가 보통 공격이 나간다.
// 순수 규칙이라 -game 헤드리스 시험에도 보이게 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyCounterLatchTest, "VB.Unit.EnemyCombat.CounterLatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBEnemyCounterLatchTest::RunTest(const FString& Parameters)
{
	const UObject* Blade = NewObject<UVBAttackConfig>();
	auto AtFrame = [](int32 Frame) { return Frame / 60.0; };
	auto Make = [Blade](double Time, int32 Swing)
	{
		FVBCounterHit Hit;
		Hit.Time = Time;
		Hit.Source = Blade;
		Hit.Swing = Swing;
		return Hit;
	};
	const double Window = 2.0;
	const int32 Threshold = 3;
	const double Grace = 3.0;
	const TArray<FVBCounterHit> Hits = {Make(AtFrame(888), 1), Make(AtFrame(922), 2), Make(AtFrame(968), 3)};
	const double PendingUntil = AtFrame(968) + Grace;
	const double AttackStart = AtFrame(1040);

	const int32 SwingsAtAttack = FVBArmoredCounterTracker::CountSwings(Hits, AttackStart, Window);
	TestEqual(TEXT("measured: only 2 swings are left in the window when the attack starts"), SwingsAtAttack, 2);
	TestFalse(TEXT("baseline: without the latch the counter is lost (the FIND-145 bug)"),
		FVBArmoredCounterTracker::IsWanted(SwingsAtAttack, Threshold, AttackStart, 0.0));
	TestTrue(TEXT("the latched counter survives until the attack starts"),
		FVBArmoredCounterTracker::IsWanted(SwingsAtAttack, Threshold, AttackStart, PendingUntil));
	TestFalse(TEXT("the latch expires after the grace time"),
		FVBArmoredCounterTracker::IsWanted(0, Threshold, PendingUntil + 0.01, PendingUntil));
	TestTrue(TEXT("a full window still wants the counter without a latch"),
		FVBArmoredCounterTracker::IsWanted(3, Threshold, AttackStart, 0.0));
	TestFalse(TEXT("no counter for an archetype without one, latch or not"),
		FVBArmoredCounterTracker::IsWanted(3, 0, AttackStart, PendingUntil));
	TestTrue(TEXT("default grace covers the measured hit-to-attack gap"),
		GetDefault<UVBEnemyConfig>()->ArmoredCounterPendingSeconds > AttackStart - AtFrame(968));
	return true;
}

// 근거 기록의 수명: 피격 줄 하나에 타 하나만 붙고, 늦은 이벤트는 옛 줄을 채우지 않으며, Reset 이 잡아 둔 반격까지 비운다.
//   대조: AttachSwing 의 시각 비교를 빼면 late event 줄이, Reset 에서 만료 시각을 안 비우면 reset 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBArmoredCounterTrackerTest, "VB.Unit.EnemyCombat.CounterTracker",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBArmoredCounterTrackerTest::RunTest(const FString& Parameters)
{
	const UObject* Blade = NewObject<UVBAttackConfig>();
	const double Window = 2.0;
	FVBArmoredCounterTracker Tracker;
	TestFalse(TEXT("a swing with no recorded hit is ignored"), Tracker.AttachSwing(1.0, Blade, 1));

	Tracker.RecordHit(1.0, Window);
	TestTrue(TEXT("the event of the same instant fills the row"), Tracker.AttachSwing(1.0, Blade, 1));
	TestFalse(TEXT("one row takes one swing"), Tracker.AttachSwing(1.0, Blade, 2));
	Tracker.RecordHit(1.1, Window);
	TestTrue(TEXT("the same swing again fills its row"), Tracker.AttachSwing(1.1, Blade, 1));
	TestEqual(TEXT("two hits of one swing count once"), Tracker.CountRecentSwings(1.2, Window), 1);

	Tracker.RecordHit(1.3, Window);
	TestFalse(TEXT("a late event leaves the row empty"), Tracker.AttachSwing(1.4, Blade, 2));
	TestEqual(TEXT("the unkeyed row is its own swing"), Tracker.CountRecentSwings(1.4, Window), 2);
	TestEqual(TEXT("rows stay until the window drops them"), Tracker.GetHitCount(), 3);
	Tracker.RecordHit(3.2, Window);
	TestEqual(TEXT("recording drops rows outside the window"), Tracker.GetHitCount(), 2);

	TestFalse(TEXT("below the threshold: no counter"), Tracker.WantsCounter(3, 3.2, Window));
	Tracker.HoldCounterUntil(5.0);
	TestTrue(TEXT("a held counter survives the empty window"), Tracker.WantsCounter(3, 4.9, Window));
	TestFalse(TEXT("a held counter expires"), Tracker.WantsCounter(3, 5.0, Window));
	Tracker.HoldCounterUntil(5.0);
	Tracker.Reset();
	TestFalse(TEXT("reset drops the held counter"), Tracker.WantsCounter(3, 4.9, Window));
	TestEqual(TEXT("reset drops the rows"), Tracker.GetHitCount(), 0);
	return true;
}
