// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBANS_AttackTraceWindow.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBCinematicSkillDefinition.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCinematicSkillTestTypes.h"
#include "VBIsolatedGameWorld.h"

// 시네마틱 스킬: 연출이 누구를 붙드는지, 피해가 언제 들어가도 되는지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicSkillRosterTest, "VB.Unit.CinematicSkill.Roster",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCinematicSkillRosterTest::RunTest(const FString& Parameters)
{
	const auto Make = [](float Distance, float Angle) { FVBCinematicRosterCandidate C; C.DistanceCm = Distance; C.AngleFromFacingDeg = Angle; return C; };
	TArray<FVBCinematicRosterCandidate> C = { Make(500, 10), Make(100, 90), Make(900, 0), Make(300, 170) };
	TArray<int32> Got = VBCinematicSkillRules::SelectRoster(C, 800, 180, 8);
	TestTrue(TEXT("in radius, nearest first"), Got == TArray<int32>({ 1, 3, 0 }));
	TestTrue(TEXT("front arc only"), VBCinematicSkillRules::SelectRoster(C, 800, 90, 8) == TArray<int32>({ 1, 0 }));
	TestTrue(TEXT("bounded count keeps the nearest"), VBCinematicSkillRules::SelectRoster(C, 800, 180, 2) == TArray<int32>({ 1, 3 }));
	TestEqual(TEXT("zero targets allowed"), VBCinematicSkillRules::SelectRoster(C, 800, 180, 0).Num(), 0);

	// 제외 조건(죽음, 공중, 태그 제외, 몸 사용 중)은 하나만으로도 가장 가까운 후보를 뺀다.
	for (int32 Flag = 0; Flag < 4; ++Flag)
	{
		TArray<FVBCinematicRosterCandidate> D = C;
		FVBCinematicRosterCandidate& Near = D[1];
		switch (Flag)
		{
		case 0: Near.bAlive = false; break;
		case 1: Near.bGrounded = false; break;
		case 2: Near.bExcludedByTag = true; break;
		default: Near.bBodyBusy = true; break;
		}

		TestFalse(*FString::Printf(TEXT("exclusion %d drops the candidate"), Flag),
			VBCinematicSkillRules::SelectRoster(D, 800, 180, 8).Contains(1));
	}

	return true;
}

// 모르면 막는다: leaf 의 Montage 에 trace window 가 꼭 하나(마지막 타격)일 때만 스킬이 돈다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicSkillReadyTest, "VB.Unit.CinematicSkill.Ready",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCinematicSkillReadyTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("no leaf"), VBCinematicSkillRules::IsReady(nullptr));
	UVBAttackConfig* Leaf = NewObject<UVBAttackConfig>(GetTransientPackage());
	const TStrongObjectPtr<UVBAttackConfig> KeepLeaf(Leaf);
	TestFalse(TEXT("leaf without a montage"), VBCinematicSkillRules::IsReady(Leaf));
	UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
	const TStrongObjectPtr<UAnimMontage> KeepMontage(Montage);
	Leaf->AttackMontage = Montage;
	TestFalse(TEXT("montage without a trace window"), VBCinematicSkillRules::IsReady(Leaf));
	FAnimNotifyEvent& First = Montage->Notifies.AddDefaulted_GetRef();
	First.NotifyStateClass = NewObject<UVBANS_AttackTraceWindow>(Montage);
	TestTrue(TEXT("exactly one trace window"), VBCinematicSkillRules::IsReady(Leaf));
	FAnimNotifyEvent& Second = Montage->Notifies.AddDefaulted_GetRef();
	Second.NotifyStateClass = NewObject<UVBANS_AttackTraceWindow>(Montage);
	FString Why;
	TestFalse(TEXT("two trace windows (an early hit would exist)"), VBCinematicSkillRules::IsReady(Leaf, &Why));
	TestTrue(TEXT("the refusal says why"), Why.Contains(TEXT("exactly one")));

	// segment 가 트는 sequence 의 trace window 도 Montage 중에 켜지는데 그 시각을 검사가 못 정하므로 거절한다.
	Montage->Notifies.RemoveAt(1);
	TestTrue(TEXT("back to one montage window"), VBCinematicSkillRules::IsReady(Leaf));
	// 트는 sequence 자리에 둘째 Montage 를 쓴다 - transient UAnimSequence 는 -game 에 없는 에디터 데이터 모델이 필요하고,
	//   UAnimSequenceBase 는 모두 같은 방식으로 Notifies 를 가진다.
	UAnimMontage* Source = NewObject<UAnimMontage>(GetTransientPackage());
	const TStrongObjectPtr<UAnimMontage> KeepSource(Source);
	FAnimNotifyEvent& SourceWindow = Source->Notifies.AddDefaulted_GetRef();
	SourceWindow.NotifyStateClass = NewObject<UVBANS_AttackTraceWindow>(Source);
	FSlotAnimationTrack& Track = Montage->SlotAnimTracks.AddDefaulted_GetRef();
	FAnimSegment& Segment = Track.AnimTrack.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(Source);
	TestFalse(TEXT("a source-sequence trace window (propagated notify)"), VBCinematicSkillRules::IsReady(Leaf));
	return true;
}

// 마지막 타격 관문: 마지막 창 밖은 없음, roster 액터만, 세션마다 한 번씩.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicSkillFinalHitTest, "VB.Unit.CinematicSkill.FinalHitOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCinematicSkillFinalHitTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};
	AActor* A = World->SpawnActor<AStaticMeshActor>();
	AActor* B = World->SpawnActor<AStaticMeshActor>();
	AActor* Outsider = World->SpawnActor<AStaticMeshActor>();
	TSet<TWeakObjectPtr<AActor>> Roster = { A, B };
	TSet<TWeakObjectPtr<AActor>> Consumed;
	const TArray<const AActor*> Hits = { A, Outsider, B, A };

	// 단계: 마지막 창이 열리기 전 이벤트는 거절, 창 끝을 30 fps 한 프레임 지난 마지막 자세 sweep 은 센다.
	TestFalse(TEXT("early trace event (50 ms before the final open)"), VBCinematicSkillRules::IsFinalPhase(1.25f, 1.30f));
	TestTrue(TEXT("the begin event's own frame"), VBCinematicSkillRules::IsFinalPhase(1.30f - 0.01f, 1.30f));
	TestTrue(TEXT("final-pose sweep 33 ms past a window ending at 1.40"), VBCinematicSkillRules::IsFinalPhase(1.40f + 0.033f, 1.30f));
	TestEqual(TEXT("an early window (not the final) applies nothing"),
		VBCinematicSkillRules::AllowFinalHits(Hits, false, Roster, Consumed).Num(), 0);
	TestEqual(TEXT("nothing consumed by the early window"), Consumed.Num(), 0);
	TestTrue(TEXT("final window: roster actors once each, the outsider never"),
		VBCinematicSkillRules::AllowFinalHits(Hits, true, Roster, Consumed) == TArray<int32>({ 0, 2 }));
	TestEqual(TEXT("a duplicate final event in the same session applies nothing"),
		VBCinematicSkillRules::AllowFinalHits(Hits, true, Roster, Consumed).Num(), 0);
	return true;
}

// 시전자 잠금: 연출 중엔 보통 공격, 회피, 막기, 상호작용이 안 시작되고 끝나면 다시 시작된다.
//   반대 대조: 표의 Ability.Attack.CinematicSkill 줄에서 BlocksWhileRunning 칸을 빼면 회피, 상호작용, 막기 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCinematicSkillBlocksOrdinaryTest, "VB.Unit.CinematicSkill.BlocksOrdinaryActions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCinematicSkillBlocksOrdinaryTest::RunTest(const FString& Parameters)
{
	UClass* Guard = StaticLoadClass(UGameplayAbility::StaticClass(), nullptr,
		TEXT("/Game/VB/GAS/Abilities/BP_GA_Guard.BP_GA_Guard_C"));
	if (!TestNotNull(TEXT("shipped BP_GA_Guard"), Guard))
	{
		return false;
	}
	// 막기는 asset tag 로 맞추므로 대역은 태그가 같을 때만 실제 guard 를 대신한다.
	if (!TestTrue(TEXT("guard double carries the shipped guard's asset tags"),
		Guard->GetDefaultObject<UGameplayAbility>()->GetAssetTags() == GetDefault<UVBCinematicSkillTestGuard>()->GetAssetTags()))
	{
		return false;
	}

	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	AActor* Avatar = Isolated.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("avatar"), Avatar))
	{
		return false;
	}
	// 게임 ASC 를 쓴다 - 막기는 그가 가진 전투 규칙 표에서 온다(엔진 ASC 는 표가 없어 아무것도 안 시작된다).
	UVBAbilitySystemComponent* VBASC = NewObject<UVBAbilitySystemComponent>(Avatar);
	VBASC->RegisterComponent();
	VBASC->InitAbilityActorInfo(Avatar, Avatar);
	UAbilitySystemComponent& ASC = *VBASC;
	const FGameplayAbilitySpecHandle Show = ASC.GiveAbility(FGameplayAbilitySpec(UVBCinematicSkillTestShow::StaticClass()));
	const TPair<const TCHAR*, FGameplayAbilitySpecHandle> Ordinary[] = {
		{TEXT("light attack"), ASC.GiveAbility(FGameplayAbilitySpec(UVBCinematicSkillTestLight::StaticClass()))},
		{TEXT("dodge"), ASC.GiveAbility(FGameplayAbilitySpec(UVBCinematicSkillTestDodge::StaticClass()))},
		{TEXT("interact"), ASC.GiveAbility(FGameplayAbilitySpec(UVBCinematicSkillTestInteract::StaticClass()))},
		{TEXT("guard"), ASC.GiveAbility(FGameplayAbilitySpec(UVBCinematicSkillTestGuard::StaticClass()))}};

	// 대조: 아무것도 안 돌 때는 각각 시작된다(여기서 끝낸다).
	for (const TPair<const TCHAR*, FGameplayAbilitySpecHandle>& Action : Ordinary)
	{
		TestTrue(*FString::Printf(TEXT("idle: %s starts"), Action.Key), ASC.TryActivateAbility(Action.Value));
		ASC.CancelAbilityHandle(Action.Value);
	}

	if (!TestTrue(TEXT("the show starts"), ASC.TryActivateAbility(Show)))
	{
		return false;
	}

	for (const TPair<const TCHAR*, FGameplayAbilitySpecHandle>& Action : Ordinary)
	{
		TestFalse(*FString::Printf(TEXT("show running: %s does not start"), Action.Key), ASC.TryActivateAbility(Action.Value));
	}

	ASC.CancelAbilityHandle(Show);
	for (const TPair<const TCHAR*, FGameplayAbilitySpecHandle>& Action : Ordinary)
	{
		TestTrue(*FString::Printf(TEXT("show ended: %s starts again"), Action.Key), ASC.TryActivateAbility(Action.Value));
		ASC.CancelAbilityHandle(Action.Value);
	}

	return true;
}
#endif
