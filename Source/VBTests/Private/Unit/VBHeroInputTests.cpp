// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "Algo/Find.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Character/VBHeroComponent.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Data/VBMovementConfig.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedPlayerInput.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/VBInputConfig.h"
#include "InputAction.h"
#include "Player/VBPlayerState.h"
#include "PlayBot/VBPlayBotScenarioBase.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBInputDispatchFixtureAbilities.h"

// hero 가 input config 를 묶고 몸은 의도만 받는다. public API 와 실제 엔진 경로만 - 빙의가 input component 를 만들고,
//   주입한 입력이 실제 trigger 평가와 binding 을 탄다. 키 -> 행동 매핑(IMC)은 여기서 안 보고 플레이 봇의 실제 키 입력이 본다.
namespace VBHeroInputTest
{
	const TCHAR* BodyClassPath = TEXT("/Game/VB/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C");
	const TCHAR* ConfigPath = TEXT("/Game/VB/Input/DA_VBInputConfig_Hero.DA_VBInputConfig_Hero");

	// 출하된 줄(태그 -> 목록 -> input action) - 에디터 이전 결과의 검사일 뿐 실행 중 config 의 둘째 주인이 아니다.
	struct FShippedRow { const TCHAR* Tag; bool bAbility; const TCHAR* Action; };
	const FShippedRow Shipped[] =
	{
		{ TEXT("Input.Move"), false, TEXT("/Game/VB/Input/Actions/IA_Move.IA_Move") },
		{ TEXT("Input.Look"), false, TEXT("/Game/VB/Input/Actions/IA_Look.IA_Look") },
		{ TEXT("Input.Jump"), false, TEXT("/Game/VB/Input/Actions/IA_Jump.IA_Jump") },
		{ TEXT("Input.Attack.Light"), false, TEXT("/Game/VB/Input/Actions/IA_Attack_Light.IA_Attack_Light") },
		{ TEXT("Input.Crouch"), false, TEXT("/Game/VB/Input/Actions/IA_Crouch.IA_Crouch") },
		{ TEXT("Input.WalkToggle"), false, TEXT("/Game/VB/Input/Actions/IA_Walk.IA_Walk") },
		{ TEXT("Input.LockOn"), false, TEXT("/Game/VB/Input/Actions/IA_LockOn.IA_LockOn") },
		{ TEXT("Input.SwitchTarget"), false, TEXT("/Game/VB/Input/Actions/IA_SwitchTarget.IA_SwitchTarget") },
		{ TEXT("Input.WeaponSlot.1"), false, TEXT("/Game/VB/Input/Actions/IA_WeaponSlot1.IA_WeaponSlot1") },
		{ TEXT("Input.WeaponSlot.2"), false, TEXT("/Game/VB/Input/Actions/IA_WeaponSlot2.IA_WeaponSlot2") },
		{ TEXT("Input.WeaponSlot.3"), false, TEXT("/Game/VB/Input/Actions/IA_WeaponSlot3.IA_WeaponSlot3") },
		{ TEXT("Input.Interact"), false, TEXT("/Game/VB/Input/Actions/IA_Interact.IA_Interact") },
		{ TEXT("Input.Ragdoll"), false, TEXT("/Game/VB/Input/Actions/IA_Ragdoll.IA_Ragdoll") },
		{ TEXT("Input.Aim"), false, TEXT("/Game/VB/Input/Actions/IA_Aim.IA_Aim") },
		{ TEXT("Input.WeaponSummon"), false, TEXT("/Game/VB/Input/Actions/IA_WeaponSummon.IA_WeaponSummon") },
		{ TEXT("Input.Combat.Guard"), true, TEXT("/Game/VB/Input/Actions/IA_Attack_Heavy.IA_Attack_Heavy") },
		{ TEXT("Input.Dodge"), true, TEXT("/Game/VB/Input/Actions/IA_Dodge.IA_Dodge") },
		{ TEXT("Input.Purify"), true, TEXT("/Game/VB/Input/Actions/IA_Purify.IA_Purify") },
		{ TEXT("Input.Execute"), true, TEXT("/Game/VB/Input/Actions/IA_Execute.IA_Execute") },
		{ TEXT("Input.Sprint"), true, TEXT("/Game/VB/Input/Actions/IA_Sprint.IA_Sprint") },
		{ TEXT("Input.CinematicSkill"), true, TEXT("/Game/VB/Input/Actions/IA_CinematicSkill.IA_CinematicSkill") },
	};

	// hero 가 출하 config 로 묶는 (input action, trigger event) 짝 - 모두 39개.
	//   crouch 의 release, cancel 도 묶는다(pad L3+R3 스킬 화음이 R3 를 뗄 때 release 를 보낸다).
	TArray<TPair<FString, ETriggerEvent>> ExpectedBindings()
	{
		using E = ETriggerEvent;
		// TPair 가 아니라 일반 aggregate - TPair(TTuple)는 forwarding 생성자라 중첩 { ... } 가 TArray 로 안 바뀐다.
		struct FTagEvents { const TCHAR* Tag; TArray<E> Events; };
		const FTagEvents Events[] =
		{
			{ TEXT("Input.Move"), { E::Triggered, E::Completed, E::Canceled } },
			{ TEXT("Input.Look"), { E::Triggered } },
			{ TEXT("Input.Jump"), { E::Started, E::Completed } },
			{ TEXT("Input.Attack.Light"), { E::Started, E::Completed, E::Canceled } },
			{ TEXT("Input.Crouch"), { E::Started, E::Completed, E::Canceled } },
			{ TEXT("Input.WalkToggle"), { E::Started } },
			{ TEXT("Input.LockOn"), { E::Started } },
			{ TEXT("Input.SwitchTarget"), { E::Triggered, E::Completed, E::Canceled } },
			{ TEXT("Input.WeaponSlot.1"), { E::Started } },
			{ TEXT("Input.WeaponSlot.2"), { E::Started } },
			{ TEXT("Input.WeaponSlot.3"), { E::Started } },
			{ TEXT("Input.Interact"), { E::Started, E::Completed } },
			{ TEXT("Input.Ragdoll"), { E::Started } },
			{ TEXT("Input.Aim"), { E::Started, E::Completed, E::Canceled } },
			{ TEXT("Input.WeaponSummon"), { E::Triggered } },
			{ TEXT("Input.Combat.Guard"), { E::Started, E::Completed } },
			{ TEXT("Input.Dodge"), { E::Started, E::Completed } },
			{ TEXT("Input.Purify"), { E::Started, E::Completed } },
			{ TEXT("Input.Execute"), { E::Started, E::Completed } },
			{ TEXT("Input.Sprint"), { E::Started, E::Completed } },
			{ TEXT("Input.CinematicSkill"), { E::Started, E::Completed } },
		};
		TArray<TPair<FString, ETriggerEvent>> Out;
		for (const auto& [Tag, TagEvents] : Events)
		{
			const FShippedRow* Row = Algo::FindByPredicate(Shipped, [Tag](const FShippedRow& R) { return FCString::Strcmp(R.Tag, Tag) == 0; });
			for (const ETriggerEvent Event : TagEvents)
			{
				Out.Emplace(Row ? FString(Row->Action) : FString(), Event);
			}
		}

		return Out;
	}

	FGameplayTag Tag(const TCHAR* Name)
	{
		return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false);
	}

	// 시험 월드 하나, PlayerState 와 play input 을 가진 로컬 PlayerController 하나, 출하 몸 하나.
	struct FFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		TStrongObjectPtr<ULocalPlayer> LocalPlayer;
		APlayerController* PC = nullptr;
		AVBPlayerState* State = nullptr;
		AVBCharacter* Body = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			UClass* BodyClass = LoadClass<AVBCharacter>(nullptr, BodyClassPath);
			if (!Test.TestNotNull(TEXT("shipped body class"), BodyClass))
			{
				return false;
			}

			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("world"), World))
			{
				return false;
			}

			Package.Reset(World->GetPackage());
			World->InitializeActorsForPlay(FURL());
			PC = World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("controller"), PC))
			{
				return false;
			}
			// 로컬 플레이어가 없으면 엔진이 원격 클라의 controller 로 보고 빙의 때 input component 를 안 만든다.
			LocalPlayer.Reset(NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient));
			PC->Player = LocalPlayer.Get();
			LocalPlayer->PlayerController = PC;
			PC->InitInputSystem();
			State = World->SpawnActor<AVBPlayerState>();
			if (!Test.TestNotNull(TEXT("player state"), State))
			{
				return false;
			}

			State->SetOwner(PC);
			PC->PlayerState = State;
			Body = World->SpawnActor<AVBCharacter>(BodyClass, FVector::ZeroVector, FRotator::ZeroRotator);
			return Test.TestNotNull(TEXT("shipped body"), Body)
				&& Test.TestNotNull(TEXT("enhanced play input"), Cast<UEnhancedPlayerInput>(PC->PlayerInput.Get()));
		}

		UVBHeroComponent* Hero() const { return UVBHeroComponent::FindFor(Body); }
		UEnhancedPlayerInput* Input() const { return Cast<UEnhancedPlayerInput>(PC->PlayerInput.Get()); }

		// play input 한 프레임: 누른 행동을 주입하고 몸의 binding 으로 평가한다.
		void Frame(TConstArrayView<TPair<const UInputAction*, FInputActionValue>> Held) const
		{
			constexpr float DeltaTime = 1.0f / 60.0f;
			for (const auto& [Action, Value] : Held)
			{
				Input()->InjectInputForAction(Action, Value);
			}

			TArray<UInputComponent*> Stack;
			if (Body->InputComponent)
			{
				Stack.Add(Body->InputComponent);
			}

			Input()->Tick(DeltaTime);
			Input()->ProcessInputStack(Stack, DeltaTime, false);
		}

		~FFixture()
		{
			if (World)
			{
				for (TActorIterator<AVBPlayerState> It(World); It; ++It)
				{
					if (UVBAbilitySystemComponent* ASC = It->GetVBAbilitySystemComponent())
					{
						ASC->CancelAllAbilities();
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

	template <typename T>
	T* InstanceOf(UAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
	{
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
		return Spec ? Cast<T>(Spec->GetPrimaryInstance()) : nullptr;
	}

	bool IsActive(UAbilitySystemComponent& ASC, FGameplayAbilitySpecHandle Handle)
	{
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
		return Spec && Spec->IsActive();
	}
}

// 출하 몸은 입력, 카메라 값을 hero 에서 읽고, 몸의 옛 칸은 없다(집은 하나).
//   이 프로세스에서 BP_VBCharacter 를 디스크에서 읽는다 - 에디터 이전의 별도 프로세스 readback.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroShippedConfigTest, "VB.Unit.Character.Hero.ShippedConfig",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroShippedConfigTest::RunTest(const FString& Parameters)
{
	using namespace VBHeroInputTest;
	UClass* BodyClass = LoadClass<AVBCharacter>(nullptr, BodyClassPath);
	const AVBCharacter* Defaults = BodyClass ? GetDefault<AVBCharacter>(BodyClass) : nullptr;
	const UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Defaults);
	const UVBInputConfig* Config = Hero ? Hero->GetInputConfig() : nullptr;
	if (!TestNotNull(TEXT("shipped body defaults"), Defaults) || !TestNotNull(TEXT("hero on the shipped body"), Hero)
		|| !TestNotNull(TEXT("hero input config"), Config))
	{
		return false;
	}

	TestEqual(TEXT("config is the migrated asset"), Config->GetPathName(), FString(ConfigPath));
	TestTrue(TEXT("config passes its own checks"), Config->FindProblems().IsEmpty());
	TestEqual(TEXT("row count"), Config->NativeInputActions.Num() + Config->AbilityInputActions.Num(), int32(UE_ARRAY_COUNT(Shipped)));
	for (const FShippedRow& Row : Shipped)
	{
		const FGameplayTag RowTag = Tag(Row.Tag);
		const UInputAction* Native = Config->FindNativeInputActionForTag(RowTag);
		const UInputAction* Ability = Config->FindAbilityInputActionForTag(RowTag);
		const UInputAction* Found = Row.bAbility ? Ability : Native;
		TestTrue(FString::Printf(TEXT("%s is in the %s list only"), Row.Tag, Row.bAbility ? TEXT("ability") : TEXT("native")),
			Found && !(Row.bAbility ? Native : Ability));
		TestEqual(FString::Printf(TEXT("%s action"), Row.Tag), GetPathNameSafe(Found), FString(Row.Action));
	}

	TestTrue(TEXT("camera style"), Hero->GetCameraStyle() == EVBCameraStyle::Medium);
	TestNull(TEXT("hero has no classic camera config"), FindFProperty<FProperty>(UVBHeroComponent::StaticClass(), TEXT("CameraConfig")));

	// 옛 집은 비었다: 몸에 입력 칸, camera style, camera config 도, camera style 함수도 없다.
	const TCHAR* OldSlots[] =
	{
		TEXT("MoveAction"), TEXT("LookAction"), TEXT("JumpAction"), TEXT("AttackLightAction"), TEXT("AttackHeavyAction"),
		TEXT("DodgeAction"), TEXT("PurifyAction"), TEXT("ExecuteAction"), TEXT("CrouchAction"), TEXT("SprintAction"),
		TEXT("WalkToggleAction"), TEXT("LockOnAction"), TEXT("WeaponSummonAction"), TEXT("SwitchTargetAction"),
		TEXT("WeaponSlot1Action"), TEXT("WeaponSlot2Action"), TEXT("WeaponSlot3Action"), TEXT("InteractAction"),
		TEXT("RagdollAction"), TEXT("AimAction"), TEXT("GASPCameraStyle"), TEXT("CameraConfig"),
		TEXT("SpringArmComponent"), TEXT("CameraComponent"),
	};
	for (const TCHAR* Name : OldSlots)
	{
		TestNull(FString::Printf(TEXT("body has no %s"), Name), FindFProperty<FProperty>(AVBCharacter::StaticClass(), Name));
	}

	TestNull(TEXT("body has no GetGASPCameraStyle"), AVBCharacter::StaticClass()->FindFunctionByName(TEXT("GetGASPCameraStyle")));
	TestNull(TEXT("body has no SetGASPCameraStyle"), AVBCharacter::StaticClass()->FindFunctionByName(TEXT("SetGASPCameraStyle")));
	return true;
}

// 빙의가 몸의 SetupPlayerInputComponent 로 input component 를 만들고, hero 는 기대한 (action, event) 짝만 정확히 묶는다
//   - 빠진 release 없음, ability 줄에 Triggered 누름 없음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroBindingsTest, "VB.Unit.Character.Hero.Bindings",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroBindingsTest::RunTest(const FString& Parameters)
{
	using namespace VBHeroInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.PC->Possess(F.Body);
	const UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(F.Body->InputComponent.Get());
	if (!TestNotNull(TEXT("possession made an enhanced input component"), Input))
	{
		return false;
	}

	TArray<TPair<FString, ETriggerEvent>> Actual;
	for (const TUniquePtr<FEnhancedInputActionEventBinding>& Binding : Input->GetActionEventBindings())
	{
		Actual.Emplace(GetPathNameSafe(Binding->GetAction()), Binding->GetTriggerEvent());
	}

	TArray<TPair<FString, ETriggerEvent>> Expected = ExpectedBindings();
	TestEqual(TEXT("binding count"), Actual.Num(), Expected.Num());
	for (const TPair<FString, ETriggerEvent>& Pair : Expected)
	{
		const int32 Index = Actual.IndexOfByKey(Pair);
		TestTrue(FString::Printf(TEXT("bound %s on %s"), *Pair.Key, *UEnum::GetValueAsString(Pair.Value)), Index != INDEX_NONE);
		if (Index != INDEX_NONE)
		{
			Actual.RemoveAtSwap(Index);
		}
	}

	for (const TPair<FString, ETriggerEvent>& Extra : Actual)
	{
		AddError(FString::Printf(TEXT("unexpected binding %s on %s"), *Extra.Key, *UEnum::GetValueAsString(Extra.Value)));
	}
	// native 계약 태그마다 줄이 있다(native 태그 목록은 UVBInputConfig::GetNativeInputContract 하나뿐).
	for (const FVBNativeInputContract& Contract : UVBInputConfig::GetNativeInputContract())
	{
		TestNotNull(FString::Printf(TEXT("contract tag %s has an action"), *Contract.InputTag.ToString()),
			F.Hero()->FindInputAction(Contract.InputTag));
	}

	return true;
}

// hero 가 적는 stick: move 이벤트마다 쓰고, 떼면 0, 빙의가 풀릴 때(엔진이 release 없이 input component 를 지운다)와
//   새 입력 세션이 시작될 때도 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroRawMoveTest, "VB.Unit.Character.Hero.RawMove",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroRawMoveTest::RunTest(const FString& Parameters)
{
	using namespace VBHeroInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.PC->Possess(F.Body);
	const UInputAction* Move = F.Hero()->FindInputAction(VBGameplayTags::Input_Move);
	if (!TestNotNull(TEXT("move action"), Move))
	{
		return false;
	}

	const FVector2D Stick(0.5, 1.0);

	F.Frame({ { Move, FInputActionValue(Stick) } });
	TestTrue(TEXT("move event records the stick"), F.Hero()->GetRawMoveInput().Equals(Stick, 1e-4));
	// 바깥 읽는 쪽(근접 이동 취소, 상호작용 취소, 자동 traversal, 플레이 봇)도 같은 값을 보고, hero 없는 액터, 액터 없음은 0.
	TestTrue(TEXT("outside lookup sees the hero's stick"), UVBHeroComponent::GetRawMoveInputFor(F.Body).Equals(Stick, 1e-4));
	TestTrue(TEXT("outside lookup of an actor without a hero is zero"), UVBHeroComponent::GetRawMoveInputFor(F.PC).IsNearlyZero());
	TestTrue(TEXT("outside lookup of no actor is zero"), UVBHeroComponent::GetRawMoveInputFor(nullptr).IsNearlyZero());
	F.Frame({});
	TestTrue(TEXT("release zeroes it"), F.Hero()->GetRawMoveInput().IsNearlyZero());

	F.Frame({ { Move, FInputActionValue(Stick) } });
	TestFalse(TEXT("held again"), F.Hero()->GetRawMoveInput().IsNearlyZero());
	F.PC->UnPossess();
	TestTrue(TEXT("unpossess ends the input session at zero"), F.Hero()->GetRawMoveInput().IsNearlyZero());
	F.PC->Possess(F.Body);
	TestTrue(TEXT("a new input session starts at zero"), F.Hero()->GetRawMoveInput().IsNearlyZero());

	// gait 주인은 입력이 묶일 때 hero 를 통해 stick 을 듣는다 - 조금 기울이면 걷고, 떼면 뛴다.
	const UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(F.Body);
	if (!TestNotNull(TEXT("gait owner"), Intent))
	{
		return false;
	}

	const float Threshold = F.Body->GetMovementConfig()->AnalogWalkRunThreshold;
	F.Frame({ { Move, FInputActionValue(FVector2D(0.0, Threshold * 0.6)) } });
	TestEqual(TEXT("a small tilt reaches the gait owner: walk"), Intent->GetGait(), EVBGait::Walk);
	F.Frame({});
	TestEqual(TEXT("release: run"), Intent->GetGait(), EVBGait::Run);

	return true;
}

// ability 줄은 몸의 dispatch 로 PlayerState ASC 에 간다: 누름은 Started 에서만(누르고 있어도 다시 안 누름), release 는 Completed,
//   release 로 끝낼지는 ability 자신의 정책이 정한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroAbilityRowsTest, "VB.Unit.Character.Hero.AbilityRows",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroAbilityRowsTest::RunTest(const FString& Parameters)
{
	using namespace VBHeroInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.PC->Possess(F.Body);
	UVBAbilitySystemComponent* ASC = F.State->GetVBAbilitySystemComponent();
	const UInputAction* Guard = F.Hero()->FindInputAction(VBGameplayTags::Input_Combat_Guard);
	const UInputAction* Dodge = F.Hero()->FindInputAction(VBGameplayTags::Input_Dodge);
	if (!TestNotNull(TEXT("PlayerState ASC"), ASC) || !TestNotNull(TEXT("guard action"), Guard)
		|| !TestNotNull(TEXT("dodge action"), Dodge))
	{
		return false;
	}

	const FGameplayAbilitySpecHandle Hold = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureHold::StaticClass()));
	const FGameplayAbilitySpecHandle Press = ASC->GiveAbility(FGameplayAbilitySpec(UVBInputFixtureDefaultRelease::StaticClass()));

	F.Frame({ { Guard, FInputActionValue(true) } });
	TestTrue(TEXT("guard row press activates the hold ability"), IsActive(*ASC, Hold));
	F.Frame({ { Guard, FInputActionValue(true) } });
	F.Frame({ { Guard, FInputActionValue(true) } });
	const UVBInputFixtureHold* HoldProbe = InstanceOf<UVBInputFixtureHold>(*ASC, Hold);
	TestEqual(TEXT("holding the guard key does not press again"), HoldProbe ? HoldProbe->PressedWhileActive : -1, 0);
	F.Frame({});
	TestFalse(TEXT("releasing the guard key ends the hold ability"), IsActive(*ASC, Hold));

	F.Frame({ { Dodge, FInputActionValue(true) } });
	TestTrue(TEXT("dodge row press activates"), IsActive(*ASC, Press));
	F.Frame({ { Dodge, FInputActionValue(true) } });
	F.Frame({});
	const UVBInputFixtureDefaultRelease* PressProbe = InstanceOf<UVBInputFixtureDefaultRelease>(*ASC, Press);
	TestTrue(TEXT("dodge row release does not end a default-policy ability"), IsActive(*ASC, Press));
	TestEqual(TEXT("it gets exactly one release notice"), PressProbe ? PressProbe->ReleasedWhileActive : -1, 1);
	TestEqual(TEXT("and no second press while held"), PressProbe ? PressProbe->PressedWhileActive : -1, 0);
	return true;
}

// 플레이 봇은 pawn 행동을 input tag 로 부른다 - 옛 칸 이름은 안 풀려서 낡은 ini 는 준비 단계에서 실패한다.
//   시나리오 코드 기본값(CameraAim, Interaction, LookReel, FastTravel, PickupCombat)은 모두 풀린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBHeroPlayBotLookupTest, "VB.Unit.Character.Hero.PlayBotLookup",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBHeroPlayBotLookupTest::RunTest(const FString& Parameters)
{
	using namespace VBHeroInputTest;
	FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	FVBPlayBotContext Ctx;
	Ctx.World = F.World;
	Ctx.Player = F.Body;
	const TCHAR* Defaults[] = { TEXT("Input.Aim"), TEXT("Input.Interact"), TEXT("Input.Sprint"), TEXT("Input.Attack.Light"),
		TEXT("Input.Execute"), TEXT("Input.WeaponSlot.3"), TEXT("Input.Combat.Guard"), TEXT("Input.Jump") };
	for (const TCHAR* Name : Defaults)
	{
		const FShippedRow* Row = Algo::FindByPredicate(Shipped, [Name](const FShippedRow& R) { return FCString::Strcmp(R.Tag, Name) == 0; });
		TestEqual(FString::Printf(TEXT("%s resolves"), Name), GetPathNameSafe(Ctx.FindAction(FName(Name))),
			FString(Row ? Row->Action : TEXT("")));
	}

	TestNull(TEXT("an old slot name does not resolve"), Ctx.FindAction(FName(TEXT("AttackLightAction"))));
	return true;
}
#endif
