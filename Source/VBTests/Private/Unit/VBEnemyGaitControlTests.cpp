// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/VBEnemyGaitControl.h"
#include "Animation/VBAnimationTypes.h"

// 걷기와 뛰기의 경계. 대조: 이탈 거리를 진입 거리로 바꾸면 keeps running 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEnemyGaitChooseTest, "VB.Unit.EnemyCombat.GaitHysteresis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEnemyGaitChooseTest::RunTest(const FString& Parameters)
{
	using VBEnemyGaitControl::ChooseGait;
	constexpr float Enter = 250.0f;
	constexpr float Exit = 180.0f;
	TestTrue(TEXT("walking below the enter distance keeps walking"), ChooseGait(200.0, false, Enter, Exit) == EVBGait::Walk);
	TestTrue(TEXT("walking at the enter distance keeps walking"), ChooseGait(250.0, false, Enter, Exit) == EVBGait::Walk);
	TestTrue(TEXT("walking past the enter distance runs"), ChooseGait(251.0, false, Enter, Exit) == EVBGait::Run);
	TestTrue(TEXT("running between the two keeps running"), ChooseGait(200.0, true, Enter, Exit) == EVBGait::Run);
	TestTrue(TEXT("running below the exit distance walks"), ChooseGait(180.0, true, Enter, Exit) == EVBGait::Walk);
	TestTrue(TEXT("negative remaining distance walks"), ChooseGait(-50.0, true, Enter, Exit) == EVBGait::Walk);
	return true;
}
#endif
