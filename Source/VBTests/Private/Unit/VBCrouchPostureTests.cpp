#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBCombatRuleTable.h"
#include "Data/VBCombatSettings.h"

// 자세 관문의 단위 시험 - DA_CombatRules 의 앉기 칸 하나를 앉기 능력, 입력 관문, 슬라이드가 같이 묻는다(IsBlockedByHeldState).
//   칸에서 상태 하나가 빠지면 그 동작 도중 C 가 "서기 몽타주 + 앉은 캡슐" 을 다시 만들므로 여기서 센다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCrouchPostureTagsTest, "VB.Unit.Crouch.PostureTags",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCrouchPostureTagsTest::RunTest(const FString& Parameters)
{
	const UVBCombatRuleTable* Rules = GetDefault<UVBCombatSettings>()->CombatRules.LoadSynchronous();
	if (!TestNotNull(TEXT("shipped combat rule table"), Rules))
	{
		return false;
	}

	FGameplayTagContainer Posture;
	FGameplayTagContainer Required;
	Rules->AppendActivationTags(FGameplayTagContainer(VBGameplayTags::Ability_Locomotion_Crouch), Posture, Required);

	// 몸통을 쓰는 동작 전부. 하나라도 빠지면 그 동작 도중의 C 가 다시 깨진다.
	const TArray<FGameplayTag> Expected = {
		VBGameplayTags::State_Combat_Attacking, VBGameplayTags::State_Combat_Finishing, VBGameplayTags::State_Combat_Guarding,
		VBGameplayTags::State_Combat_HitReacting, VBGameplayTags::State_Combat_Dodging, VBGameplayTags::State_Combat_Staggered,
		VBGameplayTags::State_Combat_Downed, VBGameplayTags::State_Combat_GettingUp, VBGameplayTags::State_Weapon_Summoning,
		VBGameplayTags::State_Weapon_Sheathing, VBGameplayTags::State_Dead, VBGameplayTags::State_Combat_Ragdoll,
		VBGameplayTags::State_Interacting_SmartObject, VBGameplayTags::State_Traversal_Hang, VBGameplayTags::State_Traversal_Ladder,
		VBGameplayTags::State_Traversal_Climb, VBGameplayTags::State_Traversal_Move, VBGameplayTags::State_Traversal_Exit };
	for (const FGameplayTag& Tag : Expected)
	{
		TestTrue(FString::Printf(TEXT("posture set has %s"), *Tag.ToString()), Tag.IsValid() && Posture.HasTagExact(Tag));
	}

	TestEqual(TEXT("posture set is exactly the expected states"), Posture.Num(), Expected.Num());
	TestEqual(TEXT("crouch requires no state"), Required.Num(), 0);
	// 질주는 막는 상태가 아니라 미끄러지기다 - 칸에 들어가면 질주 + C 의 미끄러지기가 입력 관문에서 막힌다.
	TestFalse(TEXT("sprinting is not a posture block (sprint + C = slide)"),
		Posture.HasTagExact(VBGameplayTags::State_Locomotion_Sprinting));

	// 달리는 동안 앉기 능력이 안 켜지는 것은 질주 능력의 줄이고, 회피는 앉기를 끝내고 시작한다.
	FGameplayTagContainer SprintBlocks;
	FGameplayTagContainer SprintCancels;
	Rules->AppendBlockAndCancelTags(FGameplayTagContainer(VBGameplayTags::Ability_Locomotion_Sprint), SprintBlocks, SprintCancels);
	TestTrue(TEXT("sprinting keeps the crouch ability out"), SprintBlocks.HasTagExact(VBGameplayTags::Ability_Locomotion_Crouch));
	FGameplayTagContainer DodgeBlocks;
	FGameplayTagContainer DodgeCancels;
	Rules->AppendBlockAndCancelTags(FGameplayTagContainer(VBGameplayTags::Ability_Defense_Dodge), DodgeBlocks, DodgeCancels);
	TestTrue(TEXT("dodge cancels the crouch ability"), DodgeCancels.HasTagExact(VBGameplayTags::Ability_Locomotion_Crouch));
	return true;
}
#endif
