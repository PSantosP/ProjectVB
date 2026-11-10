// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 기절 게이지 처형 - 게이지가 다 차면 창이 열리고, 완벽한 패링이 공격자의 게이지를 채운다
// 창이 열린 적도 맞으면 깎이고 반응하되 게이지는 다 찬 채이며, 공격 버튼은 처형하지 않고, 프롬프트는 처형 키가 걸 적 하나에만 뜬다
// 각 시험의 단언은 해당 규칙을 되돌리면 실패하는 반대 대조다

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/ScopeExit.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "AbilitySystem/Abilities/VBGA_Execute.h"
#include "AbilitySystem/Abilities/VBGA_LightAttack.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/SceneComponent.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBEnemyConfig.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/Indicator/VBIndicatorRegistrySubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "VBCombatAuditTestAbility.h"
#include "VBPostureExecuteTestTypes.h"
#include "Animation/VBEnemyAnimInstance.h"
#include "Character/VBFinisherComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimMontage.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace VBPostureExecuteTest
{
	constexpr auto Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;

		~FFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
			}

			if (Package.IsValid())
			{
				Package->SetDirtyFlag(false);
			}
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->GetOutermost()->SetFlags(RF_Transient);
			// 월드 시각을 돌리는 시험(창 만료, 회복)이 있어 카메라 피드백 시험처럼 월드 문맥을 붙인다.
			if (GEngine)
			{
				GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			}

			World->InitializeActorsForPlay(FURL());
			return true;
		}

		// 진짜 VB ASC(전투 규칙 표)와 체력, 기절 게이지 세트를 가진 적. 능력은 준 순서대로 부여한다.
		UAbilitySystemComponent* SpawnEnemy(FAutomationTestBase& Test, float MaxPoise, TArrayView<UClass* const> Abilities)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVBCombatAuditTestEnemy* Enemy = World->SpawnActor<AVBCombatAuditTestEnemy>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (!Test.TestNotNull(TEXT("enemy"), Enemy))
			{
				return nullptr;
			}

			Enemy->Configure(NewObject<UVBEnemyConfig>(Enemy));
			UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
			if (!Test.TestNotNull(TEXT("enemy ASC"), ASC))
			{
				return nullptr;
			}

			ASC->InitAbilityActorInfo(Enemy, Enemy);
			if (!Test.TestTrue(TEXT("enemy carries the stagger gauge set"),
				ASC->HasAttributeSetForAttribute(UVBCombatAttributeSet::GetMaxPoiseAttribute()))
				|| !Test.TestTrue(TEXT("enemy carries the health set"),
					ASC->HasAttributeSetForAttribute(UVBHealthAttributeSet::GetHealthAttribute())))
			{
				return nullptr;
			}

			ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
			ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
			ASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetMaxPoiseAttribute(), MaxPoise);
			ASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseAttribute(), MaxPoise);
			for (UClass* Ability : Abilities)
			{
				ASC->GiveAbility(FGameplayAbilitySpec(Ability, 1));
			}

			return ASC;
		}

		// 공격하는 쪽. 피해 계산이 공격자의 BaseDamage 를 읽어 기절 게이지 세트를 단다.
		UAbilitySystemComponent* SpawnSource(FAutomationTestBase& Test, ACharacter* Body = nullptr)
		{
			if (!Body)
			{
				Body = World->SpawnActor<ACharacter>();
			}

			if (!Test.TestNotNull(TEXT("source body"), Body))
			{
				return nullptr;
			}

			UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Body);
			Body->AddInstanceComponent(ASC);
			ASC->RegisterComponent();
			ASC->InitAbilityActorInfo(Body, Body);
			ASC->AddAttributeSetSubobject(NewObject<UVBCombatAttributeSet>(Body));
			return ASC;
		}

		// 게임 시각을 먼저 올리고(회복과 창이 읽는다), 타이머를 두 프레임 돌린다 - 이번 프레임에 건 타이머는 다음 프레임에야 돈다.
		void Advance(float Seconds)
		{
			World->Tick(LEVELTICK_TimeOnly, Seconds);
			++GFrameCounter;
			World->GetTimerManager().Tick(0.0f);
			++GFrameCounter;
			World->GetTimerManager().Tick(Seconds);
		}
	};

	float Attr(const UAbilitySystemComponent& ASC, const FGameplayAttribute& Attribute) { return ASC.GetNumericAttribute(Attribute); }

	// 진짜 피해 계산을 거치는 게이지 전용 피해(GE_ParryPoise 와 같은 모양).
	void ApplyPoise(UAbilitySystemComponent& Source, UAbilitySystemComponent& Target, float Amount)
	{
		const FGameplayEffectSpecHandle Spec = Source.MakeOutgoingSpec(UVBPostureTestPoiseEffect::StaticClass(), 1.0f,
			Source.MakeEffectContext());
		Spec.Data->SetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, Amount);
		Source.ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), &Target);
	}

	// 출시 근접 타격의 피해 효과(DA_Attack_Fighter_Light). 피해 파이프라인 시험과 같은 것을 쓴다.
	const UGameplayEffect* ShippedDamageEffect(FAutomationTestBase& Test)
	{
		const UVBAttackConfig* Attack = LoadObject<UVBAttackConfig>(nullptr,
			TEXT("/Game/VB/Data/DA_Attack_Fighter_Light.DA_Attack_Fighter_Light"));
		if (!Test.TestNotNull(TEXT("shipped attack config"), Attack)
			|| !Test.TestNotNull(TEXT("its damage effect"), Attack->DamageEffectClass.Get()))
		{
			return nullptr;
		}

		return Attack->DamageEffectClass->GetDefaultObject<UGameplayEffect>();
	}

	void ApplyHit(const UGameplayEffect& Effect, UAbilitySystemComponent& Source, UAbilitySystemComponent& Target, float Damage,
		float Poise,
		bool bExecution = false)
	{
		FGameplayEffectSpec Spec(&Effect, Source.MakeEffectContext(), 1.0f);
		Spec.SetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, Damage);
		Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, Poise);
		Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_KnockbackStrength, 0.0f);
		Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_KnockbackZ, 0.0f);
		if (bExecution)
		{
			Spec.AddDynamicAssetTag(VBGameplayTags::Effect_Execute);
		}

		Source.ApplyGameplayEffectSpecToTarget(Spec, &Target);
	}

	// 이 범위 동안 이 ASC 에서 켜진 능력 클래스.
	struct FActivationLog
	{
		UAbilitySystemComponent& ASC;
		TArray<UClass*> Classes;
		FDelegateHandle Handle;
		explicit FActivationLog(UAbilitySystemComponent& InASC) : ASC(InASC)
		{
			Handle = ASC.AbilityActivatedCallbacks.AddLambda([this](UGameplayAbility* Ability) { if (Ability) Classes.Add(Ability->GetClass()); });
		}

		~FActivationLog() { ASC.AbilityActivatedCallbacks.Remove(Handle); }
		bool Has(const UClass* Class) const { return Classes.Contains(Class); }
	};

	// LogVB 에서 Needle 을 담은 줄의 수.
	struct FLogLineCount : FOutputDevice
	{
		FString Needle;
		int32 Count = 0;
		explicit FLogLineCount(const TCHAR* InNeedle) : Needle(InNeedle) { if (GLog) GLog->AddOutputDevice(this); }
		virtual ~FLogLineCount() override { if (GLog) GLog->RemoveOutputDevice(this); }
		// 로그는 스레드로 늦게 도착할 수 있다 - 읽기 전에 비운다.
		int32 Read() { if (GLog) GLog->FlushThreadedLogs(); return Count; }
		virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == FName(TEXT("LogVB")) && FString(Message).Contains(Needle))
			{
				++Count;
			}
		}
	};

	// 피해 허브가 보내는 반응 사건(Event.Combat.Hit)의 수.
	struct FHitEventCount
	{
		UAbilitySystemComponent& ASC;
		int32 Count = 0;
		FDelegateHandle Handle;
		explicit FHitEventCount(UAbilitySystemComponent& InASC) : ASC(InASC)
		{
			Handle = ASC.GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit).AddLambda([this](const FGameplayEventData*) { ++Count; });
		}

		~FHitEventCount() { ASC.GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_Hit).Remove(Handle); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureExecuteDefaultsTest, "VB.Unit.Combat.PostureExecute.ConfigDefaults", VBPostureExecuteTest::Flags)
bool FVBPostureExecuteDefaultsTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("window opens on a full gauge by default"),
		GetDefault<UVBFinisherWindowConfig>()->OpenOn == EVBFinisherWindowOpenOn::PoiseBreak);
	TestTrue(TEXT("a parry fills the gauge by default"), GetDefault<UVBParryConfig>()->ParryPoiseDamage > 0.0f);
	TestEqual(TEXT("poise recovery is off until authored"), GetDefault<UVBCombatAttributeSet>()->GetPoiseRecoveryPerSecond(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureParryFillsPoiseTest, "VB.Unit.Combat.PostureExecute.ParryFillsAttackerPoise", VBPostureExecuteTest::Flags)
bool FVBPostureParryFillsPoiseTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	const UGameplayEffect* Damage = ShippedDamageEffect(*this);
	if (!Damage)
	{
		return false;
	}

	AVBPostureTestParrier* Parrier = F.World->SpawnActor<AVBPostureTestParrier>();
	if (!TestNotNull(TEXT("parrier"), Parrier))
	{
		return false;
	}

	Parrier->ParryConfig = NewObject<UVBParryConfig>(Parrier);
	Parrier->ParryConfig->ParryPoiseDamage = 40.0f;
	Parrier->ParryConfig->ParryPoiseEffect = UVBPostureTestPoiseEffect::StaticClass();
	UAbilitySystemComponent* ParrierASC = F.SpawnSource(*this, Parrier);
	if (!ParrierASC)
	{
		return false;
	}

	ParrierASC->AddAttributeSetSubobject(NewObject<UVBHealthAttributeSet>(Parrier));
	ParrierASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
	ParrierASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
	// 패링한 쪽의 기본 피해가 0 이 아니어야 공격자 체력 그대로라는 단언이 뜻을 가진다 - 피해 계산을 단 효과는 이것을 체력에 넣는다.
	ParrierASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetBaseDamageAttribute(), 15.0f);

	UAbilitySystemComponent* AttackerASC = F.SpawnEnemy(*this, 100.0f, {});
	if (!AttackerASC)
	{
		return false;
	}

	ApplyHit(*Damage, *AttackerASC, *ParrierASC, 10.0f, 0.0f);
	TestEqual(TEXT("parry: no health damage on the parrier"), Attr(*ParrierASC, UVBHealthAttributeSet::GetHealthAttribute()),
		100.0f, 0.01f);
	TestEqual(TEXT("parry: attacker gauge drops by ParryPoiseDamage"),
		Attr(*AttackerASC, UVBCombatAttributeSet::GetPoiseAttribute()), 60.0f, 0.01f);
	TestEqual(TEXT("parry: attacker health untouched (gauge only)"),
		Attr(*AttackerASC, UVBHealthAttributeSet::GetHealthAttribute()), 100.0f, 0.01f);

	// 효과 클래스가 비면 설정마다 오류 한 번(패링 두 번에도 한 번), 게이지는 그대로. 양이 0 이면 꺼짐이라 조용하다.
	Parrier->ParryConfig->ParryPoiseEffect = nullptr;
	AddExpectedErrorPlain(TEXT("효과 클래스가 비었다"), EAutomationExpectedErrorFlags::Contains, 1);
	ApplyHit(*Damage, *AttackerASC, *ParrierASC, 10.0f, 0.0f);
	ApplyHit(*Damage, *AttackerASC, *ParrierASC, 10.0f, 0.0f);
	TestEqual(TEXT("no effect class: gauge untouched"), Attr(*AttackerASC, UVBCombatAttributeSet::GetPoiseAttribute()), 60.0f, 0.01f);
#if WITH_EDITOR
	{
		FDataValidationContext Context;
		TestTrue(TEXT("no effect class with an amount: data invalid"),
			Parrier->ParryConfig->IsDataValid(Context) == EDataValidationResult::Invalid);
	}
#endif
	Parrier->ParryConfig->ParryPoiseEffect = UVBPostureTestPoiseEffect::StaticClass();
	Parrier->ParryConfig->ParryPoiseDamage = 0.0f;
	ApplyHit(*Damage, *AttackerASC, *ParrierASC, 10.0f, 0.0f);
	TestEqual(TEXT("zero parry amount: gauge untouched"), Attr(*AttackerASC, UVBCombatAttributeSet::GetPoiseAttribute()), 60.0f, 0.01f);
#if WITH_EDITOR
	{
		FDataValidationContext Context;
		TestTrue(TEXT("effect assigned: data not invalid"), Parrier->ParryConfig->IsDataValid(Context) != EDataValidationResult::Invalid);
	}
#endif

	// 피해 계산을 단 효과는 쓰지 않는다: 설정마다 오류 한 번, 공격자 체력, 게이지 그대로, 데이터 검사에서 막힘.
	Parrier->ParryConfig = NewObject<UVBParryConfig>(Parrier);
	Parrier->ParryConfig->ParryPoiseDamage = 40.0f;
	Parrier->ParryConfig->ParryPoiseEffect = UVBPostureTestDamagingPoiseEffect::StaticClass();
	AddExpectedErrorPlain(TEXT("Execution"), EAutomationExpectedErrorFlags::Contains, 1);
	ApplyHit(*Damage, *AttackerASC, *ParrierASC, 10.0f, 0.0f);
	TestEqual(TEXT("damaging effect: attacker health untouched"), Attr(*AttackerASC, UVBHealthAttributeSet::GetHealthAttribute()),
		100.0f, 0.01f);
	TestEqual(TEXT("damaging effect: gauge untouched"), Attr(*AttackerASC, UVBCombatAttributeSet::GetPoiseAttribute()), 60.0f, 0.01f);
#if WITH_EDITOR
	{
		FDataValidationContext Context;
		TestTrue(TEXT("damaging effect: data invalid"), Parrier->ParryConfig->IsDataValid(Context) == EDataValidationResult::Invalid);
	}
#endif

	// 게이지 수정값 말고 체력 수정값이 하나 더 있어도 쓰지 않는다 - 게이지 수정값이 있는지가 아니라 그 모양 하나뿐인지를 본다.
	Parrier->ParryConfig = NewObject<UVBParryConfig>(Parrier);
	Parrier->ParryConfig->ParryPoiseDamage = 40.0f;
	Parrier->ParryConfig->ParryPoiseEffect = UVBPostureTestPoisePlusHealthEffect::StaticClass();
	AddExpectedErrorPlain(TEXT("수정값이 2개다"), EAutomationExpectedErrorFlags::Contains, 1);
	ApplyHit(*Damage, *AttackerASC, *ParrierASC, 10.0f, 0.0f);
	TestEqual(TEXT("extra health modifier: attacker health untouched"),
		Attr(*AttackerASC, UVBHealthAttributeSet::GetHealthAttribute()), 100.0f, 0.01f);
	TestEqual(TEXT("extra health modifier: gauge untouched"), Attr(*AttackerASC, UVBCombatAttributeSet::GetPoiseAttribute()), 60.0f, 0.01f);
	TestFalse(TEXT("shipped shape passes the rule"),
		!UVBParryConfig::GetPoiseEffectProblem(UVBPostureTestPoiseEffect::StaticClass()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureGrantTagTest, "VB.Unit.Combat.PostureExecute.GrantTagLifetime", VBPostureExecuteTest::Flags)
bool FVBPostureGrantTagTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent* Enemy = F.SpawnEnemy(*this, 30.0f, {});
	if (!Enemy)
	{
		return false;
	}

	auto Has = [Enemy] { return Enemy->HasMatchingGameplayTag(VBGameplayTags::State_Combat_PostureExecutable); };

	const FGameplayAbilitySpecHandle Poise = Enemy->GiveAbility(FGameplayAbilitySpec(UVBPostureTestWindowOnPoise::StaticClass(), 1));
	TestTrue(TEXT("gauge window given: tag on"), Has());
	Enemy->ClearAbility(Poise);
	TestFalse(TEXT("gauge window removed: tag off"), Has());

	const FGameplayAbilitySpecHandle Health = Enemy->GiveAbility(FGameplayAbilitySpec(UVBPostureTestWindowOnHealth::StaticClass(), 1));
	TestFalse(TEXT("health window given: no tag"), Has());
	Enemy->ClearAbility(Health);

	// 설정이 빈 창: 부여 순간 오류 한 번, 태그 없음.
	AddExpectedErrorPlain(TEXT("FinisherWindowConfig"), EAutomationExpectedErrorFlags::Contains, 1);
	Enemy->GiveAbility(FGameplayAbilitySpec(UVBPostureTestWindowNoConfig::StaticClass(), 1));
	TestFalse(TEXT("window without config: no tag"), Has());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPosturePoiseBreakOpensWindowTest, "VB.Unit.Combat.PostureExecute.PoiseBreakOpensWindow", VBPostureExecuteTest::Flags)
bool FVBPosturePoiseBreakOpensWindowTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	if (!Source)
	{
		return false;
	}

	struct FCase
	{
		const TCHAR* Name;
		TArray<UClass*> Grants;
		bool bWindow;
	};
	const FCase Cases[] = {
		{ TEXT("window granted first"), { UVBPostureTestWindowOnPoise::StaticClass(), UVBPostureTestStagger::StaticClass() }, true },
		{ TEXT("stagger granted first"), { UVBPostureTestStagger::StaticClass(), UVBPostureTestWindowOnPoise::StaticClass() }, true },
		{ TEXT("no window ability: stagger as before"), { UVBPostureTestStagger::StaticClass() }, false },
		{ TEXT("window opens on health: stagger takes the break"), { UVBPostureTestWindowOnHealth::StaticClass(), UVBPostureTestStagger::StaticClass() }, false },
	};
	// 경직 대역은 콘텐츠가 없어 켜질 때마다 한 번 경고한다 - 경직이 받아야 하는 경우마다 정확히 한 번.
	int32 StaggerRuns = 0;
	for (const FCase& Case : Cases)
	{
		StaggerRuns += Case.bWindow ? 0 : 1;
	}

	AddExpectedMessagePlain(TEXT("HitReactConfig"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, StaggerRuns);
	for (const FCase& Case : Cases)
	{
		UAbilitySystemComponent* Enemy = F.SpawnEnemy(*this, 30.0f, Case.Grants);
		if (!Enemy)
		{
			return false;
		}

		FActivationLog Activated(*Enemy);
		ApplyPoise(*Source, *Enemy, 30.0f);
		const FString Name(Case.Name);
		TestEqual(Name + TEXT(": gauge refilled at the break"), Attr(*Enemy, UVBCombatAttributeSet::GetPoiseAttribute()), 30.0f, 0.01f);
		TestEqual(Name + TEXT(": window armed"), UVBAbilitySystemStatics::IsFinisherWindowArmed(Enemy), Case.bWindow);
		TestEqual(Name + TEXT(": window open (standing body)"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy), Case.bWindow);
		TestEqual(Name + TEXT(": stagger did not activate"), !Activated.Has(UVBPostureTestStagger::StaticClass()), Case.bWindow);
		TestEqual(Name + TEXT(": posture tag matches the config"),
			Enemy->HasMatchingGameplayTag(VBGameplayTags::State_Combat_PostureExecutable), Case.bWindow);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureHealthThresholdTest, "VB.Unit.Combat.PostureExecute.HealthThresholdOnlyWhenConfigured", VBPostureExecuteTest::Flags)
bool FVBPostureHealthThresholdTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	const UGameplayEffect* Damage = ShippedDamageEffect(*this);
	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	if (!Damage || !Source)
	{
		return false;
	}

	// 게이지 설정: 체력이 20% 로 떨어져도 창은 안 열리고 그 타격은 반응한다.
	UClass* const OnPoise[] = { UVBPostureTestWindowOnPoise::StaticClass() };
	UAbilitySystemComponent* PoiseEnemy = F.SpawnEnemy(*this, 0.0f, OnPoise);
	if (!PoiseEnemy)
	{
		return false;
	}

	{
		FHitEventCount Hits(*PoiseEnemy);
		int32 WindowRequests = 0;
		const FDelegateHandle RequestHandle = PoiseEnemy->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_FinisherWindow)
			.AddLambda([&WindowRequests](const FGameplayEventData*) { ++WindowRequests; });
		ON_SCOPE_EXIT { PoiseEnemy->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_FinisherWindow).Remove(RequestHandle); };
		ApplyHit(*Damage, *Source, *PoiseEnemy, 80.0f, 0.0f);
		TestEqual(TEXT("gauge config: no health window request"), WindowRequests, 0);
		TestEqual(TEXT("gauge config: health below the threshold"), Attr(*PoiseEnemy, UVBHealthAttributeSet::GetHealthAttribute()),
			20.0f, 0.01f);
		TestFalse(TEXT("gauge config: low health opens nothing"), UVBAbilitySystemStatics::IsFinisherWindowArmed(PoiseEnemy));
		TestEqual(TEXT("gauge config: the hit reacts"), Hits.Count, 1);
	}

	// 대조군: 체력 설정은 같은 타격에 창을 열고, 창을 연 타격은 반응을 보내지 않는다.
	UClass* const OnHealth[] = { UVBPostureTestWindowOnHealth::StaticClass() };
	UAbilitySystemComponent* HealthEnemy = F.SpawnEnemy(*this, 0.0f, OnHealth);
	if (!HealthEnemy)
	{
		return false;
	}

	{
		FHitEventCount Hits(*HealthEnemy);
		ApplyHit(*Damage, *Source, *HealthEnemy, 10.0f, 0.0f);
		TestFalse(TEXT("health config: above the threshold opens nothing"), UVBAbilitySystemStatics::IsFinisherWindowArmed(HealthEnemy));
		TestEqual(TEXT("health config: that hit reacts"), Hits.Count, 1);
		ApplyHit(*Damage, *Source, *HealthEnemy, 70.0f, 0.0f);
		TestTrue(TEXT("health config: below the threshold opens the window"), UVBAbilitySystemStatics::IsFinisherWindowArmed(HealthEnemy));
		TestEqual(TEXT("health config: the opening hit sends no reaction"), Hits.Count, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureHealthImmunityTest, "VB.Unit.Combat.PostureExecute.HealthWindowImmunity", VBPostureExecuteTest::Flags)
bool FVBPostureHealthImmunityTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	const UGameplayEffect* Damage = ShippedDamageEffect(*this);
	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	if (!Damage || !Source)
	{
		return false;
	}

	UClass* const OnHealth[] = { UVBPostureTestWindowOnHealth::StaticClass() };
	UAbilitySystemComponent* Enemy = F.SpawnEnemy(*this, 0.0f, OnHealth);
	if (!Enemy)
	{
		return false;
	}

	ApplyHit(*Damage, *Source, *Enemy, 80.0f, 0.0f);
	if (!TestTrue(TEXT("fixture: health window armed"), UVBAbilitySystemStatics::IsFinisherWindowArmed(Enemy)))
	{
		return false;
	}

	TestTrue(TEXT("health window: immune tag on"), Enemy->HasMatchingGameplayTag(VBGameplayTags::State_Combat_FinisherWindowImmune));

	// 체력 문턱 창: 일반 타격은 무르고 처형 피해만 받는다.
	ApplyHit(*Damage, *Source, *Enemy, 5.0f, 0.0f);
	TestEqual(TEXT("health window: normal hit refused"), Attr(*Enemy, UVBHealthAttributeSet::GetHealthAttribute()), 20.0f, 0.01f);
	ApplyHit(*Damage, *Source, *Enemy, 5.0f, 0.0f, /*bExecution=*/true);
	TestEqual(TEXT("health window: execution damage lands"), Attr(*Enemy, UVBHealthAttributeSet::GetHealthAttribute()), 15.0f, 0.01f);

	// 창이 닫히면 면역도 풀린다.
	F.Advance(GetDefault<UVBFinisherWindowConfig>()->WindowDuration + 0.1f);
	TestFalse(TEXT("closed: immune tag off"), Enemy->HasMatchingGameplayTag(VBGameplayTags::State_Combat_FinisherWindowImmune));
	ApplyHit(*Damage, *Source, *Enemy, 5.0f, 0.0f);
	TestEqual(TEXT("closed: normal hit lands"), Attr(*Enemy, UVBHealthAttributeSet::GetHealthAttribute()), 10.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureWaitPoseTest, "VB.Unit.Combat.PostureExecute.WaitPoseOnce", VBPostureExecuteTest::Flags)
bool FVBPostureWaitPoseTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	UClass* const Grants[] = { UVBPostureTestWindowWithPose::StaticClass() };
	UAbilitySystemComponent* Enemy = Source ? F.SpawnEnemy(*this, 30.0f, Grants) : nullptr;
	if (!Enemy)
	{
		return false;
	}

	FLogLineCount Poses(TEXT("처형 창 대기 자세"));
	ApplyPoise(*Source, *Enemy, 30.0f);
	if (!TestTrue(TEXT("fixture: window open"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy)))
	{
		return false;
	}

	TestEqual(TEXT("one pose on open (the watch's immediate notice is ignored)"), Poses.Read(), 1);

	// 창 동안 맞은 반응이 끝나면 자세를 한 번 되살린다.
	Enemy->AddLooseGameplayTag(VBGameplayTags::State_Combat_HitReacting);
	Enemy->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_HitReacting);
	TestEqual(TEXT("reaction ended: pose once more"), Poses.Read(), 2);

	// 넘어진 채 맞은 반응이 끝나도 선 자세를 덮지 않는다. 일어나기가 끝나면 그때 되살린다.
	Enemy->AddLooseGameplayTag(VBGameplayTags::State_Combat_Downed);
	Enemy->AddLooseGameplayTag(VBGameplayTags::State_Combat_HitReacting);
	Enemy->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_HitReacting);
	TestEqual(TEXT("lying: no standing pose over a downed body"), Poses.Read(), 2);
	Enemy->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Downed);
	Enemy->AddLooseGameplayTag(VBGameplayTags::State_Combat_GettingUp);
	Enemy->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_GettingUp);
	TestEqual(TEXT("got up: pose once more"), Poses.Read(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureAttackPressTest, "VB.Unit.Combat.PostureExecute.AttackPressDoesNotExecute", VBPostureExecuteTest::Flags)
bool FVBPostureAttackPressTest::RunTest(const FString& Parameters)
{
	// 처형은 처형 키로만 들어온다: 사건 트리거가 없고 공격에는 처형을 여는 스위치가 없다.
	const FArrayProperty* Triggers = FindFProperty<FArrayProperty>(UGameplayAbility::StaticClass(), TEXT("AbilityTriggers"));
	if (!TestNotNull(TEXT("AbilityTriggers property"), Triggers))
	{
		return false;
	}

	const UVBGA_Execute* Execute = GetDefault<UVBGA_Execute>();
	FScriptArrayHelper ExecuteTriggers(Triggers, Triggers->ContainerPtrToValuePtr<void>(Execute));
	TestEqual(TEXT("execute has no event trigger"), ExecuteTriggers.Num(), 0);
	TestTrue(TEXT("execute is bound to the execute key"), Execute->InputTag.MatchesTagExact(VBGameplayTags::Input_Execute));
	TestNull(TEXT("attacks carry no execute opener"), FindFProperty<FProperty>(UVBGA_LightAttack::StaticClass(), TEXT("bCanOpenFinisher")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureHitDuringWindowTest, "VB.Unit.Combat.PostureExecute.HitDuringWindow", VBPostureExecuteTest::Flags)
bool FVBPostureHitDuringWindowTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	const UGameplayEffect* Damage = ShippedDamageEffect(*this);
	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	if (!Damage || !Source)
	{
		return false;
	}

	UClass* const Grants[] = { UVBPostureTestWindowOnPoise::StaticClass(), UVBPostureTestStagger::StaticClass() };
	UAbilitySystemComponent* Enemy = F.SpawnEnemy(*this, 30.0f, Grants);
	if (!Enemy)
	{
		return false;
	}

	ApplyPoise(*Source, *Enemy, 30.0f);
	if (!TestTrue(TEXT("fixture: window open"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy)))
	{
		return false;
	}

	{
		FHitEventCount Hits(*Enemy);
		ApplyHit(*Damage, *Source, *Enemy, 10.0f, 10.0f);
		TestEqual(TEXT("normal hit in the window: health taken"), Attr(*Enemy, UVBHealthAttributeSet::GetHealthAttribute()), 90.0f, 0.01f);
		TestEqual(TEXT("normal hit in the window: reaction sent"), Hits.Count, 1);
		TestEqual(TEXT("normal hit in the window: gauge stays full"), Attr(*Enemy, UVBCombatAttributeSet::GetPoiseAttribute()),
			30.0f, 0.01f);
		TestTrue(TEXT("normal hit in the window: window stays"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy));
	}

	// 치명타: 보통 죽음 경로. 체력 컴포넌트 대역이 알림에 State.Dead 를 붙이고 창은 그것으로 닫힌다.
	Enemy->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 5.0f);
	const UVBHealthAttributeSet* Health = Enemy->GetSet<UVBHealthAttributeSet>();
	if (!TestNotNull(TEXT("health set"), Health))
	{
		return false;
	}

	int32 Announced = 0;
	const FDelegateHandle DeathHandle = Health->OnOutOfHealth.AddLambda([&Announced, Enemy](const FVBDeathFacts&)
	{
		++Announced;
		Enemy->AddLooseGameplayTag(VBGameplayTags::State_Dead);
	});
	ON_SCOPE_EXIT { Health->OnOutOfHealth.Remove(DeathHandle); };
	ApplyHit(*Damage, *Source, *Enemy, 10.0f, 0.0f);
	TestEqual(TEXT("lethal hit in the window: health 0"), Attr(*Enemy, UVBHealthAttributeSet::GetHealthAttribute()), 0.0f, 0.01f);
	TestEqual(TEXT("lethal hit in the window: death announced once"), Announced, 1);
	TestFalse(TEXT("lethal hit in the window: window closed by the death"), UVBAbilitySystemStatics::IsFinisherWindowArmed(Enemy));
	TestFalse(TEXT("lethal hit in the window: no executable corpse"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureWindowCueTest, "VB.Unit.Combat.PostureExecute.SceneReadinessCue", VBPostureExecuteTest::Flags)
bool FVBPostureWindowCueTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this)) { return false; }
	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	UClass* const Grants[] = { UVBPostureTestWindowOnPoise::StaticClass() };
	UAbilitySystemComponent* Enemy = Source ? F.SpawnEnemy(*this, 30.0f, Grants) : nullptr;
	ACharacter* Body = Enemy ? Cast<ACharacter>(Enemy->GetAvatarActor()) : nullptr;
	if (!TestNotNull(TEXT("victim"), Body)) { return false; }
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("fixture skeleton"), Mesh)) { return false; }
	Body->GetMesh()->SetSkeletalMesh(Mesh);
	Body->GetMesh()->SetAnimInstanceClass(UVBEnemyAnimInstance::StaticClass());
	UVBEnemyAnimInstance* Anim = Cast<UVBEnemyAnimInstance>(Body->GetMesh()->GetAnimInstance());
	if (!TestNotNull(TEXT("actual interaction pose owner"), Anim)) { return false; }
	ApplyPoise(*Source, *Enemy, 30.0f);
	const FGameplayTag Cue = VBGameplayTags::GameplayCue_Combat_FinisherWindow;
	TestTrue(TEXT("ready window shows cue"), Enemy->IsGameplayCueActive(Cue));
	FPoseSearchBlueprintResult Result;
	Result.bIsInteraction = true;
	Result.SelectedAnim = NewObject<UAnimMontage>(Anim);
	Anim->SetInteractionResult(Result);
	UVBFinisherComponent::OnInteractionParticipantChanged.Broadcast(Source->GetAvatarActor());
	TestTrue(TEXT("another actor cannot change victim cue"), Enemy->IsGameplayCueActive(Cue));
	UVBFinisherComponent::OnInteractionParticipantChanged.Broadcast(Body);
	UVBFinisherComponent::OnInteractionParticipantChanged.Broadcast(Body);
	TestFalse(TEXT("actual scene suppresses readiness cue"), Enemy->IsGameplayCueActive(Cue));
	TestTrue(TEXT("scene preserves gameplay window"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy));
	Anim->SetInteractionResult(FPoseSearchBlueprintResult());
	UVBFinisherComponent::OnInteractionParticipantChanged.Broadcast(Body);
	UVBFinisherComponent::OnInteractionParticipantChanged.Broadcast(Body);
	TestTrue(TEXT("cancelled scene restores a live window cue"), Enemy->IsGameplayCueActive(Cue));
	Enemy->CancelAbilities();
	TestFalse(TEXT("ability end removes cue exactly once"), Enemy->IsGameplayCueActive(Cue));
	UVBFinisherComponent::OnInteractionParticipantChanged.Broadcast(Body);
	TestFalse(TEXT("late scene event cannot restore an ended window"), Enemy->IsGameplayCueActive(Cue));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureWindowExpiryTest, "VB.Unit.Combat.PostureExecute.WindowExpiry", VBPostureExecuteTest::Flags)
bool FVBPostureWindowExpiryTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	UClass* const Grants[] = { UVBPostureTestWindowOnPoise::StaticClass() };
	UAbilitySystemComponent* Enemy = Source ? F.SpawnEnemy(*this, 30.0f, Grants) : nullptr;
	if (!Enemy)
	{
		return false;
	}

	const float Duration = GetDefault<UVBFinisherWindowConfig>()->WindowDuration;
	ApplyPoise(*Source, *Enemy, 30.0f);
	if (!TestTrue(TEXT("fixture: window open"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy)))
	{
		return false;
	}

	F.Advance(Duration * 0.5f);
	TestTrue(TEXT("half way: still open"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy));
	F.Advance(Duration * 0.5f + 0.1f);
	TestFalse(TEXT("expired: window tag off"), UVBAbilitySystemStatics::IsInFinisherWindow(Enemy));
	TestFalse(TEXT("expired: armed tag off"), UVBAbilitySystemStatics::IsFinisherWindowArmed(Enemy));

	// 적이 풀려난다: 게이지가 다시 깎인다.
	ApplyPoise(*Source, *Enemy, 10.0f);
	TestEqual(TEXT("after expiry: gauge drains again"), Attr(*Enemy, UVBCombatAttributeSet::GetPoiseAttribute()), 20.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPosturePoiseRecoveryTest, "VB.Unit.Combat.PostureExecute.PoiseRecovery", VBPostureExecuteTest::Flags)
bool FVBPosturePoiseRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	UAbilitySystemComponent* Enemy = Source ? F.SpawnEnemy(*this, 100.0f, {}) : nullptr;
	if (!Enemy)
	{
		return false;
	}

	auto Poise = [Enemy] { return Attr(*Enemy, UVBCombatAttributeSet::GetPoiseAttribute()); };

	// 꺼짐(초당 0): 돌아오는 것이 없다.
	ApplyPoise(*Source, *Enemy, 30.0f);
	F.Advance(3.0f);
	TestEqual(TEXT("rate 0: no recovery"), Poise(), 70.0f, 0.01f);

	// 켜짐: 지연 안에서는 그대로, 그 뒤로 초당 양만큼.
	Enemy->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseRecoveryDelaySecondsAttribute(), 1.0f);
	Enemy->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseRecoveryPerSecondAttribute(), 10.0f);
	ApplyPoise(*Source, *Enemy, 10.0f);
	F.Advance(0.5f);
	TestEqual(TEXT("inside the delay: no recovery"), Poise(), 60.0f, 0.01f);
	for (int32 Step = 0; Step < 15; ++Step)
	{
		F.Advance(0.1f);
	}

	TestTrue(FString::Printf(TEXT("after the delay: recovering (%.1f)"), Poise()), Poise() > 60.0f && Poise() <= 100.0f);

	// 창이 켜진 동안 게이지는 창의 몫이라 회복하지 않는다.
	const float Before = Poise();
	Enemy->AddLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);
	for (int32 Step = 0; Step < 10; ++Step)
	{
		F.Advance(0.1f);
	}

	TestEqual(TEXT("armed window: no recovery"), Poise(), Before, 0.01f);
	Enemy->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_FinisherWindowArmed);

	// 지연이 프레임 한가운데서 끝나면 회복은 지연 뒤 흐른 시간(0.2초)만큼이다 - 그 프레임에 타이머가 불린 횟수(약 4번)만큼이 아니다.
	//   (월드 시각은 한 틱에 0.4초까지만 간다 - WorldSettings 의 MaxUndilatedFrameTime. 그래서 0.4초 프레임 셋으로 1.2초를 간다.)
	ApplyPoise(*Source, *Enemy, 20.0f);
	const float AfterHit = Poise();
	F.Advance(0.4f);
	F.Advance(0.4f);
	TestEqual(TEXT("0.8 s after the hit: still inside the delay"), Poise(), AfterHit, 0.01f);
	F.Advance(0.4f);
	TestEqual(TEXT("delay ends mid-frame: recovery = rate x time past the delay"), Poise(), AfterHit + 2.0f, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureUnclaimedBreakTest, "VB.Unit.Combat.PostureExecute.UnclaimedBreakIsLogged", VBPostureExecuteTest::Flags)
bool FVBPostureUnclaimedBreakTest::RunTest(const FString& Parameters)
{
	// 파탄을 받는 능력이 없으면(쿨다운, 차단, 미부여) 게이지 리필만 남는다 - 그 사실이 로그 한 줄로 남는다.
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent* Source = F.SpawnSource(*this);
	UAbilitySystemComponent* Enemy = Source ? F.SpawnEnemy(*this, 30.0f, {}) : nullptr;
	if (!Enemy)
	{
		return false;
	}

	FLogLineCount Unclaimed(TEXT("켜진 능력 없음"));
	ApplyPoise(*Source, *Enemy, 30.0f);
	TestEqual(TEXT("break with no taker: one line"), Unclaimed.Read(), 1);
	TestEqual(TEXT("gauge refilled"), Attr(*Enemy, UVBCombatAttributeSet::GetPoiseAttribute()), 30.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPosturePromptTest, "VB.Unit.Combat.PostureExecute.PromptArmsOnlyForLocalControl", VBPostureExecuteTest::Flags)
bool FVBPosturePromptTest::RunTest(const FString& Parameters)
{
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBIndicatorRegistrySubsystem* Registry = F.World->GetSubsystem<UVBIndicatorRegistrySubsystem>();
	APlayerController* PC = F.World->SpawnActor<APlayerController>();
	APawn* Pawn = F.World->SpawnActor<APawn>();
	if (!TestNotNull(TEXT("registry"), Registry) || !TestNotNull(TEXT("controller"), PC) || !TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}

	auto MakeTarget = [&F]()
	{
		AActor* Target = F.World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Target);
		Target->AddInstanceComponent(Root);
		Target->SetRootComponent(Root);
		Root->RegisterComponent();
		return Target;
	};
	AActor* EnemyA = MakeTarget();
	AActor* EnemyB = MakeTarget();

	UVBPostureTestPromptComponent* Prompt = NewObject<UVBPostureTestPromptComponent>(Pawn);
	Pawn->AddInstanceComponent(Prompt);
	Prompt->RegisterComponent();
	Prompt->BeginPlayForTest();
	ON_SCOPE_EXIT { Prompt->EndPlayForTest(); };

	// 아직 로컬 플레이어의 폰이 아니다: 타이머가 없고, 대상이 있어도 요청이 없다.
	TestFalse(TEXT("unpossessed pawn: timer not armed"), Prompt->IsPromptArmed());
	Prompt->TestTarget = EnemyA;
	Prompt->RefreshPrompt();
	TestFalse(TEXT("unpossessed pawn: no request"), Prompt->GetPromptHandle().IsValid());

	// 진짜 로컬 플레이어 문맥(카메라 피드백 시험과 같다). 입력, 온라인 초기화는 하지 않는다.
	if (!TestNotNull(TEXT("engine"), GEngine))
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
	PC->Player = LocalPlayer;
	LocalPlayer->PlayerController = PC;
	ON_SCOPE_EXIT { PC->Player = nullptr; };
	PC->Possess(Pawn);
	if (!TestTrue(TEXT("fixture: local pawn"), Pawn->IsLocallyControlled()))
	{
		return false;
	}

	TestTrue(TEXT("possessed by the local player: timer armed"), Prompt->IsPromptArmed());
	Prompt->RefreshPrompt();
	const FVBIndicatorHandle First = Prompt->GetPromptHandle();
	FVBIndicatorRequest Request;
	TestTrue(TEXT("target in reach: one request registered"), Registry->TryGet(First, Request));
	TestTrue(TEXT("request names the target"), Request.Subject.Get() == EnemyA
		&& Request.AnchorComponent.Get() == EnemyA->GetRootComponent());
	TestEqual(TEXT("request group"), Request.Group, FGameplayTag(VBGameplayTags::Indicator_Group_Execute));
	TestTrue(TEXT("request is for this viewer only"), Request.ViewerScope == EVBIndicatorViewerScope::OneViewer
		&& Request.Viewer.Get() == PC);
	TestEqual(TEXT("one request for this viewer"), Registry->SnapshotForViewer(PC).Num(), 1);

	// 같은 대상: 같은 요청이고 두 번째 요청은 없다.
	Prompt->RefreshPrompt();
	TestTrue(TEXT("same target keeps the request"), Prompt->GetPromptHandle() == First);
	TestEqual(TEXT("still one request"), Registry->SnapshotForViewer(PC).Num(), 1);

	// 다른 대상: 옛 요청을 떼고 새 요청 하나.
	Prompt->TestTarget = EnemyB;
	Prompt->RefreshPrompt();
	TestFalse(TEXT("target changed: old request removed"), Registry->IsRegistered(First));
	TestTrue(TEXT("target changed: prompt on the new target"), Prompt->GetPromptTarget() == EnemyB);
	TestEqual(TEXT("target changed: still one request"), Registry->SnapshotForViewer(PC).Num(), 1);

	// 사거리를 벗어나거나 창이 닫히면(처형 판정이 답이 없으면) 뗀다.
	const FVBIndicatorHandle Second = Prompt->GetPromptHandle();
	Prompt->TestTarget = nullptr;
	Prompt->RefreshPrompt();
	TestFalse(TEXT("no target: request removed"), Registry->IsRegistered(Second));
	TestEqual(TEXT("no target: nothing for this viewer"), Registry->SnapshotForViewer(PC).Num(), 0);

	// 조종을 잃으면 타이머를 끄고 걸어 둔 요청도 뗀다.
	Prompt->TestTarget = EnemyA;
	Prompt->RefreshPrompt();
	const FVBIndicatorHandle Held = Prompt->GetPromptHandle();
	TestTrue(TEXT("fixture: request held before unpossess"), Registry->IsRegistered(Held));
	PC->UnPossess();
	TestFalse(TEXT("unpossessed: timer disarmed"), Prompt->IsPromptArmed());
	TestFalse(TEXT("unpossessed: request removed"), Registry->IsRegistered(Held));

	// 처형 능력이 없는 폰에서는 진짜 대상 질의가 답이 없다.
	UVBExecutePromptComponent* Real = NewObject<UVBExecutePromptComponent>(Pawn);
	Pawn->AddInstanceComponent(Real);
	Real->RegisterComponent();
	Real->RefreshPrompt();
	TestFalse(TEXT("no execute ability: no request"), Real->GetPromptHandle().IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPostureExecuteKeyReportTest, "VB.Unit.Combat.PostureExecute.ExecuteKeyReportsPerBody", VBPostureExecuteTest::Flags)
bool FVBPostureExecuteKeyReportTest::RunTest(const FString& Parameters)
{
	// 처형 키에 능력 둘이 묶인 설정 사고는 몸마다 한 번 말한다 - 같은 몸이 다시 물어도 조용하고, 다른 몸은 다시 듣는다.
	using namespace VBPostureExecuteTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent* First = F.SpawnSource(*this);
	UAbilitySystemComponent* Second = F.SpawnSource(*this);
	if (!First || !Second)
	{
		return false;
	}

	for (UAbilitySystemComponent* ASC : { First, Second })
	{
		ASC->GiveAbility(FGameplayAbilitySpec(UVBPostureTestExecute::StaticClass(), 1));
		ASC->GiveAbility(FGameplayAbilitySpec(UVBPostureTestExecute::StaticClass(), 1));
	}

	AddExpectedMessagePlain(TEXT("능력 2개가 묶였다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	// 시험 몸에는 락온 컴포넌트가 없다 - 대상 질의가 그 사고를 말한다(질의마다, 이 시험이 보는 것이 아니다).
	AddExpectedMessagePlain(TEXT("UVBTargetLockComponent"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
	TestNull(TEXT("first body, first ask"), UVBAbilitySystemStatics::FindExecuteTarget(First));
	TestNull(TEXT("first body, second ask"), UVBAbilitySystemStatics::FindExecuteTarget(First));
	TestNull(TEXT("second body"), UVBAbilitySystemStatics::FindExecuteTarget(Second));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
