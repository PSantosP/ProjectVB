// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBMeleeActivationMemory.h"
#include "AbilitySystem/Abilities/VBSwingTargetBinding.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Data/VBAttackConfig.h"
#include "GameFramework/Actor.h"
#include "UObject/StrongObjectPtr.h"
#include "VBIsolatedGameWorld.h"
#include "VBLoadingScreenTestProbe.h" // AVBLoadingScreenTestAbilityPawn - 실제 ASC 를 가진 pawn
#include "VBMeleeMemoryBindingTestTypes.h"

// 공격 사이 기억의 세 규칙을 값만으로 본다.
//   음성 대조: "2개 이상일 때만 커서를 민다" 를 빼면 첫 행이, 재개의 무기 비교를 빼면 "다른 무기" 행이 실패해야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMeleeActivationMemoryTest, "VB.Unit.Melee.ActivationMemory",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBMeleeActivationMemoryTest::RunTest(const FString& Parameters)
{
	FVBMeleeActivationMemory Memory;
	// 커서는 감싸지 않은 번호다 - 감싸기는 UVBAttackConfig::ResolveMontageVariant 의 몫.
	TestEqual(TEXT("one variant: the cursor does not move"), Memory.TakeVariant(1), 0);
	TestEqual(TEXT("one variant again: still the same"), Memory.TakeVariant(1), 0);
	TestEqual(TEXT("three variants: first"), Memory.TakeVariant(3), 0);
	TestEqual(TEXT("three variants: next"), Memory.TakeVariant(3), 1);
	TestEqual(TEXT("a one-variant weapon in between does not move it"), Memory.TakeVariant(1), 2);
	TestEqual(TEXT("three variants continue where they stopped"), Memory.TakeVariant(3), 2);

	// 섞인 변형 수(검토 P1-1): 돌려주는 값은 누적 커서 그대로이고, 감싼 번호가 [0, 1, 2, 0, 0] 이어야 한다.
	//   TakeVariant 안에서 감싸면 마지막이 1 이 된다 - 그 실수를 이 행이 잡는다.
	FVBMeleeActivationMemory Mixed;
	const int32 Counts[] = {3, 3, 3, 3, 2};
	const int32 RawExpected[] = {0, 1, 2, 3, 4};
	const int32 WrappedExpected[] = {0, 1, 2, 0, 0};
	for (int32 Call = 0; Call < 5; ++Call)
	{
		const int32 Raw = Mixed.TakeVariant(Counts[Call]);
		TestEqual(*FString::Printf(TEXT("mixed counts call %d: raw cursor"), Call), Raw, RawExpected[Call]);
		TestEqual(*FString::Printf(TEXT("mixed counts call %d: wrapped like ResolveMontageVariant"), Call), Raw % Counts[Call],
			WrappedExpected[Call]);
	}

	UVBAttackConfig* Config = NewObject<UVBAttackConfig>();
	UVBAttackConfig* Other = NewObject<UVBAttackConfig>();
	TestEqual(TEXT("no combo start yet"), Memory.GetComboStarts().VariantFor(Config), static_cast<int32>(INDEX_NONE));
	Memory.NoteComboStart(Config, 2, EVBComboRole::Close);
	TestEqual(TEXT("combo start for the same config"), Memory.GetComboStarts().VariantFor(Config), 2);
	TestEqual(TEXT("the band rotation sees it"), Memory.GetComboStarts().LastStartFor(Config, EVBComboRole::Close), 2);
	TestEqual(TEXT("another config does not read it"), Memory.GetComboStarts().VariantFor(Other), static_cast<int32>(INDEX_NONE));
	Memory.NoteComboStart(Config, 5, EVBComboRole::Far);
	TestEqual(TEXT("a later start overwrites the last start"), Memory.GetComboStarts().VariantFor(Config), 5);
	TestEqual(TEXT("each band keeps its own last start"), Memory.GetComboStarts().LastStartFor(Config, EVBComboRole::Close), 2);
	TestEqual(TEXT("the new band holds the new start"), Memory.GetComboStarts().LastStartFor(Config, EVBComboRole::Far), 5);

	Config->MaxComboCount = 4;
	Config->DodgeOffsetGraceSeconds = 0.5f;
	Config->ComboSectionNames = {TEXT("Default"), TEXT("Swing2"), TEXT("Swing3"), TEXT("Swing4")};
	UAnimMontage* Montage = NewObject<UAnimMontage>(Config);
	for (const FName& Name : Config->ComboSectionNames)
	{
		Montage->CompositeSections.Add(FCompositeSection());
		Montage->CompositeSections.Last().SectionName = Name;
	}

	UAnimMontage* SingleSection = NewObject<UAnimMontage>(Config);
	SingleSection->CompositeSections.Add(FCompositeSection());
	SingleSection->CompositeSections.Last().SectionName = TEXT("Default");

	const int32 None = INDEX_NONE;
	Memory.NoteDodgeCut(2, 10.0, EVBWeaponType::Fighter);
	TestEqual(TEXT("dodge resume inside the grace"), Memory.PendingDodgeResume(Config, EVBWeaponType::Fighter, Montage, 10.3), 2);
	TestEqual(TEXT("dodge resume exactly at the grace edge"), Memory.PendingDodgeResume(Config, EVBWeaponType::Fighter, Montage, 10.5), 2);
	TestEqual(TEXT("dodge resume past the grace"), Memory.PendingDodgeResume(Config, EVBWeaponType::Fighter, Montage, 10.6), None);
	TestEqual(TEXT("dodge resume with another weapon"), Memory.PendingDodgeResume(Config, EVBWeaponType::Blade, Montage, 10.3), None);
	TestEqual(TEXT("dodge resume when the montage lacks the section"),
		Memory.PendingDodgeResume(Config, EVBWeaponType::Fighter, SingleSection, 10.3), None);
	TestEqual(TEXT("dodge resume without a config"), Memory.PendingDodgeResume(nullptr, EVBWeaponType::Fighter, Montage, 10.3), None);
	Config->DodgeOffsetGraceSeconds = 0.0f;
	TestEqual(TEXT("dodge resume off when the grace is 0"), Memory.PendingDodgeResume(Config, EVBWeaponType::Fighter, Montage, 10.0), None);
	Config->DodgeOffsetGraceSeconds = 0.5f;
	Memory.ConsumeDodgeResume();
	TestEqual(TEXT("consumed: nothing to resume"), Memory.PendingDodgeResume(Config, EVBWeaponType::Fighter, Montage, 10.3), None);
	Memory.NoteDodgeCut(0, 10.0, EVBWeaponType::Fighter);
	TestEqual(TEXT("hit 0 is never a resume"), Memory.PendingDodgeResume(Config, EVBWeaponType::Fighter, Montage, 10.3), None);
	Memory.NoteDodgeCut(4, 10.0, EVBWeaponType::Fighter);
	TestEqual(TEXT("past the last hit is never a resume"), Memory.PendingDodgeResume(Config, EVBWeaponType::Fighter, Montage, 10.3), None);
	TestEqual(TEXT("the cut time is kept for the log"), Memory.DodgeCutTime(), 10.0);
	return true;
}

// 스윙 목표 영수증: 묶으면 파괴가 처리 함수에 오고, 풀면 오지 않고, 다시 묶으면 앞 대상의 바인딩이 남지 않는다.
//   뒤 절은 같은 규칙을 ASC 의 State.Dead 구독으로 본다(ASC 를 가진 시험 폰).
//   음성 대조: Unbind 의 파괴 바인딩 빼기를 지우면 "풀고 파괴하면 0번" 이 실패해야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSwingTargetBindingTest, "VB.Unit.Melee.SwingTargetBinding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSwingTargetBindingTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld* World = Isolated.World;

	TStrongObjectPtr<UVBSwingTargetDeathProbe> Probe(NewObject<UVBSwingTargetDeathProbe>());
	FScriptDelegate OnDestroyed;
	OnDestroyed.BindUFunction(Probe.Get(), GET_FUNCTION_NAME_CHECKED(UVBSwingTargetDeathProbe, OnTargetDestroyed));
	const FOnGameplayEffectTagCountChanged::FDelegate OnDeadTag;

	FVBSwingTargetBinding Binding;
	AActor* First = World->SpawnActor<AActor>();
	Binding.Bind(First, OnDestroyed, OnDeadTag);
	TestTrue(TEXT("bound: Get returns the target"), Binding.Get() == First);
	First->Destroy();
	TestEqual(TEXT("bound target destroyed: the handler ran once"), Probe->DestroyedCalls, 1);

	Binding.Unbind();
	AActor* Second = World->SpawnActor<AActor>();
	Binding.Bind(Second, OnDestroyed, OnDeadTag);
	Binding.Unbind();
	TestNull(TEXT("unbound: Get is empty"), Binding.Get());
	Second->Destroy();
	TestEqual(TEXT("unbound then destroyed: the handler did not run"), Probe->DestroyedCalls, 1);

	// 대상 없이 묶기 = 앞 대상을 풀기만 한다(능력이 대상을 못 찾은 타).
	AActor* Fifth = World->SpawnActor<AActor>();
	Binding.Bind(Fifth, OnDestroyed, OnDeadTag);
	Binding.Bind(nullptr, OnDestroyed, OnDeadTag);
	TestNull(TEXT("bound to nothing: Get is empty"), Binding.Get());
	Fifth->Destroy();
	TestEqual(TEXT("bound to nothing: the previous target was released"), Probe->DestroyedCalls, 1);

	AActor* Third = World->SpawnActor<AActor>();
	AActor* Fourth = World->SpawnActor<AActor>();
	Binding.Bind(Third, OnDestroyed, OnDeadTag);
	Binding.Bind(Fourth, OnDestroyed, OnDeadTag);
	Third->Destroy();
	TestEqual(TEXT("rebinding released the previous target"), Probe->DestroyedCalls, 1);
	Fourth->Destroy();
	TestEqual(TEXT("the new target is bound"), Probe->DestroyedCalls, 2);
	Binding.Unbind();
	Binding.Unbind();
	TestNull(TEXT("unbinding twice is safe"), Binding.Get());

	// ASC 쪽(검토 P2-1): 다시 묶으면 앞 대상의 State.Dead 구독이 남지 않고, 풀면 새 대상의 것도 빠지며, 처리 안에서 스스로 풀어도 안전하다.
	//   음성 대조: Unbind 의 ASC 핸들 빼기를 지우면 "A 의 Dead 변경은 0회" 가 실패해야 한다.
	int32 DeadCalls = 0;
	const FOnGameplayEffectTagCountChanged::FDelegate CountDead = FOnGameplayEffectTagCountChanged::FDelegate::CreateLambda(
		[&DeadCalls](const FGameplayTag, int32) { ++DeadCalls; });
	AVBLoadingScreenTestAbilityPawn* PawnA = World->SpawnActor<AVBLoadingScreenTestAbilityPawn>();
	AVBLoadingScreenTestAbilityPawn* PawnB = World->SpawnActor<AVBLoadingScreenTestAbilityPawn>();
	if (TestNotNull(TEXT("ability pawn A"), PawnA) && TestNotNull(TEXT("ability pawn B"), PawnB))
	{
		PawnA->AbilitySystem->InitAbilityActorInfo(PawnA, PawnA);
		PawnB->AbilitySystem->InitAbilityActorInfo(PawnB, PawnB);
		Binding.Bind(PawnA, OnDestroyed, CountDead);
		Binding.Bind(PawnB, OnDestroyed, CountDead);
		PawnA->AbilitySystem->AddLooseGameplayTag(VBGameplayTags::State_Dead);
		TestEqual(TEXT("rebound: A's Dead change reaches nothing"), DeadCalls, 0);
		PawnB->AbilitySystem->AddLooseGameplayTag(VBGameplayTags::State_Dead);
		TestEqual(TEXT("rebound: B's Dead change reaches the handler once"), DeadCalls, 1);
		Binding.Unbind();
		PawnB->AbilitySystem->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
		TestEqual(TEXT("unbound: B's change reaches nothing"), DeadCalls, 1);

		// 처리 안에서 스스로 푼다 - GA 의 OnSwingTargetDeath -> ClearWarpTarget -> Unbind 와 같은 순서.
		FVBSwingTargetBinding SelfRelease;
		int32 SelfCalls = 0;
		SelfRelease.Bind(PawnA, OnDestroyed, FOnGameplayEffectTagCountChanged::FDelegate::CreateLambda(
			[&SelfRelease, &SelfCalls](const FGameplayTag, int32 NewCount) { ++SelfCalls; if (NewCount > 0) SelfRelease.Unbind(); }));
		PawnA->AbilitySystem->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
		PawnA->AbilitySystem->AddLooseGameplayTag(VBGameplayTags::State_Dead);
		TestNull(TEXT("released itself inside the handler"), SelfRelease.Get());
		PawnA->AbilitySystem->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
		TestEqual(TEXT("after releasing itself it hears nothing more"), SelfCalls, 2);
	}

	return true;
}
#endif
