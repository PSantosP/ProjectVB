// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CommonInputSettings.h"
#include "CommonInputSubsystem.h"
#include "ICommonInputModule.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Input/VBInputDeviceManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "PlayBot/VBPlayBotInputDriver.h"
#include "Unit/VBUITestFixture.h"
#include "Unit/VBPlayBotInputFixture.h"
#include "UI/VBUISettings.h"
#include "UObject/UnrealType.h"

namespace VBPlayBotInputDriverTest
{

UInputMappingContext* CopyContext(UInputMappingContext* Source)
{
	return DuplicateObject<UInputMappingContext>(Source, GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), Source->GetClass(), TEXT("VBPlayBotIMCCopy")));
}

void ExpectMissing(FAutomationTestBase& Test, const FMappingFixture& Fixture, const UInputAction* Action, const FString& Label)
{
	// 새 driver 를 써서 앞서 성공한 해석 캐시가 매핑 없음 대조를 가리지 않게 한다.
	FVBPlayBotInputDriver Driver;
	FString Error;
	const bool bResolved = Driver.ResolveAction(Fixture.Player.Controller, TEXT("Input.LockOn"), Action, Error);
	Test.TestFalse(Label + TEXT(" rejects a missing usable mapping"), bResolved);
	Test.TestTrue(Label + TEXT(" preserves the actionable error"),
		Error.Contains(TEXT("no gamepad button or keyboard key is mapped to Input.LockOn")));
	Test.TestFalse(Label + TEXT(" creates no cached action"), Driver.HasAction(TEXT("Input.LockOn")));
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotInputDriverDeviceIndependentTest,
	"VB.Unit.PlayBot.InputDriver.DeviceIndependent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotInputDriverDeviceIndependentTest::RunTest(const FString& Parameters)
{
	using namespace VBPlayBotInputDriverTest;
	FMappingFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UInputAction* Action = NewObject<UInputAction>(GetTransientPackage());
	UInputMappingContext* Pad = NewObject<UInputMappingContext>(GetTransientPackage());
	UInputMappingContext* Keyboard = NewObject<UInputMappingContext>(GetTransientPackage());
	Pad->MapKey(Action, EKeys::Gamepad_RightThumbstick);
	Keyboard->MapKey(Action, EKeys::MiddleMouseButton);
	if (!Fixture.Configure(*this, Pad, Keyboard))
	{
		return false;
	}

	Fixture.Activate(Keyboard, ECommonInputType::MouseAndKeyboard);
	TestTrue(TEXT("precondition: only mouse mapping is active"),
		Fixture.Input->QueryKeysMappedToAction(Action).Contains(EKeys::MiddleMouseButton));
	for (const ECommonInputType Type : {ECommonInputType::MouseAndKeyboard, ECommonInputType::Gamepad})
	{
		Fixture.Activate(Type == ECommonInputType::Gamepad ? Pad : Keyboard, Type);
		FVBPlayBotInputDriver Driver;
		FString Error;
		TestTrue(TEXT("configured pad button resolves independently of active context"),
			Driver.ResolveAction(Fixture.Player.Controller, TEXT("Input.LockOn"), Action, Error));
		TestEqual(TEXT("configured R3 is selected"), Driver.GetKey(TEXT("Input.LockOn")).ToString(),
			EKeys::Gamepad_RightThumbstick.ToString());
		TestFalse(TEXT("configured pad binding is not keyboard fallback"), Driver.IsKeyboardFallback(TEXT("Input.LockOn")));
		AddInfo(Driver.DescribeKeys());
	}

	UInputMappingContext* Missing = CopyContext(Pad);
	Missing->UnmapKey(Action, EKeys::Gamepad_RightThumbstick);
	if (!Fixture.Configure(*this, Missing, Keyboard))
	{
		return false;
	}
	// 원래 pad IMC 가 켜져 있어서 그 context 를 물으면 빠진 설정 데이터를 거짓으로 구해 준다.
	TestTrue(TEXT("precondition: active original pad still contains R3"),
		Fixture.Input->QueryKeysMappedToAction(Action).Contains(EKeys::Gamepad_RightThumbstick));
	ExpectMissing(*this, Fixture, Action, TEXT("removed configured pad with stale active pad"));
	Fixture.Activate(Keyboard, ECommonInputType::MouseAndKeyboard);
	ExpectMissing(*this, Fixture, Action, TEXT("removed configured pad with active keyboard"));

	UInputMappingContext* AnalogOnly = CopyContext(Missing);
	AnalogOnly->MapKey(Action, EKeys::Gamepad_RightTriggerAxis);
	if (!Fixture.Configure(*this, AnalogOnly, Keyboard))
	{
		return false;
	}

	ExpectMissing(*this, Fixture, Action, TEXT("analog pad plus mouse button"));

	UInputMappingContext* KeyboardOnly = CopyContext(Keyboard);
	KeyboardOnly->UnmapKey(Action, EKeys::MiddleMouseButton);
	KeyboardOnly->MapKey(Action, EKeys::SpaceBar);
	if (!Fixture.Configure(*this, Missing, KeyboardOnly))
	{
		return false;
	}

	FVBPlayBotInputDriver KeyboardDriver;
	FString Error;
	TestTrue(TEXT("usable keyboard-only mapping still resolves"),
		KeyboardDriver.ResolveAction(Fixture.Player.Controller, TEXT("Input.LockOn"), Action, Error));
	TestEqual(TEXT("keyboard fallback key"), KeyboardDriver.GetKey(TEXT("Input.LockOn")).ToString(), EKeys::SpaceBar.ToString());
	TestTrue(TEXT("keyboard fallback route is explicit"), KeyboardDriver.IsKeyboardFallback(TEXT("Input.LockOn")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotInputDriverConfiguredAssetsTest,
	"VB.Unit.PlayBot.InputDriver.ConfiguredAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotInputDriverConfiguredAssetsTest::RunTest(const FString& Parameters)
{
	using namespace VBPlayBotInputDriverTest;
	UInputMappingContext* PadSource = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/VB/Input/Contexts/IMC_Gamepad.IMC_Gamepad"));
	UInputMappingContext* KeyboardSource = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/VB/Input/Contexts/IMC_KeyboardMouse.IMC_KeyboardMouse"));
	UInputAction* LockOn = LoadObject<UInputAction>(nullptr, TEXT("/Game/VB/Input/Actions/IA_LockOn.IA_LockOn"));
	if (!TestNotNull(TEXT("actual configured pad IMC"), PadSource)
		|| !TestNotNull(TEXT("actual configured keyboard IMC"), KeyboardSource)
		|| !TestNotNull(TEXT("actual lock action"), LockOn))
	{
		return false;
	}

	TArray<FKey> LockKeys;
	for (UInputMappingContext* Context : {PadSource, KeyboardSource})
	{
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			AddInfo(FString::Printf(TEXT("mapping_readback %s action=%s key=%s"), *Context->GetPathName(),
				*GetPathNameSafe(Mapping.Action), *Mapping.Key.ToString()));
			if (Context == PadSource && Mapping.Action == LockOn && Mapping.Key.IsGamepadKey() && !Mapping.Key.IsAnalog()
				&& Mapping.Key.IsValid())
			{
				LockKeys.AddUnique(Mapping.Key);
			}

			if (Context == KeyboardSource && Mapping.Action == LockOn)
			{
				TestTrue(TEXT("missing-pad negative has no usable keyboard-only alternative"), Mapping.Key.IsMouseButton()
					|| Mapping.Key.IsAnalog());
			}
		}
	}

	if (!TestEqual(TEXT("actual LockOn has one unambiguous pad button"), LockKeys.Num(), 1))
	{
		return false;
	}

	FMappingFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	UInputMappingContext* Pad = CopyContext(PadSource);
	UInputMappingContext* Keyboard = CopyContext(KeyboardSource);
	if (!Fixture.Configure(*this, Pad, Keyboard))
	{
		return false;
	}

	Fixture.Activate(Keyboard, ECommonInputType::MouseAndKeyboard);
	FVBPlayBotInputDriver Driver;
	FString Error;
	TestTrue(TEXT("actual LockOn resolves while keyboard context is active"),
		Driver.ResolveAction(Fixture.Player.Controller, TEXT("Input.LockOn"), LockOn, Error));
	TestEqual(TEXT("actual configured pad key is selected"), Driver.GetKey(TEXT("Input.LockOn")).ToString(), LockKeys[0].ToString());
	TestFalse(TEXT("actual LockOn does not silently use keyboard fallback"), Driver.IsKeyboardFallback(TEXT("Input.LockOn")));
	AddInfo(Driver.DescribeKeys());
	// 진단 overlay 는 이 fixture 안의 일 - 통과해도 게임 UI 가 이 키를 남긴다는 증거는 아니다.
	const UVBUISettings* UISettings = GetDefault<UVBUISettings>();
	UInputMappingContext* UI = UISettings->UIInputMapping.LoadSynchronous();
	if (!TestNotNull(TEXT("read-only UI context for priority diagnosis"), UI))
	{
		return false;
	}

	Fixture.Activate(PadSource, ECommonInputType::Gamepad);
	const bool bBeforeUI = Fixture.Input->QueryKeysMappedToAction(LockOn).Contains(LockKeys[0]);
	FModifyContextOptions PriorityOptions;
	PriorityOptions.bForceImmediately = true;
	Fixture.Input->AddMappingContext(UI, UISettings->UIInputMappingPriority, PriorityOptions);
	for (const FEnhancedActionKeyMapping& Mapping : UI->GetMappings())
	{
		AddInfo(FString::Printf(TEXT("ui_mapping_readback action=%s key=%s consume=%s"), *GetPathNameSafe(Mapping.Action),
			*Mapping.Key.ToString(), Mapping.Action && Mapping.Action->bConsumeInput ? TEXT("true") : TEXT("false")));
	}

	const bool bWithUI = Fixture.Input->QueryKeysMappedToAction(LockOn).Contains(LockKeys[0]);
	Fixture.Input->RemoveMappingContext(UI, PriorityOptions);
	const bool bAfterUI = Fixture.Input->QueryKeysMappedToAction(LockOn).Contains(LockKeys[0]);
	AddInfo(FString::Printf(TEXT("ui_priority_probe key=%s before=%s withUI=%s afterRemoval=%s"), *LockKeys[0].ToString(),
		bBeforeUI ? TEXT("true") : TEXT("false"), bWithUI ? TEXT("true") : TEXT("false"), bAfterUI ? TEXT("true") : TEXT("false")));
	TestTrue(TEXT("removing the transient diagnostic overlay restores the configured pad mapping"), bBeforeUI && bAfterUI);
	// 두 device context 의 실제 매핑을 잰다 - 복원은 판정, 가림은 진단 자료다.
	const auto KeyNames = [&Fixture](const UInputAction* Action)
	{
		TArray<FString> Names;
		for (const FKey& Key : Fixture.Input->QueryKeysMappedToAction(Action))
		{
			Names.AddUnique(Key.ToString());
		}

		Names.Sort(); return Names;
	};
	const auto KeysJson = [](const TArray<FString>& Names)
	{
		TArray<FString> Quoted;
		for (const FString& Name : Names)
		{
			Quoted.Add(TEXT("\"") + Name.ReplaceCharWithEscapedChar() + TEXT("\""));
		}

		return TEXT("[") + FString::Join(Quoted, TEXT(",")) + TEXT("]");
	};
	for (const ECommonInputType Type : {ECommonInputType::Gamepad, ECommonInputType::MouseAndKeyboard})
	{
		UInputMappingContext* Context = Type == ECommonInputType::Gamepad ? PadSource : KeyboardSource;
		Fixture.Activate(Context, Type);
		TArray<const UInputAction*> Actions;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action)
			{
				Actions.AddUnique(Mapping.Action.Get());
			}
		}

		TMap<const UInputAction*, TArray<FString>> Before, WithUI;
		for (const UInputAction* Action : Actions)
		{
			Before.Add(Action, KeyNames(Action));
		}

		Fixture.Input->AddMappingContext(UI, UISettings->UIInputMappingPriority, PriorityOptions);
		for (const UInputAction* Action : Actions)
		{
			WithUI.Add(Action, KeyNames(Action));
		}

		Fixture.Input->RemoveMappingContext(UI, PriorityOptions);
		for (const UInputAction* Action : Actions)
		{
			const TArray<FString> After = KeyNames(Action);
			TArray<FString> Dropped;
			for (const FString& Key : Before.FindChecked(Action))
			{
				if (!WithUI.FindChecked(Action).Contains(Key))
				{
					Dropped.Add(Key);
				}
			}

			const bool bRestored = Before.FindChecked(Action) == After;
			AddInfo(FString::Printf(TEXT("mapping_priority_matrix {\"device\":\"%s\",\"action\":\"%s\",\"uiPriority\":%d,\"before\":%s,\"withUI\":%s,\"dropped\":%s,\"afterRemoval\":%s,\"restored\":%s}"),
				Type == ECommonInputType::Gamepad ? TEXT("pad") : TEXT("keyboard"), *Action->GetPathName().ReplaceCharWithEscapedChar(), UISettings->UIInputMappingPriority,
				*KeysJson(Before.FindChecked(Action)), *KeysJson(WithUI.FindChecked(Action)), *KeysJson(Dropped), *KeysJson(After), bRestored ? TEXT("true") : TEXT("false")));
			TestTrue(TEXT("removing the isolated UI overlay restores each gameplay mapping"), bRestored);
		}
	}

	Pad->UnmapKey(LockOn, LockKeys[0]);
	// 고치지 않은 원본은 켜 두고, driver 는 매핑이 빠진 설정 사본을 따라야 한다.
	Fixture.Activate(PadSource, ECommonInputType::Gamepad);
	ExpectMissing(*this, Fixture, LockOn, TEXT("actual IMC copy with LockOn removed"));
	TestTrue(TEXT("the real asset was not modified"), PadSource->GetMappings().ContainsByPredicate([LockOn](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == LockOn && Mapping.Key.IsGamepadKey() && !Mapping.Key.IsAnalog(); }));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotNativeStickMappingTest,
	"VB.Unit.PlayBot.InputDriver.NativeStickMapping",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotNativeStickMappingTest::RunTest(const FString& Parameters)
{
	using namespace VBPlayBotInputDriverTest;
	FMappingFixture Fixture;
	if (!Fixture.Initialize(*this)) { return false; }
	UInputAction* Move = NewObject<UInputAction>();
	UInputAction* Look = NewObject<UInputAction>();
	UInputMappingContext* Pad = NewObject<UInputMappingContext>();
	UInputMappingContext* Keyboard = NewObject<UInputMappingContext>();
	Pad->MapKey(Move,EKeys::Gamepad_Left2D);
	Pad->MapKey(Look,EKeys::Gamepad_Right2D);
	if (!Fixture.Configure(*this,Pad,Keyboard)) { return false; }
	FVBPlayBotInputDriver Driver(EVBPlayBotClockMode::NativeSeconds);
	FString Error;
	TestTrue(TEXT("configured analog pair accepted"), Driver.ValidateStickActions(Fixture.Player.Controller,Move,Look,Error));
	TestFalse(TEXT("move/look cannot alias"), Driver.ValidateStickActions(Fixture.Player.Controller,Move,Move,Error));
	Pad->UnmapKey(Look,EKeys::Gamepad_Right2D);
	Pad->MapKey(Look,EKeys::Gamepad_RightX);
	TestFalse(TEXT("one look axis cannot stand in for a full stick"), Driver.ValidateStickActions(Fixture.Player.Controller,Move,Look,Error));
	Pad->MapKey(Look,EKeys::Gamepad_RightY);
	TestTrue(TEXT("explicit X/Y pair accepted"), Driver.ValidateStickActions(Fixture.Player.Controller,Move,Look,Error));
	Pad->UnmapKey(Move,EKeys::Gamepad_Left2D);
	Keyboard->MapKey(Move,EKeys::W);
	TestFalse(TEXT("keyboard cannot silently replace normal pad movement"), Driver.ValidateStickActions(Fixture.Player.Controller,Move,Look,Error));
	TestFalse(TEXT("missing controller fails"), Driver.ValidateStickActions(nullptr,Move,Look,Error));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotNativeGamepadTest,
	"VB.Unit.PlayBot.InputDriver.NativeGamepadRequired",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotNativeGamepadTest::RunTest(const FString& Parameters)
{
	using namespace VBPlayBotInputDriverTest;
	FMappingFixture Fixture; if (!Fixture.Initialize(*this)) { return false; }
	UInputAction* Action = NewObject<UInputAction>();
	UInputMappingContext* Pad = NewObject<UInputMappingContext>();
	UInputMappingContext* Keyboard = NewObject<UInputMappingContext>(); Keyboard->MapKey(Action,EKeys::SpaceBar);
	if (!Fixture.Configure(*this,Pad,Keyboard)) { return false; }
	FVBPlayBotInputDriver Driver(EVBPlayBotClockMode::NativeSeconds); FString Error;
	TestFalse(TEXT("native pad action rejects keyboard-only fallback"), Driver.ResolveGamepadAction(Fixture.Player.Controller,TEXT("Input.Test"),Action,Error));
	TestFalse(TEXT("rejected fallback leaves no usable cached action"), Driver.HasAction(TEXT("Input.Test")));
	TestTrue(TEXT("failure identifies the action"), Error.Contains(TEXT("Input.Test")));
	Pad->MapKey(Action,EKeys::Gamepad_FaceButton_Left);
	TestTrue(TEXT("actual configured pad action resolves"), Driver.ResolveGamepadAction(Fixture.Player.Controller,TEXT("Input.Test"),Action,Error));
	TestEqual(TEXT("resolved key is the configured pad button"), Driver.GetKey(TEXT("Input.Test")).ToString(),EKeys::Gamepad_FaceButton_Left.ToString());
	return true;
}

// 메뉴 확인/뒤로(CommonUI enhanced 액션)는 메뉴가 열릴 때만 붙는 UI 맵에서 패드 버튼으로 고른다.
//   게임 패드 맵에는 없어서 거기서 찾으면 실패해야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPlayBotInputDriverUIContextTest, "VB.Unit.PlayBot.InputDriver.UIContextKeys",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPlayBotInputDriverUIContextTest::RunTest(const FString& Parameters)
{
	const UInputMappingContext* UIContext = GetDefault<UVBUISettings>()->UIInputMapping.LoadSynchronous();
	const UCommonInputSettings& Settings = ICommonInputModule::GetSettings();
	const UInputAction* Accept = Settings.GetEnhancedInputClickAction();
	const UInputAction* Back = Settings.GetEnhancedInputBackAction();
	const UInputMappingContext* GamePad = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/VB/Input/Contexts/IMC_Gamepad.IMC_Gamepad"));
	if (!TestNotNull(TEXT("UIInputMapping is set"), UIContext) || !TestNotNull(TEXT("CommonUI accept action"), Accept)
		|| !TestNotNull(TEXT("CommonUI back action"), Back) || !TestNotNull(TEXT("gameplay pad context"), GamePad))
	{
		return false;
	}

	FVBPlayBotInputDriver Driver;
	FString Error;
	TestTrue(TEXT("accept resolves in the UI context"), Driver.ResolveActionInContext(TEXT("CommonUI.Accept"), Accept, UIContext, Error));
	TestTrue(TEXT("accept is a pad button"), Driver.GetKey(TEXT("CommonUI.Accept")).IsGamepadKey()
		&& !Driver.IsKeyboardFallback(TEXT("CommonUI.Accept")));
	TestTrue(TEXT("back resolves in the UI context"), Driver.ResolveActionInContext(TEXT("CommonUI.Back"), Back, UIContext, Error));
	TestTrue(TEXT("back is a pad button"), Driver.GetKey(TEXT("CommonUI.Back")).IsGamepadKey()
		&& !Driver.IsKeyboardFallback(TEXT("CommonUI.Back")));

	FVBPlayBotInputDriver GameOnly;
	TestFalse(TEXT("the gameplay pad context does not carry the UI accept action"),
		GameOnly.ResolveActionInContext(TEXT("CommonUI.Accept"), Accept, GamePad, Error));
	return true;
}
#endif
