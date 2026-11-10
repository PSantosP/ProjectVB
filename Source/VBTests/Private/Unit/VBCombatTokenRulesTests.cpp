// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AI/VBCombatTokenRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace VBCombatTokenRulesTests
{
	// 사거리 180, 놓는 거리 200, 접근 상한 3초의 기본 멤버.
	FVBTokenMember Member(double Distance)
	{
		FVBTokenMember Facts;
		Facts.Distance = Distance;
		Facts.PlannedStart = 180.0;
		Facts.PlannedRelease = 200.0;
		Facts.MaxApproachHoldSeconds = 3.0;
		return Facts;
	}

	FVBTokenMember Holder(double Distance, double DistanceAtGrant, double SecondsSinceGrant = 0.0)
	{
		FVBTokenMember Facts = Member(Distance);
		Facts.bHeld = true;
		Facts.DistanceAtGrant = DistanceAtGrant;
		Facts.SecondsSinceGrant = SecondsSinceGrant;
		return Facts;
	}
}

// 토큰 규칙(거두기, 순서, 역할)을 세계 없이 잰다. 대조: 부여 거리 가드를 빼면 'came closer' 줄이, 반격 앞세우기를 빼면 'counter' 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatTokenRulesTest, "VB.Unit.CombatToken.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatTokenRulesTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatTokenRules;
	using namespace VBCombatTokenRulesTests;

	TArray<FVBTokenMember> Members = {Holder(500.0, 300.0), Member(150.0)};
	TestEqual(TEXT("no candidate: nothing is revoked"), SelectRevocations(Members, {}).Num(), 0);
	TArray<FRevocation> Revoked = SelectRevocations(Members, {1});
	TestTrue(TEXT("moved away with a candidate in range: revoked"),
		Revoked.Num() == 1 && Revoked[0].MemberIndex == 0 && Revoked[0].Reason == ERevokeReason::MovedAway);

	Members = {Holder(250.0, 300.0), Member(150.0)};
	TestEqual(TEXT("came closer than at grant: kept"), SelectRevocations(Members, {1}).Num(), 0);
	Members = {Holder(500.0, 300.0), Member(190.0)};
	TestEqual(TEXT("no candidate inside its start distance: kept"), SelectRevocations(Members, {1}).Num(), 0);
	Members = {Holder(500.0, 300.0), Member(150.0)};
	Members[0].bAttackStarted = true;
	TestEqual(TEXT("attack started: never revoked"), SelectRevocations(Members, {1}).Num(), 0);

	Members = {Holder(500.0, 300.0, 5.0), Member(150.0)};
	Revoked = SelectRevocations(Members, {1});
	TestTrue(TEXT("approach overrun is decided first and the member is not revoked twice"),
		Revoked.Num() == 1 && Revoked[0].Reason == ERevokeReason::ApproachOverrun);

	Members = {Holder(300.0, 300.0), Member(450.0)};
	Members[1].bWantsCounter = true;
	Revoked = SelectRevocations(Members, {1});
	TestTrue(TEXT("armored counter request: approaching holder revoked"),
		Revoked.Num() == 1 && Revoked[0].Reason == ERevokeReason::ArmoredCounter);

	Members = {Member(150.0), Member(300.0), Member(450.0), Member(600.0)};
	TArray<int32> Order = OrderOffers(Members, {0, 1, 2}, {3});
	TestTrue(TEXT("order: retreated nearer first, inside release after, revoked last"),
		Order == TArray<int32>({1, 2, 0, 3}));
	Members[2].bWantsCounter = true;
	Members[3].bWantsCounter = true;
	Order = OrderOffers(Members, {0, 1, 2}, {3});
	TestTrue(TEXT("counter requesters move to the front, keeping their order"), Order == TArray<int32>({2, 3, 1, 0}));
	Members[0].PlannedRelease = 100.0;
	Members[2].bWantsCounter = Members[3].bWantsCounter = false;
	TestTrue(TEXT("retreated reads the planned release (one home with the moved-away rule)"), OrderOffers(Members, {0, 1}, {}) == TArray<int32>({0, 1}));

	TestTrue(TEXT("role: no token waits"), NextRole(false, EVBCombatRole::Attacker, 50.0, 180.0, 200.0) == EVBCombatRole::Surrounder);
	TestTrue(TEXT("role: enters at start"), NextRole(true, EVBCombatRole::Approaching, 180.0, 180.0, 200.0) == EVBCombatRole::Attacker);
	TestTrue(TEXT("role: approaching above start"),
		NextRole(true, EVBCombatRole::Approaching, 190.0, 180.0, 200.0) == EVBCombatRole::Approaching);
	TestTrue(TEXT("role: attacker kept until release"),
		NextRole(true, EVBCombatRole::Attacker, 195.0, 180.0, 200.0) == EVBCombatRole::Attacker);
	TestTrue(TEXT("role: attacker leaves beyond release"),
		NextRole(true, EVBCombatRole::Attacker, 210.0, 180.0, 200.0) == EVBCombatRole::Approaching);
	return true;
}

#endif
