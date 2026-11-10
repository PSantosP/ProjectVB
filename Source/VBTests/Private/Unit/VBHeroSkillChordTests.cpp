// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Input/VBKeyBindingRules.h"

#if WITH_DEV_AUTOMATION_TESTS

// 패드 L3 + R3: 서 있을 때만 필살기, 움직이는 중이면 미끄러지기, 앉기 그대로, 키보드는 늘 앉기.
//   옵션에서 필살기에 패드 버튼을 묶으면 조합은 꺼진다.
//   대조: 이동 입력 검사를 빼면 "moving" 줄이, 장치 검사를 빼면 "keyboard" 줄이, 묶음 검사를 빼면 "bound" 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroSkillChordTest, "VB.Unit.Character.HeroInput.SkillChord",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroSkillChordTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("pad, sprint held, standing: special skill"), VBKeyBinding::IsSkillChordPress(true, true, false, false));
	TestFalse(TEXT("pad, sprint held, moving: slide as today"), VBKeyBinding::IsSkillChordPress(true, true, true, false));
	TestFalse(TEXT("pad, sprint not held: crouch"), VBKeyBinding::IsSkillChordPress(true, false, false, false));
	TestFalse(TEXT("keyboard, sprint held, standing: crouch (skill is G)"), VBKeyBinding::IsSkillChordPress(false, true, false, false));
	TestFalse(TEXT("pad skill button bound in options: chord off, crouch"), VBKeyBinding::IsSkillChordPress(true, true, false, true));
	return true;
}

#endif
