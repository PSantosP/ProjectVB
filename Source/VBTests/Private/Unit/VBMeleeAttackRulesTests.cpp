// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBMotionWarpingNames.h"
#include "AnimNotifyState_MotionWarping.h"
#include "RootMotionModifier.h"
#include "RootMotionModifier_SkewWarp.h"

// 접근 멈춤 사슬을 값만으로 고정한다.
//   기대값은 지금 소스 식으로 계산한 값이다(옛 GA 를 실행해 잰 값이 아님). 기본 입력 = 멈춤 200, 사거리 270, 먼 시작 110, 여유 20, 캡슐 합 68.
//   음성 대조: 캡슐 하한을 빼면 거리 68 행이 48 로, 먼 시작 조건을 >= 로 바꾸면 거리 270 행이 110 으로 실패해야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeApproachOffsetTest, "VB.Unit.Melee.ApproachOffset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeApproachOffsetTest::RunTest(const FString& Parameters)
{
	auto Base = [](float Dist)
	{
		FVBApproachOffsetInputs In;
		In.BaseOffset = 200.0f;
		In.ScaledRange = 270.0f;
		In.FarStartApproachOffset = 110.0f;
		In.MinWarpStandoff = 20.0f;
		In.DistToTarget2D = Dist;
		In.MinSeparation = 68.0f;
		return In;
	};
	struct FRow { float Dist; float Expected; const TCHAR* Contract; };
	const FRow Rows[] = {
		{ 0.0f, 0.0f, TEXT("overlap: no goal behind the attacker") },
		{ 50.0f, 50.0f, TEXT("capsule floor never exceeds the current distance") },
		{ 68.0f, 68.0f, TEXT("capsule floor after the distance-standoff cap (CapsuleFloor)") },
		{ 100.0f, 80.0f, TEXT("distance-standoff cap") },
		{ 200.0f, 180.0f, TEXT("distance-standoff cap") },
		{ 270.0f, 200.0f, TEXT("far start needs dist > range, not >= (FarStartBoundary)") },
		{ 271.0f, 110.0f, TEXT("far start applies") },
		{ 400.0f, 110.0f, TEXT("far start holds") },
		{ 800.0f, 110.0f, TEXT("far start holds") },
	};
	for (const FRow& Row : Rows)
	{
		TestEqual(*FString::Printf(TEXT("dist %.0f: %s"), Row.Dist, Row.Contract),
			VBMeleeAttackRules::ResolveApproachOffset(Base(Row.Dist)), Row.Expected);
	}

	FVBApproachOffsetInputs FarOff = Base(400.0f);
	FarOff.FarStartApproachOffset = 0.0f;
	TestEqual(TEXT("far start 0: base offset kept"), VBMeleeAttackRules::ResolveApproachOffset(FarOff), 200.0f);

	FVBApproachOffsetInputs RangeCap = Base(400.0f);
	RangeCap.BaseOffset = 300.0f;
	RangeCap.FarStartApproachOffset = 0.0f;
	TestEqual(TEXT("range caps the base offset"), VBMeleeAttackRules::ResolveApproachOffset(RangeCap), 270.0f);

	FVBApproachOffsetInputs ZeroRange = Base(100.0f);
	ZeroRange.ScaledRange = 0.0f;
	TestEqual(TEXT("capsule floor outranks the range cap"), VBMeleeAttackRules::ResolveApproachOffset(ZeroRange), 68.0f);
	return true;
}

// 접근 워프 창 찾기: 배열 순서의 첫 AttackTarget 워프(가장 이른 시각으로 다시 고르지 않음), 다른 대상 건너뜀,
//   못 찾으면 null 이고 출력 시각을 건드리지 않음, 끝 시각 = trigger + duration.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeApproachWarpWindowTest, "VB.Unit.Melee.ApproachWarpWindow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeApproachWarpWindowTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = NewObject<UAnimMontage>();
	auto AddWarp = [Montage](FName TargetName, float Time, float Duration) -> URootMotionModifier_Warp*
	{
		UAnimNotifyState_MotionWarping* State = NewObject<UAnimNotifyState_MotionWarping>(Montage);
		URootMotionModifier_SkewWarp* Modifier = NewObject<URootMotionModifier_SkewWarp>(State);
		Modifier->WarpTargetName = TargetName;
		State->RootMotionModifier = Modifier;
		FAnimNotifyEvent& Event = Montage->Notifies.AddDefaulted_GetRef();
		Event.NotifyStateClass = State;
		Event.SetTime(Time);
		Event.SetDuration(Duration);
		return Modifier;
	};
	Montage->Notifies.AddDefaulted(); // 상태 없는 노티 - 건너뛴다
	AddWarp(TEXT("SomethingElse"), 0.1f, 0.2f); // 다른 대상 - 건너뛴다
	const URootMotionModifier_Warp* First = AddWarp(VBMotionWarpingNames::AttackTarget, 0.5f, 0.3f);
	AddWarp(VBMotionWarpingNames::AttackTarget, 0.2f, 0.1f); // 더 이르지만 배열에서 뒤 - 고르지 않는다

	float WindowEnd = -7.0f;
	TestTrue(TEXT("first AttackTarget warp in array order"), VBMeleeAttackRules::FindApproachWarpModifier(Montage, &WindowEnd) == First);
	TestEqual(TEXT("window end = trigger + duration"), WindowEnd, 0.8f, 1.0e-4f);

	float Untouched = -7.0f;
	TestNull(TEXT("no montage: null"), VBMeleeAttackRules::FindApproachWarpModifier(nullptr, &Untouched));
	UAnimMontage* NoWarp = NewObject<UAnimMontage>();
	TestNull(TEXT("no matching warp: null"), VBMeleeAttackRules::FindApproachWarpModifier(NoWarp, &Untouched));
	TestEqual(TEXT("no match leaves the out time unchanged"), Untouched, -7.0f);
	return true;
}

// 실제 몽타주의 전진, 창 끝을 옛 GA 와 같았던 값에 고정한다. 세 줄 = 창으로 자르는 플레이어 클립, 창 뒤 긴 걸음을 빼는 보스 클립, 창 없는 클립.
//   몽타주를 일부러 다시 저작했으면 다시 재서 고친다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeMontageLungePinTest, "VB.Unit.Melee.MontageLungePin",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeMontageLungePinTest::RunTest(const FString& Parameters)
{
	struct FPin { const TCHAR* Path; float Lunge; bool bWindow; float WindowEnd; };
	const FPin Pins[] = {
		{ TEXT("/Game/VB/Animations/Montage/AM_Fighter_Combo02.AM_Fighter_Combo02"), 172.5807f, true, 0.3799f },
		{ TEXT("/Game/VB/Animations/Montage/Enemy/AM_Boss_Attack_01.AM_Boss_Attack_01"), 47.5782f, true, 1.9699f },
		{ TEXT("/Game/VB/Animations/Montage/Enemy/AM_Boss_Attack_04.AM_Boss_Attack_04"), 1.1303f, false, -1.0f },
	};
	// 같은 빌드에서도 부를 때마다 전진이 0.005cm 까지 달라진다(한 실행 안의 두 호출도). 허용 0.05cm - 창을 잘못 자르거나 배율을 두 번 걸면 수십 cm 가 바뀐다.
	constexpr float LungeToleranceCm = 0.05f;
	for (const FPin& Pin : Pins)
	{
		const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, Pin.Path);
		if (!TestNotNull(Pin.Path, Montage))
		{
			continue;
		}

		float WindowEnd = -1.0f;
		const bool bWindow = VBMeleeAttackRules::FindApproachWarpModifier(Montage, &WindowEnd) != nullptr;
		TestEqual(*FString::Printf(TEXT("%s: approach window present"), Pin.Path), bWindow, Pin.bWindow);
		TestEqual(*FString::Printf(TEXT("%s: window end"), Pin.Path), WindowEnd, Pin.WindowEnd, 1.0e-3f);
		const float Lunge = VBMeleeAttackRules::MeasureMontageLungeCm(Montage, 1.0f);
		TestEqual(*FString::Printf(TEXT("%s: lunge cm (pinned before M-a; a deliberately re-authored clip means re-measure the pin)"), Pin.Path), Lunge, Pin.Lunge, LungeToleranceCm);
		TestEqual(*FString::Printf(TEXT("%s: mesh scale applied once"), Pin.Path),
			VBMeleeAttackRules::MeasureMontageLungeCm(Montage, 2.0f), 2.0f * Lunge, 2.0f * LungeToleranceCm);
	}

	return true;
}
#endif
