#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBSwingCombatProfile.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 스윙의 "강타" 등급이 맞은 부위가 고른 타격 큐를 그 큐의 강타판(.Heavy 자식)으로 바꾸는 규칙.
// 칼 마무리타는 칼 강타로, 주먹 마무리타는 주먹 강타로 - 태그를 직접 들면 주먹에 칼 강타 피가 나온다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeavyImpactTierTest, "VB.Unit.Combat.HeavyImpactTier",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeavyImpactTierTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Physical = VBGameplayTags::GameplayCue_Damage_Physical;
	const FGameplayTag NoHeavyChild = VBGameplayTags::GameplayCue_Finisher_Strike;

	UVBSwingCombatProfile* Profile = NewObject<UVBSwingCombatProfile>();
	TestEqual(TEXT("프로필 없음 - 고른 큐 그대로"), UVBSwingCombatProfile::ResolveImpactCue(nullptr, Physical), Physical);
	TestEqual(TEXT("강타 꺼짐(기본) - 지금과 같다"), UVBSwingCombatProfile::ResolveImpactCue(Profile, Physical), Physical);

	Profile->bHeavyImpact = true;
	TestEqual(TEXT("칼 큐 -> 칼 강타"), UVBSwingCombatProfile::ResolveImpactCue(Profile, Physical),
		FGameplayTag(VBGameplayTags::GameplayCue_Damage_Physical_Heavy));
	TestEqual(TEXT("주먹 큐 -> 주먹 강타"), UVBSwingCombatProfile::ResolveImpactCue(Profile, VBGameplayTags::GameplayCue_Damage_Blunt),
		FGameplayTag(VBGameplayTags::GameplayCue_Damage_Blunt_Heavy));
	// 아래 "강타판 없음" 이 계속 그 경로를 시험하는지 - 누가 Finisher.Strike.Heavy 를 만들면 여기서 먼저 깨진다
	TestFalse(TEXT("처형 큐에는 강타판이 없다(시험 전제)"),
		FGameplayTag::RequestGameplayTag(FName(*(NoHeavyChild.ToString() + TEXT(".Heavy"))), /*ErrorIfNotFound=*/false).IsValid());
	TestEqual(TEXT("강타판이 없는 큐 - 원래 큐 그대로"), UVBSwingCombatProfile::ResolveImpactCue(Profile, NoHeavyChild), NoHeavyChild);
	// 이미 강타인 큐(대검 리포스트의 공격 설정)는 ".Heavy.Heavy" 가 없으니 그대로다
	TestEqual(TEXT("이미 강타 - 두 번 올리지 않는다"),
		UVBSwingCombatProfile::ResolveImpactCue(Profile, VBGameplayTags::GameplayCue_Damage_Physical_Heavy),
		FGameplayTag(VBGameplayTags::GameplayCue_Damage_Physical_Heavy));
	TestFalse(TEXT("빈 큐는 빈 큐"), UVBSwingCombatProfile::ResolveImpactCue(Profile, FGameplayTag()).IsValid());

#if WITH_EDITOR
	// 강타 등급만 든 프로필도 "저작한 것" 이다 - 아무 효과 없는 자산으로 거절하면 안 된다.
	FDataValidationContext Context;
	TestTrue(TEXT("강타 등급만 든 프로필은 유효"), Profile->IsDataValid(Context) != EDataValidationResult::Invalid);
	// 대조: 같은 프로필에서 강타만 끄면 빈 자산 - 위 통과가 강타 조건 덕인지 확인한다
	Profile->bHeavyImpact = false;
	FDataValidationContext EmptyContext;
	TestTrue(TEXT("아무 효과 없는 프로필은 무효"), Profile->IsDataValid(EmptyContext) == EDataValidationResult::Invalid);
#endif
	return true;
}
#endif
