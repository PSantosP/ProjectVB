#include <limits>
#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBMotionWarpingNames.h"
#include "Data/VBAttackConfig.h"
#include "Enemy/VBGA_EnemyMeleeAttack.h"
#include "MotionWarpingComponent.h"
#include "RootMotionModifier_SkewWarp.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeComboApproachTest, "VB.Unit.MeleeApproach.ComboOffsets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBMeleeComboApproachTest::RunTest(const FString& Parameters)
{
	UVBAttackConfig* Config = NewObject<UVBAttackConfig>();
	const TArray<float> NoVariant;
	TestEqual(TEXT("Empty array preserves variant distance"), Config->ResolveComboApproachOffset(NoVariant, 0, 137.0f, 2.0f), 137.0f);
	Config->ComboApproachOffsets = {110.0f, 90.0f, -1.0f, 0.0f};
	TestEqual(TEXT("First swing is scaled once"), Config->ResolveComboApproachOffset(NoVariant, 0, 137.0f, 2.0f), 220.0f);
	TestEqual(TEXT("Next swing has independent reach"), Config->ResolveComboApproachOffset(NoVariant, 1, 137.0f, 2.0f), 180.0f);
	TestEqual(TEXT("Negative entry preserves variant"), Config->ResolveComboApproachOffset(NoVariant, 2, 137.0f, 2.0f), 137.0f);
	TestEqual(TEXT("Zero is an explicit offset"), Config->ResolveComboApproachOffset(NoVariant, 3, 137.0f, 2.0f), 0.0f);
	TestEqual(TEXT("Missing index preserves variant"), Config->ResolveComboApproachOffset(NoVariant, 4, 137.0f, 2.0f), 137.0f);
	TestEqual(TEXT("Negative index preserves variant"), Config->ResolveComboApproachOffset(NoVariant, -1, 137.0f, 2.0f), 137.0f);

	// 변형 타별 값이 공통 값보다 먼저다. 칸이 없거나 음수, NaN 이면 공통으로 내려간다(Fighter Combo01 = [-1, 85]).
	const TArray<float> Variant = {-1.0f, 85.0f, std::numeric_limits<float>::quiet_NaN()};
	TestEqual(TEXT("Variant negative entry falls to shared"), Config->ResolveComboApproachOffset(Variant, 0, 137.0f, 2.0f), 220.0f);
	TestEqual(TEXT("Variant entry wins over shared, scaled once"), Config->ResolveComboApproachOffset(Variant, 1, 137.0f, 2.0f), 170.0f);
	TestEqual(TEXT("Variant NaN falls to shared negative, then variant distance"),
		Config->ResolveComboApproachOffset(Variant, 2, 137.0f, 2.0f), 137.0f);
	TestEqual(TEXT("Variant missing entry falls to shared"), Config->ResolveComboApproachOffset(Variant, 3, 137.0f, 2.0f), 0.0f);
	TestEqual(TEXT("Both missing preserve variant distance"), Config->ResolveComboApproachOffset(Variant, 4, 137.0f, 2.0f), 137.0f);
	Config->ComboApproachOffsets.Reset();
	TestEqual(TEXT("Variant entry works without shared array"), Config->ResolveComboApproachOffset({80.0f}, 0, 137.0f, 1.0f), 80.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyApproachWindowTest, "VB.Unit.MeleeApproach.EnemyWindows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBEnemyApproachWindowTest::RunTest(const FString& Parameters)
{
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/VB/Animations/Montage/Enemy/AM_Enemy_Soldier_Combo3.AM_Enemy_Soldier_Combo3"));
	if (!TestNotNull(TEXT("Soldier combo asset"), Montage))
	{
		return false;
	}

	TArray<FMotionWarpingWindowData> Windows;
	UMotionWarpingUtilities::GetMotionWarpingWindowsForWarpTargetFromAnimation(Montage, VBMotionWarpingNames::AttackTarget, Windows);
	if (!TestEqual(TEXT("Three independently authored approach windows"), Windows.Num(), 3))
	{
		return false;
	}

	URootMotionModifier_SkewWarp* Modifier = NewObject<URootMotionModifier_SkewWarp>();
	Modifier->Animation = Montage;
	Modifier->WarpTargetName = VBMotionWarpingNames::AttackTarget;
	TestTrue(TEXT("New approach window can capture"), UVBGA_EnemyMeleeAttack::IsApproachWindowPending(Modifier, Montage));
	TestFalse(TEXT("Missing montage cannot capture"), UVBGA_EnemyMeleeAttack::IsApproachWindowPending(Modifier, nullptr));
	TestFalse(TEXT("Other montage cannot capture"), UVBGA_EnemyMeleeAttack::IsApproachWindowPending(Modifier, NewObject<UAnimMontage>()));
	Modifier->WarpTargetName = TEXT("OtherTarget");
	TestFalse(TEXT("Unrelated warp untouched"), UVBGA_EnemyMeleeAttack::IsApproachWindowPending(Modifier, Montage));
	Modifier->WarpTargetName = VBMotionWarpingNames::AttackTarget;
	Modifier->SetState(ERootMotionModifierState::Active);
	TestFalse(TEXT("Active swing never tracks again"), UVBGA_EnemyMeleeAttack::IsApproachWindowPending(Modifier, Montage));
	TArray<float> Distances;
	for (const FMotionWarpingWindowData& Window : Windows)
	{
		Modifier->StartTime = Window.StartTime;
		Modifier->EndTime = Window.EndTime;
		const float Distance = UVBGA_EnemyMeleeAttack::MeasureApproachWindowLunge(Modifier, 1.0f);
		Distances.Add(Distance);
		TestTrue(TEXT("Every swing contains forward motion"), Distance > 50.0f);
		TestNearlyEqual(TEXT("Mesh scale applied once"), UVBGA_EnemyMeleeAttack::MeasureApproachWindowLunge(Modifier, 2.5f),
			Distance * 2.5f);
	}

	TestTrue(TEXT("Second swing does not reuse first distance"), Distances[1] > Distances[0] + 20.0f);
	TestTrue(TEXT("Third swing does not reuse first distance"), Distances[2] > Distances[0] + 10.0f);
	TestEqual(TEXT("Missing modifier is safe"), UVBGA_EnemyMeleeAttack::MeasureApproachWindowLunge(nullptr, 1.0f), 0.0f);
	return true;
}
