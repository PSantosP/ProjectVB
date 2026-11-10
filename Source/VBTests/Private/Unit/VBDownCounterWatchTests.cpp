// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBDownCounterWatch.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Data/VBHitReactConfig.h"
#include "UObject/StrongObjectPtr.h"

// 다운 반격 감시: 저작된 세트만 감시하고, 시도 간격은 실패와 EndAbility 를 넘어 남으며, 구독은 건 대상에서 뗀다.
//   대조: WatchAbilityEnd 의 핸들 검사를 빼면 subscribes once 줄이, ReleaseAbilityEnd 를 비우면 released 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDownCounterWatchTest, "VB.Unit.Enemy.Knockdown.CounterWatch",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDownCounterWatchTest::RunTest(const FString& Parameters)
{
	FVBKnockdownSet Set;
	Set.DownAttackIntervalSeconds = 2.0f;
	FVBDownCounterWatch Watch;
	TestFalse(TEXT("no counter authored"), Watch.Arm(Set));
	Set.DownAttackInputTag = VBGameplayTags::Input_Attack_Armored;
	TestFalse(TEXT("a tag without a radius is no counter"), Watch.Arm(Set));
	Set.DownAttackTriggerRadius = 200.0f;
	TestTrue(TEXT("authored counter arms"), Watch.Arm(Set));
	TestTrue(TEXT("armed tag"), Watch.GetInputTag() == VBGameplayTags::Input_Attack_Armored.GetTag());
	TestEqual(TEXT("radius takes the body scale"), Watch.GetTriggerRadius(1.5f), 300.0f);

	TestTrue(TEXT("the first attempt is due"), Watch.IsAttemptDue(0.0));
	Watch.MarkAttempt(10.0);
	TestFalse(TEXT("inside the interval"), Watch.IsAttemptDue(11.9));
	TestTrue(TEXT("interval boundary passes"), Watch.IsAttemptDue(12.0));

	const FGameplayAbilitySpecHandle Counter = FGameplayAbilitySpec(UGameplayAbility::StaticClass()).Handle;
	const FGameplayAbilitySpecHandle Other = FGameplayAbilitySpec(UGameplayAbility::StaticClass()).Handle;
	const TStrongObjectPtr<UAbilitySystemComponent> ASC(NewObject<UAbilitySystemComponent>());
	int32 Ended = 0;
	const auto OnEnded = FGameplayAbilityEndedDelegate::FDelegate::CreateLambda([&Ended](const FAbilityEndedData&) { ++Ended; });
	Watch.WatchAbilityEnd(*ASC, Counter, OnEnded);
	Watch.WatchAbilityEnd(*ASC, Counter, OnEnded);
	TestTrue(TEXT("the counter ability is recognised"), Watch.IsCounterAbility(Counter));
	TestFalse(TEXT("another ability is not the counter"), Watch.IsCounterAbility(Other));
	ASC->OnAbilityEnded.Broadcast(FAbilityEndedData(nullptr, Counter, false, false));
	TestEqual(TEXT("subscribes once"), Ended, 1);
	Watch.ReleaseAbilityEnd();
	ASC->OnAbilityEnded.Broadcast(FAbilityEndedData(nullptr, Counter, false, false));
	TestEqual(TEXT("released"), Ended, 1);
	Watch.ReleaseAbilityEnd();
	TestFalse(TEXT("the attempt clock survives the release"), Watch.IsAttemptDue(11.0));
	TestNull(TEXT("no anim subscription to return"), Watch.TakeAnimSubscription());
	return true;
}

// 반격 끝으로 읽지 않는 몽타주 다섯 출처. 대조: 플린치를 빼면 flinch 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDownReactionOwnershipTest, "VB.Unit.Enemy.Knockdown.ReactionOwnership",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDownReactionOwnershipTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBHitReactConfig> Config(NewObject<UVBHitReactConfig>());
	const auto Make = [&Config]() { return NewObject<UAnimMontage>(Config.Get()); };
	FVBKnockdownClips Clips;
	Clips.Fall = Make();
	FVBKnockdownSet Set;
	Set.AirSlamMontage = Make();
	Set.EscapeGetUpMontage = Make();
	Set.DownHitSet.Montage = Make();
	Config->FlinchSet.Montage = Make();
	const UAnimMontage* Stranger = Make();

	TestTrue(TEXT("picked clips"), Config->IsDownReaction(Set, Clips, Clips.Fall));
	TestTrue(TEXT("air slam"), Config->IsDownReaction(Set, Clips, Set.AirSlamMontage));
	TestTrue(TEXT("escape get-up"), Config->IsDownReaction(Set, Clips, Set.EscapeGetUpMontage));
	TestTrue(TEXT("down hit"), Config->IsDownReaction(Set, Clips, Set.DownHitSet.Montage));
	TestTrue(TEXT("flinch"), Config->IsDownReaction(Set, Clips, Config->FlinchSet.Montage));
	TestFalse(TEXT("a stranger is the counter's montage"), Config->IsDownReaction(Set, Clips, Stranger));
	TestFalse(TEXT("no montage"), Config->IsDownReaction(Set, Clips, nullptr));
	return true;
}
#endif
