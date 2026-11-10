// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Unit/VBEnemyDeathTestProbe.h"
#include "Unit/VBTestAbilityTags.h"
#include "Unit/VBAbilitySetTestEffects.h"

#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AI/VBEnemyCombatComponent.h"
#include "Animation/AnimMontage.h"
#include "CharacterCommon/VBHealthComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Data/VBBossConfig.h"
#include "Data/VBEnemyConfig.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Player/VBPlayerState.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
// 아래 시험 전용 Health 효과의 SetByCaller 이름 키. 시험 모듈은 네이티브 GameplayTag 를 가질 수 없다(VerifyModuleCanContainGameplayTag).
static const FName VBEnemyDeathTestHealthDelta(TEXT("VBEnemyDeathTest.HealthDelta"));

UVBEnemyDeathTestReputationEffect::UVBEnemyDeathTestReputationEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UVBReputationAttributeSet::GetIncomingReputationChangeAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat Amount;
	Amount.DataTag = VBGameplayTags::Data_ReputationChange;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
}

UVBEnemyDeathTestHealthEffect::UVBEnemyDeathTestHealthEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UVBHealthAttributeSet::GetHealthAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat Amount;
	Amount.DataName = VBEnemyDeathTestHealthDelta;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
}

UVBEnemyDeathTestAbility::UVBEnemyDeathTestAbility()
{
	InputTag = VBGameplayTags::Input_Attack_Light;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	SetAssetTags(FGameplayTagContainer(VBTestAbilityTags::AnyAbility));
}

void UVBEnemyDeathTestAbility::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Downed);
	ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
}

void UVBEnemyDeathTestAbility::EndAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	bool bReplicateEndAbility, bool bWasCancelled)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
	ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Downed);
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

AVBEnemyDeathTestEnemy::AVBEnemyDeathTestEnemy(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVBEnemyDeathTestPresentation>(TEXT("DeathPresentationComponent")))
{
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AVBEnemyDeathTestEnemy::Configure(UVBEnemyConfig* Config)
{
	EnemyConfig = Config;
	DefaultDeathReputationEffect = UVBEnemyDeathTestReputationEffect::StaticClass();
	AbilitySet = VBAbilitySetTest::MakeSet(this, { VBAbilitySetTest::Ability(UVBEnemyDeathTestAbility::StaticClass()) }, {});
}

void AVBEnemyDeathTestEnemy::SetInteractionMomentum(const FVector& Velocity)
{
	DeathPresentationComponent->KeepMomentum(Velocity);
}

void AVBEnemyDeathTestEnemy::BeforeDeathTag(const FVBDeathFacts& Facts)
{
	++BeforeCalls;
	Steps.Add(TEXT("Before"));
	bDownedAtBefore = UVBAbilitySystemStatics::IsDowned(AbilitySystemComponent);
	Super::BeforeDeathTag(Facts);
}

void AVBEnemyDeathTestEnemy::AfterDeathTag(const FVBDeathFacts& Facts)
{
	++AfterCalls;
	Steps.Add(TEXT("AfterEntry"));
	bCancelledAtAfter = !UVBAbilitySystemStatics::IsDowned(AbilitySystemComponent)
		&& !AbilitySystemComponent->HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking);
	Super::AfterDeathTag(Facts);
	Steps.Add(TEXT("After"));
}

void AVBEnemyDeathTestEnemy::PresentDeath(const FVBDeathFacts& Facts)
{
	++PresentCalls;
	Super::PresentDeath(Facts);
	bPresentationReturned = true;
	Steps.Add(TEXT("Present"));
}

void UVBEnemyDeathTestPresentation::MulticastPresent_Implementation(const FVBEnemyDeathState& State)
{
	if (AVBEnemyDeathTestEnemy* Enemy = Cast<AVBEnemyDeathTestEnemy>(GetOwner()))
	{
		++Enemy->MulticastCalls;
		Enemy->SentState = State;
	}
	else if (AVBEnemyDeathTestBoss* Boss = Cast<AVBEnemyDeathTestBoss>(GetOwner()))
	{
		++Boss->MulticastCalls;
	}
}

void FVBEnemyDeathTestKilledEffect::Apply(const FVBRuleContext& Context) const
{
	if (AVBEnemyDeathTestEnemy* Enemy = Cast<AVBEnemyDeathTestEnemy>(Context.Counterpart))
	{
		++Enemy->KilledEffects;
		Enemy->bKillEventPrecededEffect = Enemy->KillEvents == 1;
		Enemy->Steps.Add(TEXT("KillEffect"));
	}
}

AVBEnemyDeathTestBoss::AVBEnemyDeathTestBoss(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVBEnemyDeathTestPresentation>(TEXT("DeathPresentationComponent")))
{
	AutoPossessAI = EAutoPossessAI::Disabled;
}

void AVBEnemyDeathTestBoss::Configure(UVBEnemyConfig* Config)
{
	EnemyConfig = Config;
	DefaultDeathReputationEffect = UVBEnemyDeathTestReputationEffect::StaticClass();
	AbilitySet = VBAbilitySetTest::MakeSet(this, { VBAbilitySetTest::Ability(UVBEnemyDeathTestAbility::StaticClass()) }, {});
}

void AVBEnemyDeathTestBoss::BeforeDeathTag(const FVBDeathFacts& Facts)
{
	++BeforeCalls;
	if (BeforeCalls == 1 && DuringBefore)
	{
		DuringBefore();
	}

	Super::BeforeDeathTag(Facts);
}

void AVBEnemyDeathTestBoss::AfterDeathTag(const FVBDeathFacts& Facts)
{
	Super::AfterDeathTag(Facts);
	if (bSeedStaleEngagement)
	{
		bEngaged = true;
	}
}

void AVBEnemyDeathTestBoss::PresentDeath(const FVBDeathFacts& Facts)
{
	++PresentCalls;
	Super::PresentDeath(Facts);
	bPresentationReturned = true;
}

#if WITH_DEV_AUTOMATION_TESTS
namespace VBEnemyDeathTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		UVBGameplayMessageSubsystem* Bus = nullptr;
		AVBPlayerState* Killer = nullptr;
		UVBAbilitySystemComponent* KillerASC = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			UGameInstance* Instance = nullptr;
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
						&& Context.World() && Context.World()->GetGameInstance())
					{
						Instance = Context.World()->GetGameInstance();
						break;
					}
				}
			}

			if (!Test.TestNotNull(TEXT("initialized game instance required (-game automation)"), Instance))
			{
				return false;
			}

			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("isolated enemy world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->SetGameInstance(Instance);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Bus = UVBGameplayMessageSubsystem::Get(World);
			if (!Test.TestNotNull(TEXT("real game-instance death bus"), Bus))
			{
				return false;
			}

			Killer = World->SpawnActor<AVBPlayerState>();
			if (!Test.TestNotNull(TEXT("actual player-state instigator"), Killer))
			{
				return false;
			}

			KillerASC = Cast<UVBAbilitySystemComponent>(Killer->GetAbilitySystemComponent());
			if (!Test.TestNotNull(TEXT("player ASC"), KillerASC))
			{
				return false;
			}

			KillerASC->InitAbilityActorInfo(Killer, Killer);
			return Test.TestNotNull(TEXT("actual reputation set"), KillerASC->GetSet<UVBReputationAttributeSet>());
		}

		template <class TEnemy, class TConfig = UVBEnemyConfig>
		TEnemy* Spawn(FAutomationTestBase& Test)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			TEnemy* Enemy = World->SpawnActor<TEnemy>(SpawnParameters);
			if (!Test.TestNotNull(TEXT("concrete native enemy"), Enemy))
			{
				return nullptr;
			}

			TConfig* Config = NewObject<TConfig>(Enemy);
			Config->AttackAbilityInputTag = VBGameplayTags::Input_Attack_Light;
			Config->DeathReputationChange = -7.0f;
			Config->EnemyTags.AddTag(VBGameplayTags::State_Medical_Infected);
			Config->DeathMontages.Add(NewObject<UAnimMontage>(Config));
			Enemy->Configure(Config);
			UVBAbilitySystemComponent* ASC = Cast<UVBAbilitySystemComponent>(Enemy->GetAbilitySystemComponent());
			if (!Test.TestNotNull(TEXT("enemy ASC"), ASC))
			{
				return nullptr;
			}

			ASC->InitAbilityActorInfo(Enemy, Enemy);
			if (!Test.TestNotNull(TEXT("registered health set"), ASC->GetSet<UVBHealthAttributeSet>()))
			{
				return nullptr;
			}

			ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
			ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
			Enemy->DispatchBeginPlay();
			if (!Test.TestNotNull(TEXT("constructor health component"), UVBHealthComponent::FindFor(Enemy)))
			{
				return nullptr;
			}

			return Enemy;
		}

		~FFixture()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
				Package->SetDirtyFlag(false);
			}
		}
	};

	bool ChangeHealth(FAutomationTestBase& Test, AVBEnemyBase& Enemy, AActor* Instigator, float Delta, bool bMedical = false)
	{
		UAbilitySystemComponent* ASC = Enemy.GetAbilitySystemComponent();
		FGameplayEffectContextHandle Context(new FGameplayEffectContext());
		Context.AddInstigator(Instigator, Instigator);
		FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(UVBEnemyDeathTestHealthEffect::StaticClass(), 1.0f, Context);
		if (!Test.TestTrue(TEXT("actual Health GE spec"), Spec.IsValid()))
		{
			return false;
		}

		Spec.Data->SetSetByCallerMagnitude(VBEnemyDeathTestHealthDelta, Delta);
		if (bMedical)
		{
			Spec.Data->AddDynamicAssetTag(VBGameplayTags::Effect_Execute);
		}

		ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		return true;
	}

	void AddKilledEffect(AVBEnemyDeathTestEnemy& Enemy)
	{
		UVBEnemyConfig* Config = const_cast<UVBEnemyConfig*>(Enemy.GetEnemyConfig());
		FInstancedStruct Effect;
		Effect.InitializeAs<FVBEnemyDeathTestKilledEffect>();
		Config->OnKilledEffects.Add(MoveTemp(Effect));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthEnemyOrderTest, "VB.Unit.Health.Enemy.OrderAndDowned", VBEnemyDeathTest::Flags)
bool FVBHealthEnemyOrderTest::RunTest(const FString& Parameters)
{
	using namespace VBEnemyDeathTest;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBEnemyDeathTestEnemy* Enemy = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	if (!Enemy)
	{
		return false;
	}

	AddKilledEffect(*Enemy);
	UVBHealthComponent* Health = UVBHealthComponent::FindFor(Enemy);
	UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	UVBEnemyCombatComponent* Combat = Enemy->GetCombatComponent();
	UVBCombatCoordinatorSubsystem* Coordinator = Fixture.World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
	if (!TestNotNull(TEXT("real coordinator"), Coordinator))
	{
		return false;
	}

	Combat->SetCombatTarget(Fixture.Killer);
	if (!Combat->HasAttackToken())
	{
		Coordinator->TryAcquireAttackToken(Combat, 1);
	}

	if (!TestTrue(TEXT("positive token precondition"), Combat->HasAttackToken()))
	{
		return false;
	}

	if (!TestTrue(TEXT("live cancellable ability"), ASC->TryActivateAbilityByClass(UVBEnemyDeathTestAbility::StaticClass())))
	{
		return false;
	}

	TestTrue(TEXT("Downed exists before lethal write"), UVBAbilitySystemStatics::IsDowned(ASC));
	int32 ReputationEvents = 0;
	const FDelegateHandle Reputation = Fixture.KillerASC->GetGameplayAttributeValueChangeDelegate(
		UVBReputationAttributeSet::GetReputationAttribute()).AddLambda([&](const FOnAttributeChangeData&)
	{
		++ReputationEvents;
		Enemy->Steps.Add(TEXT("Reputation"));
		TestTrue(TEXT("S0 already latched at reputation"), Health->HasStartedDeath());
		TestFalse(TEXT("reputation precedes Dead tag"), UVBAbilitySystemStatics::IsDead(ASC));
	});
	UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(Enemy);
	if (!TestNotNull(TEXT("actual narrative event source"), Events))
	{
		return false;
	}

	const FDelegateHandle Killed = Events->OnGameEvent.AddLambda([&](const FVBGameEvent& Event)
	{
		if (Event.EventTag != VBGameplayTags::Event_Enemy_Killed)
		{
			return;
		}

		++Enemy->KillEvents;
		Enemy->Steps.Add(TEXT("KillEvent"));
		TestTrue(TEXT("kill has actual player instigator"), Event.Instigator == Fixture.Killer);
		TestTrue(TEXT("kill retains configured target tags"), Event.TargetTags == Enemy->GetEnemyConfig()->EnemyTags);
		TestTrue(TEXT("cancel already removed Downed"), Enemy->bCancelledAtAfter);
		TestTrue(TEXT("combat leave follows kill effects"), Combat->IsInCombat());
	});
	int32 Completed = 0, Messages = 0;
	auto Observe = [&](const FVBDeathFacts& Facts)
	{
		TestTrue(TEXT("facts identify real producer and instigator"), Facts.DeadActor == Enemy
			&& Facts.Instigator == Fixture.Killer && !Facts.bMedical);
		TestTrue(TEXT("presentation sent before each S6 callback"), Enemy->bPresentationReturned && Enemy->MulticastCalls == 1
			&& Enemy->SentState.bDead);
		TestFalse(TEXT("S6 sees combat left"), Combat->IsInCombat());
		TestFalse(TEXT("S6 sees token returned"), Combat->HasAttackToken());
	};
	const FDelegateHandle Completion = Health->OnDeathCompleted.AddLambda([&](const FVBDeathFacts& Facts) { ++Completed; Observe(Facts); });
	FVBMessageListenerHandle Message = Fixture.Bus->RegisterListener<FVBDeathFacts>(VBGameplayTags::Message_Death,
		[&](FGameplayTag, const FVBDeathFacts& Facts) { if (Facts.DeadActor == Enemy) { ++Messages; Observe(Facts); TestEqual(TEXT("delegate precedes bus"), Completed, 1); } });
	ON_SCOPE_EXIT
	{
		Message.Unregister();
		Health->OnDeathCompleted.Remove(Completion);
		Events->OnGameEvent.Remove(Killed);
		Fixture.KillerASC->GetGameplayAttributeValueChangeDelegate(UVBReputationAttributeSet::GetReputationAttribute()).Remove(Reputation);
	};
	if (!ChangeHealth(*this, *Enemy, Fixture.Killer, -200.0f))
	{
		return false;
	}

	TestEqual(TEXT("one reputation delta"), ReputationEvents, 1);
	TestEqual(TEXT("configured reputation applied"),
		Fixture.KillerASC->GetNumericAttribute(UVBReputationAttributeSet::GetReputationAttribute()), -7.0f);
	TestTrue(TEXT("event precedes actual killed effect"), Enemy->KillEvents == 1 && Enemy->KilledEffects == 1
		&& Enemy->bKillEventPrecededEffect);
	TestTrue(TEXT("downed snapshot survives ability cancellation"), Enemy->bDownedAtBefore && Enemy->bCancelledAtAfter
		&& Enemy->SentState.DeathMontage == nullptr);
	TestTrue(TEXT("real corpse transport and timestamp preserved"), Enemy->SentState.bDead && Enemy->SentState.ServerDeathTime >= 0.0);
	TestEqual(TEXT("completion once"), Completed, 1);
	TestEqual(TEXT("bus once"), Messages, 1);
	const TArray<FName> Expected = {TEXT("Before"), TEXT("Reputation"), TEXT("AfterEntry"), TEXT("KillEvent"), TEXT("KillEffect"), TEXT("After"), TEXT("Present")};
	TestTrue(TEXT("actual cross-system order"), Enemy->Steps == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthEnemyMedicalTest, "VB.Unit.Health.Enemy.MedicalKeepsKillEffects", VBEnemyDeathTest::Flags)
bool FVBHealthEnemyMedicalTest::RunTest(const FString& Parameters)
{
	using namespace VBEnemyDeathTest;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBEnemyDeathTestEnemy* Enemy = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	if (!Enemy)
	{
		return false;
	}

	AddKilledEffect(*Enemy);
	UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(Enemy);
	if (!TestNotNull(TEXT("narrative events"), Events))
	{
		return false;
	}

	const FDelegateHandle Killed = Events->OnGameEvent.AddLambda([&](const FVBGameEvent& Event)
	{
		if (Event.EventTag == VBGameplayTags::Event_Enemy_Killed && Event.Instigator == Fixture.Killer)
		{
			++Enemy->KillEvents;
		}
	});
	bool bSawMedicalFacts = false;
	FVBMessageListenerHandle Message = Fixture.Bus->RegisterListener<FVBDeathFacts>(VBGameplayTags::Message_Death,
		[&](FGameplayTag, const FVBDeathFacts& Facts) { if (Facts.DeadActor == Enemy) bSawMedicalFacts = Facts.bMedical && Facts.Instigator == Fixture.Killer; });
	ON_SCOPE_EXIT { Message.Unregister(); Events->OnGameEvent.Remove(Killed); };
	if (!ChangeHealth(*this, *Enemy, Fixture.Killer, -200.0f, true))
	{
		return false;
	}

	TestTrue(TEXT("actual finisher asset tag reaches medical Facts"), bSawMedicalFacts);
	TestEqual(TEXT("medical default reputation skipped"),
		Fixture.KillerASC->GetNumericAttribute(UVBReputationAttributeSet::GetReputationAttribute()), 0.0f);
	TestTrue(TEXT("medical still announces kill before effects"), Enemy->KillEvents == 1 && Enemy->KilledEffects == 1
		&& Enemy->bKillEventPrecededEffect);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthEnemyNonPlayerTest, "VB.Unit.Health.Enemy.NonPlayerAndNoInstigator", VBEnemyDeathTest::Flags)
bool FVBHealthEnemyNonPlayerTest::RunTest(const FString& Parameters)
{
	using namespace VBEnemyDeathTest;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBEnemyDeathTestEnemy* Attacker = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	AVBEnemyDeathTestEnemy* Victim = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	AVBEnemyDeathTestEnemy* Environmental = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	if (!Attacker || !Victim || !Environmental)
	{
		return false;
	}

	AddKilledEffect(*Victim);
	AddKilledEffect(*Environmental);
	UVBGameEventSubsystem* Events = UVBGameEventSubsystem::Get(Victim);
	if (!TestNotNull(TEXT("narrative events"), Events))
	{
		return false;
	}

	int32 Kills = 0, Messages = 0;
	const FDelegateHandle Killed = Events->OnGameEvent.AddLambda([&](const FVBGameEvent& Event) { if (Event.EventTag == VBGameplayTags::Event_Enemy_Killed) ++Kills; });
	FVBMessageListenerHandle Message = Fixture.Bus->RegisterListener<FVBDeathFacts>(VBGameplayTags::Message_Death,
		[&](FGameplayTag, const FVBDeathFacts& Facts)
	{
		if (Facts.DeadActor == Victim) { ++Messages; TestTrue(TEXT("enemy instigator kept"), Facts.Instigator == Attacker); }
		if (Facts.DeadActor == Environmental) { ++Messages; TestNull(TEXT("direct write has no instigator"), Facts.Instigator.Get()); }
	});
	ON_SCOPE_EXIT { Message.Unregister(); Events->OnGameEvent.Remove(Killed); };
	if (!ChangeHealth(*this, *Victim, Attacker, -200.0f))
	{
		return false;
	}

	Environmental->GetAbilitySystemComponent()->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 0.0f);
	TestEqual(TEXT("both deaths use shared producer"), Messages, 2);
	TestEqual(TEXT("non-player deaths do not become world quest kills"), Kills, 0);
	TestTrue(TEXT("non-player killed effects skipped"), Victim->KilledEffects == 0 && Environmental->KilledEffects == 0);
	TestEqual(TEXT("unrelated player reputation untouched"),
		Fixture.KillerASC->GetNumericAttribute(UVBReputationAttributeSet::GetReputationAttribute()), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthEnemyPresentationTest, "VB.Unit.Health.Enemy.PresentationAndOnce", VBEnemyDeathTest::Flags)
bool FVBHealthEnemyPresentationTest::RunTest(const FString& Parameters)
{
	using namespace VBEnemyDeathTest;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBEnemyDeathTestEnemy* Standing = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	AVBEnemyDeathTestEnemy* Interaction = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	if (!Standing || !Interaction)
	{
		return false;
	}

	const FVector Launch(100.0, 20.0, 30.0);
	Interaction->SetInteractionMomentum(Launch);
	if (!ChangeHealth(*this, *Standing, Fixture.Killer, -200.0f) || !ChangeHealth(*this, *Interaction, Fixture.Killer, -200.0f))
	{
		return false;
	}

	TestTrue(TEXT("standing chooses the configured montage once"),
		Standing->SentState.DeathMontage == Standing->GetEnemyConfig()->DeathMontages[0] && Standing->SentState.bHasDeathMontage);
	TestTrue(TEXT("interaction death keeps launch without standing montage"), Interaction->SentState.bKeepMomentum
		&& Interaction->SentState.DeathMontage == nullptr && FVector(Interaction->SentState.LaunchVelocity).Equals(Launch));
	UAbilitySystemComponent* ASC = Standing->GetAbilitySystemComponent();
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
	if (!ChangeHealth(*this, *Standing, Fixture.Killer, -200.0f))
	{
		return false;
	}

	TestTrue(TEXT("numerical healing does not reset body lifetime"), Standing->BeforeCalls == 1 && Standing->AfterCalls == 1
		&& Standing->PresentCalls == 1 && Standing->MulticastCalls == 1);
	TestEqual(TEXT("no second reputation award"),
		Fixture.KillerASC->GetNumericAttribute(UVBReputationAttributeSet::GetReputationAttribute()), -14.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthBossNestedTest, "VB.Unit.Health.Boss.NestedNoPhase", VBEnemyDeathTest::Flags)
bool FVBHealthBossNestedTest::RunTest(const FString& Parameters)
{
	using namespace VBEnemyDeathTest;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBEnemyDeathTestBoss* Boss = Fixture.Spawn<AVBEnemyDeathTestBoss, UVBBossConfig>(*this);
	if (!Boss)
	{
		return false;
	}

	UVBBossConfig* Config = const_cast<UVBBossConfig*>(Boss->GetBossConfig());
	FVBBossPhase& First = Config->AdditionalPhases.AddDefaulted_GetRef();
	First.HealthRatioThreshold = 0.5f;
	First.AttackInputTag = VBGameplayTags::Input_Attack_Light;
	FVBBossPhase& Second = Config->AdditionalPhases.AddDefaulted_GetRef();
	Second.HealthRatioThreshold = 0.25f;
	Second.AttackInputTag = VBGameplayTags::Input_Attack_Light;
	UVBHealthComponent* Health = UVBHealthComponent::FindFor(Boss);
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 40.0f);
	if (!TestEqual(TEXT("healthy numerical subscription really advances a phase"), Boss->GetCurrentPhaseIndex(), 1))
	{
		return false;
	}

	int32 LethalEntries = 0;
	const UVBHealthAttributeSet* Set = ASC->GetSet<UVBHealthAttributeSet>();
	const FDelegateHandle Entry = Set->OnOutOfHealth.AddLambda([&](const FVBDeathFacts&) { ++LethalEntries; });
	ON_SCOPE_EXIT { Set->OnOutOfHealth.Remove(Entry); Boss->DuringBefore = nullptr; };
	Boss->DuringBefore = [&]
	{
		TestTrue(TEXT("S0 latch exists before Dead"), Health->HasStartedDeath());
		TestFalse(TEXT("nested fixture really precedes Dead tag"), UVBAbilitySystemStatics::IsDead(ASC));
		ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
		ChangeHealth(*this, *Boss, Fixture.Killer, -90.0f);
		TestEqual(TEXT("actual below-threshold nested write"),
			ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 10.0f);
		TestEqual(TEXT("nested living-valued broadcast cannot enter phases"), Boss->GetCurrentPhaseIndex(), 1);
		ChangeHealth(*this, *Boss, Fixture.Killer, -200.0f);
	};
	if (!ChangeHealth(*this, *Boss, Fixture.Killer, -200.0f))
	{
		return false;
	}

	TestEqual(TEXT("two actual lethal crossings reach set consumers"), LethalEntries, 2);
	TestTrue(TEXT("one outer boss death"), Boss->BeforeCalls == 1 && Boss->PresentCalls == 1 && Boss->MulticastCalls == 1);
	TestEqual(TEXT("dead boss never entered another phase"), Boss->GetCurrentPhaseIndex(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthBossPhaseTeardownTest, "VB.Unit.Health.Boss.PhaseTeardownBeforeReputation", VBEnemyDeathTest::Flags)
bool FVBHealthBossPhaseTeardownTest::RunTest(const FString& Parameters)
{
	using namespace VBEnemyDeathTest;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBEnemyDeathTestBoss* Boss = Fixture.Spawn<AVBEnemyDeathTestBoss, UVBBossConfig>(*this);
	if (!Boss)
	{
		return false;
	}

	UClass* AuthoredBossClass = LoadClass<AVBBossBase>(nullptr, TEXT("/Game/VB/Blueprints/Enemy/BP_BossTroll.BP_BossTroll_C"));
	UAnimMontage* Taunt = LoadObject<UAnimMontage>(nullptr,
		TEXT("/Game/VB/Animations/Montage/Enemy/AM_Boss_Taunt_02.AM_Boss_Taunt_02"));
	if (!TestNotNull(TEXT("authored troll body class"), AuthoredBossClass) || !TestNotNull(TEXT("authored transition montage"), Taunt))
	{
		return false;
	}

	USkeletalMeshComponent* TemplateMesh = AuthoredBossClass->GetDefaultObject<AVBBossBase>()->GetMesh();
	if (!TestNotNull(TEXT("authored troll skeletal mesh"), TemplateMesh->GetSkeletalMeshAsset())
		|| !TestNotNull(TEXT("authored troll anim class"), TemplateMesh->GetAnimClass()))
	{
		return false;
	}

	Boss->GetMesh()->SetSkeletalMeshAsset(TemplateMesh->GetSkeletalMeshAsset());
	Boss->GetMesh()->SetAnimInstanceClass(TemplateMesh->GetAnimClass());
	if (!TestNotNull(TEXT("real transition anim instance"), Boss->GetMesh()->GetAnimInstance()))
	{
		return false;
	}

	UVBBossConfig* Config = const_cast<UVBBossConfig*>(Boss->GetBossConfig());
	FVBBossPhase& Phase = Config->AdditionalPhases.AddDefaulted_GetRef();
	Phase.HealthRatioThreshold = 0.5f;
	Phase.AttackInputTag = VBGameplayTags::Input_Attack_Light;
	Phase.TransitionMontage = Taunt;
	Phase.bSuperArmorDuringTransition = true;
	UAbilitySystemComponent* ASC = Boss->GetAbilitySystemComponent();
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 40.0f);
	if (!TestTrue(TEXT("real montage opened a live transition"), Boss->IsInPhaseTransition()))
	{
		return false;
	}

	TestTrue(TEXT("positive transition tag"), ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_PhaseTransition));
	TestTrue(TEXT("positive transition armor"), ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_SuperArmor));
	int32 ReputationEvents = 0;
	const FDelegateHandle Reputation = Fixture.KillerASC->GetGameplayAttributeValueChangeDelegate(
		UVBReputationAttributeSet::GetReputationAttribute()).AddLambda([&](const FOnAttributeChangeData&)
	{
		++ReputationEvents;
		TestFalse(TEXT("phase teardown precedes inherited reputation"), Boss->IsInPhaseTransition());
		TestFalse(TEXT("transition tag removed before reputation"),
			ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_PhaseTransition));
		TestFalse(TEXT("transition armor removed before reputation"), ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_SuperArmor));
		TestFalse(TEXT("this observes S1 before Dead"), UVBAbilitySystemStatics::IsDead(ASC));
	});
	ON_SCOPE_EXIT { Fixture.KillerASC->GetGameplayAttributeValueChangeDelegate(UVBReputationAttributeSet::GetReputationAttribute()).Remove(Reputation); };
	if (!ChangeHealth(*this, *Boss, Fixture.Killer, -200.0f))
	{
		return false;
	}

	TestEqual(TEXT("teardown observation actually ran"), ReputationEvents, 1);
	TestFalse(TEXT("dead boss transition ended"), Boss->IsInPhaseTransition());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthBossEngagementTest, "VB.Unit.Health.Boss.EngagementBeforeSignals", VBEnemyDeathTest::Flags)
bool FVBHealthBossEngagementTest::RunTest(const FString& Parameters)
{
	using namespace VBEnemyDeathTest;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBEnemyDeathTestBoss* Boss = Fixture.Spawn<AVBEnemyDeathTestBoss, UVBBossConfig>(*this);
	if (!Boss)
	{
		return false;
	}

	Boss->GetCombatComponent()->SetCombatTarget(Fixture.Killer);
	if (!TestTrue(TEXT("actual combat listener engages boss"), Boss->IsEngaged()))
	{
		return false;
	}
	// 전투 알림과 별개로 PresentDeath 의 명시적 fallback 을 돌려 본다.
	Boss->bSeedStaleEngagement = true;
	UVBHealthComponent* Health = UVBHealthComponent::FindFor(Boss);
	int32 Completed = 0, Messages = 0;
	auto Observe = [&]
	{
		TestTrue(TEXT("boss presentation already returned"), Boss->bPresentationReturned && Boss->MulticastCalls == 1);
		TestFalse(TEXT("boss engagement cleared before either signal"), Boss->IsEngaged());
		TestFalse(TEXT("boss combat registration cleared"), Boss->GetCombatComponent()->IsInCombat());
	};
	const FDelegateHandle Completion = Health->OnDeathCompleted.AddLambda([&](const FVBDeathFacts&) { ++Completed; Observe(); });
	FVBMessageListenerHandle Message = Fixture.Bus->RegisterListener<FVBDeathFacts>(VBGameplayTags::Message_Death,
		[&](FGameplayTag, const FVBDeathFacts& Facts) { if (Facts.DeadActor == Boss) { ++Messages; Observe(); } });
	ON_SCOPE_EXIT { Message.Unregister(); Health->OnDeathCompleted.Remove(Completion); };
	if (!ChangeHealth(*this, *Boss, Fixture.Killer, -200.0f))
	{
		return false;
	}

	TestEqual(TEXT("boss completion once"), Completed, 1);
	TestEqual(TEXT("boss bus once"), Messages, 1);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthEnemyAirborneTest, "VB.Unit.Health.Enemy.AirbornePresentation", VBEnemyDeathTest::Flags)
bool FVBHealthEnemyAirborneTest::RunTest(const FString& Parameters)
{
	using namespace VBEnemyDeathTest;
	FFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	AVBEnemyDeathTestEnemy* Falling = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	AVBEnemyDeathTestEnemy* Standing = Fixture.Spawn<AVBEnemyDeathTestEnemy>(*this);
	if (!Falling || !Standing)
	{
		return false;
	}

	Falling->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	Falling->GetCharacterMovement()->Velocity = FVector(40.0f, 0.0f, -150.0f);
	Standing->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (!ChangeHealth(*this, *Falling, Fixture.Killer, -200.0f)
		|| !ChangeHealth(*this, *Standing, Fixture.Killer, -200.0f))
	{
		return false;
	}

	TestTrue(TEXT("air state captured before ability cancellation"), Falling->SentState.bAirborne);
	TestNull(TEXT("airborne defeat never selects standing death"), Falling->SentState.DeathMontage.Get());
	TestTrue(TEXT("airborne defeat keeps motion"), Falling->SentState.bKeepMomentum
		&& FVector(Falling->SentState.LaunchVelocity).Equals(FVector(40.0f, 0.0f, -150.0f)));
	TestFalse(TEXT("standing death remains grounded"), Standing->SentState.bAirborne);
	TestNotNull(TEXT("standing death retains authored animation"), Standing->SentState.DeathMontage.Get());
	TestEqual(TEXT("one air death presentation"), Falling->PresentCalls, 1);
	TestEqual(TEXT("one reputation award for each victim"),
		Fixture.KillerASC->GetNumericAttribute(UVBReputationAttributeSet::GetReputationAttribute()), -14.0f);
	return true;
}
#endif
