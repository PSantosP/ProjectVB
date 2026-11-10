// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CommonInputSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Input/VBInputDeviceManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Unit/VBUITestFixture.h"
#include "UI/VBUISettings.h"
#include "UObject/UnrealType.h"

namespace VBPlayBotInputDriverTest
{
struct FMappingFixture
{
	VBUITest::FRowFixture Player;
	UEnhancedInputLocalPlayerSubsystem* Input = nullptr;
	UCommonInputSubsystem* CommonInput = nullptr;
	UVBInputDeviceManager* Devices = nullptr;

	~FMappingFixture()
	{
		if (Input)
		{
			Input->ClearAllMappings();
		}

		if (AController* Controller = Player.Controller)
		{
			Controller->SetPawn(nullptr);
		}

		Player.Shutdown();
	}

	bool Initialize(FAutomationTestBase& Test)
	{
		if (!Player.Initialize(Test))
		{
			return false;
		}

		Player.Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Player.Controller);
		Input = Player.LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		CommonInput = UCommonInputSubsystem::Get(Player.LocalPlayer);
		APawn* Pawn = Player.World->SpawnActor<APawn>();
		if (!Test.TestNotNull(TEXT("input subsystem"), Input)
			|| !Test.TestNotNull(TEXT("CommonInput"), CommonInput)
			|| !Test.TestNotNull(TEXT("transient fixture pawn"), Pawn))
		{
			return false;
		}

		AController* Controller = Player.Controller;
		Controller->SetPawn(Pawn);
		Devices = NewObject<UVBInputDeviceManager>(Pawn);
		Pawn->AddInstanceComponent(Devices);
		Devices->RegisterComponent();
		return Test.TestTrue(TEXT("configured mapping owner is discoverable"), UVBInputDeviceManager::FindFor(Pawn) == Devices);
	}

	bool Configure(FAutomationTestBase& Test, UInputMappingContext* Pad, UInputMappingContext* Keyboard)
	{
		// transient fixture 만 설정한다 - 게임 코드에 friend 를 늘리거나 애셋 객체를 바꾸지 않는다.
		const FObjectPropertyBase* PadProperty = FindFProperty<FObjectPropertyBase>(UVBInputDeviceManager::StaticClass(),
			TEXT("IMC_Gamepad"));
		const FObjectPropertyBase* KeyboardProperty = FindFProperty<FObjectPropertyBase>(UVBInputDeviceManager::StaticClass(),
			TEXT("IMC_KeyboardMouse"));
		if (!Test.TestNotNull(TEXT("pad context property"), PadProperty)
			|| !Test.TestNotNull(TEXT("keyboard context property"), KeyboardProperty))
		{
			return false;
		}

		PadProperty->SetObjectPropertyValue_InContainer(Devices, Pad);
		KeyboardProperty->SetObjectPropertyValue_InContainer(Devices, Keyboard);
		return true;
	}

	void Activate(UInputMappingContext* Context, ECommonInputType Type) const
	{
		Input->ClearAllMappings();
		CommonInput->SetCurrentInputType(Type);
		FModifyContextOptions Options;
		Options.bForceImmediately = true;
		Input->AddMappingContext(Context, 0, Options);
	}
};

}
