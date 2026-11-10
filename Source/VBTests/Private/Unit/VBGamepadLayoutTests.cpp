// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "PlayerMappableKeySettings.h"
#include "UObject/Package.h"

// 패드 배치: 버튼 하나에 행동 하나. L3 질주(0.2초 길게), R3 앉기, B 회피, View 인벤토리.
//   걷기는 스틱 기울기, 무기 넣기는 들고 있는 무기의 D-pad 를 다시 누른다(UVBHeroComponent::HandleWeaponSlot).
//   한 버튼에 둘이 걸리면 둘 다 발동한다(L3 에 앉기가 겹치면 사다리에서 손을 놓는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGamepadLayoutTest, "VB.Unit.Input.GamepadLayout",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGamepadLayoutTest::RunTest(const FString& Parameters)
{
	const UInputMappingContext* Pad = LoadObject<UInputMappingContext>(nullptr,
		TEXT("/Game/VB/Input/Contexts/IMC_Gamepad.IMC_Gamepad"));
	if (!TestNotNull(TEXT("IMC_Gamepad"), Pad))
	{
		return false;
	}

	TMap<FKey, TArray<FString>> ActionsByKey;
	const FEnhancedActionKeyMapping* SprintRow = nullptr;
	for (const FEnhancedActionKeyMapping& Mapping : Pad->GetMappings())
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s row has an action"), *Mapping.Key.ToString()), Mapping.Action.Get()))
		{
			continue;
		}

		ActionsByKey.FindOrAdd(Mapping.Key).AddUnique(Mapping.Action->GetName());
		if (Mapping.Action->GetName() == TEXT("IA_Sprint"))
		{
			SprintRow = &Mapping;
		}
	}

	for (const TPair<FKey, TArray<FString>>& Pair : ActionsByKey)
	{
		TestEqual(*FString::Printf(TEXT("%s drives one action (%s)"), *Pair.Key.ToString(), *FString::Join(Pair.Value, TEXT(", "))),
			Pair.Value.Num(), 1);
	}

	const auto Drives = [&ActionsByKey](const FKey& Key, const TCHAR* Action)
	{
		const TArray<FString>* Actions = ActionsByKey.Find(Key);
		return Actions && Actions->Contains(Action);
	};
	TestTrue(TEXT("L3 sprints"), Drives(EKeys::Gamepad_LeftThumbstick, TEXT("IA_Sprint")));
	TestTrue(TEXT("R3 crouches"), Drives(EKeys::Gamepad_RightThumbstick, TEXT("IA_Crouch")));
	TestTrue(TEXT("B dodges"), Drives(EKeys::Gamepad_FaceButton_Right, TEXT("IA_Dodge")));
	TestTrue(TEXT("View opens the inventory"), Drives(EKeys::Gamepad_Special_Left, TEXT("IA_Inventory")));
	for (const TCHAR* OffPad : { TEXT("IA_Walk"), TEXT("IA_Ragdoll"), TEXT("IA_WeaponSummon") })
	{
		for (const TPair<FKey, TArray<FString>>& Pair : ActionsByKey)
		{
			TestFalse(*FString::Printf(TEXT("%s is not on the pad (%s)"), OffPad, *Pair.Key.ToString()), Pair.Value.Contains(OffPad));
		}
	}

	// 질주는 0.2초 길게 누를 때 - 답 40 은 이 문턱을 그대로 두기로 했다.
	const UInputTriggerHold* Hold = nullptr;
	if (SprintRow)
	{
		for (const TObjectPtr<UInputTrigger>& Trigger : SprintRow->Triggers)
		{
			if (const UInputTriggerHold* AsHold = Cast<UInputTriggerHold>(Trigger.Get()))
			{
				Hold = AsHold;
			}
		}
	}

	if (TestNotNull(TEXT("sprint row holds"), Hold))
	{
		TestEqual(TEXT("sprint hold threshold"), Hold->HoldTimeThreshold, 0.2f, 1e-4f);
	}

	return true;
}
// 옵션 화면의 한 줄은 이름 하나다(UVBInputUserSettings 가 이름으로 장치별 칸을 모은다). 그래서 이름 하나는 행동 하나에만 붙는다 -
//   다른 행동 둘이 이름을 같이 쓰면 키보드 줄과 패드 줄이 서로 다른 행동인데 한 줄로 보이고, 하나를 바꾸면 다른 하나가 바뀐다.
//   대조: 일부러 겹친 시험용 맥락 둘에서 겹침을 찾아야 한다(못 찾으면 이 검사가 아무것도 못 막는다).
namespace VBMappingNameTestUtil
{
	TArray<FString> FindNameClashes(const TArray<const UInputMappingContext*>& Contexts)
	{
		TMap<FName, const UInputAction*> ActionByName;
		TArray<FString> Clashes;
		for (const UInputMappingContext* Context : Contexts)
		{
			for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
			{
				if (!Mapping.IsPlayerMappable() || !Mapping.Action)
				{
					continue;
				}

				const FName Name = Mapping.GetMappingName();
				const UInputAction*& Owner = ActionByName.FindOrAdd(Name, Mapping.Action.Get());
				if (Owner != Mapping.Action.Get())
				{
					Clashes.Add(FString::Printf(TEXT("%s: %s / %s (%s)"), *Name.ToString(), *Owner->GetName(),
						*Mapping.Action->GetName(), *Context->GetName()));
				}
			}
		}

		return Clashes;
	}

	UInputAction* MakeNamedAction(const TCHAR* MappingName)
	{
		UInputAction* Action = NewObject<UInputAction>(GetTransientPackage());
		UPlayerMappableKeySettings* KeySettings = NewObject<UPlayerMappableKeySettings>(Action);
		KeySettings->Name = FName(MappingName);
		FObjectProperty* Property = FindFProperty<FObjectProperty>(UInputAction::StaticClass(), TEXT("PlayerMappableKeySettings"));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Action, KeySettings);
		return Action;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputMappingNameTest, "VB.Unit.Input.MappingNames",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInputMappingNameTest::RunTest(const FString& Parameters)
{
	using namespace VBMappingNameTestUtil;
	UInputMappingContext* ClashKeyboard = NewObject<UInputMappingContext>(GetTransientPackage());
	ClashKeyboard->MapKey(MakeNamedAction(TEXT("Test.Shared")), EKeys::G);
	UInputMappingContext* ClashPad = NewObject<UInputMappingContext>(GetTransientPackage());
	ClashPad->MapKey(MakeNamedAction(TEXT("Test.Shared")), EKeys::Gamepad_FaceButton_Top);
	TestEqual(TEXT("control: two actions under one name are found"), FindNameClashes({ ClashKeyboard, ClashPad }).Num(), 1);

	TArray<const UInputMappingContext*> Shipped;
	for (const TCHAR* Path : { TEXT("/Game/VB/Input/Contexts/IMC_KeyboardMouse.IMC_KeyboardMouse"), TEXT("/Game/VB/Input/Contexts/IMC_Gamepad.IMC_Gamepad") })
	{
		const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, Path);
		if (!TestNotNull(Path, Context))
		{
			return false;
		}

		Shipped.Add(Context);
	}

	const TArray<FString> Clashes = FindNameClashes(Shipped);
	TestEqual(*FString::Printf(TEXT("shipped contexts: one action per mapping name (%s)"), *FString::Join(Clashes, TEXT("; "))),
		Clashes.Num(), 0);
	return true;
}
#endif
