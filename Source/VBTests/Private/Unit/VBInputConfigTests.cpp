// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Input/VBInputConfig.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputTriggers.h"
#include "UObject/Package.h"

// 입력 설정은 조회에 답하고 잘못된 행을 알리기만 한다
namespace VBInputConfigTest
{
	UInputAction* MakeAction(EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(GetTransientPackage());
		Action->ValueType = Type;
		return Action;
	}

	FVBInputAction Row(const UInputAction* Action, const FGameplayTag& Tag)
	{
		FVBInputAction Out;
		Out.InputAction = Action;
		Out.InputTag = Tag;
		return Out;
	}

	UVBInputConfig* MakeConfig(TArray<FVBInputAction> Native, TArray<FVBInputAction> Ability)
	{
		UVBInputConfig* Config = NewObject<UVBInputConfig>(GetTransientPackage());
		Config->NativeInputActions = MoveTemp(Native);
		Config->AbilityInputActions = MoveTemp(Ability);
		return Config;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputConfigFindTest, "VB.Unit.Input.Config.Find",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBInputConfigFindTest::RunTest(const FString& Parameters)
{
	using namespace VBInputConfigTest;
	namespace Tags = VBGameplayTags;
	UInputAction* Crouch = MakeAction(EInputActionValueType::Boolean);
	UInputAction* Dodge = MakeAction(EInputActionValueType::Boolean);
	const UVBInputConfig* Config = MakeConfig(
		{ Row(Crouch, Tags::Input_Crouch) },
		{ Row(Dodge, Tags::Input_Dodge), Row(nullptr, Tags::Input_Sprint) });

	TestTrue(TEXT("native row by its tag"), Config->FindNativeInputActionForTag(Tags::Input_Crouch) == Crouch);
	TestTrue(TEXT("ability row by its tag"), Config->FindAbilityInputActionForTag(Tags::Input_Dodge) == Dodge);
	TestNull(TEXT("ability tag is not looked up in the native list"), Config->FindNativeInputActionForTag(Tags::Input_Dodge));
	TestNull(TEXT("native tag is not looked up in the ability list"), Config->FindAbilityInputActionForTag(Tags::Input_Crouch));
	TestNull(TEXT("invalid tag"), Config->FindNativeInputActionForTag(FGameplayTag()));
	const FGameplayTag InputRoot = FGameplayTag::RequestGameplayTag(TEXT("Input"), false);
	// 부모 노드는 암묵적으로만 있다 (Input 태그 자체는 선언하지 않는다) - root 가 무효면 다음 행이 공짜로 통과한다
	if (!TestTrue(TEXT("fixture: the implicit Input parent tag resolves"), InputRoot.IsValid()))
	{
		return false;
	}

	TestNull(TEXT("parent tag does not match a child row"), Config->FindNativeInputActionForTag(InputRoot));
	TestNull(TEXT("a row without an action answers nothing"), Config->FindAbilityInputActionForTag(Tags::Input_Sprint));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputConfigValidationTest, "VB.Unit.Input.Config.Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBInputConfigValidationTest::RunTest(const FString& Parameters)
{
	using namespace VBInputConfigTest;
	namespace Tags = VBGameplayTags;
	const auto Axis2D = [] { return MakeAction(EInputActionValueType::Axis2D); };
	const auto Bool = [] { return MakeAction(EInputActionValueType::Boolean); };
	// 각 경우는 규칙 하나만 깨므로 답은 정확히 문제 하나다 - 깨끗한 기준이 fixture 자체의 문제를 세지 않음을 보인다
	struct FCase { const TCHAR* Name; UVBInputConfig* Config; int32 Expected; };
	UInputAction* Shared = Bool();
	const FCase Cases[] =
	{
		{ TEXT("clean"), MakeConfig({ Row(Axis2D(), Tags::Input_Move) }, { Row(Bool(), Tags::Input_Dodge) }), 0 },
		{ TEXT("summon may be an ability input"), MakeConfig({}, { Row(Bool(), Tags::Input_WeaponSummon) }), 0 },
		{ TEXT("empty action"), MakeConfig({}, { Row(nullptr, Tags::Input_Dodge) }), 1 },
		{ TEXT("tag outside Input"), MakeConfig({}, { Row(Bool(), Tags::State_Dead) }), 1 },
		{ TEXT("the bare Input tag"), MakeConfig({}, { Row(Bool(), FGameplayTag::RequestGameplayTag(TEXT("Input"), false)) }), 1 },
		{ TEXT("same action twice, one row with a bad tag"), MakeConfig({}, { Row(Shared, Tags::Input_Dodge), Row(Shared, Tags::State_Dead) }), 2 },
		{ TEXT("same tag in both lists"), MakeConfig({ Row(Bool(), Tags::Input_WeaponSummon) }, { Row(Bool(), Tags::Input_WeaponSummon) }), 1 },
		{ TEXT("same action twice"), MakeConfig({ Row(Shared, Tags::Input_Jump), Row(Shared, Tags::Input_Interact) }, {}), 1 },
		{ TEXT("native tag the hero does not handle"), MakeConfig({ Row(Bool(), Tags::Input_Dodge) }, {}), 1 },
		{ TEXT("hero input placed in the ability list"), MakeConfig({}, { Row(Bool(), Tags::Input_Jump) }), 1 },
		{ TEXT("native value type"), MakeConfig({ Row(Bool(), Tags::Input_Move) }, {}), 1 },
		{ TEXT("ability value type"), MakeConfig({}, { Row(Axis2D(), Tags::Input_Dodge) }), 1 },
	};
	for (const FCase& Case : Cases)
	{
		const TArray<FText> Problems = Case.Config->FindProblems();
		TestEqual(FString::Printf(TEXT("%s: problem count"), Case.Name), Problems.Num(), Case.Expected);
		for (const FText& Problem : Problems)
		{
			AddInfo(FString::Printf(TEXT("%s: %s"), Case.Name, *Problem.ToString()));
		}
	}

	// 계약 태그는 모두 유효하고 서로 다른 Input.* 태그다 - 이 표가 validator 와 hero 가 같이 읽는 유일한 목록이다
	TSet<FGameplayTag> Seen;
	for (const FVBNativeInputContract& Contract : UVBInputConfig::GetNativeInputContract())
	{
		TestTrue(FString::Printf(TEXT("contract tag %s is valid"), *Contract.InputTag.ToString()), Contract.InputTag.IsValid());
		bool bAlready = false;
		Seen.Add(Contract.InputTag, &bAlready);
		TestFalse(FString::Printf(TEXT("contract tag %s appears once"), *Contract.InputTag.ToString()), bAlready);
	}

	TestEqual(TEXT("contract size (14 hero inputs + summon)"), Seen.Num(), 15);
	return true;
}

// 공격 홀드는 행동과 매핑 모두 한 프레임짜리 Pressed 트리거를 쓰지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttackHoldMappingTest, "VB.Unit.Input.Config.AttackHold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAttackHoldMappingTest::RunTest(const FString& Parameters)
{
	const UInputAction* Action = LoadObject<UInputAction>(nullptr,
		TEXT("/Game/VB/Input/Actions/IA_Attack_Light.IA_Attack_Light"));
	if (!TestNotNull(TEXT("attack action"), Action))
	{
		return false;
	}

	for (const UInputTrigger* Trigger : Action->Triggers)
	{
		TestFalse(TEXT("attack action preserves the held lifetime"), Trigger && Trigger->IsA<UInputTriggerPressed>());
	}

	const TCHAR* ContextPaths[] = {
		TEXT("/Game/VB/Input/Contexts/IMC_KeyboardMouse.IMC_KeyboardMouse"),
		TEXT("/Game/VB/Input/Contexts/IMC_Gamepad.IMC_Gamepad")
	};
	for (const TCHAR* Path : ContextPaths)
	{
		const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, Path);
		if (!TestNotNull(TEXT("authored mapping context"), Context))
		{
			return false;
		}

		int32 Count = 0;
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			if (Mapping.Action != Action)
			{
				continue;
			}

			++Count;
			for (const UInputTrigger* Trigger : Mapping.Triggers)
			{
				TestFalse(FString::Printf(TEXT("%s attack mapping keeps the hold; old Pressed mapping fails"), *Context->GetName()),
					Trigger && Trigger->IsA<UInputTriggerPressed>());
			}
		}

		TestTrue(FString::Printf(TEXT("%s maps the attack"), *Context->GetName()), Count > 0);
	}

	return true;
}
#endif
