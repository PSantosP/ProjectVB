// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/AutomationCommon.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "EnhancedPlayerInput.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Input/VBInputModifiers.h"
#include "Character/VBCharacter.h"
#include "Player/VBPlayerController.h"
#include "Player/VBWeaponWheelComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "UI/VBTouchControls.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Layout/Geometry.h"
#include "Widgets/SWidget.h"

namespace VBExactlyOnce
{
	constexpr float DeltaTime = 1.0f / 60.0f;

	// 저장 객체의 메모리만 바꾸며 모든 실패 경로에서 되돌린다. UpdateSettings 는 디스크에 저장하므로 부르지 않는다.
	struct FSettingsScope
	{
		explicit FSettingsScope(UVBSaveGame& InSave) : Save(InSave), Previous(InSave.Settings) {}
		~FSettingsScope() { Save.Settings = Previous; }
		UVBSaveGame& Save;
		FVBSettingsSaveData Previous;
	};

	// PC 자체의 축 배율(레거시 입력 배율이 켜져 있으면 1 이 아니다). UVBHeroComponent::HandleLook -> AddControllerYaw/PitchInput -> PC 가 곱하는 값을 잰다.
	FVector2D MeasureControllerScale(AVBPlayerController& Controller)
	{
		Controller.RotationInput = FRotator::ZeroRotator;
		Controller.AddYawInput(1.0f);
		Controller.AddPitchInput(1.0f);
		const FVector2D Scale(Controller.RotationInput.Yaw, Controller.RotationInput.Pitch);
		Controller.RotationInput = FRotator::ZeroRotator;
		return Scale;
	}

	FVector2D Evaluate(AVBPlayerController& Controller, AVBCharacter& Character,
		UEnhancedPlayerInput& PlayerInput)
	{
		// 같은 콜백 안에서 한 번만 평가하고, 다음 PC Tick 이 RotationInput 을 지우기 전에 읽는다.
		Controller.RotationInput = FRotator::ZeroRotator;
		TArray<UInputComponent*> Stack;
		Stack.Add(Character.InputComponent);
		Stack.Add(Controller.InputComponent);
		PlayerInput.Tick(DeltaTime);
		PlayerInput.ProcessInputStack(Stack, DeltaTime, false);
		return FVector2D(Controller.RotationInput.Yaw, Controller.RotationInput.Pitch);
	}

	void FeedAxis(AVBPlayerController& Controller, FKey XKey, FKey YKey, const FVector2D& Value)
	{
		// Mouse2D/Right2D 자체에 스칼라를 넣지 않는다. 엔진이 두 1D 키로 2D 키 상태를 합친다.
		Controller.InputKey(FInputKeyEventArgs::CreateSimulated(XKey, IE_Axis, Value.X, 1));
		Controller.InputKey(FInputKeyEventArgs::CreateSimulated(YKey, IE_Axis, Value.Y, 1));
	}

	// 절대값 고정(사전 점검 P1): 비율만 보면 시점 처리(UVBHeroComponent::HandleLook)의 상수 배율, 부호 오류가 기준값과 함께 움직여 통과한다.
	//   회전 입력 = 평가된 IA_Look 값 x PC 배율 이어야 시점 처리가 값을 그대로 넘긴 것이다.
	bool CheckPassThrough(FAutomationTestBase& Test, const TCHAR* Label, const FVector2D& Rotation,
		const UEnhancedPlayerInput& PlayerInput, const UInputAction& Look, const FVector2D& ControllerScale)
	{
		const FVector2D Action = PlayerInput.GetActionValue(&Look).Get<FVector2D>();
		const bool bYaw = Test.TestTrue(FString::Printf(TEXT("%s yaw = IA_Look x PC scale (the hero look handler passes it through)"), Label),
			FMath::IsNearlyEqual(Rotation.X, Action.X * ControllerScale.X, 0.01f));
		const bool bPitch = Test.TestTrue(FString::Printf(TEXT("%s pitch = IA_Look x PC scale (the hero look handler passes it through)"), Label),
			FMath::IsNearlyEqual(Rotation.Y, Action.Y * ControllerScale.Y, 0.01f));
		return bYaw && bPitch;
	}

	bool CheckPair(FAutomationTestBase& Test, const TCHAR* Label, const FVector2D& Baseline,
		const FVector2D& Calibrated, float Sensitivity)
	{
		if (!Test.TestTrue(FString::Printf(TEXT("%s baseline has both axes"), Label),
			!FMath::IsNearlyZero(Baseline.X) && !FMath::IsNearlyZero(Baseline.Y)))
		{
			return false;
		}

		const bool bYaw = Test.TestTrue(FString::Printf(TEXT("%s yaw calibrated exactly once"), Label),
			FMath::IsNearlyEqual(Calibrated.X, Baseline.X * Sensitivity, 0.01f));
		const bool bPitch = Test.TestTrue(FString::Printf(TEXT("%s inverted pitch calibrated exactly once"), Label),
			FMath::IsNearlyEqual(Calibrated.Y, -Baseline.Y * Sensitivity, 0.01f));
		return bYaw && bPitch;
	}

	bool CheckLookMapping(FAutomationTestBase& Test, const UInputMappingContext& Context,
		const UInputAction& Look, FKey Key, EVBLookDevice Device, bool bPad)
	{
		int32 Matches = 0;
		for (const FEnhancedActionKeyMapping& Mapping : Context.GetMappings())
		{
			if (Mapping.Action != &Look || Mapping.Key != Key)
			{
				continue;
			}

			++Matches;
			int32 CalibrationCount = 0;
			int32 StickCount = 0;
			for (const UInputModifier* Modifier : Mapping.Modifiers)
			{
				if (!Test.TestNotNull(TEXT("authored Look modifier"), Modifier))
				{
					return false;
				}

				if (const auto* Calibration = Cast<UVBInputModifier_LookCalibration>(Modifier))
				{
					++CalibrationCount;
					Test.TestEqual(TEXT("Look device is authored on the mapping"), Calibration->Device, Device);
				}

				if (const auto* Stick = Cast<UVBInputModifier_StickDeadZone>(Modifier))
				{
					++StickCount;
					Test.TestEqual(TEXT("Look stick is selected"), Stick->Stick, EVBStick::Look);
				}

				if (bPad)
				{
					Test.TestFalse(TEXT("Engine DeadZone removed from pad Look"), Modifier->IsA<UInputModifierDeadZone>());
				}
			}

			Test.TestEqual(TEXT("Exactly one LookCalibration per mapping"), CalibrationCount, 1);
			Test.TestTrue(TEXT("LookCalibration is last authored modifier"),
				!Mapping.Modifiers.IsEmpty() && Mapping.Modifiers.Last()->IsA<UVBInputModifier_LookCalibration>());
			if (bPad)
			{
				Test.TestEqual(TEXT("Exactly one settings dead zone on pad Look"), StickCount, 1);
			}
		}

		return Test.TestEqual(TEXT("Exactly one real Look mapping for expected key"), Matches, 1);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputCalibrationExactlyOnceTest,
	"VB.Unit.Input.Calibration.ExactlyOnce",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FVBInputCalibrationExactlyOnceTest::RunTest(const FString& Parameters)
{
	// 별도 게임 프로세스(headless-test.sh 의 -game)에서만 돈다. 에디터에서 AutomationOpenMap 은 PIE 를 여는데(AutomationCommon.h)
	//   PIE 는 user 의 자리다 - ClientContext 로만 표시하고, 그래도 에디터에서 불리면 멈춘다.
	if (GIsEditor)
	{
		AddError(TEXT("game process only - run through .claude/scripts/headless-test.sh"));
		return false;
	}
	// 실제 GameInstance, LocalPlayer, 빙의한 BP_VBCharacter 가 있는 게임 맵(플레이 봇과 같은 맵).
	AutomationOpenMap(TEXT("/Game/VB/Maps/Tests/L_VBTest"));
	// 고정 대기가 아니라 PC, 빙의한 폰, 입력 컴포넌트가 생길 때까지 기다린다 - 고정 대기는 느린 로드에서 흔들린다.
	//   30초 안에 안 생기면 준비 실패로 아래 TestNotNull 들이 오류를 낸다 - 통과로 떨어지지 않는다.
	const double ReadyDeadline = FPlatformTime::Seconds() + 30.0;
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, ReadyDeadline]()
	{
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game && Context.World())
			{
				World = Context.World();
				break;
			}
		}

		AVBPlayerController* PC = World ? Cast<AVBPlayerController>(World->GetFirstPlayerController()) : nullptr;
		AVBCharacter* Pawn = PC ? Cast<AVBCharacter>(PC->GetPawn()) : nullptr;
		if ((!Pawn || !Pawn->InputComponent || !PC->InputComponent) && FPlatformTime::Seconds() < ReadyDeadline)
		{
			return false; // 다음 프레임에 다시 본다
		}

		ULocalPlayer* LP = PC ? PC->GetLocalPlayer() : nullptr;
		UEnhancedInputLocalPlayerSubsystem* Input = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		UEnhancedPlayerInput* PlayerInput = Input ? Input->GetPlayerInput() : nullptr;
		UVBSaveGameSubsystem* Saves = World && World->GetGameInstance()
			? World->GetGameInstance()->GetSubsystem<UVBSaveGameSubsystem>() : nullptr;
		UVBSaveGame* Save = Saves ? Saves->LoadOrCreateSettings() : nullptr;
		UInputAction* Look = LoadObject<UInputAction>(nullptr, TEXT("/Game/VB/Input/Actions/IA_Look.IA_Look"));
		UInputMappingContext* Mouse = LoadObject<UInputMappingContext>(nullptr,
			TEXT("/Game/VB/Input/Contexts/IMC_KeyboardMouse.IMC_KeyboardMouse"));
		UInputMappingContext* Pad = LoadObject<UInputMappingContext>(nullptr,
			TEXT("/Game/VB/Input/Contexts/IMC_Gamepad.IMC_Gamepad"));
		if (!TestNotNull(TEXT("game controller"), PC) || !TestNotNull(TEXT("possessed VBCharacter"), Pawn)
			|| !TestNotNull(TEXT("local player"), LP) || !TestNotNull(TEXT("Enhanced Input subsystem"), Input)
			|| !TestNotNull(TEXT("Enhanced PlayerInput"), PlayerInput)
			|| !TestNotNull(TEXT("game-instance settings subsystem"), Saves)
			|| !TestNotNull(TEXT("mutable in-memory settings"), Save)
			|| !TestNotNull(TEXT("real IA_Look"), Look)
			|| !TestNotNull(TEXT("real keyboard/mouse context"), Mouse)
			|| !TestNotNull(TEXT("real gamepad context"), Pad))
		{
			return true;
		}

		if (!TestNotNull(TEXT("possessed pawn input bindings"), Pawn->InputComponent.Get())
			|| !TestNotNull(TEXT("controller input component"), PC->InputComponent.Get())
			|| !TestTrue(TEXT("local controller"), PC->IsLocalController())
			|| !TestTrue(TEXT("look is not suppressed"), !PC->IsLookInputIgnored())
			|| !TestTrue(TEXT("wheel closed"), !PC->GetWeaponWheel() || !PC->GetWeaponWheel()->IsOpen())
			|| !TestEqual(TEXT("real Look is 2D"), Look->ValueType, EInputActionValueType::Axis2D)
			|| !TestEqual(TEXT("IA_Look has no action-level modifier"), Look->Modifiers.Num(), 0))
		{
			return true;
		}

		if (!VBExactlyOnce::CheckLookMapping(*this, *Mouse, *Look, EKeys::Mouse2D, EVBLookDevice::Mouse, false)
			|| !VBExactlyOnce::CheckLookMapping(*this, *Pad, *Look, EKeys::Gamepad_Right2D, EVBLookDevice::Gamepad, true))
		{
			return true;
		}

		const FVector2D ControllerScale = VBExactlyOnce::MeasureControllerScale(*PC);
		if (!TestFalse(TEXT("controller look scale is measurable"), ControllerScale.IsNearlyZero()))
		{
			return true;
		}

		VBExactlyOnce::FSettingsScope Restore(*Save);
		Save->Settings.LookSensitivity = 1.0f;
		Save->Settings.GamepadLookSensitivity = 1.0f;
		Save->Settings.GamepadLookDeadZone = 0.15f;
		Save->Settings.bInvertYAxis = false;
		FModifyContextOptions Immediate;
		Immediate.bForceImmediately = true;
		Immediate.bIgnoreAllPressedKeysUntilRelease = false;

		// 각 입력마다 1회 평가한다. 기준값도 실제 자산의 Negate/Scalar/AxisConfig 를 통과한다.
		Input->RemoveMappingContext(Pad, Immediate);
		Input->RemoveMappingContext(Mouse, Immediate);
		Input->AddMappingContext(Mouse, 0, Immediate);
		if (!TestTrue(TEXT("mouse context active"), Input->HasMappingContext(Mouse)))
		{
			return true;
		}

		VBExactlyOnce::FeedAxis(*PC, EKeys::MouseX, EKeys::MouseY, FVector2D(12.0, 8.0));
		const FVector2D MouseBase = VBExactlyOnce::Evaluate(*PC, *Pawn, *PlayerInput);
		Save->Settings.LookSensitivity = 1.7f;
		Save->Settings.bInvertYAxis = true;
		VBExactlyOnce::FeedAxis(*PC, EKeys::MouseX, EKeys::MouseY, FVector2D(12.0, 8.0));
		const FVector2D MouseScaled = VBExactlyOnce::Evaluate(*PC, *Pawn, *PlayerInput);
		VBExactlyOnce::CheckPassThrough(*this, TEXT("mouse"), MouseScaled, *PlayerInput, *Look, ControllerScale);
		VBExactlyOnce::CheckPair(*this, TEXT("mouse"), MouseBase, MouseScaled, 1.7f);

		Input->RemoveMappingContext(Mouse, Immediate);
		Input->AddMappingContext(Pad, 0, Immediate);
		if (!TestTrue(TEXT("pad context active"), Input->HasMappingContext(Pad)))
		{
			return true;
		}

		Save->Settings.LookSensitivity = 1.0f;
		Save->Settings.GamepadLookSensitivity = 1.0f;
		Save->Settings.bInvertYAxis = false;
		VBExactlyOnce::FeedAxis(*PC, EKeys::Gamepad_RightX, EKeys::Gamepad_RightY, FVector2D(0.7, 0.4));
		const FVector2D PadBase = VBExactlyOnce::Evaluate(*PC, *Pawn, *PlayerInput);
		Save->Settings.GamepadLookSensitivity = 1.8f;
		Save->Settings.bInvertYAxis = true;
		VBExactlyOnce::FeedAxis(*PC, EKeys::Gamepad_RightX, EKeys::Gamepad_RightY, FVector2D(0.7, 0.4));
		const FVector2D PadScaled = VBExactlyOnce::Evaluate(*PC, *Pawn, *PlayerInput);
		VBExactlyOnce::CheckPassThrough(*this, TEXT("pad"), PadScaled, *PlayerInput, *Look, ControllerScale);
		VBExactlyOnce::CheckPair(*this, TEXT("pad"), PadBase, PadScaled, 1.8f);

		// Slate 공개 이벤트는 SObjectWidget -> NativeOnTouchMoved -> 실제 LookPad::OnTouchMove 로 간다.
		// CPP 단독 초안에 UHT 테스트 UCLASS 를 넣거나 기존 FVBTouchTestAccess 를 다시 정의하지 않는다.
		Input->RemoveMappingContext(Pad, Immediate);
		PlayerInput->FlushPressedKeys();
		UVBTouchLookPad* Touch = CreateWidget<UVBTouchLookPad>(PC, UVBTouchLookPad::StaticClass());
		if (!TestNotNull(TEXT("real touch look widget"), Touch))
		{
			return true;
		}

		Touch->SetInputAction(Look);
		const TSharedRef<SWidget> Slate = Touch->TakeWidget();
		const FGeometry Geometry;
		const FPointerEvent Start( /*UserIndex*/0, /*PointerIndex*/0, FVector2D::ZeroVector,
			FVector2D::ZeroVector, /*Force*/1.0f, /*bPressLeftMouseButton*/true);
		if (!TestTrue(TEXT("touch start reaches real widget"), Slate->OnTouchStarted(Geometry, Start).IsEventHandled()))
		{
			return true;
		}

		Save->Settings.LookSensitivity = 1.0f;
		Save->Settings.bInvertYAxis = false;
		const FPointerEvent Move( /*UserIndex*/0, /*PointerIndex*/0, FVector2D(12.0, -8.0),
			FVector2D::ZeroVector, /*Force*/1.0f, /*bPressLeftMouseButton*/true);
		if (!TestTrue(TEXT("touch move reaches real widget"), Slate->OnTouchMoved(Geometry, Move).IsEventHandled()))
		{
			return true;
		}

		const FVector2D TouchBase = VBExactlyOnce::Evaluate(*PC, *Pawn, *PlayerInput);
		Save->Settings.LookSensitivity = 1.7f;
		Save->Settings.bInvertYAxis = true;
		if (!TestTrue(TEXT("second touch move reaches real widget"), Slate->OnTouchMoved(Geometry, Move).IsEventHandled()))
		{
			return true;
		}

		const FVector2D TouchScaled = VBExactlyOnce::Evaluate(*PC, *Pawn, *PlayerInput);
		VBExactlyOnce::CheckPassThrough(*this, TEXT("touch"), TouchScaled, *PlayerInput, *Look, ControllerScale);
		VBExactlyOnce::CheckPair(*this, TEXT("touch"), TouchBase, TouchScaled, 1.7f);
		Touch->ReleaseTouch();
		// 장치 관리자가 처음에 건 상태(키보드, 마우스 IMC 하나)로 돌려 둔다 - 같은 프로세스의 다음 시험이 이 월드를 볼 수 있다.
		Input->AddMappingContext(Mouse, 0, Immediate);
		return true;
	}));
	return true;
}
#endif
