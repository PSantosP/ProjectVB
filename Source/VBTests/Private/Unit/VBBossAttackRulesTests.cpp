// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AI/VBEnemyCombatComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Data/VBBossConfig.h"
#include "Enemy/VBBossAttackRules.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCombatAuditTestAbility.h"
#include "VBEnemyBarTestBoss.h"

#if WITH_DEV_AUTOMATION_TESTS

// 상황 행 고르기(순수): 각도 경계, 상황, 쿨다운, 켤 수 있음 거르기, 무게 뽑기.
//   대조: 쿨다운을 보지 않으면 cooling 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBossAttackRulesTest, "VB.Unit.Boss.AttackRows.Rules",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBBossAttackRulesTest::RunTest(const FString& Parameters)
{
	using namespace VBBossAttackRules;
	const FVector Forward = FVector::ForwardVector;
	const auto At = [](float Degrees) { return FRotator(0.0f, Degrees, 0.0f).Vector() * 300.0; };
	TestFalse(TEXT("119 deg is not behind"), IsTargetBehind(Forward, At(119.0f), 120.0f));
	TestTrue(TEXT("121 deg is behind"), IsTargetBehind(Forward, At(121.0f), 120.0f));
	TestTrue(TEXT("other side too"), IsTargetBehind(Forward, At(-150.0f), 120.0f));
	TestFalse(TEXT("on top of the boss: not behind"), IsTargetBehind(Forward, FVector::ZeroVector, 120.0f));

	FVBBossAttackRow Behind;
	Behind.Situation = EVBBossAttackSituation::TargetBehind;
	FVBBossAttackRow Far;
	Far.Situation = EVBBossAttackSituation::TargetFar;
	Far.Weight = 3.0f;
	FVBBossAttackRow Burst;
	Burst.Situation = EVBBossAttackSituation::AfterHitBurst;
	const TArray<FVBBossAttackRow> Rows = {Behind, Far, Burst};
	const auto Always = [](const FVBBossAttackRow&) { return true; };
	const TConstArrayView<double> NoCooldown;
	FVBBossSituationFacts Facts;
	TestEqual(TEXT("no situation: default"), ChooseRow(Rows, Facts, NoCooldown, 10.0, Always, 0.5f), INDEX_NONE);
	TestEqual(TEXT("no rows: default"), ChooseRow(TConstArrayView<FVBBossAttackRow>(), Facts, NoCooldown, 10.0, Always, 0.5f), INDEX_NONE);
	Facts.bTargetBehind = true;
	TestEqual(TEXT("behind: the behind row"), ChooseRow(Rows, Facts, NoCooldown, 10.0, Always, 0.5f), 0);
	TestEqual(TEXT("cooling: default"), ChooseRow(Rows, Facts, TArray<double>({20.0, 0.0, 0.0}), 10.0, Always, 0.5f), INDEX_NONE);
	TestEqual(TEXT("cooldown passed: the row again"), ChooseRow(Rows, Facts, TArray<double>({10.0, 0.0, 0.0}), 10.0, Always, 0.5f), 0);
	TestEqual(TEXT("cannot activate: default"),
		ChooseRow(Rows, Facts, NoCooldown, 10.0, [](const FVBBossAttackRow&) { return false; }, 0.5f), INDEX_NONE);
	Facts.bTargetFar = true;
	TestEqual(TEXT("weights 1:3, low draw: first"), ChooseRow(Rows, Facts, NoCooldown, 10.0, Always, 0.2f), 0);
	TestEqual(TEXT("weights 1:3, high draw: second"), ChooseRow(Rows, Facts, NoCooldown, 10.0, Always, 0.3f), 1);
	TestEqual(TEXT("draw 1.0 stays in range"), ChooseRow(Rows, Facts, NoCooldown, 10.0, Always, 1.0f), 1);
	return true;
}

// 보스가 계획마다 한 번 고르고, 고른 행은 토큰을 쥔 동안만 쓴다. 준비 검사(토큰 없음)는 늘 기본 공격을 묻는다.
//   대조: 토큰을 보지 않으면 without a token 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBossAttackRowsHoldTest, "VB.Unit.Boss.AttackRows.HeldPerPlan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBBossAttackRowsHoldTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(true).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVBEnemyBarTestBoss* Boss = World->SpawnActor<AVBEnemyBarTestBoss>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	AActor* Target = World->SpawnActor<AActor>();
	UVBCombatCoordinatorSubsystem* Coordinator = World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
	if (!TestTrue(TEXT("boss, target, coordinator"), Boss && Target && Coordinator))
	{
		return false;
	}

	UVBBossConfig* Config = NewObject<UVBBossConfig>(Boss);
	Config->AttackAbilityInputTag = VBGameplayTags::Input_Attack_Light;
	FVBBossAttackRow& Row = Config->FirstPhaseRows.AddDefaulted_GetRef();
	Row.InputTag = VBGameplayTags::Input_Attack_Armored;
	Row.Situation = EVBBossAttackSituation::TargetBehind;
	Row.CooldownSeconds = 30.0f;
	Boss->Configure(Config);
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	UVBEnemyCombatComponent* Combat = Boss->GetCombatComponent();
	if (!TestTrue(TEXT("ASC and combat"), ASC && Combat))
	{
		return false;
	}

	ASC->InitAbilityActorInfo(Boss, Boss);
	ASC->GiveAbility(FGameplayAbilitySpec(UVBBossRowTestAbility::StaticClass()));
	USceneComponent* TargetRoot = NewObject<USceneComponent>(Target);
	Target->SetRootComponent(TargetRoot);
	TargetRoot->RegisterComponent();
	Target->SetActorLocation(FVector(-300.0, 0.0, 0.0)); // 등 뒤(루트가 없으면 위치가 안 바뀐다)
	Combat->SetCombatTarget(Target);

	TestTrue(TEXT("before any plan: the phase default"), Boss->GetCurrentAttackInputTag() == VBGameplayTags::Input_Attack_Light);
	if (!TestTrue(TEXT("token granted"), Coordinator->TryAcquireAttackToken(Combat, 1)))
	{
		return false;
	}

	TestTrue(TEXT("planned with the target behind: the behind row"),
		Boss->GetCurrentAttackInputTag() == VBGameplayTags::Input_Attack_Armored);
	Boss->NotifyAttackStarted(); // 고른 행으로 공격이 시작됐다 - 쿨다운이 여기서 걸린다
	Coordinator->ReleaseAttackToken(Combat);
	TestTrue(TEXT("token released: the phase default (the plan was forgotten)"),
		Boss->GetCurrentAttackInputTag() == VBGameplayTags::Input_Attack_Light);
	if (!TestTrue(TEXT("token granted again"), Coordinator->TryAcquireAttackToken(Combat, 1)))
	{
		return false;
	}

	TestTrue(TEXT("row cooling after its attack started: default even with the target behind"),
		Boss->GetCurrentAttackInputTag() == VBGameplayTags::Input_Attack_Light);
	Coordinator->ReleaseAttackToken(Combat);
	// 고르기만 하고 공격이 시작되지 않은 행(회수 등)은 쿨다운을 쓰지 않는다.
	FVBBossAttackRow& Unused = Config->FirstPhaseRows.AddDefaulted_GetRef();
	Unused = Config->FirstPhaseRows[0];
	Unused.CooldownSeconds = 30.0f;
	if (!TestTrue(TEXT("token granted for the second row"), Coordinator->TryAcquireAttackToken(Combat, 1)))
	{
		return false;
	}

	TestTrue(TEXT("second row chosen"), Boss->GetCurrentAttackInputTag() == VBGameplayTags::Input_Attack_Armored);
	Coordinator->ReleaseAttackToken(Combat);
	if (!TestTrue(TEXT("token granted again for the unused row"), Coordinator->TryAcquireAttackToken(Combat, 1)))
	{
		return false;
	}

	TestTrue(TEXT("revoked before its attack started: the row is still free"),
		Boss->GetCurrentAttackInputTag() == VBGameplayTags::Input_Attack_Armored);
	Coordinator->ReleaseAttackToken(Combat);
	Boss->ChooseNextAttack();
	TestTrue(TEXT("a row held without a token is never used"), Boss->GetCurrentAttackInputTag() == VBGameplayTags::Input_Attack_Light);
	Combat->LeaveCombat();
	return true;
}

#endif
