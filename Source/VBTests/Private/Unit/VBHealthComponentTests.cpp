// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AI/VBAIController.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetLockComponent.h"
#include "CharacterCommon/VBHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Data/VBEnemyConfig.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Enemy/VBEncounterGateComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Player/VBPlayerState.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCombatAuditTestAbility.h"
#include "VBHealthComponentTestTypes.h"
#include "VBPawnExtensionTestTypes.h"

// 체력 컴포넌트 공통 행 - 순서, 두 알림, 재진입, effect 밖 쓰기, 서버 gate, AI 놓기, 조우 gate, 플레이어 몸 (적, 보스 행은 VBEnemyDeathTests.cpp)
// 사망 메시지가 실제 UVBGameplayMessageSubsystem 을 거치므로 초기화된 game instance(headless -game)가 필요하다
namespace VBHealthComponentTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	// 실제 game instance 를 빌린 격리 월드 - UVBGameplayMessageSubsystem::Get(world) 가 실제 bus 다
	// 월드를 없애기 전에 world context 를 뺀다 (엔진 목록에 남으면 죽은 월드를 tick 한다)
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		UVBGameplayMessageSubsystem* Bus = nullptr;
		TArray<TStrongObjectPtr<UObject>> Keep;
		// 행이 손으로 BeginPlay 를 부른 actor - 월드가 있을 때 없애야 나중 GC 가 월드 없이 EndPlay 를 돌리지 않는다
		TArray<TWeakObjectPtr<AActor>> Begun;

		bool Initialize(FAutomationTestBase& Test)
		{
			UGameInstance* Instance = nullptr;
			if (GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE) && Context.World()
						&& Context.World()->GetGameInstance())
					{
						Instance = Context.World()->GetGameInstance();
						break;
					}
				}
			}

			if (!Test.TestNotNull(TEXT("initialized game instance required (headless -game)"), Instance))
			{
				return false;
			}

			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
				.CreateNavigation(false).CreateAISystem(true).ShouldSimulatePhysics(false).SetTransactional(false); // blackboard init reads the AI system
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("isolated world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->SetGameInstance(Instance);
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			// 이게 없으면 actor 가 초기화 없이 생긴다 - PostInitializeComponents 가 없어 ASC 가 attribute set 을 등록하지 않는다
			World->InitializeActorsForPlay(FURL());
			Bus = UVBGameplayMessageSubsystem::Get(World);
			return Test.TestNotNull(TEXT("real death bus"), Bus);
		}

		void BeginPlayOn(AActor* Actor)
		{
			Actor->DispatchBeginPlay();
			Begun.Add(Actor);
		}

		AVBHealthTestBody* SpawnBody(float Health)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVBHealthTestBody* Body = World->SpawnActor<AVBHealthTestBody>(Params);
			if (!Body)
			{
				return nullptr;
			}

			Body->AbilitySystem->InitAbilityActorInfo(Body, Body);
			Body->AbilitySystem->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
			Body->AbilitySystem->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), Health);
			Body->Health->InitializeWithAbilitySystem(Body->AbilitySystem);
			return Body;
		}

		// meta attribute 하나(IncomingDamage / IncomingHealing)에 거는 instant effect
		// 일부러 손으로 만들어 UVBDamageExecution 을 건너뛴다 - 이 행들은 배율이 아니라 사망을 본다
		const UGameplayEffect* MakeInstant(const FGameplayAttribute& Attribute, float Magnitude)
		{
			UGameplayEffect* Effect = NewObject<UGameplayEffect>(GetTransientPackage());
			Keep.Emplace(Effect);
			Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
			FGameplayModifierInfo& Modifier = Effect->Modifiers.AddDefaulted_GetRef();
			Modifier.Attribute = Attribute;
			Modifier.ModifierOp = EGameplayModOp::Additive;
			Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Magnitude));
			return Effect;
		}

		~FWorldFixture()
		{
			for (const TWeakObjectPtr<AActor>& Actor : Begun)
			{
				if (Actor.IsValid())
				{
					Actor->Destroy();
				}
			}

			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}

			if (Package)
			{
				Package->SetDirtyFlag(false);
			}
		}
	};

	void Apply(UAbilitySystemComponent& Source, UAbilitySystemComponent& Target, const UGameplayEffect* Effect)
	{
		FGameplayEffectSpec Spec(Effect, Source.MakeEffectContext(), 1.0f);
		Source.ApplyGameplayEffectSpecToTarget(Spec, &Target);
	}

	float HealthOf(const UAbilitySystemComponent& ASC)
	{
		return ASC.GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
	}

	// 한 몸의 사망 두 알림과 각자 본 것 - probe 가 사라지면 bus listener 도 뺀다
	struct FAnnounceProbe
	{
		TArray<FVBDeathFacts> Completed;
		TArray<FVBDeathFacts> Messages;
		FVBMessageListenerHandle Listener;
		TWeakObjectPtr<UVBHealthComponent> Health;
		FDelegateHandle CompletedHandle;

		void Watch(UVBGameplayMessageSubsystem& Bus, UVBHealthComponent& InHealth, TFunction<void(const TCHAR*)> OnEach = nullptr)
		{
			Health = &InHealth;
			AActor* Owner = InHealth.GetOwner();
			CompletedHandle = InHealth.OnDeathCompleted.AddLambda([this, OnEach](const FVBDeathFacts& Facts)
			{
				Completed.Add(Facts);
				if (OnEach)
				{
					OnEach(TEXT("delegate"));
				}
			});
			Listener = Bus.RegisterListener<FVBDeathFacts>(VBGameplayTags::Message_Death,
				[this, Owner, OnEach](FGameplayTag, const FVBDeathFacts& Facts)
				{
					if (Facts.DeadActor != Owner)
					{
						return; // the bus is shared with the rest of the run
					}

					Messages.Add(Facts);
					if (OnEach)
					{
						OnEach(TEXT("bus"));
					}
				});
		}

		~FAnnounceProbe()
		{
			Listener.Unregister();
			if (UVBHealthComponent* Strong = Health.Get())
			{
				Strong->OnDeathCompleted.Remove(CompletedHandle);
			}
		}
	};

	bool SameFacts(const FVBDeathFacts& A, const FVBDeathFacts& B)
	{
		return A.DeadActor == B.DeadActor && A.Instigator == B.Instigator && A.bMedical == B.bMedical;
	}

	UBlackboardComponent* GiveTargetBlackboard(FAutomationTestBase& Test, FWorldFixture& F, AVBAIController& Controller)
	{
		UBlackboardData* Data = NewObject<UBlackboardData>(&Controller);
		F.Keep.Emplace(Data);
		FBlackboardEntry& Entry = Data->Keys.AddDefaulted_GetRef();
		Entry.EntryName = TEXT("TargetActor");
		Entry.KeyType = NewObject<UBlackboardKeyType_Object>(Data);
		UBlackboardComponent* BB = nullptr;
		Test.TestTrue(TEXT("blackboard initialised"), Controller.UseBlackboard(Data, BB) && BB);
		return BB;
	}

	UVBEncounterGateComponent* AddObservedGate(AVBHealthTestBody& Body)
	{
		UVBEncounterGateComponent* Gate = NewObject<UVBEncounterGateComponent>(&Body);
		const FArrayProperty* Prop = FindFProperty<FArrayProperty>(UVBEncounterGateComponent::StaticClass(), TEXT("DefeatedEffects"));
		if (!Prop)
		{
			return nullptr;
		}

		TArray<FInstancedStruct>& Effects = *Prop->ContainerPtrToValuePtr<TArray<FInstancedStruct>>(Gate);
		FVBHealthTestDefeatObserver Observer;
		Observer.Body = &Body;
		Effects.Add(FInstancedStruct::Make(Observer));
		Body.AddInstanceComponent(Gate);
		Gate->RegisterComponent();
		return Gate;
	}
}

// 사망 하나와 두 알림의 순서 - 두 알림은 연출 뒤, delegate 는 bus 앞에 돈다
// 두 관찰자 모두 연출을 확인하므로 bus 만 PresentDeath 앞으로 옮겨도 여기서 실패한다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthComponentOrderTest, "VB.Unit.Health.Component.OrderAndAnnouncements", VBHealthComponentTest::Flags)
bool FVBHealthComponentOrderTest::RunTest(const FString& Parameters)
{
	using namespace VBHealthComponentTest;
	FWorldFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBHealthTestBody* Body = F.SpawnBody(10.0f);
	AVBHealthTestBody* Attacker = F.SpawnBody(100.0f);
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("attacker"), Attacker))
	{
		return false;
	}

	FAnnounceProbe Probe;
	int32 DelegatesSoFar = 0;
	Probe.Watch(*F.Bus, *Body->Health, [&](const TCHAR* Who)
	{
		const bool bBus = FCString::Strcmp(Who, TEXT("bus")) == 0;
		Body->Steps.Add(FString::Printf(TEXT("%s present=%d dead=%d delegates=%d"), Who, Body->bPresented ? 1 : 0,
			Body->HasDeadTag() ? 1 : 0, bBus ? DelegatesSoFar : 0));
		if (!bBus)
		{
			++DelegatesSoFar;
		}
	});
	Apply(*Attacker->AbilitySystem, *Body->AbilitySystem, F.MakeInstant(UVBHealthAttributeSet::GetIncomingDamageAttribute(), 50.0f));

	TestEqual(TEXT("health"), HealthOf(*Body->AbilitySystem), 0.0f, 0.01f);
	TestEqual(TEXT("steps and announcements in order"), FString::Join(Body->Steps, TEXT(" | ")),
		FString(TEXT("before dead=0 | after dead=1 | present | delegate present=1 dead=1 delegates=0 | bus present=1 dead=1 delegates=1")));
	TestTrue(TEXT("started"), Body->Health->HasStartedDeath());
	if (TestEqual(TEXT("one completed"), Probe.Completed.Num(), 1) && TestEqual(TEXT("one message"), Probe.Messages.Num(), 1))
	{
		TestTrue(TEXT("dead actor"), Probe.Completed[0].DeadActor == Body);
		TestTrue(TEXT("killer = the effect's instigator"), Probe.Completed[0].Instigator == Attacker);
		TestFalse(TEXT("not medical"), Probe.Completed[0].bMedical);
		TestTrue(TEXT("delegate and bus carry the same facts"), SameFacts(Probe.Completed[0], Probe.Messages[0]));
	}

	return true;
}

// 한 번만 - 시체에 두 번째 치명타, 첫 단계 안에서 재진입하는 회복 후 사망
// set 의 입구는 두 번 닿지만 단계와 두 알림은 한 번이다 - 한 번을 보장하는 것은 latch 하나다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthComponentOnceTest, "VB.Unit.Health.Component.OnceWithReentry", VBHealthComponentTest::Flags)
bool FVBHealthComponentOnceTest::RunTest(const FString& Parameters)
{
	using namespace VBHealthComponentTest;
	FWorldFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBHealthTestBody* Body = F.SpawnBody(10.0f);
	AVBHealthTestBody* Attacker = F.SpawnBody(100.0f);
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("attacker"), Attacker))
	{
		return false;
	}

	const UGameplayEffect* Lethal = F.MakeInstant(UVBHealthAttributeSet::GetIncomingDamageAttribute(), 50.0f);
	const UGameplayEffect* Heal = F.MakeInstant(UVBHealthAttributeSet::GetIncomingHealingAttribute(), 30.0f);

	int32 Entries = 0;
	const FDelegateHandle EntryHandle = Body->HealthSet->OnOutOfHealth.AddLambda([&](const FVBDeathFacts&) { ++Entries; });
	bool bReentered = false;
	Body->OnBefore = [&](AVBHealthTestBody& Self)
	{
		if (bReentered)
		{
			return; // once - without the latch the nested kill would re-enter this step forever
		}

		bReentered = true;
		Apply(*Attacker->AbilitySystem, *Self.AbilitySystem, Heal);
		Apply(*Attacker->AbilitySystem, *Self.AbilitySystem, Lethal);
	};
	FAnnounceProbe Probe;
	Probe.Watch(*F.Bus, *Body->Health);

	Apply(*Attacker->AbilitySystem, *Body->AbilitySystem, Lethal);
	Body->OnBefore = nullptr;
	Apply(*Attacker->AbilitySystem, *Body->AbilitySystem, Lethal); // the corpse: 0 -> 0 is not a crossing
	Body->HealthSet->OnOutOfHealth.Remove(EntryHandle);

	TestTrue(TEXT("the reentry ran"), bReentered);
	TestEqual(TEXT("set entry reached twice"), Entries, 2);
	TestEqual(TEXT("steps once"), FString::Join(Body->Steps, TEXT(" | ")), FString(TEXT("before dead=0 | after dead=1 | present")));
	TestEqual(TEXT("completed once"), Probe.Completed.Num(), 1);
	TestEqual(TEXT("message once"), Probe.Messages.Num(), 1);
	TestEqual(TEXT("final health"), HealthOf(*Body->AbilitySystem), 0.0f, 0.01f);
	return true;
}

// effect 밖 쓰기(세이브 복구의 SetNumericAttributeBase, 디버그 kill 키의 ApplyModToAttribute)도 killer 없이 같은 입구에 닿는다
// 입구를 effect 안으로만 좁히면 둘 다 실패한다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthComponentNoEffectTest, "VB.Unit.Health.Component.WritesOutsideAnEffect", VBHealthComponentTest::Flags)
bool FVBHealthComponentNoEffectTest::RunTest(const FString& Parameters)
{
	using namespace VBHealthComponentTest;
	FWorldFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBHealthTestBody* Restored = F.SpawnBody(60.0f);
	AVBHealthTestBody* DebugKilled = F.SpawnBody(60.0f);
	if (!TestNotNull(TEXT("bodies"), Restored) || !TestNotNull(TEXT("bodies"), DebugKilled))
	{
		return false;
	}

	FAnnounceProbe RestoredProbe, DebugProbe;
	RestoredProbe.Watch(*F.Bus, *Restored->Health);
	DebugProbe.Watch(*F.Bus, *DebugKilled->Health);

	Restored->AbilitySystem->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 0.0f);
	DebugKilled->AbilitySystem->ApplyModToAttribute(UVBHealthAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 0.0f);

	for (const TPair<AVBHealthTestBody*, FAnnounceProbe*>& Case : { TPair<AVBHealthTestBody*, FAnnounceProbe*>(Restored, &RestoredProbe),
		TPair<AVBHealthTestBody*, FAnnounceProbe*>(DebugKilled, &DebugProbe) })
	{
		const FString Name = Case.Key == Restored ? TEXT("save restore 0") : TEXT("debug kill");
		TestEqual(Name + TEXT(": steps once"), Case.Key->Steps.Num(), 3);
		if (TestEqual(Name + TEXT(": one completed"), Case.Value->Completed.Num(), 1))
		{
			TestNull(Name + TEXT(": no killer"), Case.Value->Completed[0].Instigator.Get());
			TestTrue(Name + TEXT(": dead actor"), Case.Value->Completed[0].DeadActor == Case.Key);
		}

		TestEqual(Name + TEXT(": one message"), Case.Value->Messages.Num(), 1);
	}

	return true;
}

// 서버에서만, 그리고 몸이 듣기를 멈춘 뒤에는 아무것도 없다 - simulated proxy 의 0 쓰기는 복제 값이지 사망이 아니다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthComponentSilentTest, "VB.Unit.Health.Component.ClientAndUnboundSilent", VBHealthComponentTest::Flags)
bool FVBHealthComponentSilentTest::RunTest(const FString& Parameters)
{
	using namespace VBHealthComponentTest;
	FWorldFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBHealthTestBody* Proxy = F.SpawnBody(60.0f);
	AVBHealthTestBody* Unbound = F.SpawnBody(60.0f);
	if (!TestNotNull(TEXT("bodies"), Proxy) || !TestNotNull(TEXT("bodies"), Unbound))
	{
		return false;
	}

	FAnnounceProbe ProxyProbe, UnboundProbe;
	ProxyProbe.Watch(*F.Bus, *Proxy->Health);
	UnboundProbe.Watch(*F.Bus, *Unbound->Health);

	Proxy->SetRole(ROLE_SimulatedProxy);
	Proxy->AbilitySystem->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 0.0f);
	Unbound->Health->UninitializeFromAbilitySystem();
	Unbound->Health->UninitializeFromAbilitySystem(); // idempotent
	Unbound->AbilitySystem->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 0.0f);

	TestEqual(TEXT("proxy: health written"), HealthOf(*Proxy->AbilitySystem), 0.0f, 0.01f);
	TestEqual(TEXT("proxy: no steps"), Proxy->Steps.Num(), 0);
	TestEqual(TEXT("proxy: no message"), ProxyProbe.Messages.Num(), 0);
	TestFalse(TEXT("proxy: not started"), Proxy->Health->HasStartedDeath());
	TestEqual(TEXT("unbound: no steps"), Unbound->Steps.Num(), 0);
	TestEqual(TEXT("unbound: no message"), UnboundProbe.Messages.Num(), 0);
	TestFalse(TEXT("unbound: set no longer bound to the component"), Unbound->HealthSet->OnOutOfHealth.IsBoundToObject(Unbound->Health));
	Proxy->SetRole(ROLE_Authority);
	return true;
}

// bus 를 통한 AI 놓기 - 죽은 몸을 노리던 AI 만 놓고 다른 AI 는 살아 있는 대상을 유지한다
// 대상이 이미 죽은 뒤 듣기 시작한 controller 는 그 대상을 한 번 지운다 (bus 는 다시 알리지 않는다)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthAIReleaseTest, "VB.Unit.Health.AI.ReleaseOnDeathMessage", VBHealthComponentTest::Flags)
bool FVBHealthAIReleaseTest::RunTest(const FString& Parameters)
{
	using namespace VBHealthComponentTest;
	FWorldFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBHealthTestBody* Victim = F.SpawnBody(10.0f);
	AVBHealthTestBody* Alive = F.SpawnBody(100.0f);
	AVBHealthTestBody* AlreadyDead = F.SpawnBody(100.0f);
	AVBHealthTestBody* Attacker = F.SpawnBody(100.0f);
	AVBAIController* Hunter = F.World->SpawnActor<AVBAIController>();
	AVBAIController* Other = F.World->SpawnActor<AVBAIController>();
	AVBAIController* Late = F.World->SpawnActor<AVBAIController>();
	if (!TestTrue(TEXT("actors"), Victim && Alive && AlreadyDead && Attacker && Hunter && Other && Late))
	{
		return false;
	}

	UBlackboardComponent* HunterBB = GiveTargetBlackboard(*this, F, *Hunter);
	UBlackboardComponent* OtherBB = GiveTargetBlackboard(*this, F, *Other);
	UBlackboardComponent* LateBB = GiveTargetBlackboard(*this, F, *Late);
	if (!HunterBB || !OtherBB || !LateBB)
	{
		return false;
	}

	HunterBB->SetValueAsObject(TEXT("TargetActor"), Victim);
	OtherBB->SetValueAsObject(TEXT("TargetActor"), Alive);
	F.BeginPlayOn(Hunter);
	F.BeginPlayOn(Other);

	Apply(*Attacker->AbilitySystem, *Victim->AbilitySystem, F.MakeInstant(UVBHealthAttributeSet::GetIncomingDamageAttribute(), 50.0f));
	TestNull(TEXT("the hunter let go of the dead body"), HunterBB->GetValueAsObject(TEXT("TargetActor")));
	TestTrue(TEXT("the other AI keeps its live target"), OtherBB->GetValueAsObject(TEXT("TargetActor")) == Alive);

	// 늦은 listener - 대상이 BeginPlay 전에 이미 State.Dead 를 들고 있다
	AlreadyDead->AbilitySystem->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 0.0f);
	if (!TestTrue(TEXT("fixture: target is dead"), AlreadyDead->HasDeadTag()))
	{
		return false;
	}

	LateBB->SetValueAsObject(TEXT("TargetActor"), AlreadyDead);
	F.BeginPlayOn(Late);
	TestNull(TEXT("late listener cleared its already dead target"), LateBB->GetValueAsObject(TEXT("TargetActor")));
	return true;
}

// 조우 gate 의 패배는 주인의 사망 완료에서 온다 - 연출 뒤 한 번
// 첫 사망 단계가 몸을 회복시켰다 다시 죽이거나 회복만 하고 돌아와도 마찬가지다 (중첩 delegate 가 먼저 적용하면 안 된다)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthGateTest, "VB.Unit.Health.Gate.DefeatAfterDeathOnce", VBHealthComponentTest::Flags)
bool FVBHealthGateTest::RunTest(const FString& Parameters)
{
	using namespace VBHealthComponentTest;
	FWorldFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	AVBHealthTestBody* Attacker = F.SpawnBody(100.0f);
	if (!TestNotNull(TEXT("attacker"), Attacker))
	{
		return false;
	}

	const UGameplayEffect* Lethal = F.MakeInstant(UVBHealthAttributeSet::GetIncomingDamageAttribute(), 50.0f);
	const UGameplayEffect* Heal = F.MakeInstant(UVBHealthAttributeSet::GetIncomingHealingAttribute(), 30.0f);

	struct FCase { const TCHAR* Name; bool bKillAgain; bool bInAfter; float FinalHealth; };
	for (const FCase& Case : { FCase{ TEXT("heal then kill inside the first step"), true, false, 0.0f },
		FCase{ TEXT("healed inside the third step"), false, true, 30.0f } })
	{
		AVBHealthTestBody* Body = F.SpawnBody(10.0f);
		if (!TestNotNull(TEXT("body"), Body))
		{
			return false;
		}

		if (!TestNotNull(TEXT("gate"), AddObservedGate(*Body)))
		{
			return false;
		}

		F.BeginPlayOn(Body);
		TSharedRef<bool> bRan = MakeShared<bool>(false);
		TFunction<void(AVBHealthTestBody&)> Reenter = [&, bRan, bKillAgain = Case.bKillAgain](AVBHealthTestBody& Self)
		{
			if (*bRan)
			{
				return; // once, even if a broken latch re-enters the step
			}

			*bRan = true;
			Apply(*Attacker->AbilitySystem, *Self.AbilitySystem, Heal);
			if (bKillAgain)
			{
				Apply(*Attacker->AbilitySystem, *Self.AbilitySystem, Lethal);
			}
		};
		(Case.bInAfter ? Body->OnAfter : Body->OnBefore) = Reenter;

		Apply(*Attacker->AbilitySystem, *Body->AbilitySystem, Lethal);

		const FString Name = Case.Name;
		TestEqual(Name + TEXT(": final health"), HealthOf(*Body->AbilitySystem), Case.FinalHealth, 0.01f);
		TestEqual(Name + TEXT(": gate defeat once"), Body->GateDefeats, 1);
		TestEqual(Name + TEXT(": gate ran last, after the presentation"), Body->Steps.Num() > 0 ? Body->Steps.Last() : FString(),
			FString(TEXT("gate present=1 dead=1")));
	}

	return true;
}

// 실제 pawn extension 을 거친 플레이어 몸의 단계 - 두 알림 때 몸은 이미 죽었고, 돌던 ability 는 취소, 락온은 해제,
// 연출(capsule 이 pawn 을 막지 않음)도 끝나 있다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHealthPlayerOrderTest, "VB.Unit.Health.Player.StepsBeforeAnnouncements", VBHealthComponentTest::Flags)
bool FVBHealthPlayerOrderTest::RunTest(const FString& Parameters)
{
	using namespace VBHealthComponentTest;
	FWorldFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient));
	APlayerController* PC = F.World->SpawnActor<APlayerController>();
	AVBPlayerState* State = F.World->SpawnActor<AVBPlayerState>();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVBCharacter* Body = F.World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	AVBCombatAuditTestEnemy* Target = F.World->SpawnActor<AVBCombatAuditTestEnemy>(FVector(200.0, 0.0, 0.0), FRotator::ZeroRotator, Params);
	AVBHealthTestBody* Attacker = F.SpawnBody(100.0f);
	if (!TestTrue(TEXT("actors"), PC && State && Body && Target && Attacker))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		if (UVBAbilitySystemComponent* StateASC = State->GetVBAbilitySystemComponent())
		{
			StateASC->CancelAllAbilities();
		}

		PC->Player = nullptr;
	};
	// local player 가 없는 controller 는 엔진에게 원격 클라 controller 다 (VBPawnExtensionTests 참고)
	PC->Player = LocalPlayer.Get();
	LocalPlayer->PlayerController = PC;
	State->SetOwner(PC);
	PC->PlayerState = State;
	Target->Configure(NewObject<UVBEnemyConfig>(Target));
	PC->Possess(Body);

	UVBAbilitySystemComponent* ASC = State->GetVBAbilitySystemComponent();
	UVBHealthComponent* Health = UVBHealthComponent::FindFor(Body);
	UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Body);
	if (!TestNotNull(TEXT("player ASC"), ASC) || !TestNotNull(TEXT("health component"), Health) || !TestNotNull(TEXT("lock"), Lock))
	{
		return false;
	}

	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
	ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 10.0f);
	const FGameplayAbilitySpecHandle Held = ASC->GiveAbility(FGameplayAbilitySpec(UVBPawnExtensionTestHeldAbility::StaticClass(), 1));
	ASC->TryActivateAbility(Held);
	Lock->FaceAndLockParryTarget(Target, /*bLock=*/true, /*bFace=*/false);
	const FGameplayAbilitySpec* HeldSpec = ASC->FindAbilitySpecFromHandle(Held);
	if (!TestTrue(TEXT("fixture: ability running"), HeldSpec && HeldSpec->IsActive())
		|| !TestTrue(TEXT("fixture: locked on"), Lock->GetLockedTarget() == Target)
		|| !TestEqual(TEXT("fixture: capsule blocks pawns"), Body->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn),
			ECR_Block))
	{
		return false;
	}

	TArray<FString> Seen;
	FAnnounceProbe Probe;
	Probe.Watch(*F.Bus, *Health, [&](const TCHAR* Who)
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Held);
		Seen.Add(FString::Printf(TEXT("%s dead=%d ability=%d lock=%d pawnblock=%d"), Who,
			ASC->HasMatchingGameplayTag(VBGameplayTags::State_Dead) ? 1 : 0, Spec && Spec->IsActive() ? 1 : 0,
			Lock->GetLockedTarget() ? 1 : 0, Body->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block ? 1 : 0));
	});
	Apply(*Attacker->AbilitySystem, *ASC, F.MakeInstant(UVBHealthAttributeSet::GetIncomingDamageAttribute(), 50.0f));

	TestEqual(TEXT("both announcements see the finished player steps"), FString::Join(Seen, TEXT(" | ")),
		FString(TEXT("delegate dead=1 ability=0 lock=0 pawnblock=0 | bus dead=1 ability=0 lock=0 pawnblock=0")));
	if (TestEqual(TEXT("one completed"), Probe.Completed.Num(), 1))
	{
		TestTrue(TEXT("dead actor is the body, not the PlayerState"), Probe.Completed[0].DeadActor == Body);
	}

	return true;
}
#endif
