// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "VBAirComboTestAbility.h"
#include "VBWeaponDefinitionTestUtil.h"
#include "AbilitySystem/Abilities/VBAirStagingLease.h"
#include "AbilitySystem/Abilities/VBAirSuspension.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitImpact.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/VBANS_AttackTraceWindow.h"
#include "Animation/VBANS_ComboWindow.h"
#include "Animation/VBANS_WeaponTrail.h"
#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "Character/VBAirAfterimageComponent.h"
#include "CharacterCommon/VBMeshRefresh.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Data/VBAirAfterimageConfig.h"
#include "Data/VBBodyFadeConfig.h"
#include "Data/VBCombatFeedbackProfile.h"
#include "Data/VBAirComboConfig.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBHitReactConfig.h"
#include "Data/VBSwingCombatProfile.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace VBAirComboTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	UVBAirComboConfig* MakeComplete(UObject* Outer)
	{
		UVBAirComboConfig* Config = NewObject<UVBAirComboConfig>(Outer);
		Config->ChaseTakeoffMontage = NewObject<UAnimMontage>(Config);
		Config->ChaseMontage = NewObject<UAnimMontage>(Config);
		Config->StringFollowTimeMapping = NewObject<UCurveFloat>(Config);
		Config->StringFollowTimeMapping->FloatCurve.AddKey(0.0f, 1.0f);
		Config->StringFollowTimeMapping->FloatCurve.AddKey(1.0f, 1.0f);
		Config->StringAttack = NewObject<UVBAttackConfig>(Config);
		Config->StringAttack->AttackMontage = NewObject<UAnimMontage>(Config);
		Config->StringAttack->MaxComboCount = 4;
		Config->StringAttack->ComboSectionNames = {TEXT("Air1"), TEXT("Air2"), TEXT("Air3"), TEXT("Air4")};
		return Config;
	}

	struct FWorld
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		bool Create(FAutomationTestBase& Test)
		{
			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->InitializeActorsForPlay(FURL());
			return true;
		}

		~FWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				Package->SetDirtyFlag(false);
			}
		}

		UAbilitySystemComponent* GiveASC(AActor* Owner) const
		{
			UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Owner);
			Owner->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			ASC->InitAbilityActorInfo(Owner, Owner);
			return ASC;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirComboConfigTest, "VB.Unit.AirCombo.Config", VBAirComboTest::Flags)
bool FVBAirComboConfigTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBAirComboConfig> Config(VBAirComboTest::MakeComplete(GetTransientPackage()));
	TestEqual(TEXT("complete config has no problems"), Config->FindProblems().Num(), 0);
	UCurveFloat* FollowCurve = Config->StringFollowTimeMapping;
	Config->StringFollowTimeMapping = nullptr;
	TestEqual(TEXT("missing follow mapping fails instead of silently freezing spacing"), Config->FindProblems().Num(), 1);
	Config->StringFollowTimeMapping = FollowCurve;
	FollowCurve->FloatCurve.Reset();
	FollowCurve->FloatCurve.AddKey(0.0f, 0.0f);
	FollowCurve->FloatCurve.AddKey(1.0f, 1.0f);
	TestEqual(TEXT("linear mapping cannot maintain target spacing"), Config->FindProblems().Num(), 1);
	FollowCurve->FloatCurve.Reset();
	FollowCurve->FloatCurve.AddKey(0.0f, 1.0f);
	FollowCurve->FloatCurve.AddKey(1.0f, 1.0f);
	Config->ChaseMontage = nullptr;
	TestEqual(TEXT("missing chase montage is one problem"), Config->FindProblems().Num(), 1);
	Config->ChaseMontage = NewObject<UAnimMontage>(Config.Get());
	Config->StringAttack->ComboSectionNames.Pop();
	TestEqual(TEXT("section count off by one is one problem"), Config->FindProblems().Num(), 1);
	Config->StringAttack->ComboSectionNames.Add(TEXT("Air4"));
	Config->ChaseSeconds = Config->AttackerMaxAirSeconds;
	TestEqual(TEXT("chase as long as the air limit is one problem"), Config->FindProblems().Num(), 1);
	Config->ChaseSeconds = 0.35f;
	Config->ChaseArrivalTolerance = 0.0f;
	TestEqual(TEXT("zero arrival tolerance is one problem"), Config->FindProblems().Num(), 1);
	TestFalse(TEXT("invalid tolerance cannot accept an arrival"), Config->HasReachedChaseTarget(FVector::ZeroVector, FVector::ZeroVector));
	Config->ChaseArrivalTolerance = 20.0f;
	TestTrue(TEXT("arrival inside tolerance"), Config->HasReachedChaseTarget(FVector(19.0f, 0.0f, 0.0f), FVector::ZeroVector));
	TestFalse(TEXT("obstruction outside tolerance cannot start string"),
		Config->HasReachedChaseTarget(FVector(0.0f, 0.0f, 21.0f), FVector::ZeroVector));
	Config->StringAttack->MontageVariants.AddDefaulted();
	TestEqual(TEXT("a string with montage variants is one problem"), Config->FindProblems().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirSlamConfigTest, "VB.Unit.AirCombo.SlamConfig", VBAirComboTest::Flags)
bool FVBAirSlamConfigTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBAirComboConfig> Config(VBAirComboTest::MakeComplete(GetTransientPackage()));
	UAnimMontage* Montage = Config->StringAttack->AttackMontage;
	for (const TCHAR* Name : {TEXT("Air1"), TEXT("Air2"), TEXT("Air3"), TEXT("Air4"), TEXT("SlamStart"), TEXT("SlamLoop")})
	{
		FCompositeSection& Section = Montage->CompositeSections.AddDefaulted_GetRef();
		Section.SectionName = Name;
	}

	TestFalse(TEXT("no slam sections = no slam"), Config->HasSlam());
	Config->SlamStartSection = TEXT("SlamStart");
	Config->SlamLoopSection = TEXT("SlamLoop");
	Config->SlamEndSection = TEXT("SlamEnd");
	TestEqual(TEXT("missing end section is one problem"), Config->FindProblems().Num(), 1);
	Montage->CompositeSections.AddDefaulted_GetRef().SectionName = TEXT("SlamEnd");
	TestEqual(TEXT("all slam sections present"), Config->FindProblems().Num(), 0);
	Montage->CompositeSections[3].NextSectionName = TEXT("SlamStart");
	TestEqual(TEXT("last air section linked into the slam is one problem"), Config->FindProblems().Num(), 1);
	Montage->CompositeSections[3].NextSectionName = NAME_None;
	TestTrue(TEXT("terminal landing section has finite recovery"), Config->HasFiniteSlamRecovery());
	Montage->CompositeSections.Last().NextSectionName = TEXT("SlamEnd");
	TestFalse(TEXT("self-looping landing is not finite"), Config->HasFiniteSlamRecovery());
	TestEqual(TEXT("looping landing is one problem"), Config->FindProblems().Num(), 1);
	Montage->CompositeSections.Last().NextSectionName = NAME_None;
	Montage->bEnableAutoBlendOut = false;
	TestFalse(TEXT("landing without automatic completion is not finite"), Config->HasFiniteSlamRecovery());
	TestEqual(TEXT("disabled automatic completion is one problem"), Config->FindProblems().Num(), 1);
	Montage->bEnableAutoBlendOut = true;
	Config->SlamFallSpeed = 0.0f;
	TestFalse(TEXT("zero descent is rejected by runtime motion policy"), Config->IsSlamMotionValid());
	TestEqual(TEXT("zero slam descent cannot synchronize landing"), Config->FindProblems().Num(), 1);
	Config->SlamFallSpeed = 1500.0f;
	TestTrue(TEXT("finite descent and deadline are valid"), Config->IsSlamMotionValid());
	Config->bSlamOnFinalHit = true;
	TestEqual(TEXT("automatic finish requires a ground impact cue"), Config->FindProblems().Num(), 1);
	Config->SlamImpactCue = VBGameplayTags::GameplayCue_Combat_AirSlam;
	TestEqual(TEXT("automatic finish has explicit movement and impact data"), Config->FindProblems().Num(), 0);
	Config->SlamMaxSeconds = 0.0f;
	TestFalse(TEXT("zero deadline is rejected by runtime motion policy"), Config->IsSlamMotionValid());
	TestEqual(TEXT("slam requires its own finite landing deadline"), Config->FindProblems().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirBudgetTest, "VB.Unit.AirCombo.VictimAirBudget", VBAirComboTest::Flags)
bool FVBAirBudgetTest::RunTest(const FString& Parameters)
{
	FVBKnockdownSet Set;
	Set.MaxAirSeconds = 2.5f;
	TestEqual(TEXT("no extension keeps the budget"), Set.ExtendAirBudget(2.5f), 2.5f);
	Set.AirHitExtendSeconds = 0.4f;
	Set.MaxAirTotalSeconds = 3.5f;
	TestEqual(TEXT("one air hit extends"), Set.ExtendAirBudget(2.5f), 2.9f);
	TestEqual(TEXT("extension stops at the total cap"), Set.ExtendAirBudget(3.4f), 3.5f);
	Set.MaxAirTotalSeconds = 1.0f;
	TestEqual(TEXT("a cap below MaxAirSeconds never shortens it"), Set.ExtendAirBudget(2.5f), 2.5f);
	return true;
}

// 피해자의 공중 정지. 붙잡기는 한 번, 빌린 중력은 빌린 이동 컴포넌트로 돌아가고, 남이 바꾼 중력은 덮지 않는다.
//   대조: Release 의 GravityScale == 0 조건을 빼면 foreign gravity 줄이, FreezeIfHeld 의 상승 조건을 빼면 still rising 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirSuspensionTest, "VB.Unit.AirCombo.Suspension", VBAirComboTest::Flags)
bool FVBAirSuspensionTest::RunTest(const FString& Parameters)
{
	using namespace VBAirComboTest;
	FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}

	ACharacter* Character = W.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("character"), Character))
	{
		return false;
	}

	UCharacterMovementComponent* Move = Character->GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);
	Move->GravityScale = 2.0f;

	FVBAirSuspension Air;
	Air.Begin(2.5f);
	TestFalse(TEXT("a non-positive hold is refused"), Air.TryHold(0.0f));
	TestFalse(TEXT("an infinite hold is refused"), Air.TryHold(std::numeric_limits<float>::infinity()));
	TestFalse(TEXT("refused holds do not hold"), Air.IsHeld());
	Air.FreezeIfHeld(*Move, /*bFalling=*/true);
	TestFalse(TEXT("no hold: gravity stays"), Air.IsGravityBorrowed());
	TestTrue(TEXT("first hold is accepted"), Air.TryHold(4.0f));
	TestEqual(TEXT("hold grows the budget"), Air.GetBudgetSeconds(), 4.0f);
	TestFalse(TEXT("second hold of one launch is ignored"), Air.TryHold(9.0f));
	TestEqual(TEXT("ignored hold keeps the budget"), Air.GetBudgetSeconds(), 4.0f);

	Move->Velocity = FVector(0.0, 0.0, 100.0);
	Air.FreezeIfHeld(*Move, /*bFalling=*/true);
	TestFalse(TEXT("still rising: gravity stays"), Air.IsGravityBorrowed());
	Move->Velocity = FVector(0.0, 0.0, -50.0);
	Air.FreezeIfHeld(*Move, /*bFalling=*/false);
	TestFalse(TEXT("not falling: gravity stays"), Air.IsGravityBorrowed());
	Air.FreezeIfHeld(*Move, /*bFalling=*/true);
	TestTrue(TEXT("held and coming down: gravity is borrowed"), Air.IsGravityBorrowed());
	TestEqual(TEXT("frozen gravity"), Move->GravityScale, 0.0f);
	TestEqual(TEXT("frozen fall"), Move->Velocity.Z, 0.0);
	Air.Release();
	TestEqual(TEXT("release returns the borrowed gravity"), Move->GravityScale, 2.0f);
	TestFalse(TEXT("release ends the hold"), Air.IsHeld());

	Air.TryHold(1.0f);
	Move->Velocity = FVector(0.0, 0.0, -50.0);
	Air.FreezeIfHeld(*Move, /*bFalling=*/true);
	Move->GravityScale = 3.0f;
	Air.Release();
	TestEqual(TEXT("foreign gravity is not overwritten"), Move->GravityScale, 3.0f);
	Air.Release();
	Move->GravityScale = 2.0f;

	Air.Begin(1.0f);
	TestFalse(TEXT("slam needs a positive speed"), Air.CanSlam(0.0f, 1.0f));
	TestFalse(TEXT("slam needs a finite speed"), Air.CanSlam(std::numeric_limits<float>::infinity(), 1.0f));
	TestFalse(TEXT("slam needs a finite landing limit"), Air.CanSlam(1200.0f, std::numeric_limits<float>::infinity()));
	TestTrue(TEXT("slam allowed"), Air.CanSlam(1200.0f, 3.0f));
	Air.TryHold(1.0f);
	Move->Velocity = FVector(0.0, 0.0, -50.0);
	Air.FreezeIfHeld(*Move, /*bFalling=*/true);
	Air.Slam(*Move, 1200.0f, 3.0f);
	TestEqual(TEXT("slam pushes down"), Move->Velocity.Z, -1200.0);
	TestEqual(TEXT("slam returns gravity"), Move->GravityScale, 2.0f);
	TestEqual(TEXT("slam reserves its landing time"), Air.GetBudgetSeconds(), 3.0f);
	TestFalse(TEXT("slam ends the hold"), Air.IsHeld());

	FVBKnockdownSet Set;
	Set.MaxAirSeconds = 2.5f;
	Set.AirHitExtendSeconds = 0.4f;
	Set.MaxAirTotalSeconds = 3.5f;
	Set.AirHitHoldUpSpeed = 300.0f;
	Air.Begin(2.5f);
	Move->Velocity = FVector(0.0, 0.0, -100.0);
	TestTrue(TEXT("air hit answers"), Air.HoldUpAfterHit(Set, Move));
	TestEqual(TEXT("air hit extends the budget"), Air.GetBudgetSeconds(), 2.9f);
	TestEqual(TEXT("air hit holds the body up"), Move->Velocity.Z, 300.0);
	Air.TryHold(0.1f);
	TestFalse(TEXT("a held victim is not pushed by air hits"), Air.HoldUpAfterHit(Set, Move));
	TestEqual(TEXT("a held victim keeps its budget"), Air.GetBudgetSeconds(), 2.9f);
	Air.Release();
	return true;
}

// 체공 감시의 네 결론. 대조: bLeftGround 를 안 세우면 landed 줄이 never lifted 로 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirWatchTest, "VB.Unit.AirCombo.SuspensionWatch", VBAirComboTest::Flags)
bool FVBAirWatchTest::RunTest(const FString& Parameters)
{
	constexpr float Grace = 0.25f;
	{
		FVBAirSuspension Air;
		Air.Begin(2.5f);
		TestTrue(TEXT("on the ground inside the grace: wait"), Air.AdvanceWatch(0.2f, false, false, Grace) == EVBAirWatchResult::Stay);
		TestTrue(TEXT("never lifted past the grace"), Air.AdvanceWatch(0.1f, false, false, Grace) == EVBAirWatchResult::NeverLeftGround);
	}
	{
		FVBAirSuspension Air;
		Air.Begin(2.5f);
		TestTrue(TEXT("in the air: wait"), Air.AdvanceWatch(0.05f, true, false, Grace) == EVBAirWatchResult::Stay);
		TestTrue(TEXT("landed"), Air.AdvanceWatch(0.05f, false, false, Grace) == EVBAirWatchResult::Landed);
		TestEqual(TEXT("clock follows the watch"), Air.GetElapsedSeconds(), 0.1f);
	}
	{
		FVBAirSuspension Air;
		Air.Begin(0.5f);
		TestTrue(TEXT("airborne inside the budget"), Air.AdvanceWatch(0.3f, true, false, Grace) == EVBAirWatchResult::Stay);
		TestTrue(TEXT("airborne past the budget"), Air.AdvanceWatch(0.3f, true, false, Grace) == EVBAirWatchResult::BudgetSpent);
	}
	{
		FVBAirSuspension Air;
		Air.Begin(1.0f);
		TestTrue(TEXT("slam descent"), Air.AdvanceWatch(0.1f, true, true, Grace) == EVBAirWatchResult::Stay);
		TestTrue(TEXT("slam reaction delays the landing"), Air.AdvanceWatch(0.1f, false, true, Grace) == EVBAirWatchResult::Stay);
		TestTrue(TEXT("the budget ends the delay"), Air.AdvanceWatch(1.0f, false, true, Grace) == EVBAirWatchResult::Landed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirOpeningTest, "VB.Unit.AirCombo.Opening", VBAirComboTest::Flags)
bool FVBAirOpeningTest::RunTest(const FString& Parameters)
{
	using namespace VBAirComboTest;
	FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}

	AActor* Attacker = W.World->SpawnActor<AActor>();
	ACharacter* Victim = W.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("attacker"), Attacker) || !TestNotNull(TEXT("victim"), Victim))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = W.GiveASC(Attacker);
	UAbilitySystemComponent* VictimASC = W.GiveASC(Victim);
	Victim->DispatchBeginPlay(); // 사라짐(EndPlay) 알림은 시작한 액터에만 온다

	UVBWeaponDefinition* Weapon = VBWeaponDefinitionTest::Make(Attacker, EVBWeaponType::Fighter);
	ASC->GiveAbility(FGameplayAbilitySpec(UVBAirComboTestAbility::StaticClass(), 1, INDEX_NONE, Weapon));
	FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(UVBAirComboTestAbility::StaticClass());
	UVBAirComboTestAbility* Air = Spec ? Cast<UVBAirComboTestAbility>(Spec->GetPrimaryInstance()) : nullptr;
	if (!TestNotNull(TEXT("instanced air combo"), Air))
	{
		return false;
	}

	const FGameplayTag Opening = VBGameplayTags::State_Combat_AirOpening;

	auto Launch = [&]()
	{
		FGameplayEventData Payload;
		Payload.EventTag = VBGameplayTags::Event_Combat_LaunchedTarget;
		Payload.Target = Victim;
		ASC->HandleGameplayEvent(Payload.EventTag, &Payload);
	};

	Launch();
	TestFalse(TEXT("weapon without an air combo opens nothing"), ASC->HasMatchingGameplayTag(Opening));

	VBWeaponDefinitionTest::SetField(*Weapon, TEXT("AirCombo"), TObjectPtr<UVBAirComboConfig>(MakeComplete(Weapon)));
	Launch();
	TestTrue(TEXT("own launch opens the chase"), ASC->HasMatchingGameplayTag(Opening) && Air->GetOpeningVictim() == Victim);
	Launch();
	TestEqual(TEXT("a second launch replaces, never stacks the tag"), ASC->GetTagCount(Opening), 1);

	Victim->Landed(FHitResult());
	TestFalse(TEXT("victim landing closes"), ASC->HasMatchingGameplayTag(Opening) || Air->IsOpeningOpen());

	Launch();
	VictimASC->AddLooseGameplayTag(VBGameplayTags::State_Dead);
	TestFalse(TEXT("victim death closes"), ASC->HasMatchingGameplayTag(Opening));
	VictimASC->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);

	// 거리 밖 누름은 거절이지 기회를 닫지 않는다(닫기는 쫓기가 실제로 시작할 때).
	Launch();
	const UVBAirComboConfig* AirCfg = Weapon->GetAirCombo();
	Victim->SetActorLocation(Attacker->GetActorLocation() + FVector(AirCfg->MaxChaseDistance + 100.0f, 0.0f, 0.0f));
	TestNotNull(TEXT("too far is a refusal"), UVBGA_AirCombo::FindChaseRefusal(AirCfg, Attacker, Victim));
	TestTrue(TEXT("a refusal leaves the opening up"), ASC->HasMatchingGameplayTag(Opening));
	Victim->SetActorLocation(Attacker->GetActorLocation() + FVector(100.0f, 0.0f, 0.0f));
	TestNull(TEXT("in reach and alive: chase allowed"), UVBGA_AirCombo::FindChaseRefusal(AirCfg, Attacker, Victim));

	ASC->ClearAbility(Spec->Handle);
	TestFalse(TEXT("weapon removal closes"), ASC->HasMatchingGameplayTag(Opening));

	// 대상이 사라지면(디스폰, 파괴) 기회가 남지 않는다 - 남으면 경공격만 막힌다.
	const FGameplayAbilitySpecHandle Again = ASC->GiveAbility(FGameplayAbilitySpec(UVBAirComboTestAbility::StaticClass(), 1,
		INDEX_NONE, Weapon));
	Launch();
	TestTrue(TEXT("re-granted ability opens again"), ASC->HasMatchingGameplayTag(Opening));
	Victim->Destroy();
	TestFalse(TEXT("victim gone closes"), ASC->HasMatchingGameplayTag(Opening));
	ASC->ClearAbility(Again);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirReleaseTest, "VB.Unit.AirCombo.ReleaseAirMovement", VBAirComboTest::Flags)
bool FVBAirReleaseTest::RunTest(const FString& Parameters)
{
	using namespace VBAirComboTest;
	FWorld W;
	if (!W.Create(*this))
	{
		return false;
	}

	ACharacter* Character = W.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("character"), Character))
	{
		return false;
	}

	UCharacterMovementComponent* Move = Character->GetCharacterMovement();

	Move->SetMovementMode(MOVE_Flying);
	UVBGA_AirCombo::ReleaseAirMovement(Character, /*bWeSetFlying=*/false);
	TestEqual(TEXT("flying set by someone else stays"), Move->MovementMode.GetValue(), MOVE_Flying);
	UVBGA_AirCombo::ReleaseAirMovement(Character, /*bWeSetFlying=*/true);
	TestEqual(TEXT("our flying falls on any end"), Move->MovementMode.GetValue(), MOVE_Falling);
	Move->SetMovementMode(MOVE_Walking);
	UVBGA_AirCombo::ReleaseAirMovement(Character, /*bWeSetFlying=*/true);
	TestEqual(TEXT("already grounded is left alone"), Move->MovementMode.GetValue(), MOVE_Walking);
	UVBGA_AirCombo::ReleaseAirMovement(nullptr, true);
	return true;
}

// Fighter 런처는 대상을 수평으로 보내지 않고 추격할 높이로 띄운다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFighterLauncherDataTest, "VB.Unit.AirCombo.AuthoredFighterLauncher", VBAirComboTest::Flags)
bool FVBFighterLauncherDataTest::RunTest(const FString& Parameters)
{
	const UVBSwingCombatProfile* Launcher = LoadObject<UVBSwingCombatProfile>(nullptr,
		TEXT("/Game/VB/Data/SwingCombat/DA_SwingCombat_FighterLauncher.DA_SwingCombat_FighterLauncher"));
	const UVBAttackConfig* Light = LoadObject<UVBAttackConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_Attack_Fighter_Light.DA_Attack_Fighter_Light"));
	if (!TestNotNull(TEXT("authored launcher"), Launcher) || !TestNotNull(TEXT("authored attack"), Light))
	{
		return false;
	}

	TestTrue(TEXT("launcher explicitly declares Launch"), Launcher->bOverrideHitImpact && Launcher->HitImpact == EVBHitImpact::Launch);
	TestEqual(TEXT("old horizontal scale two fails this contact contract"), Launcher->Scales.KnockbackStrengthScale, 0.0f);
	TestNearlyEqual(TEXT("authored launch height remains 200 cm"), Launcher->LaunchHeightOverride, 200.0f, 0.01f);
	const FVBHitMovement Movement = VBResolveHitMovement(FVector::ForwardVector,
		Light->KnockbackStrength * Launcher->Scales.KnockbackStrengthScale, false, Launcher->LaunchHeightOverride, 980.0f);
	TestTrue(TEXT("zero horizontal push still produces a physical vertical launch"), Movement.Mode == EVBHitMovement::Launch
		&& FMath::IsNearlyZero(Movement.Velocity.SizeSquared2D()) && Movement.Velocity.Z > 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFighterChaseDataTest, "VB.Unit.AirCombo.AuthoredFighterChase", VBAirComboTest::Flags)
bool FVBFighterChaseDataTest::RunTest(const FString& Parameters)
{
	const UVBAirComboConfig* Config = LoadObject<UVBAirComboConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_AirCombo_Fighter.DA_AirCombo_Fighter"));
	if (!TestNotNull(TEXT("authored Fighter air config"), Config))
	{
		return false;
	}

	TestTrue(TEXT("old 0.35 second chase fails immediate approach"), Config->ChaseSeconds > 0.0f && Config->ChaseSeconds <= 0.15f);
	TestNearlyEqual(TEXT("attacker and victim stay at equal height"), Config->ChaseHeightOffset, 0.0f, 0.01f);
	TestTrue(TEXT("authored arrival tolerance is valid"), Config->HasReachedChaseTarget(FVector::ZeroVector, FVector::ZeroVector));
	TestTrue(TEXT("landing recovery finishes without airborne timeout"), Config->HasFiniteSlamRecovery());
	const UAnimMontage* Montage = Config->StringAttack ? Config->StringAttack->AttackMontage.Get() : nullptr;
	if (Montage)
	{
		float StringSeconds = 0.0f;
		for (const FName Name : Config->StringAttack->ComboSectionNames)
		{
			const int32 Index = Montage->GetSectionIndex(Name);
			if (TestTrue(TEXT("authored string section exists"), Index != INDEX_NONE))
			{
				StringSeconds += Montage->GetSectionLength(Index);
			}
		}

		const float Rate = Config->StringAttack->MontagePlayRateScale;
		TestTrue(TEXT("air budget covers the complete string at its authored tempo"), Rate > 0.0f
			&& Config->AttackerMaxAirSeconds >= Config->ChaseSeconds + StringSeconds / Rate);
	}

	TestEqual(TEXT("authored air config is valid"), Config->FindProblems().Num(), 0);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFighterAirRootOwnershipTest, "VB.Unit.AirCombo.AuthoredRootOwnership", VBAirComboTest::Flags)
bool FVBFighterAirRootOwnershipTest::RunTest(const FString& Parameters)
{
	const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/VB/Animations/Montage/AM_Fighter_AirCombo"));
	if (!TestNotNull(TEXT("Fighter air montage"), Montage))
	{
		return false;
	}

	if (!TestEqual(TEXT("one authored slot"), Montage->SlotAnimTracks.Num(), 1))
	{
		return false;
	}

	const TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
	if (!TestTrue(TEXT("four string segments exist"), Segments.Num() >= 4))
	{
		return false;
	}

	for (int32 Index = 0; Index < 4; ++Index)
	{
		const UAnimSequence* Clip = Cast<UAnimSequence>(Segments[Index].GetAnimReference());
		if (!TestNotNull(TEXT("string segment is a sequence"), Clip))
		{
			continue;
		}

		TestFalse(*FString::Printf(TEXT("%s must not compete with air movement"), *Clip->GetName()), Clip->bEnableRootMotion);
		TestTrue(*FString::Printf(TEXT("%s must not drift inside the capsule"), *Clip->GetName()), Clip->bForceRootLock);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFighterAirStrikeTest, "VB.Unit.AirCombo.AuthoredStrikeWindows", VBAirComboTest::Flags)
bool FVBFighterAirStrikeTest::RunTest(const FString& Parameters)
{
	const UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/VB/Animations/Montage/AM_Fighter_AirCombo"));
	if (!TestNotNull(TEXT("Fighter air montage"), Montage))
	{
		return false;
	}

	struct FStrike
	{
		FName Section;
		FGameplayTag Shape;
		float Start;
		float Duration;
		int32 ComboWindows;
	};
	const FStrike Strikes[] = {
		{TEXT("Default"), VBGameplayTags::Melee_Shape_FootR, 0.18f, 0.12f, 1},
		{TEXT("Air2"), VBGameplayTags::Melee_Shape_FootL, 0.12f, 0.12f, 1},
		{TEXT("Air3"), VBGameplayTags::Melee_Shape_HandR, 0.14f, 0.18f, 1},
		{TEXT("Air4"), VBGameplayTags::Melee_Shape_HandR, 0.28f, 0.30f, 0}};
	for (const FStrike& Strike : Strikes)
	{
		const int32 Section = Montage->GetSectionIndex(Strike.Section);
		if (!TestTrue(TEXT("authored string section exists"), Section != INDEX_NONE))
		{
			continue;
		}

		float Start = 0.0f, End = 0.0f;
		Montage->GetSectionStartAndEndTime(Section, Start, End);
		int32 Hits = 0, Trails = 0, Combos = 0;
		for (const FAnimNotifyEvent& Event : Montage->Notifies)
		{
			if (Event.GetTime() < Start || Event.GetTime() >= End)
			{
				continue;
			}

			if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UVBANS_ComboWindow>())
			{
				++Combos;
				TestTrue(TEXT("combo window closes after the strike"),
					Event.GetTime() + Event.GetDuration() >= Start + Strike.Start + Strike.Duration);
				TestTrue(TEXT("combo window closes inside its section"), Event.GetTime() + Event.GetDuration() < End);
				continue;
			}

			const UVBANS_AttackTraceWindow* Hit = Cast<UVBANS_AttackTraceWindow>(Event.NotifyStateClass);
			const UVBANS_WeaponTrail* Trail = Cast<UVBANS_WeaponTrail>(Event.NotifyStateClass);
			const FGameplayTagContainer* Shapes = Hit ? &Hit->Shapes : Trail ? &Trail->Shapes : nullptr;
			if (!Shapes)
			{
				continue;
			}

			Hits += Hit != nullptr;
			Trails += Trail != nullptr;
			const FString Label = Strike.Section.ToString() + (Hit ? TEXT(" hit") : TEXT(" trail"));
			TestTrue(*(Label + TEXT(" follows the striking limb")), Shapes->Num() == 1 && Shapes->HasTagExact(Strike.Shape));
			TestNearlyEqual(*(Label + TEXT(" measured start")), Event.GetTime() - Start, Strike.Start - (Trail ? 0.02f : 0.0f), 0.001f);
			TestNearlyEqual(*(Label + TEXT(" measured duration")), Event.GetDuration(), Strike.Duration, 0.001f);
		}

		TestEqual(TEXT("one damage window per strike"), Hits, 1);
		TestEqual(TEXT("one trail per strike"), Trails, 1);
		TestEqual(*FString::Printf(TEXT("%s preserves its combo decision windows"), *Strike.Section.ToString()), Combos,
			Strike.ComboWindows);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFighterSlamReactionTest, "VB.Unit.AirCombo.AuthoredSlamReaction", VBAirComboTest::Flags)
bool FVBFighterSlamReactionTest::RunTest(const FString& Parameters)
{
	const UVBAirComboConfig* Air = LoadObject<UVBAirComboConfig>(nullptr, TEXT("/Game/VB/Data/DA_AirCombo_Fighter"));
	if (!TestNotNull(TEXT("Fighter air config"), Air))
	{
		return false;
	}

	TestTrue(TEXT("final accepted hit drops the victim"), Air->bSlamOnFinalHit);
	TestEqual(TEXT("authored final-hit config is valid"), Air->FindProblems().Num(), 0);
	TestTrue(TEXT("final impact has an explicit cue"), Air->SlamImpactCue.IsValid());
	const UAnimMontage* Reaction = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/VB/Animations/Montage/Enemy/AM_Enemy_AirSlam_Blade"));
	if (!TestNotNull(TEXT("authored victim reaction"), Reaction))
	{
		return false;
	}

	for (const TCHAR* Name : {TEXT("DA_HitReact_Soldier"), TEXT("DA_HitReact_Default")})
	{
		const UVBHitReactConfig* Victim = LoadObject<UVBHitReactConfig>(nullptr, *FString::Printf(TEXT("/Game/VB/Data/%s"), Name));
		if (TestNotNull(Name, Victim))
		{
			TestTrue(TEXT("victim owns its exact slam reaction"), Victim->KnockdownSet.AirSlamMontage == Reaction);
		}
	}

	if (!TestTrue(TEXT("reaction has one sequence segment"), Reaction->SlotAnimTracks.Num() == 1
		&& Reaction->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 1))
	{
		return false;
	}

	const UAnimSequence* Clip = Cast<UAnimSequence>(Reaction->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference());
	if (!TestNotNull(TEXT("reaction sequence"), Clip))
	{
		return false;
	}

	TestEqual(TEXT("requested source motion is retained"), Clip->GetFName(), FName(TEXT("M_katana_Blade_KnockDown_Front_Damage_Manny")));
	TestFalse(TEXT("CMC owns reaction descent"), Clip->bEnableRootMotion);
	TestTrue(TEXT("reaction pose does not drift from its capsule"), Clip->bForceRootLock);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFighterAfterimageDataTest, "VB.Unit.AirCombo.AuthoredAfterimage", VBAirComboTest::Flags)
bool FVBFighterAfterimageDataTest::RunTest(const FString& Parameters)
{
	const UVBAirComboConfig* Air = LoadObject<UVBAirComboConfig>(nullptr, TEXT("/Game/VB/Data/DA_AirCombo_Fighter"));
	if (!TestNotNull(TEXT("Fighter air config"), Air) || !TestNotNull(TEXT("authored chase staging"), Air->Staging.Get()))
	{
		return false;
	}

	TestNotNull(TEXT("authored ghost material"), Air->Staging->GhostMaterial.Get());
	TestEqual(TEXT("material and lifetime contract"), Air->Staging->FindProblems().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirAfterimageOpacityTest, "VB.Unit.AirCombo.AfterimageOpacity", VBAirComboTest::Flags)
bool FVBAirAfterimageOpacityTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBAirAfterimageConfig> Config(NewObject<UVBAirAfterimageConfig>());
	TestNearlyEqual(TEXT("initial opacity"), Config->OpacityAt(0.0f), 0.3f);
	TestNearlyEqual(TEXT("half-life fades once"), Config->OpacityAt(Config->GhostLifetime * 0.5f), 0.15f);
	TestEqual(TEXT("expired ghost is transparent"), Config->OpacityAt(Config->GhostLifetime), 0.0f);
	TestEqual(TEXT("negative age is inactive"), Config->OpacityAt(-1.0f), 0.0f);
	TestEqual(TEXT("nonfinite age never reaches the material"), Config->OpacityAt(std::numeric_limits<float>::quiet_NaN()), 0.0f);
	Config->GhostLifetime = 0.0f;
	TestEqual(TEXT("zero lifetime avoids division"), Config->OpacityAt(0.0f), 0.0f);
	Config->GhostLifetime = 0.22f;
	Config->OpacityCurve.GetRichCurve()->AddKey(0.0f, 1.0f);
	Config->OpacityCurve.GetRichCurve()->AddKey(0.5f, 0.2f);
	Config->OpacityCurve.GetRichCurve()->AddKey(1.0f, 0.0f);
	TestNearlyEqual(TEXT("authored envelope multiplies the starting opacity"), Config->OpacityAt(0.11f), 0.06f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirAfterimageConfigTest, "VB.Unit.AirCombo.AfterimageConfig", VBAirComboTest::Flags)
bool FVBAirAfterimageConfigTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBAirAfterimageConfig> Config(NewObject<UVBAirAfterimageConfig>());
	TestEqual(TEXT("optional visual layers may be empty"), Config->FindProblems().Num(), 0);
	Config->MaxGhosts = 0;
	TestEqual(TEXT("empty-pool indexing is rejected by its data owner"), Config->FindProblems().Num(), 1);
	Config->MaxGhosts = 3;
	Config->MaxPartsPerGhost = 16;
	TestEqual(TEXT("combined mesh budget is bounded"), Config->FindProblems().Num(), 1);
	Config->MaxPartsPerGhost = 8;
	Config->GhostLifetime = std::numeric_limits<float>::quiet_NaN();
	TestEqual(TEXT("nonfinite lifetime cannot leave a live ghost forever"), Config->FindProblems().Num(), 1);
	Config->GhostLifetime = 0.22f;
	Config->MinSampleDistance = 0.0f;
	TestEqual(TEXT("stationary sampling is not allowed"), Config->FindProblems().Num(), 1);
	Config->MinSampleDistance = 20.0f;
	UVBCombatFeedbackProfile* Feedback = NewObject<UVBCombatFeedbackProfile>(Config.Get());
	Config->WhooshProfile = Feedback;
	Feedback->bScreenImpact = false;
	TestEqual(TEXT("a screen-only effect must enable its screen layer"), Config->FindProblems().Num(), 1);
	Feedback->bScreenImpact = true;
	Feedback->SlowMotionSteps.AddDefaulted();
	TestEqual(TEXT("chase feedback cannot introduce a second time owner"), Config->FindProblems().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirAfterimageLifecycleTest, "VB.Unit.AirCombo.AfterimageLifecycle", VBAirComboTest::Flags)
bool FVBAirAfterimageLifecycleTest::RunTest(const FString& Parameters)
{
	VBAirComboTest::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}

	const UVBAirAfterimageConfig* Authored = LoadObject<UVBAirAfterimageConfig>(nullptr,
		TEXT("/Game/VB/Data/DA_AirAfterimage_Fighter"));
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("authored ghost config"), Authored) || !TestNotNull(TEXT("skeletal fixture"), Asset))
	{
		return false;
	}

	const TStrongObjectPtr<UVBAirAfterimageConfig> Config(DuplicateObject(Authored, GetTransientPackage()));
	Config->MaxGhosts = 2;
	AActor* Owner = World.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("visual owner"), Owner))
	{
		return false;
	}

	USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Owner);
	Owner->AddInstanceComponent(Body);
	Owner->SetRootComponent(Body);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetSkeletalMeshAsset(Asset);
	Body->RegisterComponent();
	Body->RefreshBoneTransforms();
	UVBAirAfterimageComponent* Effect = UVBAirAfterimageComponent::FindOrAdd(Owner);
	if (!TestNotNull(TEXT("registered ghost owner"), Effect))
	{
		return false;
	}

	TestEqual(TEXT("repeated lookup reuses the owner"), UVBAirAfterimageComponent::FindOrAdd(Owner), Effect);
	const uint64 First = Effect->BeginSampling(Config.Get());
	const uint64 Current = Effect->BeginSampling(Config.Get());
	if (!TestTrue(TEXT("valid requests have distinct identities"), First != 0 && Current != 0 && First != Current))
	{
		return false;
	}

	Effect->StopSampling(First);
	TestTrue(TEXT("stale stop cannot cancel the current request"), Effect->IsSampling());
	TestEqual(TEXT("request replacement retains one pose refresh lease"), FVBMeshRefresh::GetRequestCount(Body), 1);
	const auto Sample = [&]()
	{
		Owner->AddActorWorldOffset(FVector(Config->MinSampleDistance + 1.0f, 0.0f, 0.0f));
		Effect->TickComponent(Config->SampleInterval, LEVELTICK_All, nullptr);
	};
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Sample();
	}

	TInlineComponentArray<UPoseableMeshComponent*> Snapshots(Owner);
	if (!TestEqual(TEXT("moving samples reuse the two-mesh pool"), Snapshots.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("live snapshots stay within budget"), Effect->GetLiveGhostCount(), 2);
	UMaterialInstanceDynamic* FirstMaterial = Cast<UMaterialInstanceDynamic>(Snapshots[0]->GetMaterial(0));
	UMaterialInstanceDynamic* SecondMaterial = Cast<UMaterialInstanceDynamic>(Snapshots[1]->GetMaterial(0));
	TestTrue(TEXT("snapshots own independent material parameters"), FirstMaterial && SecondMaterial && FirstMaterial != SecondMaterial);
	Effect->StopSampling(Current);
	TestFalse(TEXT("current stop ends sampling"), Effect->IsSampling());
	TestEqual(TEXT("sampling stop returns its pose refresh lease"), FVBMeshRefresh::GetRequestCount(Body), 0);
	TestTrue(TEXT("existing snapshots still need fade ticks"), Effect->IsComponentTickEnabled());
	Effect->TickComponent(Config->GhostLifetime, LEVELTICK_All, nullptr);
	TestEqual(TEXT("stop lets all snapshots expire"), Effect->GetLiveGhostCount(), 0);
	TestFalse(TEXT("idle effect stops ticking"), Effect->IsComponentTickEnabled());

	// 빌린 표: 거절(0)은 기억하지 않고, 돌려주기는 여러 번 불러도 안전하다.
	FVBAirStagingLease Lease;
	const uint64 Leased = Effect->BeginSampling(Config.Get());
	Lease.HoldAfterimage(*Effect, 0);
	Lease.Release();
	TestTrue(TEXT("a refused request is not stopped by the lease"), Effect->IsSampling());
	Lease.HoldAfterimage(*Effect, Leased);
	Lease.Release();
	TestFalse(TEXT("the lease stops the request it holds"), Effect->IsSampling());
	Lease.Release();

	TestTrue(TEXT("restart succeeds after fade"), Effect->BeginSampling(Config.Get()) != 0);
	Sample();
	Body->SetVisibility(false);
	Effect->TickComponent(Config->SampleInterval, LEVELTICK_All, nullptr);
	TestFalse(TEXT("disappearing source ends sampling"), Effect->IsSampling());
	TestEqual(TEXT("disappearing source removes snapshots"), Effect->GetLiveGhostCount(), 0);
	TestFalse(TEXT("disappearing source disables ticking"), Effect->IsComponentTickEnabled());
	for (UPoseableMeshComponent* Snapshot : Snapshots)
	{
		TestFalse(TEXT("retired snapshot components are unregistered"), Snapshot->IsRegistered());
	}

	Body->SetVisibility(true);
	TestTrue(TEXT("restored source can start a new request"), Effect->BeginSampling(Config.Get()) != 0);
	Sample();
	Effect->UnregisterComponent();
	TestFalse(TEXT("unregister cancels the request"), Effect->IsSampling());
	TestEqual(TEXT("unregister removes live snapshots"), Effect->GetLiveGhostCount(), 0);
	TestEqual(TEXT("unregister returns its pose refresh lease"), FVBMeshRefresh::GetRequestCount(Body), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBodyFadeEnvelopeTest, "VB.Unit.AirCombo.BodyFadeEnvelope", VBAirComboTest::Flags)
bool FVBBodyFadeEnvelopeTest::RunTest(const FString& Parameters)
{
	const TStrongObjectPtr<UVBBodyFadeConfig> Config(NewObject<UVBBodyFadeConfig>());
	TestEqual(TEXT("fade starts solid"), Config->CoverageAt(0.0f), 1.0f);
	TestNearlyEqual(TEXT("authored minimum at first quarter"), Config->CoverageAt(0.25f), 0.65f);
	TestEqual(TEXT("arrival is solid"), Config->CoverageAt(1.0f), 1.0f);
	TestEqual(TEXT("invalid progress remains visible"), Config->CoverageAt(std::numeric_limits<float>::quiet_NaN()), 1.0f);
	TestEqual(TEXT("missing material mapping is rejected"), Config->FindProblems().Num(), 1);
	Config->FadeInFraction = 0.0f;
	TestEqual(TEXT("zero fade interval is rejected"), Config->FindProblems().Num(), 2);
	TestEqual(TEXT("invalid envelope cannot hide the body"), Config->CoverageAt(0.25f), 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBBodyFadeOwnershipTest, "VB.Unit.AirCombo.BodyFadeOwnership", VBAirComboTest::Flags)
bool FVBBodyFadeOwnershipTest::RunTest(const FString& Parameters)
{
	VBAirComboTest::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}

	const UVBBodyFadeConfig* Config = LoadObject<UVBBodyFadeConfig>(nullptr, TEXT("/Game/VB/Data/DA_BodyFade_Fighter"));
	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/UEFN_Mannequin/Meshes/SKM_UEFN_Mannequin"));
	if (!TestNotNull(TEXT("authored fade mapping"), Config) || !TestNotNull(TEXT("actual body asset"), Asset))
	{
		return false;
	}

	if (!TestEqual(TEXT("authored fade config is valid"), Config->FindProblems().Num(), 0))
	{
		return false;
	}

	const TStrongObjectPtr<UVBBodyFadeConfig> OldBinding(DuplicateObject(Config, GetTransientPackage()));
	OldBinding->Materials[0].Faded = OldBinding->Materials[0].Faded->GetMaterial();
	const TArray<FText> OldProblems = OldBinding->FindProblems();
	TestEqual(TEXT("raw master cannot bypass effective blend validation"), OldProblems.Num(), 1);
	for (const FText& Problem : OldProblems)
	{
		AddInfo(Problem.ToString());
	}

	ACharacter* Owner = World.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("appearance character"), Owner))
	{
		return false;
	}

	USkeletalMeshComponent* Body = Owner->GetMesh();
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetSkeletalMeshAsset(Asset);
	UVBCharacterAppearanceComponent* Appearance = NewObject<UVBCharacterAppearanceComponent>(Owner);
	Owner->AddInstanceComponent(Appearance);
	Appearance->RegisterComponent();
	UMaterialInterface* Original = Body->GetMaterial(0);
	UMaterialInstanceDynamic* OriginalInstance = UMaterialInstanceDynamic::Create(Original, Body);
	if (!TestNotNull(TEXT("existing runtime body material"), OriginalInstance))
	{
		return false;
	}

	const FLinearColor Color(0.2f, 0.4f, 0.6f);
	OriginalInstance->SetVectorParameterValue(TEXT("Color"), Color);
	Body->SetMaterial(0, OriginalInstance);
	Body->SetOverlayMaterial(Original);
	const uint64 First = Appearance->BeginBodyFade(Config, 1.0f);
	const uint64 Current = Appearance->BeginBodyFade(Config, 1.0f);
	if (!TestTrue(TEXT("request replacement has a fresh identity"), First != 0 && Current != 0 && First != Current))
	{
		return false;
	}

	Appearance->EndBodyFade(First);
	TestTrue(TEXT("stale request cannot restore newer fade"), Appearance->IsBodyFadeActive());
	Appearance->TickComponent(0.25f, LEVELTICK_All, nullptr);
	UMaterialInstanceDynamic* Faded = Cast<UMaterialInstanceDynamic>(Body->GetMaterial(0));
	if (!TestNotNull(TEXT("fade owns a dynamic variant"), Faded))
	{
		return false;
	}

	float Coverage = 1.0f;
	Faded->GetScalarParameterValue(FHashedMaterialParameterInfo(UVBBodyFadeConfig::CoverageParameter), Coverage);
	TestNearlyEqual(TEXT("actual material reaches coverage minimum"), Coverage, Config->MinimumCoverage);
	FLinearColor CopiedColor;
	Faded->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Color")), CopiedColor);
	TestTrue(TEXT("existing appearance parameters survive replacement"), CopiedColor.Equals(Color));
	TestEqual(TEXT("hit-flash overlay is untouched"), Body->GetOverlayMaterial(), Original);
	Appearance->EndBodyFade(Current);
	TestEqual(TEXT("end restores the exact original instance"), Body->GetMaterial(0), static_cast<UMaterialInterface*>(OriginalInstance));
	TestFalse(TEXT("fade-only owner stops ticking"), Appearance->IsComponentTickEnabled());
	Appearance->BeginBodyFade(Config, 1.0f);
	Body->SetMaterial(0, Original);
	Appearance->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("external material replacement is not overwritten"), Body->GetMaterial(0), Original);
	TestFalse(TEXT("external replacement releases the request"), Appearance->IsBodyFadeActive());
	Appearance->BeginBodyFade(Config, 1.0f);
	Appearance->UnregisterComponent();
	TestEqual(TEXT("unregister restores body material"), Body->GetMaterial(0), Original);
	TestFalse(TEXT("unregister releases the fade"), Appearance->IsBodyFadeActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAirVisibleMeshesTest, "VB.Unit.AirCombo.VisibleMeshes", VBAirComboTest::Flags)
bool FVBAirVisibleMeshesTest::RunTest(const FString& Parameters)
{
	VBAirComboTest::FWorld World;
	if (!World.Create(*this))
	{
		return false;
	}

	USkeletalMesh* Asset = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/UE5_Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("actual skeletal mesh fixture"), Asset))
	{
		return false;
	}

	AActor* Owner = World.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("visual source owner"), Owner))
	{
		return false;
	}

	const auto MakeMesh = [Owner, Asset](FName Name)
	{
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(Owner, Name);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetSkeletalMeshAsset(Asset);
		Owner->AddInstanceComponent(Mesh);
		Mesh->RegisterComponent();
		return Mesh;
	};
	USkeletalMeshComponent* Body = MakeMesh(TEXT("Body"));
	USkeletalMeshComponent* Clothes = MakeMesh(TEXT("Clothes"));
	USkeletalMeshComponent* Hidden = MakeMesh(TEXT("HiddenDriver"));
	Hidden->SetHiddenInGame(true);
	TArray<USkeletalMeshComponent*> Visible;
	UVBCharacterAppearanceComponent::GatherVisibleMeshes(Owner, Visible);
	TestTrue(TEXT("visible body and clothing are included; hidden driver is excluded"), Visible.Num() == 2
		&& Visible.Contains(Body) && Visible.Contains(Clothes) && !Visible.Contains(Hidden));
	Clothes->SetVisibility(false);
	UVBCharacterAppearanceComponent::GatherVisibleMeshes(Owner, Visible);
	TestTrue(TEXT("hidden pieces disappear from the snapshot source"), Visible.Num() == 1 && Visible[0] == Body);
	Owner->SetActorHiddenInGame(true);
	UVBCharacterAppearanceComponent::GatherVisibleMeshes(Owner, Visible);
	TestEqual(TEXT("a hidden actor cannot produce visible snapshots"), Visible.Num(), 0);
	UVBCharacterAppearanceComponent::GatherVisibleMeshes(nullptr, Visible);
	TestEqual(TEXT("missing owner has no stale source"), Visible.Num(), 0);
	return true;
}
#endif
