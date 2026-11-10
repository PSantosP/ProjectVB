// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CommonInputSubsystem.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Input/VBInputUserSettings.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Misc/ScopeExit.h"
#include "PlayerMappableKeySettings.h"
#include "UI/VBInputKeyText.h"
#include "Unit/VBUITestFixture.h"
#include "UObject/UnrealType.h"

// 액션 조회와 사용자 키 줄 조회가 같은 이름을 내며, 선택한 장치는 현재 장치와 독립적이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputKeyTextTest, "VB.Unit.Input.KeyText",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInputKeyTextTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	ULocalPlayer* LocalPlayer = Fixture.LocalPlayer;
	UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(LocalPlayer);
	if (!TestNotNull(TEXT("enhanced input subsystem"), Input)
		|| !TestNotNull(TEXT("common input subsystem"), CommonInput))
	{
		return false;
	}

	// 디스크의 사용자 키 설정을 읽거나 쓰지 않는다. 이 플레이어의 시험용 입력과 키 프로필만 붙인다.
	Fixture.Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Fixture.Controller);
	UVBInputUserSettings* Settings = NewObject<UVBInputUserSettings>(LocalPlayer);
	FPlayerMappableKeyProfileCreationArgs ProfileArgs;
	ProfileArgs.ProfileType = UVBPlayerMappableKeyProfile::StaticClass();
	ProfileArgs.ProfileStringIdentifier = TEXT("VBInputKeyTextTest");
	ProfileArgs.bSetAsCurrentProfile = true;
	Settings->CreateNewKeyProfile(ProfileArgs);
	FObjectProperty* SettingsProperty = FindFProperty<FObjectProperty>(
		UEnhancedInputLocalPlayerSubsystem::StaticClass(), TEXT("UserSettings"));
	if (!TestNotNull(TEXT("user settings property"), SettingsProperty)
		|| !TestNotNull(TEXT("test key profile"), Settings->GetActiveKeyProfile()))
	{
		return false;
	}

	SettingsProperty->SetObjectPropertyValue_InContainer(Input, Settings);

	UInputAction* Action = NewObject<UInputAction>(GetTransientPackage());
	UPlayerMappableKeySettings* KeySettings = NewObject<UPlayerMappableKeySettings>(Action);
	const FName MappingName(TEXT("VBTest.KeyText"));
	KeySettings->Name = MappingName;
	KeySettings->DisplayName = FText::FromString(TEXT("Key text test"));
	FObjectProperty* ActionSettingsProperty = FindFProperty<FObjectProperty>(
		UInputAction::StaticClass(), TEXT("PlayerMappableKeySettings"));
	if (!TestNotNull(TEXT("action mapping property"), ActionSettingsProperty))
	{
		return false;
	}

	ActionSettingsProperty->SetObjectPropertyValue_InContainer(Action, KeySettings);

	UInputMappingContext* KeyboardIMC = NewObject<UInputMappingContext>(GetTransientPackage());
	UInputMappingContext* GamepadIMC = NewObject<UInputMappingContext>(GetTransientPackage());
	KeyboardIMC->MapKey(Action, EKeys::SpaceBar);
	GamepadIMC->MapKey(Action, EKeys::Gamepad_FaceButton_Bottom);
	Settings->RegisterInputMappingContext(KeyboardIMC);
	Settings->RegisterInputMappingContext(GamepadIMC);
	FModifyContextOptions ContextOptions;
	ContextOptions.bForceImmediately = true;
	Input->AddMappingContext(KeyboardIMC, 0, ContextOptions);

	CommonInput->SetCurrentInputType(ECommonInputType::MouseAndKeyboard);
	bool bUnmapped = false;
	const FText ActionKey = VBInputKeyText::ForAction(Fixture.Controller, Action, ECommonInputType::MouseAndKeyboard, bUnmapped);
	const FText KeyboardKey = VBInputKeyText::ForMapping(LocalPlayer, MappingName, EVBKeyDevice::KeyboardMouse);
	TestFalse(TEXT("active action key is mapped"), bUnmapped);
	TestEqual(TEXT("action and mapping name show the same key"), ActionKey.ToString(), KeyboardKey.ToString());
	TestEqual(TEXT("single key uses the same short name"), KeyboardKey.ToString(),
		VBInputKeyText::ForKey(EKeys::SpaceBar).ToString());
	TestEqual(TEXT("current keyboard device"), VBInputKeyText::ForCurrentDevice(LocalPlayer, MappingName).ToString(),
		KeyboardKey.ToString());

	const FText GamepadKey = VBInputKeyText::ForMapping(LocalPlayer, MappingName, EVBKeyDevice::Gamepad);
	TestEqual(TEXT("explicit gamepad key while current input is keyboard"), GamepadKey.ToString(),
		VBInputKeyText::ForKey(EKeys::Gamepad_FaceButton_Bottom).ToString());
	TestNotEqual(TEXT("device rows differ"), GamepadKey.ToString(), KeyboardKey.ToString());
	CommonInput->SetCurrentInputType(ECommonInputType::Gamepad);
	TestEqual(TEXT("current gamepad device"), VBInputKeyText::ForCurrentDevice(LocalPlayer, MappingName).ToString(),
		GamepadKey.ToString());
	CommonInput->SetCurrentInputType(ECommonInputType::Touch);
	TestTrue(TEXT("touch has no mapped key label"), VBInputKeyText::ForCurrentDevice(LocalPlayer, MappingName).IsEmpty());
	TestTrue(TEXT("touch action has no key label"),
		VBInputKeyText::ForAction(Fixture.Controller, Action, ECommonInputType::Touch, bUnmapped).IsEmpty());
	TestFalse(TEXT("touch is not reported as an unmapped action"), bUnmapped);

	// EqualTo 는 글자만 비교해 지역화 키가 바뀌어도 통과하므로 네임스페이스, 키를 직접 본다 (키가 그대로여야 번역이 따라온다).
	const FText Unmapped = VBInputKeyText::ForMapping(LocalPlayer, TEXT("VBTest.Missing"), EVBKeyDevice::KeyboardMouse);
	TestEqual(TEXT("unmapped text keeps the tutorial loc namespace"), FTextInspector::GetNamespace(Unmapped).Get(FString()),
		FString(TEXT("VBTutorialUI")));
	TestEqual(TEXT("unmapped text keeps the tutorial loc key"), FTextInspector::GetKey(Unmapped).Get(FString()), FString(TEXT("Unmapped")));
	TestTrue(TEXT("invalid key is blank"), VBInputKeyText::ForKey(FKey()).IsEmpty());
	TestTrue(TEXT("empty mapping name is blank"),
		VBInputKeyText::ForMapping(LocalPlayer, NAME_None, EVBKeyDevice::KeyboardMouse).IsEmpty());
	return true;
}
#endif
