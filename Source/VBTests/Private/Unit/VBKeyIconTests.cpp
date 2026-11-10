// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "CommonInputBaseTypes.h"
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

// 실제 대응표 자산이 플랫폼 설정에 걸린 상태의 조회. 자산/설정 누락은 시험 실패다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBKeyIconTest, "VB.Unit.Input.KeyIcon",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBKeyIconTest::RunTest(const FString& Parameters)
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

	const auto KeyboardData = Platform->GetControllerDataForInputType(ECommonInputType::MouseAndKeyboard, NAME_None);
	const auto XSXData = Platform->GetControllerDataForInputType(ECommonInputType::Gamepad, TEXT("XSX"));
	const auto PS5Data = Platform->GetControllerDataForInputType(ECommonInputType::Gamepad, TEXT("PS5"));
	if (!TestTrue(TEXT("real keyboard controller data configured"), !KeyboardData.IsEmpty())
		|| !TestTrue(TEXT("real XSX controller data configured"), !XSXData.IsEmpty())
		|| !TestTrue(TEXT("real PS5 controller data configured"), !PS5Data.IsEmpty()))
	{
		return false;
	}

	Fixture.Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Fixture.Controller);
	UVBInputUserSettings* Settings = NewObject<UVBInputUserSettings>(LP);
	FPlayerMappableKeyProfileCreationArgs ProfileArgs;
	ProfileArgs.ProfileType = UVBPlayerMappableKeyProfile::StaticClass();
	ProfileArgs.ProfileStringIdentifier = TEXT("VBKeyIconTest");
	ProfileArgs.bSetAsCurrentProfile = true;
	Settings->CreateNewKeyProfile(ProfileArgs);
	FObjectProperty* UserSettingsProperty = FindFProperty<FObjectProperty>(
		UEnhancedInputLocalPlayerSubsystem::StaticClass(), TEXT("UserSettings"));
	if (!TestNotNull(TEXT("UserSettings property"), UserSettingsProperty)
		|| !TestNotNull(TEXT("active key profile"), Settings->GetActiveKeyProfile()))
	{
		return false;
	}

	UserSettingsProperty->SetObjectPropertyValue_InContainer(Input, Settings);

	UInputAction* Action = NewObject<UInputAction>(GetTransientPackage());
	UPlayerMappableKeySettings* KeySettings = NewObject<UPlayerMappableKeySettings>(Action);
	const FName MappingName(TEXT("VBTest.KeyIcon"));
	KeySettings->Name = MappingName;
	FObjectProperty* ActionSettingsProperty = FindFProperty<FObjectProperty>(
		UInputAction::StaticClass(), TEXT("PlayerMappableKeySettings"));
	if (!TestNotNull(TEXT("action mapping property"), ActionSettingsProperty))
	{
		return false;
	}

	ActionSettingsProperty->SetObjectPropertyValue_InContainer(Action, KeySettings);
	UInputMappingContext* KeyboardIMC = NewObject<UInputMappingContext>(GetTransientPackage());
	UInputMappingContext* GamepadIMC = NewObject<UInputMappingContext>(GetTransientPackage());
	KeyboardIMC->MapKey(Action, EKeys::LeftMouseButton);
	GamepadIMC->MapKey(Action, EKeys::Gamepad_FaceButton_Bottom);
	Settings->RegisterInputMappingContext(KeyboardIMC);
	Settings->RegisterInputMappingContext(GamepadIMC);
	FModifyContextOptions Options;
	Options.bForceImmediately = true;
	Input->AddMappingContext(KeyboardIMC, 0, Options);
	Input->AddMappingContext(GamepadIMC, 0, Options);

	const auto CheckIcon = [this, Common, LP, Action, MappingName, &Fixture](ECommonInputType Type, FName PadName,
		const TCHAR* Label) -> UObject*
	{
		Common->SetCurrentInputType(Type);
		if (Type == ECommonInputType::Gamepad)
		{
			Common->SetGamepadInputType(PadName);
		}

		if (!TestEqual(*FString::Printf(TEXT("%s selected device"), Label), Common->GetCurrentInputType(), Type))
		{
			return nullptr;
		}

		if (Type == ECommonInputType::Gamepad && !TestEqual(*FString::Printf(TEXT("%s selected pad"), Label),
			Common->GetCurrentGamepadName(), PadName))
		{
			return nullptr;
		}

		const TOptional<FSlateBrush> ByAction = VBInputKeyText::IconForAction(Fixture.Controller, Action);
		const TOptional<FSlateBrush> ByMapping = VBInputKeyText::IconForMapping(LP, MappingName);
		if (!TestTrue(*FString::Printf(TEXT("%s action icon"), Label), ByAction.IsSet())
			|| !TestTrue(*FString::Printf(TEXT("%s mapping icon"), Label), ByMapping.IsSet()))
		{
			return nullptr;
		}

		UObject* ActionImage = ByAction->GetResourceObject();
		UObject* MappingImage = ByMapping->GetResourceObject();
		TestNotNull(*FString::Printf(TEXT("%s action image"), Label), ActionImage);
		TestTrue(*FString::Printf(TEXT("%s same image"), Label), ActionImage == MappingImage);
		return ActionImage;
	};
	UObject* KeyboardImage = CheckIcon(ECommonInputType::MouseAndKeyboard, NAME_None, TEXT("keyboard"));
	UObject* XSXImage = CheckIcon(ECommonInputType::Gamepad, TEXT("XSX"), TEXT("XSX"));
	UObject* PS5Image = CheckIcon(ECommonInputType::Gamepad, TEXT("PS5"), TEXT("PS5"));
	TestTrue(TEXT("XSX and PS5 use different textures"), XSXImage && PS5Image && XSXImage != PS5Image);
	if (!KeyboardImage || !XSXImage || !PS5Image)
	{
		return false;
	}

	Common->SetCurrentInputType(ECommonInputType::Touch);
	TestFalse(TEXT("touch action has no icon"), VBInputKeyText::IconForAction(Fixture.Controller, Action).IsSet());
	TestFalse(TEXT("touch mapping has no icon"), VBInputKeyText::IconForMapping(LP, MappingName).IsSet());
	Common->SetCurrentInputType(ECommonInputType::MouseAndKeyboard);
	TestFalse(TEXT("missing mapping has no icon"), VBInputKeyText::IconForMapping(LP, TEXT("VBTest.Absent")).IsSet());
	TestFalse(TEXT("null controller has no icon"), VBInputKeyText::IconForAction(nullptr, Action).IsSet());
	TestFalse(TEXT("null action has no icon"), VBInputKeyText::IconForAction(Fixture.Controller, nullptr).IsSet());
	// 키는 매핑되어 있지만 그림 표에는 없는 경우도 글자로 돌아간다.
	UInputAction* NoIconAction = NewObject<UInputAction>(GetTransientPackage());
	UPlayerMappableKeySettings* NoIconSettings = NewObject<UPlayerMappableKeySettings>(NoIconAction);
	const FName NoIconName(TEXT("VBTest.NoIcon"));
	NoIconSettings->Name = NoIconName;
	ActionSettingsProperty->SetObjectPropertyValue_InContainer(NoIconAction, NoIconSettings);
	UInputMappingContext* NoIconIMC = NewObject<UInputMappingContext>(GetTransientPackage());
	NoIconIMC->MapKey(NoIconAction, EKeys::F11);
	Settings->RegisterInputMappingContext(NoIconIMC);
	Input->AddMappingContext(NoIconIMC, 0, Options);
	TestFalse(TEXT("unpictured mapped action has no icon"), VBInputKeyText::IconForAction(Fixture.Controller, NoIconAction).IsSet());
	TestFalse(TEXT("unpictured mapped row has no icon"), VBInputKeyText::IconForMapping(LP, NoIconName).IsSet());

	// 엔진 조회는 빈 그림 행도 성공이라 한다. CDO에 일시적으로 빈 행을 넣고 복구해 실제 래퍼의 실패 처리를 본다.
	UCommonInputBaseControllerData* KeyboardCDO = const_cast<UCommonInputBaseControllerData*>(KeyboardData[0]);
	const TArray<FCommonInputKeyBrushConfiguration> OriginalRows = KeyboardCDO->InputBrushDataMap;
	ON_SCOPE_EXIT { KeyboardCDO->InputBrushDataMap = OriginalRows; };
	FCommonInputKeyBrushConfiguration Empty;
	Empty.Key = EKeys::F12;
	KeyboardCDO->InputBrushDataMap.Add(Empty);
	UInputAction* EmptyAction = NewObject<UInputAction>(GetTransientPackage());
	UPlayerMappableKeySettings* EmptySettings = NewObject<UPlayerMappableKeySettings>(EmptyAction);
	const FName EmptyName(TEXT("VBTest.EmptyBrush"));
	EmptySettings->Name = EmptyName;
	ActionSettingsProperty->SetObjectPropertyValue_InContainer(EmptyAction, EmptySettings);
	UInputMappingContext* EmptyIMC = NewObject<UInputMappingContext>(GetTransientPackage());
	EmptyIMC->MapKey(EmptyAction, EKeys::F12);
	Settings->RegisterInputMappingContext(EmptyIMC);
	Input->AddMappingContext(EmptyIMC, 0, Options);
	FSlateBrush RawBrush;
	TestTrue(TEXT("empty brush row is found by engine"), Platform->TryGetInputBrush(RawBrush,
		EKeys::F12, ECommonInputType::MouseAndKeyboard, NAME_None));
	TestNull(TEXT("empty brush has no resource"), RawBrush.GetResourceObject());
	TestFalse(TEXT("empty brush action falls back to text"), VBInputKeyText::IconForAction(Fixture.Controller, EmptyAction).IsSet());
	TestFalse(TEXT("empty brush mapping falls back to text"), VBInputKeyText::IconForMapping(LP, EmptyName).IsSet());
	return true;
}
#endif
