// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBCombatInputComponent.h"
#include "Data/VBCombatRuleTable.h"
#include "Data/VBCombatSettings.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Player/VBPlayerState.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBCombatInputTestTypes.h"
#include "VBWeaponDefinitionTestUtil.h"
#include "GameFramework/CharacterMovementComponent.h"

// UVBCombatInputComponent 의 공격 누름 계약 - 로컬이 아닌 Controller 로 서버 RPC 함수를 직접 불러 로컬 입력에 기대지 않음을 본다.
//   누름이 끝난 뒤 light-attack 능력이 활성이면 통과로 센다. 무기를 든 몸만 공격이 있고, 꺼낼 무기가 없는 몸은 한 줄로 따로 본다.
namespace VBCombatInputTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	struct FPressParams
	{
		int32 PressSerial = INDEX_NONE;
		FVector2D MoveIntent = FVector2D::ZeroVector;
	};

	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		APlayerController* PC = nullptr;
		AVBPlayerState* State = nullptr;
		UVBAbilitySystemComponent* ASC = nullptr;
		FGameplayAbilitySpecHandle Attack;

		bool Initialize(FAutomationTestBase& Test)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->InitializeActorsForPlay(FURL());
			PC = World->SpawnActor<APlayerController>(); // ULocalPlayer 없음 - 서버가 보는 원격 클라이언트의 Controller
			State = World->SpawnActor<AVBPlayerState>();
			if (!Test.TestNotNull(TEXT("controller"), PC) || !Test.TestNotNull(TEXT("player state"), State))
			{
				return false;
			}

			if (!Test.TestFalse(TEXT("controller is not local"), PC->IsLocalController()))
			{
				return false;
			}

			State->SetOwner(PC);
			PC->PlayerState = State;
			ASC = State->GetVBAbilitySystemComponent();
			if (!Test.TestNotNull(TEXT("PlayerState ASC"), ASC))
			{
				return false;
			}

			Attack = ASC->GiveAbility(FGameplayAbilitySpec(UVBCombatInputTestAttackAbility::StaticClass(), 1));
			return true;
		}

		AVBCharacter* SpawnBody(FVector At, UVBCombatInputComponent*& OutInput, bool bWeaponInHand = true) const
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVBCharacter* Body = World->SpawnActor<AVBCharacter>(At, FRotator::ZeroRotator, Params);
			OutInput = nullptr;
			if (!Body)
			{
				return nullptr;
			}
			// 실제 몸은 BP_VBCharacter 의 컴포넌트 목록에서 받고, C++ 몸은 여기서 붙인다.
			OutInput = NewObject<UVBCombatInputComponent>(Body);
			Body->AddInstanceComponent(OutInput);
			OutInput->RegisterComponent();
			// 빈 세트의 katana 를 손에 든다 - 누름 경로는 무기를 보고 넘기고, 공격은 위 fixture 능력이 맡는다.
			UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Body);
			if (Weapons && bWeaponInHand)
			{
				UVBWeaponDefinition* Blade = VBWeaponDefinitionTest::Make(Weapons, EVBWeaponType::Blade);
				VBWeaponDefinitionTest::GiveSet(*Blade, {});
				VBWeaponDefinitionTest::Equip(*Weapons, {Blade}, EVBWeaponType::None);
				Weapons->SetOwnedWeapons({EVBWeaponType::Blade});
				Weapons->SummonWeapon(EVBWeaponType::Blade);
			}

			return Body;
		}

		bool AttackActive() const
		{
			const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Attack);
			return Spec && Spec->IsActive();
		}

		void EndAttack() const { ASC->CancelAbilityHandle(Attack); }

		static bool Press(UVBCombatInputComponent& Input, int32 Serial, FVector2D Intent = FVector2D::ZeroVector)
		{
			UFunction* Server = Input.FindFunction(TEXT("ServerAttackPressed"));
			if (!Server)
			{
				return false;
			}

			FPressParams Params;
			Params.PressSerial = Serial;
			Params.MoveIntent = Intent;
			Input.ProcessEvent(Server, &Params); // authority 에서 RPC 가 닿는 같은 구현을 실행한다
			return true;
		}

		// Advance - 월드 시계를 옮기고 만기 timer 만 쏜다. 누름 나이가 월드 시계로 재지므로 둘이 함께 움직여야 한다.
		//   엔진처럼 두 프레임을 돈다 - 이번 tick 전에 건 timer 는 pending 에 있다가 tick 끝에 활성이 되고, Tick 은 GFrameCounter 마다 한 번이다.
		void Advance(float Seconds) const
		{
			FTimerManager& Timers = World->GetTimerManager();
			++GFrameCounter;
			Timers.Tick(0.0f);
			++GFrameCounter;
			World->TimeSeconds += Seconds;
			Timers.Tick(Seconds);
		}

		~FFixture()
		{
			if (World)
			{
				if (ASC)
				{
					ASC->CancelAllAbilities();
				}

				World->DestroyWorld(false);
			}

			if (Package)
			{
				Package->SetDirtyFlag(false);
			}
		}
	};
}

// 새 누름은 활성화 또는 현재 능력의 입력으로 전달하고, 같은 번호의 누름은 무시한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatInputDispatchTest, "VB.Unit.Character.CombatInput.PressDispatch", VBCombatInputTest::Flags)
bool FVBCombatInputDispatchTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBCombatInputComponent* Input = nullptr;
	AVBCharacter* Body = F.SpawnBody(FVector::ZeroVector, Input);
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("combat input"), Input))
	{
		return false;
	}

	F.PC->Possess(Body);
	if (!TestTrue(TEXT("server entry found"), FFixture::Press(*Input, 1, FVector2D(0.6, 0.0))))
	{
		return false;
	}

	TestTrue(TEXT("idle press activates the attack"), F.AttackActive());
	TestTrue(TEXT("the press direction is kept for the attack"), Input->ConsumeAttackIntent().Equals(FVector2D(0.6, 0.0)));
	TestTrue(TEXT("and handed out once"), Input->ConsumeAttackIntent().IsZero());
	const FGameplayAbilitySpec* ActiveSpec = F.ASC->FindAbilitySpecFromHandle(F.Attack);
	const UVBCombatInputTestAttackAbility* ActiveAttack = Cast<UVBCombatInputTestAttackAbility>(ActiveSpec ? ActiveSpec->GetPrimaryInstance() : nullptr);
	if (!TestNotNull(TEXT("active attack instance"), ActiveAttack))
	{
		return false;
	}

	int32 ComboEvents = 0;
	const FDelegateHandle ComboHandle = F.ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combo_InputReceived)
		.AddLambda([&](const FGameplayEventData*) { ++ComboEvents; });
	F.ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
	FFixture::Press(*Input, 2);
	F.ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
	TestEqual(TEXT("active attack receives its continuation"), ActiveAttack->InputPressCount, 1);
	TestEqual(TEXT("component does not manufacture the ability's combo event"), ComboEvents, 0);
	TestTrue(TEXT("the same attack remains active"), F.AttackActive());

	FFixture::Press(*Input, 2);
	TestEqual(TEXT("a known serial is ignored"), ActiveAttack->InputPressCount, 1);
	F.ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combo_InputReceived).Remove(ComboHandle);
	F.EndAttack();

	// 꺼낼 무기가 없으면 누름은 능력 앞에서 멈춘다 - 맨손 대체 동작은 없다.
	UVBCombatInputComponent* BareInput = nullptr;
	AVBCharacter* Bare = F.SpawnBody(FVector(0.0, 600.0, 0.0), BareInput, /*bWeaponInHand=*/false);
	if (!TestNotNull(TEXT("bare body"), Bare) || !TestNotNull(TEXT("bare body combat input"), BareInput))
	{
		return false;
	}

	F.PC->Possess(Bare);
	FFixture::Press(*BareInput, 3);
	TestFalse(TEXT("nothing to draw: the press does not reach the attack"), F.AttackActive());
	return true;
}

// 같은 입력 후보는 명시적 취소 관계와 시작 조건을 모두 만족할 때만 현재 능력을 교체한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatInputReplacementTest, "VB.Unit.Character.CombatInput.Replacement", VBCombatInputTest::Flags)
bool FVBCombatInputReplacementTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatInputTest;
	for (const bool bCandidatesFirst : { false, true })
	{
		FFixture F;
		if (!F.Initialize(*this))
		{
			return false;
		}

		const UVBCombatSettings* Settings = GetDefault<UVBCombatSettings>();
		UVBCombatRuleTable* Table = Settings->CombatRules.LoadSynchronous();
		if (!TestNotNull(TEXT("shipped combat rules"), Table))
		{
			return false;
		}

		FGameplayTagContainer Block;
		FGameplayTagContainer Cancel;
		Table->AppendBlockAndCancelTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Air), Block, Cancel);
		if (!TestTrue(TEXT("authored Air transition cancels Light; old empty row fails"),
			FGameplayTagContainer(VBGameplayTags::Ability_Attack_Light).HasAny(Cancel)))
		{
			return false;
		}

		FVBAbilityRule* AirRule = Table->AbilityRules.FindByPredicate([](const FVBAbilityRule& Rule) { return Rule.Ability == VBGameplayTags::Ability_Attack_Air; });
		if (!TestNotNull(TEXT("direct air rule"), AirRule))
		{
			return false;
		}

		const FGameplayTagContainer SavedCancel = AirRule->CancelsOnStart;
		ON_SCOPE_EXIT { AirRule->CancelsOnStart = SavedCancel; };
		UVBCombatInputComponent* Input = nullptr;
		AVBCharacter* Body = F.SpawnBody(FVector::ZeroVector, Input);
		if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("input"), Input))
		{
			return false;
		}

		F.PC->Possess(Body);
		const FGameplayAbilitySpecHandle Air = F.ASC->GiveAbility(FGameplayAbilitySpec(UVBCombatInputTestAirAbility::StaticClass(), 1));
		const FGameplayAbilitySpecHandle NonReplacing = F.ASC->GiveAbility(FGameplayAbilitySpec(UVBCombatInputTestNonReplacingAbility::StaticClass(), 1));
		if (bCandidatesFirst)
		{
			F.ASC->ClearAbility(F.Attack);
			F.Attack = F.ASC->GiveAbility(FGameplayAbilitySpec(UVBCombatInputTestAttackAbility::StaticClass(), 1));
		}

		auto Active = [&](FGameplayAbilitySpecHandle Handle)
		{
			const FGameplayAbilitySpec* Spec = F.ASC->FindAbilitySpecFromHandle(Handle);
			return Spec && Spec->IsActive();
		};
		FFixture::Press(*Input, 1);
		TestTrue(TEXT("ordinary press starts Light"), F.AttackActive());
		F.ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Attacking);
		F.ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);
		TestTrue(TEXT("non-replacing candidate satisfies its state conditions"),
			F.ASC->CanStartUnderCombatRules(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Riposte)));
		FFixture::Press(*Input, 2);
		TestTrue(TEXT("ineligible Air does not cancel the current owner"), F.AttackActive() && !Active(Air));
		TestFalse(TEXT("eligible candidate without cancellation cannot steal the input"), Active(NonReplacing));
		F.ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_AirOpening);
		FFixture::Press(*Input, 3);
		TestTrue(TEXT("eligible explicit replacement takes over"), Active(Air) && !F.AttackActive());
		const FGameplayAbilitySpec* AirSpec = F.ASC->FindAbilitySpecFromHandle(Air);
		TestTrue(TEXT("replacement owns the held press"), AirSpec && AirSpec->InputPressed);
		F.ASC->CancelAbilityHandle(Air);
		F.ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_AirOpening);
		F.ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);
		TestTrue(TEXT("restart original owner for old-data control"), F.ASC->TryActivateAbility(F.Attack));
		AirRule->CancelsOnStart.Reset();
		F.ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_AirOpening);
		FFixture::Press(*Input, 4);
		TestTrue(TEXT("old empty cancellation row cannot perform the handoff"), F.AttackActive() && !Active(Air));
	}

	return true;
}

// Dodge commit 중 누름은 저장만 되고, commit timer 가 buffer 안에 풀리면 그 누름으로 발동한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatInputDodgeTest, "VB.Unit.Character.CombatInput.DodgeCommitFires", VBCombatInputTest::Flags)
bool FVBCombatInputDodgeTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBCombatInputComponent* Input = nullptr;
	AVBCharacter* Body = F.SpawnBody(FVector::ZeroVector, Input);
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("combat input"), Input))
	{
		return false;
	}

	F.PC->Possess(Body);

	Input->BeginDodgeCommit(0.1f, 1.0f);
	TestEqual(TEXT("commit tag raised once"), F.ASC->GetTagCount(VBGameplayTags::State_Combat_DodgeCommit), 1);
	FFixture::Press(*Input, 1);
	TestFalse(TEXT("press during the commit is stored, not activated"), F.AttackActive());
	F.Advance(0.2f);
	TestEqual(TEXT("commit tag lowered when the timer ends"), F.ASC->GetTagCount(VBGameplayTags::State_Combat_DodgeCommit), 0);
	TestTrue(TEXT("stored press fires within the buffer"), F.AttackActive());
	return true;
}

// buffer 보다 오래된 저장 누름은 방향과 함께 버린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatInputDodgeStaleTest, "VB.Unit.Character.CombatInput.DodgeCommitStale", VBCombatInputTest::Flags)
bool FVBCombatInputDodgeStaleTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBCombatInputComponent* Input = nullptr;
	AVBCharacter* Body = F.SpawnBody(FVector::ZeroVector, Input);
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("combat input"), Input))
	{
		return false;
	}

	F.PC->Possess(Body);

	Input->BeginDodgeCommit(0.3f, 0.1f);
	FFixture::Press(*Input, 2, FVector2D(0.0, 1.0));
	F.Advance(0.4f);
	TestFalse(TEXT("stored press older than the buffer is dropped"), F.AttackActive());
	TestTrue(TEXT("its direction is dropped too"), Input->ConsumeAttackIntent().IsZero());
	return true;
}

// dodge 가 끊겨 commit 이 취소되면 tag 를 내리고 저장 누름과 방향을 버린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatInputDodgeCancelTest, "VB.Unit.Character.CombatInput.DodgeCommitCancel", VBCombatInputTest::Flags)
bool FVBCombatInputDodgeCancelTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBCombatInputComponent* Input = nullptr;
	AVBCharacter* Body = F.SpawnBody(FVector::ZeroVector, Input);
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("combat input"), Input))
	{
		return false;
	}

	F.PC->Possess(Body);

	Input->BeginDodgeCommit(0.3f, 1.0f);
	FFixture::Press(*Input, 3, FVector2D(1.0, 0.0));
	Input->CancelDodgeCommit();
	TestEqual(TEXT("cancel lowers the tag"), F.ASC->GetTagCount(VBGameplayTags::State_Combat_DodgeCommit), 0);
	F.Advance(0.4f);
	TestFalse(TEXT("cancelled commit fires nothing"), F.AttackActive());
	TestTrue(TEXT("cancel drops the direction"), Input->ConsumeAttackIntent().IsZero());
	return true;
}

// 앉은 채 누르면 일어설 때까지 기다렸다 한 번 발동한다. 다른 몸으로 옮겨간 옛 avatar 의 누름은 새 누름이 아니다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatInputStandAndAvatarTest, "VB.Unit.Character.CombatInput.StandUpAndStaleAvatar", VBCombatInputTest::Flags)
bool FVBCombatInputStandAndAvatarTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBCombatInputComponent* InputA = nullptr;
	UVBCombatInputComponent* InputB = nullptr;
	AVBCharacter* A = F.SpawnBody(FVector::ZeroVector, InputA);
	AVBCharacter* B = F.SpawnBody(FVector(600.0, 0.0, 0.0), InputB);
	if (!TestTrue(TEXT("bodies"), A && B && InputA && InputB))
	{
		return false;
	}

	F.PC->Possess(A);

	UCharacterMovementComponent* Move = A->GetCharacterMovement();
	Move->GetNavAgentPropertiesRef().bCanCrouch = true;
	Move->Crouch(false);
	if (!TestTrue(TEXT("fixture: crouched"), A->bIsCrouched))
	{
		return false;
	}

	FFixture::Press(*InputA, 1);
	TestFalse(TEXT("crouched press waits"), F.AttackActive());
	Move->UnCrouch(false);
	TestFalse(TEXT("fixture: stood up"), A->bIsCrouched);
	TestTrue(TEXT("press fires once after standing up"), F.AttackActive());
	F.EndAttack();

	// ASC 는 이미 B 를 가리키는데 A 의 extension 이 아직 ASC 를 쥔 늦은 순서를 만든다.
	//   Possess(B) 를 쓰면 A 쪽이 먼저 닫혀 ASC 를 못 찾으니 이 gate 가 시험되지 않는다.
	F.ASC->InitAbilityActorInfo(F.State, B);
	if (!TestTrue(TEXT("fixture: A still holds the ASC, which names B"), A->GetAbilitySystemComponent() == F.ASC
		&& F.ASC->GetAvatarActor_Direct() == B))
	{
		return false;
	}

	FFixture::Press(*InputA, 2);
	TestFalse(TEXT("old avatar's press is not a new press"), F.AttackActive());
	F.PC->Possess(B);
	FFixture::Press(*InputB, 2);
	TestTrue(TEXT("the current avatar's press with that serial still works"), F.AttackActive());
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatAirRecoveryTest, "VB.Unit.Character.CombatInput.AirRecovery", VBCombatInputTest::Flags)
bool FVBCombatAirRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBCombatInputComponent* Input = nullptr;
	AVBCharacter* Body = F.SpawnBody(FVector::ZeroVector, Input);
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("input"), Input))
	{
		return false;
	}

	F.PC->Possess(Body);
	F.ASC->AddLooseGameplayTag(VBGameplayTags::Passive_Dodge);
	TestTrue(TEXT("grounded attack is admitted before recovery"),
		F.ASC->CanStartUnderCombatRules(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Light)));
	UCharacterMovementComponent* Move = Body->GetCharacterMovement();
	Move->SetMovementMode(MOVE_Falling);
	Input->BeginAirRecovery();
	Input->BeginAirRecovery();
	TestEqual(TEXT("one recovery lease"), F.ASC->GetTagCount(VBGameplayTags::State_Combat_AirRecovery), 1);
	const FGameplayTag Attacks[] = {VBGameplayTags::Ability_Attack_Light, VBGameplayTags::Ability_Attack_Riposte,
		VBGameplayTags::Ability_Attack_Air, VBGameplayTags::Ability_Attack_CinematicSkill};
	for (const FGameplayTag& Attack : Attacks)
	{
		TestFalse(Attack.ToString() + TEXT(" blocked until landing"), F.ASC->CanStartUnderCombatRules(FGameplayTagContainer(Attack)));
	}

	TestTrue(TEXT("recovery permits dodge"), F.ASC->CanStartUnderCombatRules(FGameplayTagContainer(VBGameplayTags::Ability_Defense_Dodge)));
	FFixture::Press(*Input, 1);
	TestFalse(TEXT("falling press does not activate attack"), F.AttackActive());
	Input->BeginDodgeCommit(0.1f, 0.3f);
	FFixture::Press(*Input, 2);
	F.Advance(0.2f);
	TestFalse(TEXT("dodge cannot bypass air recovery"), F.AttackActive());
	Body->Landed(FHitResult());
	Move->SetMovementMode(MOVE_Walking);
	TestFalse(TEXT("landing releases recovery"), F.ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_AirRecovery));
	FFixture::Press(*Input, 3);
	TestTrue(TEXT("fresh grounded press attacks"), F.AttackActive());
	F.EndAttack();
	Move->SetMovementMode(MOVE_Falling);
	Input->BeginAirRecovery();
	Input->ResetForAvatarSession(F.ASC);
	TestFalse(TEXT("avatar reset releases recovery"), F.ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_AirRecovery));
	Input->BeginAirRecovery();
	Move->SetMovementMode(MOVE_None);
	Move->SetMovementMode(MOVE_Walking);
	TestFalse(TEXT("ground movement recovery without Landed releases the lease"),
		F.ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_AirRecovery));
	Input->BeginAirRecovery();
	TestFalse(TEXT("grounded end creates no lease"), F.ASC->HasMatchingGameplayTag(VBGameplayTags::State_Combat_AirRecovery));
	return true;
}
#endif
