// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CommonInputSubsystem.h"
#include "Components/CanvasPanel.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/IConsoleManager.h"
#include "Input/CommonUIActionRouterBase.h"
#include "Input/Events.h"
#include "Input/Reply.h"
#include "Input/UIActionBindingHandle.h"
#include "InputAction.h"
#include "Layout/Geometry.h"
#include "UI/Slots/VBUISlotSubsystem.h"
#include "UI/Slots/VBUISlotWidget.h"
#include "UI/VBHUDWidget.h"
#include "UI/VBTouchControls.h"
#include "VBUITestFixture.h"

// 터치 조작의 자기 바인딩 - 실제 WBP_TouchControls 를 실제 WBP_HUD 가 주인인 UI 슬롯 자리에 등록해 구성한다
// 못 보는 것: 실제 손가락, 스트리밍 입력, Slate 캡처 적용, 해제, 메뉴 화면 표시, CommonInput 의 실제 알림 방송
// (시험 LP 에는 뷰포트가 없어 엔진이 방송하지 않는다 - 구독 여부, 현재 값, 처리 함수는 본다)
class FVBTouchTestAccess
{
public:
	static const TArray<TObjectPtr<UVBTouchInputWidget>>& Controls(const UVBTouchControlsWidget& Touch) { return Touch.CreatedControls; }
	static ECommonInputType InputType(const UVBTouchControlsWidget& Touch) { return Touch.InputType; }
	static ECommonInputMode InputMode(const UVBTouchControlsWidget& Touch) { return Touch.InputMode; }
	static int32 RefreshCount(const UVBTouchControlsWidget& Touch) { return Touch.RefreshCount; }
	static const UInputAction* Action(const UVBTouchInputWidget& Control) { return Control.InputAction; }
	static bool HasActiveTouch(const UVBTouchInputWidget& Control) { return Control.HasActiveTouch(); }
	static const FVBTouchUIActionHandler& Handler(const UVBTouchButton& Button) { return Button.UIActionHandler; }
	static void SendInputType(UVBTouchControlsWidget& Touch, ECommonInputType Type) { Touch.HandleInputMethodChanged(Type); }
	static FReply TouchStart(UVBTouchInputWidget& Control, uint32 PointerIndex)
	{
		const FPointerEvent Event( /*UserIndex*/0, PointerIndex, FVector2D::ZeroVector, FVector2D::ZeroVector, /*Force*/1.0f, /*bPressLeftMouseButton*/true);
		return Control.NativeOnTouchStarted(FGeometry(), Event);
	}
};

namespace VBTouchTest
{
	using VBUITest::FRowFixture;
	using VBUITest::FSlateLife;

	const TCHAR* TouchClassPath = TEXT("/Game/VB/UI/Touch/WBP_TouchControls.WBP_TouchControls_C");
	const TCHAR* HUDClassPath = TEXT("/Game/VB/UI/WBP_HUD.WBP_HUD_C");

	// 콘솔 변수는 프로세스 공통이다 - 시험이 끝나면 원래 값으로 돌린다.
	struct FTouchModeScope
	{
		IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("vb.TouchControls"));
		int32 Original = Var ? Var->GetInt() : 0;
		void Set(int32 Value) const { if (Var) { Var->Set(Value, ECVF_SetByConsole); } }
		~FTouchModeScope() { Set(Original); }
	};

	// 실제 HUD 가 주인인 터치 자리 하나와 그 출처들.
	struct FTouchRig
	{
		FRowFixture Fixture;
		FSlateLife Life;
		UVBHUDWidget* HUD = nullptr;
		UVBUISlotWidget* SlotWidget = nullptr;
		UVBUISlotSubsystem* Registry = nullptr;
		UCommonInputSubsystem* CommonInput = nullptr;
		UCommonUIActionRouterBase* Router = nullptr;
		UEnhancedInputLocalPlayerSubsystem* EnhancedInput = nullptr;
		FVBUISlotHandle Handle;

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Fixture.Initialize(Test))
			{
				return false;
			}

			HUD = Fixture.MakeRow<UVBHUDWidget>(Test, HUDClassPath);
			Registry = UVBUISlotSubsystem::Get(Fixture.Controller);
			CommonInput = UCommonInputSubsystem::Get(Fixture.LocalPlayer);
			Router = Fixture.LocalPlayer->GetSubsystem<UCommonUIActionRouterBase>();
			EnhancedInput = Fixture.LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			SlotWidget = CreateWidget<UVBUISlotWidget>(Fixture.Controller, UVBUISlotWidget::StaticClass());
			if (!Test.TestNotNull(TEXT("HUD"), HUD) || !Test.TestNotNull(TEXT("slot registry"), Registry)
				|| !Test.TestNotNull(TEXT("CommonInput on the local player"),
					CommonInput) || !Test.TestNotNull(TEXT("action router on the local player"), Router)
				|| !Test.TestNotNull(TEXT("enhanced input on the local player"), EnhancedInput) || !Test.TestNotNull(TEXT("slot"),
					SlotWidget))
			{
				return false;
			}

			SlotWidget->SetSlot(VBGameplayTags::UI_Slot_HUD_TouchControls, HUD);
			Life.Construct(*SlotWidget);
			return true;
		}

		// HUD 와 같은 등록(범위 = HUD). 자리가 이미 구성돼 있어 등록 안에서 만들고 붙이고 구성한다.
		UVBTouchControlsWidget* Register(FAutomationTestBase& Test)
		{
			FVBUISlotRequest Request;
			Request.Slot = VBGameplayTags::UI_Slot_HUD_TouchControls;
			Request.WidgetClass = LoadClass<UUserWidget>(nullptr, TouchClassPath);
			Request.Scope = HUD;
			Request.Registrant = HUD;
			if (!Test.TestNotNull(TEXT("WBP_TouchControls class"), Request.WidgetClass.Get()))
			{
				return nullptr;
			}

			Handle = Registry->RegisterWidget(Request);
			const UCanvasPanel* Canvas = Cast<UCanvasPanel>(SlotWidget->GetRootWidget());
			UVBTouchControlsWidget* Touch = Canvas && Canvas->GetChildrenCount() > 0 ? Cast<UVBTouchControlsWidget>(Canvas->GetChildAt(0)) : nullptr;
			Test.TestNotNull(TEXT("slot created the touch controls"), Touch);
			return Touch;
		}

		// CommonInput 은 로컬 플레이어의 뷰포트로 월드를 얻어야 알린다(BroadcastInputMethodChanged 가 GetWorld 를 본다) - 시험 LP 에는 뷰포트가 없다.
		// 값은 실제로 바꾸고, 알림은 이 위젯이 그 이벤트에 묶여 있을 때만 엔진이 줄 값 그대로 넘긴다 (구독을 빼면 알림도 안 간다)
		void ChangeInputType(UVBTouchControlsWidget& Touch, ECommonInputType Type) const
		{
			CommonInput->SetCurrentInputType(Type);
			if (CommonInput->OnInputMethodChangedNative.IsBoundToObject(&Touch))
			{
				FVBTouchTestAccess::SendInputType(Touch, CommonInput->GetCurrentInputType());
			}
		}

		void SetMode(ECommonInputMode Mode) const
		{
			Router->SetActiveUIInputConfig(FUIInputConfig(Mode, EMouseCaptureMode::NoCapture));
		}

		void Shutdown()
		{
			if (SlotWidget)
			{
				Life.Destruct(*SlotWidget);
			}

			Handle.Unregister();
			Fixture.Shutdown();
		}
	};

	bool IsShown(const UUserWidget& Widget) { return Widget.GetVisibility() == ESlateVisibility::SelfHitTestInvisible; }
	bool IsHidden(const UUserWidget& Widget) { return Widget.GetVisibility() == ESlateVisibility::Collapsed; }

	UVBTouchButton* FindButton(const UVBTouchControlsWidget& Touch, const TCHAR* ActionName)
	{
		for (UVBTouchInputWidget* Control : FVBTouchTestAccess::Controls(Touch))
		{
			UVBTouchButton* Button = Cast<UVBTouchButton>(Control);
			const UInputAction* Action = Button ? FVBTouchTestAccess::Action(*Button) : nullptr;
			if (Action && Action->GetName() == ActionName)
			{
				return Button;
			}
		}

		return nullptr;
	}

	// 시험 LP 의 빈 자리가 내는 엔진 로그. 시험이 보는 값(모드, 알림, 처리기)은 그대로 바뀐다.
	// 뷰포트 없음 -> 라우터 입력 설정 적용 실패, PlayerInput 없음 -> Enhanced Input 모드 태그 실패,
	// 플랫폼 사용자 없음 -> HUD 의 CommonUI 화면 키 묶음 넷이 "invalid UserIndex" (키 경로는 이 시험 대상이 아니다)
	void IgnoreTestPlayerNoise(FAutomationTestBase& Test)
	{
		Test.AddExpectedMessagePlain(TEXT("Failed to commit change"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, -1);
		Test.AddExpectedMessagePlain(TEXT("Null player input"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, -1);
		Test.AddExpectedMessagePlain(TEXT("invalid UserIndex"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 4);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTouchVisibilityTest, "VB.Unit.Touch.Visibility",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTouchVisibilityTest::RunTest(const FString& Parameters)
{
	// 옛 HUD 진리표(현황 Inventory_3-1d2 2절)를 손으로 옮긴 기댓값. 메뉴가 맨 위면 강제 1 이어도 숨긴다.
	struct FRow { int32 Mode; ECommonInputMode InputMode; ECommonInputType Type; bool bShow; };
	const FRow Rows[] = {
		{0, ECommonInputMode::Game, ECommonInputType::Touch, true},
		{0, ECommonInputMode::Game, ECommonInputType::MouseAndKeyboard, false},
		{0, ECommonInputMode::Game, ECommonInputType::Gamepad, false},
		{0, ECommonInputMode::All, ECommonInputType::Touch, true},
		{0, ECommonInputMode::Menu, ECommonInputType::Touch, false},
		{1, ECommonInputMode::Game, ECommonInputType::MouseAndKeyboard, true},
		{1, ECommonInputMode::All, ECommonInputType::Gamepad, true},
		{1, ECommonInputMode::Menu, ECommonInputType::Touch, false},
		{1, ECommonInputMode::Menu, ECommonInputType::MouseAndKeyboard, false},
		{2, ECommonInputMode::Game, ECommonInputType::Touch, false},
		{2, ECommonInputMode::All, ECommonInputType::Touch, false},
		{2, ECommonInputMode::Menu, ECommonInputType::Touch, false},
	};
	for (const FRow& Row : Rows)
	{
		TestEqual(*FString::Printf(TEXT("mode %d, input mode %d, type %d"), Row.Mode, static_cast<int32>(Row.InputMode),
			static_cast<int32>(Row.Type)),
			UVBTouchControlsWidget::ShouldShow(Row.Mode, Row.InputMode, Row.Type), Row.bShow);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTouchSelfBindingTest, "VB.Unit.Touch.SelfBinding",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTouchSelfBindingTest::RunTest(const FString& Parameters)
{
	using namespace VBTouchTest;
	IgnoreTestPlayerNoise(*this);
	FTouchModeScope TouchMode;
	if (!TestNotNull(TEXT("vb.TouchControls"), TouchMode.Var))
	{
		return false;
	}

	FTouchRig Rig;
	if (!Rig.Initialize(*this))
	{
		Rig.Shutdown();
		return false;
	}

	// 붙기 전에 이미 Touch - 같은 종류는 다시 알려 오지 않으니 지금 값을 읽어야 뜬다(D5, 10.1).
	TouchMode.Set(0);
	Rig.CommonInput->SetCurrentInputType(ECommonInputType::Touch);
	TestEqual(TEXT("CommonInput already reports touch before construct"), Rig.CommonInput->GetCurrentInputType(), ECommonInputType::Touch);
	UVBTouchControlsWidget* Touch = Rig.Register(*this);
	if (!Touch)
	{
		Rig.Shutdown();
		return false;
	}

	TestTrue(TEXT("already touch at construct -> shown without any event"), IsShown(*Touch));
	TestTrue(TEXT("controls were created"), FVBTouchTestAccess::Controls(*Touch).Num() > 0);
	TestTrue(TEXT("touch controls find the HUD through their slot"), UVBUISlotWidget::FindSlotOwnerOf(*Touch) == Rig.HUD);

	// 사건 셋 - 틱 없이.
	TestTrue(TEXT("subscribed to CommonInput while constructed"), Rig.CommonInput->OnInputMethodChangedNative.IsBoundToObject(Touch));
	TestTrue(TEXT("subscribed to the router while constructed"), Rig.Router->OnActiveInputModeChanged().IsBoundToObject(Touch));
	Rig.ChangeInputType(*Touch, ECommonInputType::Gamepad);
	TestTrue(TEXT("gamepad -> hidden"), IsHidden(*Touch));
	TouchMode.Set(1);
	TestTrue(TEXT("console 1 while collapsed -> shown (no tick needed)"), IsShown(*Touch));
	Rig.SetMode(ECommonInputMode::Menu);
	TestTrue(TEXT("menu on top hides even with console 1"), IsHidden(*Touch));
	Rig.SetMode(ECommonInputMode::Game);
	TestTrue(TEXT("back to game -> shown"), IsShown(*Touch));
	TouchMode.Set(2);
	TestTrue(TEXT("console 2 -> hidden"), IsHidden(*Touch));

	// 같은 UObject 재구성: 해체 동안 출처가 바뀌어도 옛 값이 안 남고, 해체된 위젯은 출처에 안 묶인다.
	const TArray<TObjectPtr<UVBTouchInputWidget>> ControlsBefore = FVBTouchTestAccess::Controls(*Touch);
	TouchMode.Set(0);
	Rig.ChangeInputType(*Touch, ECommonInputType::Touch);
	TestTrue(TEXT("touch again -> shown"), IsShown(*Touch));
	Rig.Life.Destruct(*Rig.SlotWidget);
	TestFalse(TEXT("destruct unsubscribes from CommonInput"), Rig.CommonInput->OnInputMethodChangedNative.IsBoundToObject(Touch));
	TestFalse(TEXT("destruct unsubscribes from the router"), Rig.Router->OnActiveInputModeChanged().IsBoundToObject(Touch));
	// 해체가 출처 값을 비운다(MouseAndKeyboard, All). 묶여 있으면 아래 바뀜이 그 값을 덮는다.
	Rig.CommonInput->SetCurrentInputType(ECommonInputType::Gamepad);
	Rig.SetMode(ECommonInputMode::Menu);
	TestEqual(TEXT("CommonInput really moved to gamepad"), Rig.CommonInput->GetCurrentInputType(), ECommonInputType::Gamepad);
	const int32 RefreshWhileDestructed = FVBTouchTestAccess::RefreshCount(*Touch);
	TouchMode.Set(1);
	TestEqual(TEXT("destructed widget does not hear the console variable"), FVBTouchTestAccess::RefreshCount(*Touch),
		RefreshWhileDestructed);
	TestEqual(TEXT("destructed widget does not hear CommonInput"), FVBTouchTestAccess::InputType(*Touch),
		ECommonInputType::MouseAndKeyboard);
	TestEqual(TEXT("destructed widget does not hear the router"), FVBTouchTestAccess::InputMode(*Touch), ECommonInputMode::All);
	// 다시 구성할 때는 지금 값(Touch, Game)을 읽어야 뜬다 - 비운 값(MouseAndKeyboard)이 남으면 숨는다.
	Rig.CommonInput->SetCurrentInputType(ECommonInputType::Touch);
	Rig.SetMode(ECommonInputMode::Game);
	TouchMode.Set(0);
	Rig.Life.Construct(*Rig.SlotWidget);
	const UCanvasPanel* Canvas = Cast<UCanvasPanel>(Rig.SlotWidget->GetRootWidget());
	TestTrue(TEXT("reconstruct reuses the same touch controls object"), Canvas && Canvas->GetChildrenCount() == 1
		&& Canvas->GetChildAt(0) == Touch);
	TestTrue(TEXT("and the same control objects"), FVBTouchTestAccess::Controls(*Touch) == ControlsBefore);
	TestTrue(TEXT("source became touch while destructed -> shown after reconstruct"), IsShown(*Touch));
	const int32 RefreshBefore = FVBTouchTestAccess::RefreshCount(*Touch);
	TouchMode.Set(2);
	TestEqual(TEXT("one console change refreshes once after reconstruct (single subscription)"),
		FVBTouchTestAccess::RefreshCount(*Touch), RefreshBefore + 1);
	TestTrue(TEXT("and hides"), IsHidden(*Touch));

	Rig.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTouchStartsInMenuTest, "VB.Unit.Touch.StartsInMenu",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTouchStartsInMenuTest::RunTest(const FString& Parameters)
{
	// 붙기 전에 이미 Menu - 라우터의 지금 모드를 읽어야 강제 1 에서도 숨는다(D6, 10.1).
	using namespace VBTouchTest;
	IgnoreTestPlayerNoise(*this);
	FTouchModeScope TouchMode;
	FTouchRig Rig;
	if (!TouchMode.Var || !Rig.Initialize(*this))
	{
		Rig.Shutdown();
		return false;
	}

	TouchMode.Set(1);
	Rig.SetMode(ECommonInputMode::Menu);
	TestEqual(TEXT("router already reports menu before construct"), Rig.Router->GetActiveInputMode(), ECommonInputMode::Menu);
	UVBTouchControlsWidget* Touch = Rig.Register(*this);
	if (Touch)
	{
		TestTrue(TEXT("already menu at construct -> hidden without any event"), IsHidden(*Touch));
		Rig.SetMode(ECommonInputMode::Game);
		TestTrue(TEXT("game -> shown"), IsShown(*Touch));
	}

	Rig.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTouchReleaseTest, "VB.Unit.Touch.Release",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTouchReleaseTest::RunTest(const FString& Parameters)
{
	// 숨기는 네 경로마다 실제 연속 주입이 멈추는지(10.2).
	using namespace VBTouchTest;
	IgnoreTestPlayerNoise(*this);
	FTouchModeScope TouchMode;
	FTouchRig Rig;
	if (!TouchMode.Var || !Rig.Initialize(*this))
	{
		Rig.Shutdown();
		return false;
	}

	TouchMode.Set(1);
	UVBTouchControlsWidget* Touch = Rig.Register(*this);
	UVBTouchButton* Jump = Touch ? FindButton(*Touch, TEXT("IA_Jump")) : nullptr;
	const UInputAction* JumpAction = Jump ? FVBTouchTestAccess::Action(*Jump) : nullptr;
	if (!TestNotNull(TEXT("jump button"), Jump) || !TestNotNull(TEXT("jump action"), JumpAction))
	{
		Rig.Shutdown();
		return false;
	}

	auto Press = [&](const TCHAR* Path) -> bool
	{
		const FReply Reply = FVBTouchTestAccess::TouchStart(*Jump, 0);
		const bool bPressed = FVBTouchTestAccess::HasActiveTouch(*Jump) && Rig.EnhancedInput->HasContinuousInputInjectionForAction(JumpAction);
		TestTrue(*FString::Printf(TEXT("[%s] pressing a game button injects continuously"), Path), bPressed);
		TestTrue(*FString::Printf(TEXT("[%s] pressing captures the finger"), Path), Reply.GetMouseCaptor().IsValid());
		return bPressed;
	};
	auto ExpectReleased = [&](const TCHAR* Path)
	{
		TestFalse(*FString::Printf(TEXT("[%s] finger released"), Path), FVBTouchTestAccess::HasActiveTouch(*Jump));
		TestFalse(*FString::Printf(TEXT("[%s] continuous injection stopped"), Path),
			Rig.EnhancedInput->HasContinuousInputInjectionForAction(JumpAction));
	};

	// 1) 콘솔 변수로 숨김
	if (Press(TEXT("console")))
	{
		TouchMode.Set(2);
		ExpectReleased(TEXT("console"));
	}
	// 2) 입력 종류로 숨김(콘솔 변수 0 + 게임패드. 헤드리스에는 마우스가 안 붙어 CommonInput 이 MouseAndKeyboard 로 바꾸기를 거절한다)
	TouchMode.Set(0);
	Rig.ChangeInputType(*Touch, ECommonInputType::Touch);
	if (Press(TEXT("device")))
	{
		Rig.ChangeInputType(*Touch, ECommonInputType::Gamepad);
		ExpectReleased(TEXT("device"));
	}
	// 3) Menu
	TouchMode.Set(1);
	if (Press(TEXT("menu")))
	{
		Rig.SetMode(ECommonInputMode::Menu);
		ExpectReleased(TEXT("menu"));
	}

	Rig.SetMode(ECommonInputMode::Game);
	// 4) 해체
	if (Press(TEXT("destruct")))
	{
		Rig.Life.Destruct(*Rig.SlotWidget);
		ExpectReleased(TEXT("destruct"));
		Rig.Life.Construct(*Rig.SlotWidget);
	}

	Rig.Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTouchScreenButtonsTest, "VB.Unit.Touch.ScreenButtons",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTouchScreenButtonsTest::RunTest(const FString& Parameters)
{
	// 화면 버튼 넷이 HUD 의 실제 화면 액션 표로 가는지(10.3), 손가락 시작 중의 놓기(9절).
	using namespace VBTouchTest;
	IgnoreTestPlayerNoise(*this);
	FTouchModeScope TouchMode;
	FTouchRig Rig;
	if (!TouchMode.Var || !Rig.Initialize(*this))
	{
		Rig.Shutdown();
		return false;
	}

	TouchMode.Set(1);
	UVBTouchControlsWidget* Touch = Rig.Register(*this);
	if (!Touch)
	{
		Rig.Shutdown();
		return false;
	}

	// 참 경로는 PC 의 실제 화면 요청을 탄다. 시험 LP 에는 뿌리 레이아웃이 없어 UI 관리자가 요청마다 한 번 말한다 - 그 횟수로 요청이 갔음을 관측한다.
	//   넷(직접 처리기) + 대표 버튼 손가락 시작 하나 = 5.
	AddExpectedMessagePlain(TEXT("뿌리 레이아웃이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 5);
	const TCHAR* ScreenActions[] = { TEXT("IA_Pause"), TEXT("IA_Inventory"), TEXT("IA_Journal"), TEXT("IA_SkillTree") };
	for (const TCHAR* ActionName : ScreenActions)
	{
		UVBTouchButton* Button = FindButton(*Touch, ActionName);
		if (!TestNotNull(*FString::Printf(TEXT("%s button"), ActionName), Button))
		{
			continue;
		}

		const FVBTouchUIActionHandler& Handler = FVBTouchTestAccess::Handler(*Button);
		TestTrue(*FString::Printf(TEXT("%s button handler is bound"), ActionName), Handler.IsBound());
		TestTrue(*FString::Printf(TEXT("%s is consumed as a screen action"), ActionName), Handler.IsBound()
			&& Handler.Execute(FVBTouchTestAccess::Action(*Button)));
	}

	UVBTouchButton* Jump = FindButton(*Touch, TEXT("IA_Jump"));
	if (TestNotNull(TEXT("jump button"), Jump))
	{
		const FVBTouchUIActionHandler& Handler = FVBTouchTestAccess::Handler(*Jump);
		TestFalse(TEXT("a game action is not a screen action"), Handler.IsBound() && Handler.Execute(FVBTouchTestAccess::Action(*Jump)));
		TestFalse(TEXT("null is not a screen action"), Handler.IsBound() && Handler.Execute(nullptr));
	}

	// 대표 화면 버튼: 실제 손가락 시작 - 주입하지 않는다.
	UVBTouchButton* Pause = FindButton(*Touch, TEXT("IA_Pause"));
	const UInputAction* PauseAction = Pause ? FVBTouchTestAccess::Action(*Pause) : nullptr;
	if (Pause && PauseAction)
	{
		FVBTouchTestAccess::TouchStart(*Pause, 1);
		TestFalse(TEXT("pause button does not inject"), Rig.EnhancedInput->HasContinuousInputInjectionForAction(PauseAction));
		Pause->ReleaseTouch();

		// 9절: 처리기 안에서 묶음 전체 놓기가 끼어든다(같은 호출 안 Menu 전환). 놓은 손가락을 캡처하지 않고 주입도 안 한다.
		TWeakObjectPtr<UVBTouchControlsWidget> WeakTouch = Touch;
		Pause->SetUIActionHandler(FVBTouchUIActionHandler::CreateLambda([WeakTouch](const UInputAction*)
		{
			if (UVBTouchControlsWidget* Bundle = WeakTouch.Get())
			{
				Bundle->ReleaseAllTouches();
			}

			return true;
		}));
		const FReply Reply = FVBTouchTestAccess::TouchStart(*Pause, 2);
		TestFalse(TEXT("released during touch start -> no capture"), Reply.GetMouseCaptor().IsValid());
		TestFalse(TEXT("released during touch start -> no finger held"), FVBTouchTestAccess::HasActiveTouch(*Pause));

		// 처리기가 놓고 false 를 돌려줘도 주입하지 않는다 - 멈출 손가락이 없다.
		Pause->SetUIActionHandler(FVBTouchUIActionHandler::CreateLambda([WeakTouch](const UInputAction*)
		{
			if (UVBTouchControlsWidget* Bundle = WeakTouch.Get())
			{
				Bundle->ReleaseAllTouches();
			}

			return false;
		}));
		FVBTouchTestAccess::TouchStart(*Pause, 3);
		TestFalse(TEXT("released then not consumed -> no stuck injection"),
			Rig.EnhancedInput->HasContinuousInputInjectionForAction(PauseAction));
	}

	Rig.Shutdown();
	return true;
}
#endif
