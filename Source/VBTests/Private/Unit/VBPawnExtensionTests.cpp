// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBPawnExtensionComponent.h"
#include "Character/VBCombatInputComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "CharacterCommon/VBHealthComponent.h"
#include "Data/VBEnemyConfig.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "Player/VBPlayerState.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCombatAuditTestAbility.h"
#include "VBPawnExtensionTestTypes.h"
#include "VBAbilitySetTestEffects.h"

// PlayerController 하나와 PlayerState 하나가 살아 있는 두 몸 A, B 사이를 옮긴다. B 빙의 중에도 A 가 살아 있어 A 의 EndPlay 에 기대지 않는다.
// 공개 API 만 쓴다 - AVBCharacter 에 friend 접근을 더하지 않는다.
namespace VBPawnExtensionTest
{
	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		APlayerController* PC = nullptr;
		AVBPlayerState* State = nullptr;
		AVBCharacter* A = nullptr;
		AVBCharacter* B = nullptr;
		UVBAbilitySystemComponent* ASC = nullptr;

		// bStartupLists - 첫 빙의 전에 PlayerState 에 쥐는 능력, passive 능력, 시작 효과를 하나씩 준다
		bool Initialize(FAutomationTestBase& Test, bool bStartupLists = false)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			// 이것이 없으면 actor 가 초기화 없이 스폰돼 PostInitializeComponents 를 건너뛴다 - 몸이 pawn extension 알림에
			//   등록하지 않아 넘김 줄이 아무것도 안 잰다.
			World->InitializeActorsForPlay(FURL());
			PC = World->SpawnActor<APlayerController>();
			// ULocalPlayer 없는 PlayerController 는 엔진에게 원격 클라이언트 controller 다 - ServerInitiated 능력이 같은 프로세스에서
			//   서버와 클라 활성화로 두 번 켜져(ActiveCount 2) 한 번 취소로는 남는다. 게임의 controller 에는 local player 가 있다.
			if (PC)
			{
				LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient));
				PC->Player = LocalPlayer.Get();
				LocalPlayer->PlayerController = PC;
			}

			State = SpawnState(bStartupLists);
			A = World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
			B = World->SpawnActor<AVBCharacter>(FVector(500.0, 0.0, 0.0), FRotator::ZeroRotator);
			if (!Test.TestNotNull(TEXT("controller"), PC) || !Test.TestNotNull(TEXT("player state"), State)
				|| !Test.TestNotNull(TEXT("body A"), A) || !Test.TestNotNull(TEXT("body B"), B))
			{
				return false;
			}

			AssignState(State);
			ASC = State->GetVBAbilitySystemComponent();
			return Test.TestNotNull(TEXT("PlayerState ASC"), ASC)
				&& Test.TestNotNull(TEXT("A pawn extension"), UVBPawnExtensionComponent::FindFor(A))
				&& Test.TestNotNull(TEXT("B pawn extension"), UVBPawnExtensionComponent::FindFor(B));
		}

		AVBPlayerState* SpawnState(bool bStartupLists) const
		{
			AVBPlayerState* NewState = World->SpawnActor<AVBPlayerState>();
			if (NewState && bStartupLists)
			{
				using namespace VBAbilitySetTest;
				NewState->StartupAbilitySet = MakeSet(NewState,
					{Ability(UVBPawnExtensionTestHeldAbility::StaticClass()), Ability(UVBPawnExtensionTestPassiveAbility::StaticClass())},
					{Effect(UVBPawnExtensionTestStartupEffect::StaticClass())});
			}

			return NewState;
		}

		void AssignState(AVBPlayerState* NewState) const
		{
			NewState->SetOwner(PC);
			PC->PlayerState = NewState;
		}

		~FFixture()
		{
			// PlayerState 의 ASC 는 몸보다 오래 살고 몇 줄은 passive 능력을 켜 둔다. 월드가 ASC 를 비우기 전에 끝내야
			//   엔진의 ClearAllAbilities 검사가 샌 것으로 보고하지 않는다.
			if (World)
			{
				for (TActorIterator<AVBPlayerState> It(World); It; ++It)
				{
					if (UVBAbilitySystemComponent* StateASC = It->GetVBAbilitySystemComponent())
					{
						StateASC->CancelAllAbilities();
					}
				}

				if (PC)
				{
					PC->Player = nullptr;
				}

				World->DestroyWorld(false);
			}

			if (Package)
			{
				Package->SetDirtyFlag(false);
			}
		}
	};

	// 몸의 죽음 주인은 이 ASC 의 health set 을 듣는다 - 빙의 세션마다 바인딩 하나
	bool IsHealthBound(UVBAbilitySystemComponent& ASC, const AVBCharacter* Body)
	{
		const UVBHealthAttributeSet* Set = ASC.GetSet<UVBHealthAttributeSet>();
		const UVBHealthComponent* Health = UVBHealthComponent::FindFor(Body);
		return Set && Health && Set->OnOutOfHealth.IsBoundToObject(Health);
	}

	int32 CountSpecs(const UAbilitySystemComponent& ASC, const UClass* AbilityClass)
	{
		int32 Count = 0;
		for (const FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->GetClass() == AbilityClass)
			{
				++Count;
			}
		}

		return Count;
	}

	bool IsSpecActive(UAbilitySystemComponent& ASC, const UClass* AbilityClass)
	{
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromClass(const_cast<UClass*>(AbilityClass));
		return Spec && Spec->IsActive();
	}

	FGameplayAbilitySpecHandle GiveAndActivateHeld(UVBAbilitySystemComponent& ASC)
	{
		const FGameplayAbilitySpecHandle Handle = ASC.GiveAbility(FGameplayAbilitySpec(UVBPawnExtensionTestHeldAbility::StaticClass(), 1));
		ASC.TryActivateAbility(Handle);
		return Handle;
	}

	bool IsHandleActive(UVBAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
	{
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
		return Spec && Spec->IsActive();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionHealthSubscriptionsTest, "VB.Unit.Character.PawnExtension.HealthSubscriptions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionHealthSubscriptionsTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;

	F.PC->Possess(F.A);
	TestTrue(TEXT("1 A possessed: A subscribed to Health"), IsHealthBound(ASC, F.A));
	TestFalse(TEXT("1 A possessed: B not subscribed"), IsHealthBound(ASC, F.B));

	F.PC->Possess(F.B); // unpossesses A first; A stays alive
	TestFalse(TEXT("2 B possessed: live A no longer subscribed"), IsHealthBound(ASC, F.A));
	TestTrue(TEXT("2 B possessed: B subscribed"), IsHealthBound(ASC, F.B));

	F.PC->Possess(F.A);
	TestTrue(TEXT("3 A repossessed: A subscribed again"), IsHealthBound(ASC, F.A));
	TestFalse(TEXT("3 A repossessed: B released"), IsHealthBound(ASC, F.B));

	// 다른 ASC - 같은 controller 에 새 PlayerState. 옛 ASC 는 A 를 잃고 새 ASC 가 얻어야 한다
	F.PC->UnPossess();
	AVBPlayerState* Other = F.SpawnState(false);
	if (!TestNotNull(TEXT("second player state"), Other))
	{
		return false;
	}

	F.AssignState(Other);
	F.PC->Possess(F.A);
	TestFalse(TEXT("4 other ASC: old ASC released A"), IsHealthBound(ASC, F.A));
	TestTrue(TEXT("4 other ASC: new ASC holds A"), IsHealthBound(*Other->GetVBAbilitySystemComponent(), F.A));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionAvatarUnlinkTest, "VB.Unit.Character.PawnExtension.AvatarUnlink",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionAvatarUnlinkTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;

	F.PC->Possess(F.A);
	TestTrue(TEXT("1 A possessed: avatar A"), ASC.GetAvatarActor_Direct() == F.A);
	TestTrue(TEXT("1 A possessed: A reads the PlayerState ASC"), F.A->GetAbilitySystemComponent() == &ASC);
	TestTrue(TEXT("1 A possessed: extension initialized"), UVBPawnExtensionComponent::FindFor(F.A)->IsAbilitySystemInitialized());

	F.PC->UnPossess();
	TestNull(TEXT("2 A unpossessed: avatar cleared before anyone else links"), ASC.GetAvatarActor_Direct());
	TestTrue(TEXT("2 A unpossessed: owner stays the PlayerState"), ASC.GetOwnerActor() == F.State);
	TestNull(TEXT("2 A unpossessed: A has no ASC"), F.A->GetAbilitySystemComponent());
	TestFalse(TEXT("2 A unpossessed: extension not initialized"), UVBPawnExtensionComponent::FindFor(F.A)->IsAbilitySystemInitialized());

	F.PC->Possess(F.B);
	TestTrue(TEXT("3 B possessed: avatar B"), ASC.GetAvatarActor_Direct() == F.B);
	TestNull(TEXT("3 B possessed: A still has no ASC"), F.A->GetAbilitySystemComponent());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionActiveAbilityHandoffTest, "VB.Unit.Character.PawnExtension.ActiveAbilityHandoff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionActiveAbilityHandoffTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;

	F.PC->Possess(F.A);
	const FGameplayAbilitySpecHandle Held = GiveAndActivateHeld(ASC);
	ASC.GiveAbility(FGameplayAbilitySpec(UVBPawnExtensionTestPassiveAbility::StaticClass(), 1)); // OnSpawn: active on grant
	if (!TestTrue(TEXT("fixture: A runs the held ability"), IsHandleActive(ASC, Held))
		|| !TestTrue(TEXT("fixture: passive active on grant"),
			IsSpecActive(ASC, UVBPawnExtensionTestPassiveAbility::StaticClass())))
	{
		return false;
	}

	F.PC->Possess(F.B);
	TestFalse(TEXT("B possessed: A's held ability was cancelled"), IsHandleActive(ASC, Held));
	TestTrue(TEXT("B possessed: passive survives the swap"), IsSpecActive(ASC, UVBPawnExtensionTestPassiveAbility::StaticClass()));
	TestEqual(TEXT("B possessed: grants are not removed"), CountSpecs(ASC, UVBPawnExtensionTestHeldAbility::StaticClass()), 1);
	TestTrue(TEXT("B possessed: B can run the held ability"), ASC.TryActivateAbility(Held) && IsHandleActive(ASC, Held));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionDeferredInputHandoffTest, "VB.Unit.Character.PawnExtension.DeferredInputHandoff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionDeferredInputHandoffTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;

	// 회피 GA 의 제품 경로 - 몸의 combat input component 가 공유 ASC 에 commit 태그를 올리고 타이머를 쥔다.
	//   실제 몸은 BP_VBCharacter 에서 받고, 이 C++ 몸은 여기서 단다.
	UVBCombatInputComponent* CombatInputA = NewObject<UVBCombatInputComponent>(F.A);
	F.A->AddInstanceComponent(CombatInputA);
	CombatInputA->RegisterComponent();
	F.PC->Possess(F.A);
	CombatInputA->BeginDodgeCommit(5.0f, 1.0f);
	if (!TestEqual(TEXT("fixture: commit tag on the shared ASC"), ASC.GetTagCount(VBGameplayTags::State_Combat_DodgeCommit), 1))
	{
		return false;
	}

	F.PC->Possess(F.B);
	TestEqual(TEXT("B possessed: A's commit tag returned"), ASC.GetTagCount(VBGameplayTags::State_Combat_DodgeCommit), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionLockHandoffTest, "VB.Unit.Character.PawnExtension.LockHandoff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionLockHandoffTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;
	AVBCombatAuditTestEnemy* Target = F.World->SpawnActor<AVBCombatAuditTestEnemy>(FVector(200.0, 0.0, 0.0), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("lock target"), Target))
	{
		return false;
	}

	Target->Configure(NewObject<UVBEnemyConfig>(Target));

	F.PC->Possess(F.A);
	UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(F.A);
	if (!TestNotNull(TEXT("A lock component"), Lock))
	{
		return false;
	}

	Lock->FaceAndLockParryTarget(Target, /*bLock=*/true, /*bFace=*/false);   // product server path: SetLockOn adds the tag through the PlayerState
	if (!TestTrue(TEXT("fixture: A locked on"), Lock->GetLockedTarget() == Target)
		|| !TestEqual(TEXT("fixture: lock tag on the shared ASC"), ASC.GetTagCount(VBGameplayTags::State_Combat_LockedOn), 1))
	{
		return false;
	}

	F.PC->Possess(F.B);
	TestEqual(TEXT("B possessed: A's lock tag returned"), ASC.GetTagCount(VBGameplayTags::State_Combat_LockedOn), 0);
	TestNull(TEXT("B possessed: A no longer locked"), Lock->GetLockedTarget());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionStaleAvatarReturnTest, "VB.Unit.Character.PawnExtension.StaleAvatarReturn",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionStaleAvatarReturnTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;

	F.PC->Possess(F.A);
	// ASC 가 이미 B 를 가리키는데 A 의 extension 이 아직 쥐고 있다(늦은 클라 순서). B 를 A extension 밖에서 잇는다
	ASC.InitAbilityActorInfo(F.State, F.B);
	const FGameplayAbilitySpecHandle Held = GiveAndActivateHeld(ASC);
	if (!TestTrue(TEXT("fixture: A's extension still holds the ASC"), F.A->GetAbilitySystemComponent() == &ASC)
		|| !TestTrue(TEXT("fixture: avatar is B"), ASC.GetAvatarActor_Direct() == F.B)
		|| !TestTrue(TEXT("fixture: B runs an ability"), IsHandleActive(ASC, Held))
		|| !TestTrue(TEXT("fixture: A subscribed"), IsHealthBound(ASC, F.A)))
	{
		return false;
	}

	// 공유 ASC 의 Riposte 창 몫 - 다른 주인 창이 먼저, 다음 A 의 것. A 가 늦게 돌려줄 때 A 몫만 가져가야 한다
	//   ("태그 있음" 으로 세면 A 가 자기 추가를 건너뛰고 남의 창을 지운다).
	ASC.AddLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow, 1, EGameplayTagReplicationState::CountToOwner);
	F.A->OpenRiposteWindow(5.0f, nullptr);
	TestEqual(TEXT("fixture: two window contributions"), ASC.GetTagCount(VBGameplayTags::State_Combat_RiposteWindow), 2);

	F.PC->UnPossess(); // A's late return
	TestEqual(TEXT("late A return: the other window stays open"), ASC.GetTagCount(VBGameplayTags::State_Combat_RiposteWindow), 1);
	TestFalse(TEXT("late A return: A's own subscription removed"), IsHealthBound(ASC, F.A));
	TestTrue(TEXT("late A return: B stays the avatar"), ASC.GetAvatarActor_Direct() == F.B);
	TestTrue(TEXT("late A return: B's ability keeps running"), IsHandleActive(ASC, Held));
	TestNull(TEXT("late A return: A released its pointer"), F.A->GetAbilitySystemComponent());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionStartupGrantTest, "VB.Unit.Character.PawnExtension.StartupGrant",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionStartupGrantTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	FFixture F;
	if (!F.Initialize(*this, /*bStartupLists=*/true)) return false;
	UVBAbilitySystemComponent& ASC = *F.ASC;
	TestEqual(TEXT("before possession: nothing granted"), CountSpecs(ASC, UVBPawnExtensionTestHeldAbility::StaticClass()), 0);

	F.PC->Possess(F.A);
	TestEqual(TEXT("first possession: held ability granted"), CountSpecs(ASC, UVBPawnExtensionTestHeldAbility::StaticClass()), 1);
	TestEqual(TEXT("first possession: passive granted"), CountSpecs(ASC, UVBPawnExtensionTestPassiveAbility::StaticClass()), 1);
	TestTrue(TEXT("first possession: passive activated with A as avatar"),
		IsSpecActive(ASC, UVBPawnExtensionTestPassiveAbility::StaticClass()));
	TestEqual(TEXT("first possession: startup effect applied"),
		ASC.GetGameplayEffectCount(UVBPawnExtensionTestStartupEffect::StaticClass(), nullptr), 1);
	const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromClass(UVBPawnExtensionTestHeldAbility::StaticClass());
	TestTrue(TEXT("first possession: source object is the PlayerState (the ASC's owner, not a body that can die)"),
		Spec && Spec->SourceObject.Get() == F.State);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionStartupGrantOnceTest, "VB.Unit.Character.PawnExtension.StartupGrantOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionStartupGrantOnceTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	FFixture F;
	if (!F.Initialize(*this, /*bStartupLists=*/true)) return false;
	UVBAbilitySystemComponent& ASC = *F.ASC;

	F.PC->Possess(F.A);
	F.PC->Possess(F.B);
	F.PC->Possess(F.A);
	TestEqual(TEXT("A -> B -> A: held ability once"), CountSpecs(ASC, UVBPawnExtensionTestHeldAbility::StaticClass()), 1);
	TestEqual(TEXT("A -> B -> A: passive once"), CountSpecs(ASC, UVBPawnExtensionTestPassiveAbility::StaticClass()), 1);
	TestEqual(TEXT("A -> B -> A: startup effect once"),
		ASC.GetGameplayEffectCount(UVBPawnExtensionTestStartupEffect::StaticClass(), nullptr), 1);

	// 새 PlayerState(레벨 다시 엶)는 새 수명이라 한 번 더 부여한다
	F.PC->UnPossess();
	AVBPlayerState* Fresh = F.SpawnState(true);
	if (!TestNotNull(TEXT("fresh player state"), Fresh))
	{
		return false;
	}

	F.AssignState(Fresh);
	F.PC->Possess(F.B);
	UVBAbilitySystemComponent& FreshASC = *Fresh->GetVBAbilitySystemComponent();
	TestEqual(TEXT("fresh PlayerState: held ability once"), CountSpecs(FreshASC, UVBPawnExtensionTestHeldAbility::StaticClass()), 1);
	TestEqual(TEXT("fresh PlayerState: startup effect once"),
		FreshASC.GetGameplayEffectCount(UVBPawnExtensionTestStartupEffect::StaticClass(), nullptr), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionInitializedNotificationTest, "VB.Unit.Character.PawnExtension.InitializedNotification",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionInitializedNotificationTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	int32 Calls = 0;
	UVBAbilitySystemComponent* Notified = nullptr;
	FFixture F; // after the counters: the world may notify during teardown
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;
	UVBPawnExtensionComponent& Extension = *UVBPawnExtensionComponent::FindFor(F.A);
	FDelegateHandle Initialized = Extension.RegisterAndCallOnAbilitySystemInitialized(
		FVBAbilitySystemInitializedDelegate::FDelegate::CreateLambda([&](UVBAbilitySystemComponent& In) { ++Calls; Notified = &In; }));
	FDelegateHandle Uninitialized;
	TestEqual(TEXT("early registration: not called before possession"), Calls, 0);

	F.PC->Possess(F.A);
	TestEqual(TEXT("1 possession: one ready notice"), Calls, 1);
	TestTrue(TEXT("1 possession: notice carries the PlayerState ASC"), Notified == &ASC);
	Extension.InitializeAbilitySystem(&ASC, F.State);
	TestEqual(TEXT("1 repeated same-source initialize: no second notice"), Calls, 1);

	// PlayerReadiness 도 소비자다 - ASC 태그 구독이 controller 변경 순간이 아니라 연결을 따른다
	UVBPlayerReadinessComponent* Readiness = F.A->FindComponentByClass<UVBPlayerReadinessComponent>();
	if (TestNotNull(TEXT("readiness created on possession"), Readiness))
	{
		TestTrue(TEXT("1 possession: readiness subscribed to the ASC"),
			ASC.RegisterGameplayTagEvent(VBGameplayTags::State_Interacting,
				EGameplayTagEventType::NewOrRemoved).IsBoundToObject(Readiness));
	}

	F.PC->Possess(F.B);
	TestEqual(TEXT("2 A unpossessed: no ready notice"), Calls, 1);
	if (Readiness)
	{
		TestFalse(TEXT("2 A unpossessed: readiness released the ASC"),
			ASC.RegisterGameplayTagEvent(VBGameplayTags::State_Interacting,
				EGameplayTagEventType::NewOrRemoved).IsBoundToObject(Readiness));
	}

	F.PC->Possess(F.A);
	TestEqual(TEXT("3 A repossessed: second ready notice"), Calls, 2);

	Extension.UnregisterAbilitySystemDelegates(Initialized, Uninitialized);
	F.PC->Possess(F.B);
	F.PC->Possess(F.A);
	TestEqual(TEXT("4 after unregister: no further notice"), Calls, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionLateRegistrationTest, "VB.Unit.Character.PawnExtension.LateRegistration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionLateRegistrationTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	int32 LateCalls = 0;
	int32 UnlinkedCalls = 0;
	UVBAbilitySystemComponent* Notified = nullptr;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;
	UVBPawnExtensionComponent& Extension = *UVBPawnExtensionComponent::FindFor(F.A);

	F.PC->Possess(F.A);
	FDelegateHandle Late = Extension.RegisterAndCallOnAbilitySystemInitialized(
		FVBAbilitySystemInitializedDelegate::FDelegate::CreateLambda([&](UVBAbilitySystemComponent& In) { ++LateCalls; Notified = &In; }));
	TestEqual(TEXT("late registration while linked: called at once"), LateCalls, 1);
	TestTrue(TEXT("late registration while linked: current ASC"), Notified == &ASC);

	F.PC->UnPossess();
	FDelegateHandle Unlinked = Extension.RegisterAndCallOnAbilitySystemInitialized(
		FVBAbilitySystemInitializedDelegate::FDelegate::CreateLambda([&](UVBAbilitySystemComponent&) { ++UnlinkedCalls; }));
	TestEqual(TEXT("registration while unlinked: not called"), UnlinkedCalls, 0);

	FDelegateHandle None;
	Extension.UnregisterAbilitySystemDelegates(Late, None);
	Extension.UnregisterAbilitySystemDelegates(Unlinked, None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPawnExtensionUninitializedNotificationTest, "VB.Unit.Character.PawnExtension.UninitializedNotification",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPawnExtensionUninitializedNotificationTest::RunTest(const FString& Parameters)
{
	using namespace VBPawnExtensionTest;
	int32 Calls = 0;
	bool bAvatarSessionSeen = false;
	bool bLinkedDuringNotice = false;
	UVBAbilitySystemComponent* Notified = nullptr;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent& ASC = *F.ASC;
	UVBPawnExtensionComponent& Extension = *UVBPawnExtensionComponent::FindFor(F.A);
	FDelegateHandle None;
	FDelegateHandle Uninitialized = Extension.RegisterOnAbilitySystemUninitialized(
		FVBAbilitySystemUninitializedDelegate::FDelegate::CreateLambda([&](UVBAbilitySystemComponent& In, bool bAvatarSession)
		{
			++Calls;
			Notified = &In;
			bAvatarSessionSeen = bAvatarSession;
			// 소비자는 몸을 거쳐 자원을 돌려주므로 여기서는 연결이 아직 살아 있어야 한다
			bLinkedDuringNotice = In.GetAvatarActor_Direct() == F.A && F.A->GetAbilitySystemComponent() == &In;
		}));

	F.PC->Possess(F.A);
	TestEqual(TEXT("1 possession: no release notice"), Calls, 0);
	F.PC->Possess(F.B);
	TestEqual(TEXT("2 A unpossessed: one release notice"), Calls, 1);
	TestTrue(TEXT("2 A unpossessed: notice carries the old ASC"), Notified == &ASC);
	TestTrue(TEXT("2 A unpossessed: avatar session"), bAvatarSessionSeen);
	TestTrue(TEXT("2 A unpossessed: sent before the unlink"), bLinkedDuringNotice);
	Extension.UninitializeAbilitySystem();
	TestEqual(TEXT("3 second release while unlinked: no notice"), Calls, 1);

	Extension.UnregisterAbilitySystemDelegates(None, Uninitialized);
	return true;
}

#endif
