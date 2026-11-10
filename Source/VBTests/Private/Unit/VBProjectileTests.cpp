// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitSpec.h"
#include "Combat/VBProjectile.h"
#include "Combat/VBProjectileConfig.h"
#include "Combat/VBProjectileRules.h"
#include "Data/VBEnemyConfig.h"
#include "Types/VBTeamTypes.h"
#include "VBCombatAuditTestAbility.h"

#if WITH_DEV_AUTOMATION_TESTS

// 이름 있는 네임스페이스 - 유니티 빌드가 여러 시험 파일을 한 번역 단위로 묶어도 이름이 부딪히지 않게.
namespace VBProjectileTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	// 모든 칸이 서로 다른 값 - 칸 하나가 다른 칸으로 잘못 들어가도 드러난다.
	FVBHitSpecValues DistinctValues()
	{
		FVBHitSpecValues Values;
		Values.Damage = 37.0f;
		Values.KnockbackStrength = 410.0f;
		Values.PoiseDamage = 22.0f;
		Values.HitImpact = EVBHitImpact::Heavy;
		Values.LaunchHeight = 75.0f;
		Values.PushYaw = 135.0f;
		Values.ImpactCue = VBGameplayTags::GameplayCue_Combat_SwingImpact;
		Values.ChargeSource = EVBCombatChargeSource::CinematicSkill;
		Values.AttackAnswer = VBGameplayTags::Attack_Answer_DodgeOnly;
		return Values;
	}

	FGameplayEffectSpec MakeBlankSpec()
	{
		return FGameplayEffectSpec(GetDefault<UGameplayEffect>(), FGameplayEffectContextHandle(new FVBGameplayEffectContext()), 1.0f);
	}

	// 근접 스윙(UVBGA_MeleeAttackBase::ApplySwingHits)이 지금 스펙을 채우는 줄을 그대로 옮긴 사본. 입력은 그 함수의 지역값 이름이다.
	struct FSwingInputs
	{
		float FinalDamage = 0.0f;
		float KnockbackYaw = 0.0f;
		bool bCinematicSkill = false;
		EVBHitImpact SwingImpact = EVBHitImpact::Light;
		float SwingLaunchHeight = 0.0f;
		// UVBSwingCombatProfile::ResolveImpactCue 를 거친 뒤의 태그.
		FGameplayTag ResolvedImpactCue;
		float KnockbackStrength = 0.0f;
		float KnockbackStrengthScale = 1.0f;
		float PoiseDamage = 0.0f;
		float PoiseDamageScale = 1.0f;
		FGameplayTag AttackAnswer;
	};

	void InlineSwingFill(FGameplayEffectSpecHandle& SpecHandle, const FSwingInputs& In, const FHitResult& Hit)
	{
		FGameplayEffectContextHandle ContextHandle = SpecHandle.Data.Get()->GetContext();
		ContextHandle.AddHitResult(Hit);
		FVBGameplayEffectContext* const HitFacts = FVBGameplayEffectContext::Get(ContextHandle);
		HitFacts->SetPushYaw(In.KnockbackYaw);
		HitFacts->SetChargeSource(In.bCinematicSkill ? EVBCombatChargeSource::CinematicSkill : EVBCombatChargeSource::OrdinaryAttack);
		HitFacts->SetDeclaredHitImpact(In.SwingImpact);
		HitFacts->SetLaunchHeight(In.SwingLaunchHeight);
		HitFacts->SetImpactCue(In.ResolvedImpactCue);
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, In.FinalDamage);
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(VBGameplayTags::Data_KnockbackStrength,
			In.KnockbackStrength * In.KnockbackStrengthScale);
		SpecHandle.Data.Get()->SetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, In.PoiseDamage * In.PoiseDamageScale);
		if (In.AttackAnswer.IsValid())
		{
			SpecHandle.Data.Get()->AddDynamicAssetTag(In.AttackAnswer);
		}
	}

	// 스윙이 VBHitSpec::Fill 로 옮겨 갈 때 쓸 값 사상. 배율은 부르는 쪽이 곱한다.
	FVBHitSpecValues ToValues(const FSwingInputs& In)
	{
		FVBHitSpecValues Values;
		Values.Damage = In.FinalDamage;
		Values.KnockbackStrength = In.KnockbackStrength * In.KnockbackStrengthScale;
		Values.PoiseDamage = In.PoiseDamage * In.PoiseDamageScale;
		Values.HitImpact = In.SwingImpact;
		Values.LaunchHeight = In.SwingLaunchHeight;
		Values.PushYaw = In.KnockbackYaw;
		Values.ImpactCue = In.ResolvedImpactCue;
		Values.ChargeSource = In.bCinematicSkill ? EVBCombatChargeSource::CinematicSkill : EVBCombatChargeSource::OrdinaryAttack;
		Values.AttackAnswer = In.AttackAnswer;
		return Values;
	}

	void ExpectSameSpec(FAutomationTestBase& Test, const FString& Case, const FGameplayEffectSpec& Got, const FGameplayEffectSpec& Want)
	{
		Test.TestEqual(Case + TEXT(": SetByCaller count"), Got.SetByCallerTagMagnitudes.Num(), Want.SetByCallerTagMagnitudes.Num());
		for (const TPair<FGameplayTag, float>& Pair : Want.SetByCallerTagMagnitudes)
		{
			const float* const Value = Got.SetByCallerTagMagnitudes.Find(Pair.Key);
			if (Test.TestNotNull(Case + TEXT(": has ") + Pair.Key.ToString(), Value))
			{
				Test.TestEqual(Case + TEXT(": ") + Pair.Key.ToString(), *Value, Pair.Value);
			}
		}

		Test.TestTrue(FString::Printf(TEXT("%s: dynamic asset tags %s == %s"), *Case, *Got.GetDynamicAssetTags().ToString(),
			*Want.GetDynamicAssetTags().ToString()), Got.GetDynamicAssetTags() == Want.GetDynamicAssetTags());

		const FVBGameplayEffectContext* const GotFacts = FVBGameplayEffectContext::Get(Got.GetContext());
		const FVBGameplayEffectContext* const WantFacts = FVBGameplayEffectContext::Get(Want.GetContext());
		if (!Test.TestNotNull(Case + TEXT(": filled context"), GotFacts) || !Test.TestNotNull(Case + TEXT(": copied context"), WantFacts))
		{
			return;
		}

		Test.TestEqual(Case + TEXT(": impact"), static_cast<int32>(GotFacts->GetDeclaredHitImpact()),
			static_cast<int32>(WantFacts->GetDeclaredHitImpact()));
		Test.TestEqual(Case + TEXT(": launch height"), GotFacts->GetLaunchHeight(), WantFacts->GetLaunchHeight());
		float GotYaw = -1.0f, WantYaw = -2.0f;
		Test.TestEqual(Case + TEXT(": yaw present"), GotFacts->GetPushYaw(GotYaw), WantFacts->GetPushYaw(WantYaw));
		Test.TestEqual(Case + TEXT(": yaw"), GotYaw, WantYaw);
		Test.TestEqual(Case + TEXT(": impact cue"), GotFacts->GetImpactCue(), WantFacts->GetImpactCue());
		Test.TestEqual(Case + TEXT(": charge source"), static_cast<int32>(GotFacts->GetChargeSource()),
			static_cast<int32>(WantFacts->GetChargeSource()));
		const FHitResult* const GotHit = GotFacts->GetHitResult();
		const FHitResult* const WantHit = WantFacts->GetHitResult();
		if (Test.TestNotNull(Case + TEXT(": filled hit"), GotHit) && Test.TestNotNull(Case + TEXT(": copied hit"), WantHit))
		{
			Test.TestEqual(Case + TEXT(": hit impact point"), FVector(GotHit->ImpactPoint), FVector(WantHit->ImpactPoint));
			Test.TestEqual(Case + TEXT(": hit normal"), FVector(GotHit->ImpactNormal), FVector(WantHit->ImpactNormal));
			Test.TestTrue(Case + TEXT(": hit actor"), GotHit->GetActor() == WantHit->GetActor());
		}
	}

	// 피해 효과 하나가 걸릴 때마다 센다. 걸린 스펙의 사본에서 피해, 방향, 답 태그를 읽어 둔다.
	struct FApplyCounter
	{
		UAbilitySystemComponent* ASC = nullptr;
		FDelegateHandle Handle;
		int32 Count = 0;
		float LastDamage = -1.0f;
		bool bLastYaw = false;
		float LastYaw = 0.0f;
		bool bLastDodgeOnly = false;

		void Bind(UAbilitySystemComponent* InASC)
		{
			ASC = InASC;
			Handle = ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
				[this](UAbilitySystemComponent*, const FGameplayEffectSpec& Applied, FActiveGameplayEffectHandle)
				{
					++Count;
					LastDamage = Applied.GetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, false, -1.0f);
					const FVBGameplayEffectContext* const Facts = FVBGameplayEffectContext::Get(Applied.GetContext());
					bLastYaw = Facts && Facts->GetPushYaw(LastYaw);
					bLastDodgeOnly = Applied.GetDynamicAssetTags().HasTagExact(VBGameplayTags::Attack_Answer_DodgeOnly);
				});
		}

		~FApplyCounter()
		{
			if (ASC)
			{
				ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
			}
		}
	};

	// 고립된 게임 월드 하나와 던진 쪽(감염체 편), 적대 표적 둘(플레이어 편), 같은 편 하나.
	struct FThrowScene
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<UVBProjectileConfig> Config;
		AVBCombatAuditTestEnemy* Thrower = nullptr;
		AVBCombatAuditTestEnemy* Hostile = nullptr;
		AVBCombatAuditTestEnemy* Hostile2 = nullptr;
		AVBCombatAuditTestEnemy* Ally = nullptr;

		~FThrowScene()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}

			if (Package.IsValid())
			{
				Package->SetDirtyFlag(false);
			}
		}

		bool Init(FAutomationTestBase& Test)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(true)
				.ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("isolated world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			// 없으면 PostInitializeComponents 가 안 불려 투사체가 겹침 알림을 묶지 않는다.
			World->InitializeActorsForPlay(FURL());

			Config.Reset(NewObject<UVBProjectileConfig>(GetTransientPackage()));
			Config->DamageEffectClass = UGameplayEffect::StaticClass();
			Config->Speed = 1500.0f;
			Config->LifetimeSeconds = 3.0f;
			Config->CollisionRadius = 20.0f;
			Config->DamageMultiplier = 2.0f;

			Thrower = SpawnEnemy(EVBTeam::Infected, FVector(0.0, 0.0, 0.0));
			Hostile = SpawnEnemy(EVBTeam::Player, FVector(1000.0, 0.0, 0.0));
			Hostile2 = SpawnEnemy(EVBTeam::Player, FVector(2000.0, 0.0, 0.0));
			Ally = SpawnEnemy(EVBTeam::Infected, FVector(3000.0, 0.0, 0.0));
			return Test.TestTrue(TEXT("thrower, two hostiles and an ally"), Thrower && Hostile && Hostile2 && Ally);
		}

		AVBCombatAuditTestEnemy* SpawnEnemy(EVBTeam Team, const FVector& Location) const
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVBCombatAuditTestEnemy* Enemy = World->SpawnActor<AVBCombatAuditTestEnemy>(Location, FRotator::ZeroRotator, Params);
			if (!Enemy)
			{
				return nullptr;
			}

			UVBEnemyConfig* EnemyConfig = NewObject<UVBEnemyConfig>(Enemy);
			EnemyConfig->Team = Team;
			Enemy->Configure(EnemyConfig);
			Enemy->GetAbilitySystemComponent()->InitAbilityActorInfo(Enemy, Enemy);
			return Enemy;
		}

		// 어느 액터와도 겹치지 않는 자리에 스폰한다. 서버 사본이면 Velocity 로 발사한다.
		AVBProjectile* Spawn() const
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			return World->SpawnActor<AVBProjectile>(FVector(500.0, 500.0, 0.0), FRotator::ZeroRotator, Params);
		}

		void Launch(AVBProjectile& Projectile, const FVector& Velocity) const
		{
			UAbilitySystemComponent* const ThrowerASC = Thrower->GetAbilitySystemComponent();
			const FGameplayEffectSpecHandle Spec = ThrowerASC->MakeOutgoingSpec(UGameplayEffect::StaticClass(), 1.0f,
				ThrowerASC->MakeEffectContext());
			// 발사 때의 방향은 0 도로 둔다 - 맞은 순간 비행 방향이 이것을 바꾸는지 본다.
			Projectile.Launch(*Config, *Thrower, Spec, Config->MakeHitValues(10.0f, 0.0f), Velocity);
		}

		AVBProjectile* Throw(const FVector& Velocity = FVector(0.0, 1500.0, 0.0)) const
		{
			AVBProjectile* const Projectile = Spawn();
			if (Projectile)
			{
				Launch(*Projectile, Velocity);
			}

			return Projectile;
		}

		// 스윕이 폰에 닿은 것과 같은 알림을 실제 묶인 델리게이트로 보낸다.
		static void Overlap(AVBProjectile& Projectile, AVBCombatAuditTestEnemy& Other)
		{
			UCapsuleComponent* const Capsule = Other.GetCapsuleComponent();
			const FHitResult Sweep(&Other, Capsule, Other.GetActorLocation() + FVector(0.0, -30.0, 0.0), FVector(0.0, -1.0, 0.0));
			Projectile.GetCollision()->OnComponentBeginOverlap.Broadcast(Projectile.GetCollision(), &Other, Capsule, 0, true, Sweep);
		}

		static bool IsGone(const AVBProjectile* Projectile)
		{
			return !IsValid(Projectile) || Projectile->IsActorBeingDestroyed();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBProjectileAimPointTest, "VB.Unit.Combat.Projectile.AimPoint", VBProjectileTests::Flags)
bool FVBProjectileAimPointTest::RunTest(const FString& Parameters)
{
	using namespace VBProjectileRules;
	const FVector Origin(0.0, 0.0, 0.0);
	const FVector Target(1500.0, 0.0, 0.0);

	TestEqual(TEXT("stationary target is the aim point"), AimPoint(Target, FVector::ZeroVector, Origin, 1500.0f, 300.0f), Target);
	// 1500 cm / 1500 cm/s = 1 초 - 앞선 거리 = 속도 x 1 초.
	TestEqual(TEXT("lead equals velocity x flight time"), AimPoint(Target, FVector(0.0, 100.0, 0.0), Origin, 1500.0f, 300.0f),
		FVector(1500.0, 100.0, 0.0));
	TestEqual(TEXT("half speed doubles the lead"), AimPoint(Target, FVector(0.0, 100.0, 0.0), Origin, 750.0f, 300.0f),
		FVector(1500.0, 200.0, 0.0));
	TestEqual(TEXT("lead clamps at MaxLeadDistance, direction kept"), AimPoint(Target, FVector(0.0, 1000.0, 0.0), Origin, 1500.0f, 300.0f),
		FVector(1500.0, 300.0, 0.0));
	TestEqual(TEXT("MaxLeadDistance 0 aims at the target"), AimPoint(Target, FVector(0.0, 1000.0, 0.0), Origin, 1500.0f, 0.0f), Target);
	TestEqual(TEXT("zero speed aims at the target"), AimPoint(Target, FVector(0.0, 1000.0, 0.0), Origin, 0.0f, 300.0f), Target);
	TestEqual(TEXT("negative speed aims at the target"), AimPoint(Target, FVector(0.0, 1000.0, 0.0), Origin, -5.0f, 300.0f), Target);

	TestEqual(TEXT("no gravity: straight at speed"), LaunchVelocity(Origin, Target, 1500.0f, 0.0f), FVector(1500.0, 0.0, 0.0));
	TestEqual(TEXT("zero speed: no velocity"), LaunchVelocity(Origin, Target, 0.0f, -980.0f), FVector::ZeroVector);
	TestEqual(TEXT("same point: no velocity"), LaunchVelocity(Origin, Origin, 1500.0f, -980.0f), FVector::ZeroVector);
	{
		// 중력 아래에서 같은 비행 시간 뒤의 자리가 겨눈 점이어야 한다.
		const FVector Aim(1200.0, 300.0, 150.0);
		const float GravityZ = -980.0f;
		const FVector Velocity = LaunchVelocity(Origin, Aim, 1500.0f, GravityZ);
		const double Seconds = FVector::Dist(Origin, Aim) / 1500.0;
		const FVector Landed = Origin + Velocity * Seconds + FVector(0.0, 0.0, 0.5 * GravityZ * Seconds * Seconds);
		TestTrue(FString::Printf(TEXT("gravity: arrives at the aim point (%s vs %s)"), *Landed.ToString(), *Aim.ToString()),
			Landed.Equals(Aim, 0.01));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitSpecFillTest, "VB.Unit.Combat.HitSpec.Fill", VBProjectileTests::Flags)
bool FVBHitSpecFillTest::RunTest(const FString& Parameters)
{
	using namespace VBProjectileTests;
	const FVBHitSpecValues Values = DistinctValues();
	const FHitResult Hit(nullptr, nullptr, FVector(10.0, 20.0, 30.0), FVector(0.0, 0.0, 1.0));

	FGameplayEffectSpec Spec = MakeBlankSpec();
	if (!TestTrue(TEXT("fill succeeds on our context"), VBHitSpec::Fill(Spec, Values, Hit)))
	{
		return false;
	}

	TestEqual(TEXT("three SetByCaller values, no more"), Spec.SetByCallerTagMagnitudes.Num(), 3);
	TestEqual(TEXT("damage"), Spec.GetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, false, -1.0f), Values.Damage);
	TestEqual(TEXT("knockback strength"), Spec.GetSetByCallerMagnitude(VBGameplayTags::Data_KnockbackStrength, false, -1.0f),
		Values.KnockbackStrength);
	TestEqual(TEXT("poise"), Spec.GetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, false, -1.0f), Values.PoiseDamage);
	TestTrue(TEXT("answer tag is a dynamic asset tag"), Spec.GetDynamicAssetTags().HasTagExact(Values.AttackAnswer));
	TestEqual(TEXT("only the answer tag"), Spec.GetDynamicAssetTags().Num(), 1);

	const FVBGameplayEffectContext* const Facts = FVBGameplayEffectContext::Get(Spec.GetContext());
	if (!TestNotNull(TEXT("facts context"), Facts))
	{
		return false;
	}

	TestEqual(TEXT("declared impact"), static_cast<int32>(Facts->GetDeclaredHitImpact()), static_cast<int32>(Values.HitImpact));
	TestEqual(TEXT("launch height"), Facts->GetLaunchHeight(), Values.LaunchHeight);
	float Yaw = -1.0f;
	TestTrue(TEXT("push yaw present"), Facts->GetPushYaw(Yaw));
	TestEqual(TEXT("push yaw"), Yaw, Values.PushYaw);
	TestEqual(TEXT("impact cue"), Facts->GetImpactCue(), Values.ImpactCue);
	TestEqual(TEXT("charge source"), static_cast<int32>(Facts->GetChargeSource()), static_cast<int32>(Values.ChargeSource));
	const FHitResult* const StoredHit = Facts->GetHitResult();
	if (TestNotNull(TEXT("hit result"), StoredHit))
	{
		TestEqual(TEXT("hit impact point"), FVector(StoredHit->ImpactPoint), FVector(Hit.ImpactPoint));
	}

	// 답이 비면 동적 태그가 없다.
	{
		FVBHitSpecValues NoAnswer = Values;
		NoAnswer.AttackAnswer = FGameplayTag();
		FGameplayEffectSpec Plain = MakeBlankSpec();
		TestTrue(TEXT("no answer: fill succeeds"), VBHitSpec::Fill(Plain, NoAnswer, Hit));
		TestEqual(TEXT("no answer: no dynamic tag"), Plain.GetDynamicAssetTags().Num(), 0);
	}

	// 컨텍스트가 우리 타입이 아니면 아무것도 쓰지 않는다.
	{
		AddExpectedError(TEXT("FVBGameplayEffectContext 가 아니다"), EAutomationExpectedErrorFlags::Contains, 1);
		FGameplayEffectSpec Foreign(GetDefault<UGameplayEffect>(), FGameplayEffectContextHandle(new FGameplayEffectContext()), 1.0f);
		TestFalse(TEXT("foreign context: refused"), VBHitSpec::Fill(Foreign, Values, Hit));
		TestEqual(TEXT("foreign context: no SetByCaller"), Foreign.SetByCallerTagMagnitudes.Num(), 0);
		TestEqual(TEXT("foreign context: no tag"), Foreign.GetDynamicAssetTags().Num(), 0);
		TestNull(TEXT("foreign context: no hit"), Foreign.GetContext().GetHitResult());
	}

	// 같은 스펙을 두 번 채우면 엔진 assert 대신 거절한다.
	{
		AddExpectedError(TEXT("이미 HitResult"), EAutomationExpectedErrorFlags::Contains, 1);
		FVBHitSpecValues Second = Values;
		Second.Damage = 999.0f;
		TestFalse(TEXT("second fill: refused"), VBHitSpec::Fill(Spec, Second, Hit));
		TestEqual(TEXT("second fill: damage unchanged"),
			Spec.GetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, false, -1.0f), Values.Damage);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHitSpecMatchesSwingTest, "VB.Unit.Combat.HitSpec.MatchesSwingFill", VBProjectileTests::Flags)
bool FVBHitSpecMatchesSwingTest::RunTest(const FString& Parameters)
{
	using namespace VBProjectileTests;
	TArray<TPair<FString, FSwingInputs>> Rows;
	{
		FSwingInputs In;
		In.FinalDamage = 12.5f; In.KnockbackYaw = 90.0f; In.SwingImpact = EVBHitImpact::Light;
		In.KnockbackStrength = 300.0f; In.PoiseDamage = 5.0f;
		In.AttackAnswer = VBGameplayTags::Attack_Answer_Parryable;
		Rows.Emplace(TEXT("plain parryable"), In);
	}

	{
		FSwingInputs In;
		In.FinalDamage = 40.0f; In.KnockbackYaw = -45.0f; In.bCinematicSkill = true; In.SwingImpact = EVBHitImpact::Launch;
		In.SwingLaunchHeight = 250.0f; In.ResolvedImpactCue = VBGameplayTags::GameplayCue_Combat_SwingImpact;
		In.KnockbackStrength = 500.0f; In.KnockbackStrengthScale = 1.5f;
		In.PoiseDamage = 30.0f; In.PoiseDamageScale = 0.25f; In.AttackAnswer = VBGameplayTags::Attack_Answer_DodgeOnly;
		Rows.Emplace(TEXT("cinematic launch, scaled"), In);
	}

	{
		FSwingInputs In;
		In.FinalDamage = 0.0f; In.KnockbackYaw = 0.0f; In.SwingImpact = EVBHitImpact::Knockdown;
		Rows.Emplace(TEXT("zero values, no answer, no cue"), In);
	}

	for (const TPair<FString, FSwingInputs>& Row : Rows)
	{
		const FHitResult Hit(nullptr, nullptr, FVector(3.0, -4.0, 5.0), FVector(1.0, 0.0, 0.0));
		FGameplayEffectSpecHandle Copied(new FGameplayEffectSpec(MakeBlankSpec()));
		InlineSwingFill(Copied, Row.Value, Hit);
		FGameplayEffectSpec Filled = MakeBlankSpec();
		if (!TestTrue(Row.Key + TEXT(": fill succeeds"), VBHitSpec::Fill(Filled, ToValues(Row.Value), Hit)))
		{
			continue;
		}

		ExpectSameSpec(*this, Row.Key, Filled, *Copied.Data.Get());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBProjectileHitsOnceTest, "VB.Unit.Combat.Projectile.HitsOnce", VBProjectileTests::Flags)
bool FVBProjectileHitsOnceTest::RunTest(const FString& Parameters)
{
	using namespace VBProjectileTests;
	FThrowScene Scene;
	if (!Scene.Init(*this))
	{
		return false;
	}

	FApplyCounter First, Second;
	First.Bind(Scene.Hostile->GetAbilitySystemComponent());
	Second.Bind(Scene.Hostile2->GetAbilitySystemComponent());

	AVBProjectile* const Projectile = Scene.Throw(FVector(0.0, 1500.0, 0.0));
	if (!TestNotNull(TEXT("projectile"), Projectile) || !TestTrue(TEXT("launched"), Projectile->IsLaunched()))
	{
		return false;
	}
	// 한 프레임에 겹침 셋 - 같은 표적 둘, 다른 표적 하나.
	FThrowScene::Overlap(*Projectile, *Scene.Hostile);
	FThrowScene::Overlap(*Projectile, *Scene.Hostile);
	FThrowScene::Overlap(*Projectile, *Scene.Hostile2);

	TestEqual(TEXT("first hostile hit once"), First.Count, 1);
	TestEqual(TEXT("second hostile not hit"), Second.Count, 0);
	TestTrue(TEXT("spent"), Projectile->IsSpent());
	TestTrue(TEXT("destroyed after the hit"), FThrowScene::IsGone(Projectile));
	// 걸린 스펙이 Fill 로 채워졌다: 공격력 10 x 배율 2, 미는 방향은 발사 때 값(0)이 아니라 비행 방향(+Y = 90도).
	TestEqual(TEXT("applied damage"), First.LastDamage, 20.0f);
	TestTrue(TEXT("applied push yaw present"), First.bLastYaw);
	TestEqual(TEXT("applied push yaw follows flight"), First.LastYaw, 90.0f, 0.01f);
	TestTrue(TEXT("applied answer is DodgeOnly (config default)"), First.bLastDodgeOnly);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBProjectileSkipsFriendsTest, "VB.Unit.Combat.Projectile.SkipsThrowerAllyDead", VBProjectileTests::Flags)
bool FVBProjectileSkipsFriendsTest::RunTest(const FString& Parameters)
{
	using namespace VBProjectileTests;
	FThrowScene Scene;
	if (!Scene.Init(*this))
	{
		return false;
	}

	FApplyCounter ThrowerHits, AllyHits, HostileHits;
	ThrowerHits.Bind(Scene.Thrower->GetAbilitySystemComponent());
	AllyHits.Bind(Scene.Ally->GetAbilitySystemComponent());
	HostileHits.Bind(Scene.Hostile->GetAbilitySystemComponent());

	AVBProjectile* const Projectile = Scene.Throw();
	if (!TestNotNull(TEXT("projectile"), Projectile))
	{
		return false;
	}

	FThrowScene::Overlap(*Projectile, *Scene.Thrower);
	TestEqual(TEXT("thrower ignored"), ThrowerHits.Count, 0);
	FThrowScene::Overlap(*Projectile, *Scene.Ally);
	TestEqual(TEXT("same team ignored"), AllyHits.Count, 0);
	UAbilitySystemComponent* const HostileASC = Scene.Hostile->GetAbilitySystemComponent();
	HostileASC->AddLooseGameplayTag(VBGameplayTags::State_Dead);
	FThrowScene::Overlap(*Projectile, *Scene.Hostile);
	TestEqual(TEXT("dead hostile ignored"), HostileHits.Count, 0);
	HostileASC->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
	TestFalse(TEXT("still live after skipped overlaps"), Projectile->IsSpent() || FThrowScene::IsGone(Projectile));

	FThrowScene::Overlap(*Projectile, *Scene.Hostile);
	TestEqual(TEXT("live hostile then hit"), HostileHits.Count, 1);

	// 던진 쪽이 사라지면 편을 모른다 - 때리지 않는다.
	AVBProjectile* const Orphan = Scene.Throw();
	if (!TestNotNull(TEXT("second projectile"), Orphan))
	{
		return false;
	}

	Scene.Thrower->Destroy();
	FApplyCounter Hostile2Hits;
	Hostile2Hits.Bind(Scene.Hostile2->GetAbilitySystemComponent());
	FThrowScene::Overlap(*Orphan, *Scene.Hostile2);
	TestEqual(TEXT("thrower gone: no damage"), Hostile2Hits.Count, 0);
	TestFalse(TEXT("thrower gone: not spent"), Orphan->IsSpent());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBProjectileWorldAndLifetimeTest, "VB.Unit.Combat.Projectile.WallAndLifetimeNoDamage", VBProjectileTests::Flags)
bool FVBProjectileWorldAndLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace VBProjectileTests;
	FThrowScene Scene;
	if (!Scene.Init(*this))
	{
		return false;
	}

	FApplyCounter HostileHits;
	HostileHits.Bind(Scene.Hostile->GetAbilitySystemComponent());

	// 벽: 피해 없이 사라지고, 뒤늦은 겹침도 무시한다.
	{
		AVBProjectile* const Projectile = Scene.Throw();
		AActor* const Wall = Scene.World->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("projectile"), Projectile) || !TestNotNull(TEXT("wall"), Wall))
		{
			return false;
		}

		const FHitResult WallHit(Wall, nullptr, FVector(500.0, 600.0, 0.0), FVector(0.0, -1.0, 0.0));
		Projectile->GetMovement()->OnProjectileStop.Broadcast(WallHit);
		TestTrue(TEXT("wall: spent"), Projectile->IsSpent());
		TestTrue(TEXT("wall: destroyed"), FThrowScene::IsGone(Projectile));
		FThrowScene::Overlap(*Projectile, *Scene.Hostile);
		TestEqual(TEXT("wall: no damage, even after a late overlap"), HostileHits.Count, 0);
	}

	// 수명: 설정의 수명이 걸리고, 끝나면 피해 없이 사라진다.
	{
		AVBProjectile* const Projectile = Scene.Throw();
		if (!TestNotNull(TEXT("projectile"), Projectile))
		{
			return false;
		}

		TestEqual(TEXT("lifetime from config"), Projectile->GetLifeSpan(), Scene.Config->LifetimeSeconds, 0.01f);
		Projectile->LifeSpanExpired();
		TestTrue(TEXT("lifetime: destroyed"), FThrowScene::IsGone(Projectile));
		TestEqual(TEXT("lifetime: no damage"), HostileHits.Count, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBProjectileClientCopyTest, "VB.Unit.Combat.Projectile.ClientCopyInert", VBProjectileTests::Flags)
bool FVBProjectileClientCopyTest::RunTest(const FString& Parameters)
{
	using namespace VBProjectileTests;
	FThrowScene Scene;
	if (!Scene.Init(*this))
	{
		return false;
	}

	FApplyCounter HostileHits;
	HostileHits.Bind(Scene.Hostile->GetAbilitySystemComponent());

	AVBProjectile* const Projectile = Scene.Spawn();
	if (!TestNotNull(TEXT("projectile"), Projectile))
	{
		return false;
	}

	Projectile->SetRole(ROLE_SimulatedProxy);
	// 엔진의 이동 컴포넌트는 방향 기본값 (1,0,0) 을 갖는다 - 'Launch 가 바꾸지 않았다' 를 잰다.
	const FVector VelocityBefore = Projectile->GetMovement()->Velocity;
	Scene.Launch(*Projectile, FVector(0.0, 1500.0, 0.0));
	TestFalse(TEXT("client copy: not launched"), Projectile->IsLaunched());
	TestTrue(TEXT("client copy: velocity unchanged by Launch"), Projectile->GetMovement()->Velocity.Equals(VelocityBefore));
	TestEqual(TEXT("client copy: no lifetime set"), Projectile->GetLifeSpan(), 0.0f);
	FThrowScene::Overlap(*Projectile, *Scene.Hostile);
	TestEqual(TEXT("client copy: overlap does no damage"), HostileHits.Count, 0);
	const FHitResult WallHit(nullptr, nullptr, FVector(500.0, 600.0, 0.0), FVector(0.0, -1.0, 0.0));
	Projectile->GetMovement()->OnProjectileStop.Broadcast(WallHit);
	TestFalse(TEXT("client copy: a stop does not destroy it (the server does)"), FThrowScene::IsGone(Projectile));
	return true;
}

// TODO(월드 시험 불가): 실제 스윕 비행으로 캡슐에 닿기(고립 월드에서 이동 컴포넌트 틱, 스윕 겹침, 속력에 따른 관통) - 물리 바디가 등록된 틱 월드가 필요하다. PIE 로만 확인.
// TODO(월드 시험 불가): 던진 쪽 ASC 로 나가는 닿은 자리 큐(ExecuteGameplayCue) - 감사용 적 픽스처에 큐를 받아 적는 자리가 없다.

// 설정 반경으로 발사 자리에서 이미 겹친 적대 폰도 맞는다 - 반경을 바꾸는 순간 엔진이 겹침을 다시 잰다(SetSphereRadius).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBProjectileSpawnInsideTest, "VB.Unit.Combat.Projectile.HitsPawnInsideAtLaunch", VBProjectileTests::Flags)
bool FVBProjectileSpawnInsideTest::RunTest(const FString& Parameters)
{
	using namespace VBProjectileTests;
	FThrowScene Scene;
	if (!Scene.Init(*this))
	{
		return false;
	}

	FApplyCounter HostileHits;
	HostileHits.Bind(Scene.Hostile->GetAbilitySystemComponent());
	Scene.Config->CollisionRadius = 80.0f;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 적대 폰 중심에서 100cm - 기본 반경(20) + 캡슐 반경으로는 안 닿고, 설정 반경(80) 으로는 닿는 자리.
	AVBProjectile* const Projectile = Scene.World->SpawnActor<AVBProjectile>(Scene.Hostile->GetActorLocation() + FVector(100.0, 0.0, 0.0),
		FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("projectile"), Projectile))
	{
		return false;
	}
	// 실제 던지기는 시작된 월드에서 스폰하므로 Launch 때 이미 BeginPlay 가 돌았다 - 엔진은 그 전에는 겹침을 갱신하지 않는다.
	Projectile->DispatchBeginPlay();
	Scene.Launch(*Projectile, FVector(0.0, 1500.0, 0.0));
	TestEqual(TEXT("pawn inside the configured radius at launch is hit once"), HostileHits.Count, 1);
	// 플레이를 시작한 액터는 월드를 지우기 전에 없앤다(EndPlay 없이 GC 되지 않게).
	if (IsValid(Projectile))
	{
		Projectile->Destroy();
	}

	return true;
}

#endif
