// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBComboRuntime.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBMoveCancelGate.h"

// Fighter 후딜 끊기 - 콤보 창이 열린 동안 새로 민 스틱은 공격 후딜을 끊는다.
// 적중 때부터 밀고 있던 스틱은 끊지 않고, 저장된 공격 입력이 이긴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRecoveryCutOnMoveTest, "VB.Unit.Combat.RecoveryCutOnMove",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRecoveryCutOnMoveTest::RunTest(const FString& Parameters)
{
	// 1. 판정 - 조건이 모두 맞아야 끊는다.
	struct FRow { bool bEnabled, bWindow, bSaved, bFresh, bCut; const TCHAR* Name; };
	const FRow Rows[] = {
		{ true, true, false, true, true, TEXT("fresh push in the open window cuts") },
		{ false, true, false, true, false, TEXT("attack asset without the flag does not cut") },
		{ true, false, false, true, false, TEXT("outside the window does not cut") },
		{ true, true, true, true, false, TEXT("a saved attack press wins over the push") },
		{ true, true, false, false, false, TEXT("no fresh push, no cut") },
	};
	for (const FRow& Row : Rows)
	{
		TestEqual(Row.Name, FVBComboRuntime::ShouldCutRecoveryOnMove(Row.bEnabled, Row.bWindow, Row.bSaved, Row.bFresh), Row.bCut);
	}

	// 2. "새로" = 창이 열릴 때 gate 를 비운다. 열릴 때부터 밀던 스틱은 새것이 아니고, 뗐다가 다시 밀면 새것이다.
	FVBMoveCancelGate Gate;
	const FVector NoAcceleration = FVector::ZeroVector;
	Gate.Reset();
	TestFalse(TEXT("held through the window opening: not fresh"),
		Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, FVector2D(0.0, 1.0))));
	TestFalse(TEXT("released: not fresh"), Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, FVector2D::ZeroVector)));
	TestTrue(TEXT("pushed again: fresh"), Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, FVector2D(0.0, 1.0))));

	// 3. 데이터 약속 - Fighter 경공격만 켜져 있다 (Blade / BigSword / 반격은 꺼짐).
	struct FAsset { const TCHAR* Path; bool bOn; };
	const FAsset Assets[] = {
		{ TEXT("/Game/VB/Data/DA_Attack_Fighter_Light.DA_Attack_Fighter_Light"), true },
		{ TEXT("/Game/VB/Data/DA_Attack_Fighter_Riposte.DA_Attack_Fighter_Riposte"), false },
		{ TEXT("/Game/VB/Data/DA_Attack_Blade_Light.DA_Attack_Blade_Light"), false },
		{ TEXT("/Game/VB/Data/DA_Attack_BigSword_Light.DA_Attack_BigSword_Light"), false },
	};
	for (const FAsset& Asset : Assets)
	{
		const UVBAttackConfig* Config = LoadObject<UVBAttackConfig>(nullptr, Asset.Path);
		if (!TestNotNull(Asset.Path, Config))
		{
			continue;
		}

		TestEqual(*FString::Printf(TEXT("%s: bMoveCancelsRecoveryInWindow"), Asset.Path), Config->bMoveCancelsRecoveryInWindow, Asset.bOn);
		// 밀고 있는 스틱 허용은 비교용 스위치라 어떤 asset 도 켜지 않는다.
		TestFalse(*FString::Printf(TEXT("%s: bMoveCutAcceptsHeldStick off"), Asset.Path), Config->bMoveCutAcceptsHeldStick);
	}

	// 4. 밀고 있는 스틱 스위치 - console 값 0/1 은 asset 값을 덮고, 그 밖의 값은 asset 을 따른다.
	// 반대 대조: 0/1 분기를 바꾸면 "console 0 forces fresh-push only" 가 실패한다.
	TestFalse(TEXT("console 0 forces fresh-push only"), FVBComboRuntime::ResolveMoveCutAcceptsHeld(true, 0));
	TestTrue(TEXT("console 1 forces held too"), FVBComboRuntime::ResolveMoveCutAcceptsHeld(false, 1));
	TestTrue(TEXT("console -1 keeps asset on"), FVBComboRuntime::ResolveMoveCutAcceptsHeld(true, -1));
	TestFalse(TEXT("console -1 keeps asset off"), FVBComboRuntime::ResolveMoveCutAcceptsHeld(false, -1));
	TestTrue(TEXT("unknown console value keeps the asset"), FVBComboRuntime::ResolveMoveCutAcceptsHeld(true, 7));
	return true;
}
#endif
