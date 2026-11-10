// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "Input/CommonUIActionRouterBase.h"
#include "InputMappingContext.h"
#include "Misc/ScopeExit.h"
#include "UI/VBUIActionRouter.h"
#include "UI/VBUIManagerSubsystem.h"
#include "UI/VBUISettings.h"
#include "VBUITestFixture.h"

// UI mapping(확인, 뒤로, 탭)은 gameplay mapping 보다 우선하고 키를 먹으므로 메뉴가 입력을 가질 때만 건다
// Game 모드에서 켜져 있으면 패드 점프, 회피, 조준, 락온과 키보드 Escape 일시정지가 키를 잃는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIInputMappingPolicyTest, "VB.Unit.UI.InputMappingPolicy.Rule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBUIInputMappingPolicyTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Game mode leaves the gameplay keys to gameplay"), UVBUIManagerSubsystem::UsesUIInputMapping(ECommonInputMode::Game));
	TestTrue(TEXT("Menu mode has confirm / back"), UVBUIManagerSubsystem::UsesUIInputMapping(ECommonInputMode::Menu));
	TestTrue(TEXT("Game-and-menu mode has confirm / back"), UVBUIManagerSubsystem::UsesUIInputMapping(ECommonInputMode::All));
	return true;
}

// 실제 local player 의 실제 subsystem - 출시 UI mapping 이 router 의 입력 모드를 따른다
// fixture 에 game viewport 가 없어 router 와 Enhanced Input 이 경고를 남기지만 모드 자체는 바뀌고 알려진다 (터치 시험과 같은 기대 메시지)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIInputMappingFollowsModeTest, "VB.Unit.UI.InputMappingPolicy.FollowsMode",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBUIInputMappingFollowsModeTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("Failed to commit change"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, -1);
	AddExpectedMessagePlain(TEXT("Null player input"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, -1);
	VBUITest::FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	// Enhanced Input 은 player input 이 있는 controller 에만 mapping context 를 건다 - 없으면 모든 행이 "not applied" 라 아무것도 증명 못 한다
	Fixture.Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Fixture.Controller);
	UVBUIManagerSubsystem* UI = Fixture.LocalPlayer->GetSubsystem<UVBUIManagerSubsystem>();
	UCommonUIActionRouterBase* Router = Fixture.LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>();
	UEnhancedInputLocalPlayerSubsystem* Input = Fixture.LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	const UInputMappingContext* UIMapping = GetDefault<UVBUISettings>()->UIInputMapping.LoadSynchronous();
	if (!TestNotNull(TEXT("UI manager"), UI) || !TestNotNull(TEXT("router"), Router) || !TestNotNull(TEXT("enhanced input"), Input)
		|| !TestNotNull(TEXT("shipped UI mapping"), UIMapping))
	{
		return false;
	}

	// 실제 입구 - local player 가 controller 를 받아 manager 가 layout 을 만든다 (아직 입력을 가진 화면은 없다)
	UI->PlayerControllerChanged(Fixture.Controller);
	TestFalse(TEXT("before any screen takes input the UI mapping is off"), Input->HasMappingContext(UIMapping));

	Router->SetActiveUIInputConfig(FUIInputConfig(ECommonInputMode::Menu, EMouseCaptureMode::NoCapture));
	TestTrue(TEXT("menu mode applies the UI mapping"), Input->HasMappingContext(UIMapping));
	Router->SetActiveUIInputConfig(FUIInputConfig(ECommonInputMode::Game, EMouseCaptureMode::CapturePermanently));
	TestFalse(TEXT("game mode removes the UI mapping (gameplay keeps A / B / LB / RB / Escape)"), Input->HasMappingContext(UIMapping));
	Router->SetActiveUIInputConfig(FUIInputConfig(ECommonInputMode::All, EMouseCaptureMode::CapturePermanently));
	TestTrue(TEXT("game-and-menu mode applies it again"), Input->HasMappingContext(UIMapping));

	// controller 가 사라지면 layout 도, UI mapping 도 함께 사라진다
	UI->PlayerControllerChanged(nullptr);
	TestFalse(TEXT("no layout, no UI mapping"), Input->HasMappingContext(UIMapping));
	return true;
}
// 패드 A 는 CommonUI 가 마우스 클릭으로 바꾸는 accept 키다 - 프로젝트 router 가 이를 메뉴 입력으로 제한하므로
// 실제 local player 는 엔진 기본 클래스가 아니라 프로젝트 router 를 받아야 한다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIActionRouterTest, "VB.Unit.UI.InputMappingPolicy.Router",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBUIActionRouterTest::RunTest(const FString& Parameters)
{
	VBUITest::FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		return false;
	}

	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	const UCommonUIActionRouterBase* Router = Fixture.LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>();
	TestNotNull(TEXT("the local player gets the project router (pad A stays a game key in Game mode)"), Cast<UVBUIActionRouter>(Router));
	return true;
}
#endif
