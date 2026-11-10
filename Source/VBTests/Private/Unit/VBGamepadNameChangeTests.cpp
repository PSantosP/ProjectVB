// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CommonInputBaseTypes.h"
#include "CommonInputSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Input/VBInputDeviceManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/ScopeExit.h"
#include "Unit/VBGamepadNameChangeProbe.h"
#include "Unit/VBUITestFixture.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

namespace VBDeviceManagerTest
{
	// 격리 월드의 Pawn을 시작하고 실제 CommonInput 알림을 구독한다.
	struct FManagerRig
	{
		UVBInputDeviceManager* Manager = nullptr;
		UInputMappingContext* KeyboardIMC = nullptr;
		UInputMappingContext* GamepadIMC = nullptr;
		TWeakObjectPtr<ULocalPlayer> LocalPlayer;
		TWeakObjectPtr<UGameViewportClient> PreviousViewport;
		TStrongObjectPtr<UVBGamepadNameChangeViewport> Viewport;

		~FManagerRig()
		{
			if (LocalPlayer.IsValid() && LocalPlayer->ViewportClient == Viewport.Get())
			{
				LocalPlayer->ViewportClient = PreviousViewport.Get();
			}
		}

		bool Begin(FAutomationTestBase& Test, VBUITest::FRowFixture& Fixture)
		{
			LocalPlayer = Fixture.LocalPlayer;
			PreviousViewport = Fixture.LocalPlayer->ViewportClient;
			Viewport.Reset(NewObject<UVBGamepadNameChangeViewport>(GEngine));
			Viewport->FixtureWorld = Fixture.World;
			Fixture.LocalPlayer->ViewportClient = Viewport.Get();
			const UCommonInputSubsystem* Common = UCommonInputSubsystem::Get(Fixture.LocalPlayer);
			if (!Test.TestTrue(TEXT("CommonInput resolves the fixture world"), Common && Common->GetWorld() == Fixture.World))
			{
				return false;
			}

			Fixture.Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Fixture.Controller);
			AGameStateBase* GameState = Fixture.World->SpawnActor<AGameStateBase>();
			if (!Test.TestNotNull(TEXT("game state"), GameState))
			{
				return false;
			}

			Fixture.World->SetGameState(GameState);
			GameState->HandleBeginPlay();
			if (!Test.TestTrue(TEXT("world began play"), Fixture.World->HasBegunPlay()))
			{
				return false;
			}

			APawn* Pawn = Fixture.World->SpawnActor<APawn>();
			if (!Test.TestNotNull(TEXT("pawn"), Pawn))
			{
				return false;
			}

			Fixture.Controller->Possess(Pawn);
			if (!Test.TestTrue(TEXT("pawn locally controlled"), Pawn->IsLocallyControlled()))
			{
				return false;
			}

			Manager = NewObject<UVBInputDeviceManager>(Pawn);
			KeyboardIMC = NewObject<UInputMappingContext>(Pawn);
			GamepadIMC = NewObject<UInputMappingContext>(Pawn);
			FObjectProperty* KeyboardProperty = FindFProperty<FObjectProperty>(UVBInputDeviceManager::StaticClass(),
				TEXT("IMC_KeyboardMouse"));
			FObjectProperty* GamepadProperty = FindFProperty<FObjectProperty>(UVBInputDeviceManager::StaticClass(), TEXT("IMC_Gamepad"));
			if (!Test.TestNotNull(TEXT("keyboard IMC property"), KeyboardProperty)
				|| !Test.TestNotNull(TEXT("gamepad IMC property"), GamepadProperty))
			{
				return false;
			}

			KeyboardProperty->SetObjectPropertyValue_InContainer(Manager, KeyboardIMC);
			GamepadProperty->SetObjectPropertyValue_InContainer(Manager, GamepadIMC);
			return true;
		}

		bool Register(FAutomationTestBase& Test) const
		{
			Manager->RegisterComponent();
			const UCommonInputSubsystem* Common = LocalPlayer.IsValid() ? UCommonInputSubsystem::Get(LocalPlayer.Get()) : nullptr;
			return Test.TestTrue(TEXT("manager BeginPlay ran"), Manager->HasBegunPlay())
				&& Test.TestTrue(TEXT("manager listens to the engine input event"), Common
					&& Common->OnInputMethodChangedNative.IsBoundToObject(Manager));
		}
	};
}

// 같은 패드 종류의 이름 변경은 그림 알림 하나만 내며 IMC를 다시 걸지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGamepadNameChangeTest, "VB.Unit.Input.GamepadNameChange",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGamepadNameChangeTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	ULocalPlayer* LP = Fixture.LocalPlayer;
	UCommonInputSubsystem* Common = UCommonInputSubsystem::Get(LP);
	UEnhancedInputLocalPlayerSubsystem* Input = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	UCommonInputPlatformSettings* Platform = UCommonInputPlatformSettings::Get();
	if (!TestNotNull(TEXT("CommonInput"), Common) || !TestNotNull(TEXT("EnhancedInput"), Input)
		|| !TestNotNull(TEXT("platform settings"), Platform))
	{
		return false;
	}

	if (!TestTrue(TEXT("gamepad name may change in platform config"), Platform->CanChangeGamepadType()))
	{
		return false;
	}

	VBDeviceManagerTest::FManagerRig Rig;
	if (!Rig.Begin(*this, Fixture))
	{
		return false;
	}

	Common->SetCurrentInputType(ECommonInputType::Gamepad);
	Common->SetGamepadInputType(TEXT("XSX"));
	if (!TestEqual(TEXT("gamepad selected"), Common->GetCurrentInputType(), ECommonInputType::Gamepad)
		|| !TestEqual(TEXT("XSX selected"), Common->GetCurrentGamepadName(), FName(TEXT("XSX"))))
	{
		return false;
	}

	if (!Rig.Register(*this)
		|| !TestEqual(TEXT("manager started on gamepad"), Rig.Manager->GetCurrentDeviceType(), ECommonInputType::Gamepad)
		|| !TestTrue(TEXT("gamepad IMC active"), Input->HasMappingContext(Rig.GamepadIMC)))
	{
		return false;
	}

	UVBGamepadNameChangeProbe* Probe = NewObject<UVBGamepadNameChangeProbe>(Fixture.World);
	UVBInputDeviceManager* Manager = Rig.Manager;
	Manager->OnInputDeviceChanged.AddDynamic(Probe, &UVBGamepadNameChangeProbe::OnDeviceChanged);
	Input->OnMappingContextAdded.AddDynamic(Probe, &UVBGamepadNameChangeProbe::OnContextAdded);
	Input->OnMappingContextRemoved.AddDynamic(Probe, &UVBGamepadNameChangeProbe::OnContextRemoved);
	ON_SCOPE_EXIT
	{
		Manager->OnInputDeviceChanged.RemoveDynamic(Probe, &UVBGamepadNameChangeProbe::OnDeviceChanged);
		Input->OnMappingContextAdded.RemoveDynamic(Probe, &UVBGamepadNameChangeProbe::OnContextAdded);
		Input->OnMappingContextRemoved.RemoveDynamic(Probe, &UVBGamepadNameChangeProbe::OnContextRemoved);
	};

	Common->SetGamepadInputType(TEXT("PS5"));
	TestEqual(TEXT("pad name changed"), Common->GetCurrentGamepadName(), FName(TEXT("PS5")));
	TestEqual(TEXT("one refresh broadcast"), Probe->DeviceChanges, 1);
	TestEqual(TEXT("refresh device remains gamepad"), Probe->LastDevice, ECommonInputType::Gamepad);
	TestEqual(TEXT("no context re-add"), Probe->ContextAdds, 0);
	TestEqual(TEXT("no context removal"), Probe->ContextRemoves, 0);
	TestTrue(TEXT("gamepad IMC remains active"), Input->HasMappingContext(Rig.GamepadIMC));
	Common->SetGamepadInputType(TEXT("PS5"));
	TestEqual(TEXT("same name adds no refresh"), Probe->DeviceChanges, 1);
	// EndPlay 만 부른다. 액터를 지우면 매니저가 쓰레기가 되고 엔진 대리자의 약한 참조가 저절로 끊겨, 구독 해제가 빠져도 아래 확인이 통과한다.
	static_cast<UActorComponent*>(Manager)->EndPlay(EEndPlayReason::RemovedFromWorld);
	if (!TestTrue(TEXT("manager is still a live object after EndPlay"), IsValid(Manager)))
	{
		return false;
	}

	TestFalse(TEXT("EndPlay removes the engine subscription"), Common->OnInputMethodChangedNative.IsBoundToObject(Manager));
	Common->SetGamepadInputType(TEXT("XSX"));
	TestEqual(TEXT("engine changes after EndPlay do not reach the manager"), Probe->DeviceChanges, 1);
	return true;
}

// 장치가 바뀌었다는 알림을 받는 순간 매핑이 이미 새 장치의 것이다 - 구독자(키 이름, 그림)가 그 순간 새 키를 읽는다(L3d 검토 P2).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDeviceSwitchKeysTest, "VB.Unit.Input.DeviceSwitchKeys",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDeviceSwitchKeysTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	ULocalPlayer* LP = Fixture.LocalPlayer;
	UCommonInputSubsystem* Common = UCommonInputSubsystem::Get(LP);
	UEnhancedInputLocalPlayerSubsystem* Input = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!TestNotNull(TEXT("CommonInput"), Common) || !TestNotNull(TEXT("EnhancedInput"), Input))
	{
		return false;
	}

	VBDeviceManagerTest::FManagerRig Rig;
	if (!Rig.Begin(*this, Fixture))
	{
		return false;
	}

	UInputAction* Action = NewObject<UInputAction>(GetTransientPackage());
	Rig.KeyboardIMC->MapKey(Action, EKeys::SpaceBar);
	Rig.GamepadIMC->MapKey(Action, EKeys::Gamepad_FaceButton_Bottom);
	Common->SetCurrentInputType(ECommonInputType::MouseAndKeyboard);
	if (!Rig.Register(*this)
		|| !TestEqual(TEXT("manager started on keyboard"), Rig.Manager->GetCurrentDeviceType(),
			ECommonInputType::MouseAndKeyboard))
	{
		return false;
	}

	UVBGamepadNameChangeProbe* Probe = NewObject<UVBGamepadNameChangeProbe>(Fixture.World);
	Probe->Input = Input;
	Probe->Action = Action;
	UVBInputDeviceManager* Manager = Rig.Manager;
	Manager->OnInputDeviceChanged.AddDynamic(Probe, &UVBGamepadNameChangeProbe::OnDeviceChanged);
	ON_SCOPE_EXIT { Manager->OnInputDeviceChanged.RemoveDynamic(Probe, &UVBGamepadNameChangeProbe::OnDeviceChanged); };

	Common->SetCurrentInputType(ECommonInputType::Gamepad);
	TestEqual(TEXT("switch to pad broadcast once"), Probe->DeviceChanges, 1);
	TestTrue(TEXT("pad key readable at the broadcast"), Probe->KeysAtBroadcast.Num() == 1
		&& Probe->KeysAtBroadcast[0] == EKeys::Gamepad_FaceButton_Bottom);
	Common->SetCurrentInputType(ECommonInputType::MouseAndKeyboard);
	TestEqual(TEXT("switch back broadcast"), Probe->DeviceChanges, 2);
	TestTrue(TEXT("keyboard key readable at the broadcast"), Probe->KeysAtBroadcast.Num() == 1
		&& Probe->KeysAtBroadcast[0] == EKeys::SpaceBar);
	return true;
}
#endif
