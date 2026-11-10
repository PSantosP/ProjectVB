// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponVisualComponent.h"

#if WITH_DEV_AUTOMATION_TESTS
// 주운 물건(방패)에 걸린 무기 외형: 그 패시브(가드를 여는 것과 같은 사실)가 없으면 숨는다. 패시브가 걸리지 않은 외형은 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStoryGatedWeaponTest, "VB.Unit.Weapon.StoryGated",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBStoryGatedWeaponTest::RunTest(const FString& Parameters)
{
	FVBWeaponVisualPart Tray;
	Tray.RequiredPassive = VBGameplayTags::Passive_Parry;
	FGameplayTagContainer Owned;
	TestTrue(TEXT("ungated part always shown"), UVBWeaponVisualComponent::IsPartShown(FVBWeaponVisualPart(), Owned));
	TestFalse(TEXT("gated part hidden while the passive is missing"), UVBWeaponVisualComponent::IsPartShown(Tray, Owned));
	Owned.AddTag(VBGameplayTags::Passive_Dodge);
	TestFalse(TEXT("another passive does not show it"), UVBWeaponVisualComponent::IsPartShown(Tray, Owned));
	Owned.AddTag(VBGameplayTags::Passive_Parry);
	TestTrue(TEXT("passive acquired: shield shown"), UVBWeaponVisualComponent::IsPartShown(Tray, Owned));
	return true;
}
#endif
