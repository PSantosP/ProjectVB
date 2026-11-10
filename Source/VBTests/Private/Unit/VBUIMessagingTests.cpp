// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CommonActivatableWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "HAL/PlatformTime.h"
#include "UI/VBConfirmationDialogWidget.h"
#include "UI/VBPrimaryLayout.h"
#include "UI/VBUIMessagingSubsystem.h"
#include "UI/VBUIManagerSubsystem.h"
#include "UI/VBUISettings.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"
#include "VBUITestFixture.h"
#include "Widgets/CommonActivatableWidgetContainer.h"

// 확인 요청(UVBUIMessagingSubsystem)과 확인 대화상자 WBP 시험. 디스크, 설정 쓰기, 뷰포트, 전역 ticker 펌프, GC 없이 돈다
// 서브시스템 시험은 공개 요청, 결과만 쓰고, 위젯 시험은 실제 WBP, 스택, Slate 를 쓴다. 경로나 WBP 가 없으면 건너뛰지 않고 실패한다
// 실제 키보드, 게임패드 포커스와 뷰포트 렌더링은 이 시험이 보지 않는다 (PIE 몫)

class FVBUIMessagingTestAccess
{
public:
	static void AwaitPush(UVBUIMessagingSubsystem& Owner)
	{
		Owner.DialogState = UVBUIMessagingSubsystem::EDialogState::Requested;
	}

	static bool HasScheduledPresent(const UVBUIMessagingSubsystem& Owner)
	{
		return Owner.PresentTicker.IsValid();
	}

	static bool RunOwnedPresentTick(UVBUIMessagingSubsystem& Owner)
	{
		if (!Owner.PresentTicker.IsValid())
		{
			return false;
		}

		FTSTicker::GetCoreTicker().RemoveTicker(Owner.PresentTicker);
		// 제품 콜백은 Present 를 돌리기 전에 자기 핸들을 비운다.
		return !Owner.HandlePresentTick(0.0f);
	}

	static bool InstallLayout(UVBUIManagerSubsystem& UI, UVBPrimaryLayout* Layout)
	{
		FObjectProperty* Property = FindFProperty<FObjectProperty>(UI.GetClass(), TEXT("Layout"));
		if (!Property)
		{
			return false;
		}

		Property->SetObjectPropertyValue_InContainer(&UI, Layout);
		return UI.GetLayout() == Layout;
	}

	static FVBConfirmationHandle Shown(const UVBConfirmationDialogWidget& Widget) { return Widget.ShownHandle; }
	static UButton* Confirm(const UVBConfirmationDialogWidget& Widget) { return Widget.BtnConfirm; }
	static UButton* Decline(const UVBConfirmationDialogWidget& Widget) { return Widget.BtnDecline; }
	static UTextBlock* Body(const UVBConfirmationDialogWidget& Widget) { return Widget.TxtBody; }
	static UWidget* DesiredFocus(const UVBConfirmationDialogWidget& Widget) { return Widget.NativeGetDesiredFocusTarget(); }
	static double ShownAt(const UVBConfirmationDialogWidget& Widget) { return Widget.ShownAtSeconds; }
	static float GuardSeconds(const UVBConfirmationDialogWidget& Widget) { return Widget.NewRequestInputGuardSeconds; }
	static void SetRequestAge(UVBConfirmationDialogWidget& Widget, double AgeSeconds)
	{
		Widget.ShownAtSeconds = FPlatformTime::Seconds() - AgeSeconds;
	}

	static bool Back(UVBConfirmationDialogWidget& Widget) { return Widget.NativeOnHandleBackAction(); }
	static bool HasPushInputReason(const UVBUIManagerSubsystem& UI, FGameplayTag Tag)
	{
		return UI.SuspendedInputReasons.Contains(UI.MakePushReason(Tag));
	}
};

namespace VBUIMessagingTests
{
	struct FConfirmationFixture
	{
		VBUITest::FRowFixture Row;
		UVBUIMessagingSubsystem* Owner = nullptr;
		UVBUIManagerSubsystem* UI = nullptr;
		UVBPrimaryLayout* Layout = nullptr;
		VBUITest::FSlateLife LayoutLife;
		bool bInjectedLayout = false;
		TStrongObjectPtr<UGameInstance> WidgetGameInstance;
		TStrongObjectPtr<UGameViewportClient> WidgetViewport;

		bool InstallLocalPlayerIndex(FAutomationTestBase& Test)
		{
			// CommonUI 는 UserIndex 를 LP.ViewportClient -> GI.LocalPlayers 로 푼다 - 그 연결만 주고 GI, 뷰포트, Slate user 는 초기화하지 않는다.
			WidgetGameInstance.Reset(NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient));
			WidgetViewport.Reset(NewObject<UGameViewportClient>(GEngine, NAME_None, RF_Transient));
			FObjectProperty* GIProperty = FindFProperty<FObjectProperty>(UGameViewportClient::StaticClass(), TEXT("GameInstance"));
			FObjectProperty* WorldProperty = FindFProperty<FObjectProperty>(UGameViewportClient::StaticClass(), TEXT("World"));
			FArrayProperty* PlayersProperty = FindFProperty<FArrayProperty>(UGameInstance::StaticClass(), TEXT("LocalPlayers"));
			if (!GIProperty || !WorldProperty || !PlayersProperty)
			{
				Test.AddError(TEXT("CommonUI fixture identity properties unavailable"));
				return false;
			}

			auto* Inner = CastField<FObjectProperty>(PlayersProperty->Inner);
			if (!Test.TestNotNull(TEXT("CommonUI fixture reflected player item"), Inner))
			{
				return false;
			}

			GIProperty->SetObjectPropertyValue_InContainer(WidgetViewport.Get(), WidgetGameInstance.Get());
			WorldProperty->SetObjectPropertyValue_InContainer(WidgetViewport.Get(), Row.World);
			FScriptArrayHelper Players(PlayersProperty, PlayersProperty->ContainerPtrToValuePtr<void>(WidgetGameInstance.Get()));
			Inner->SetObjectPropertyValue(Players.GetRawPtr(Players.AddValue()), Row.LocalPlayer);
			Row.LocalPlayer->ViewportClient = WidgetViewport.Get();
			return Test.TestEqual(TEXT("CommonUI fixture local player has an index"), Row.LocalPlayer->GetLocalPlayerIndex(), 0);
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Row.Initialize(Test))
			{
				return false;
			}

			Owner = Row.LocalPlayer->GetSubsystem<UVBUIMessagingSubsystem>();
			UI = Row.LocalPlayer->GetSubsystem<UVBUIManagerSubsystem>();
			return Test.TestNotNull(TEXT("local-player messaging owner"), Owner)
				&& Test.TestNotNull(TEXT("local-player UI manager"), UI)
				&& Test.TestNull(TEXT("fixture has no viewport layout"), UI->GetLayout());
		}

		bool MakeLayout(FAutomationTestBase& Test, bool bInject)
		{
			if (!InstallLocalPlayerIndex(Test))
			{
				return false;
			}

			Layout = CreateWidget<UVBPrimaryLayout>(Row.Controller, UVBPrimaryLayout::StaticClass());
			if (!Test.TestNotNull(TEXT("real native PrimaryLayout"), Layout))
			{
				return false;
			}

			LayoutLife.Construct(*Layout);
			if (bInject)
			{
				bInjectedLayout = FVBUIMessagingTestAccess::InstallLayout(*UI, Layout);
				if (!Test.TestTrue(TEXT("isolated UIManager layout injection"), bInjectedLayout))
				{
					return false;
				}
			}

			return true;
		}

		UCommonActivatableWidgetContainerBase* DialogLayer(FAutomationTestBase& Test) const
		{
			UCommonActivatableWidgetContainerBase* Layer = Layout ? Layout->GetLayer(VBGameplayTags::UI_Layer_Dialog) : nullptr;
			if (!Test.TestNotNull(TEXT("real Dialog layer"), Layer))
			{
				return nullptr;
			}

			Test.TestEqual(TEXT("Dialog transition duration is zero"), Layer->GetTransitionDuration(), 0.0f);
			return Layer;
		}

		UClass* RequiredDialogClass(FAutomationTestBase& Test) const
		{
			const FVBScreenRoute* Route = GetDefault<UVBUISettings>()->Screens.Find(VBGameplayTags::UI_Screen_Confirmation);
			if (!Route || Route->ScreenClass.IsNull())
			{
				Test.AddError(TEXT("Required UI.Screen.Confirmation route/WBP is missing; real-widget coverage cannot run."));
				return nullptr;
			}

			if (!Test.TestTrue(TEXT("confirmation route uses Dialog layer"), Route->Layer == VBGameplayTags::UI_Layer_Dialog))
			{
				return nullptr;
			}

			UClass* Class = Route->ScreenClass.LoadSynchronous();
			if (!Test.TestNotNull(TEXT("configured confirmation class loads"), Class))
			{
				return nullptr;
			}

			if (!Class->IsChildOf(UVBConfirmationDialogWidget::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
			{
				Test.AddError(TEXT("Confirmation route must name a concrete VBConfirmationDialogWidget WBP."));
				return nullptr;
			}

			return Class;
		}

		~FConfirmationFixture()
		{
			// Slate 가 열린 대화상자를 놓기 전에 결과 콜백을 먼저 끊는다. 뒤의 PlayerRemoved 가 정리를 한 번 더 해도 무해하다.
			if (Owner)
			{
				Owner->Deinitialize();
				Owner->OnPresentedChanged.Clear();
			}

			if (bInjectedLayout)
			{
				FVBUIMessagingTestAccess::InstallLayout(*UI, nullptr);
			}

			if (Layout)
			{
				LayoutLife.Destruct(*Layout);
			}

			Row.Shutdown();
			if (Row.LocalPlayer)
			{
				Row.LocalPlayer->ViewportClient = nullptr;
			}

			WidgetViewport.Reset();
			WidgetGameInstance.Reset();
		}
	};

	FVBConfirmationRequest Request(const TCHAR* Name)
	{
		FVBConfirmationRequest Value;
		Value.Header = FText::FromString(Name);
		Value.Body = FText::FromString(Name);
		return Value;
	}

	FVBConfirmationHandle Presented(const UVBUIMessagingSubsystem& Owner)
	{
		FVBConfirmationHandle Handle;
		Owner.GetPresented(Handle);
		return Handle;
	}

	FVBConfirmationResultDelegate CountResult(int32& Count, EVBConfirmationResult& Last)
	{
		return FVBConfirmationResultDelegate::CreateLambda([&Count, &Last](EVBConfirmationResult Result)
		{
			++Count;
			Last = Result;
		});
	}

	constexpr EAutomationTestFlags MessagingFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingLifetimeTest, "VB.Unit.UIMessaging.Lifetime", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingLifetimeTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.Owner->NotifyDialogActivated(); // Subsystem-only: stand in for an already active dialog.
	int32 CallsA = 0, CallsB = 0;
	EVBConfirmationResult LastA = EVBConfirmationResult::Failed, LastB = EVBConfirmationResult::Failed;
	const FVBConfirmationHandle A = F.Owner->ShowConfirmation(Request(TEXT("A")), CountResult(CallsA, LastA));
	const FVBConfirmationHandle B = F.Owner->ShowConfirmation(Request(TEXT("B")), CountResult(CallsB, LastB));
	TestTrue(TEXT("accepted A has a valid handle"), A.IsValid());
	F.Owner->Answer(A, EVBConfirmationResult::Confirmed);
	TestEqual(TEXT("b: A callback exactly once"), CallsA, 1);
	TestTrue(TEXT("A result confirmed"), LastA == EVBConfirmationResult::Confirmed);
	F.Owner->Answer(A, EVBConfirmationResult::Declined);
	F.Owner->Cancel(A);
	TestEqual(TEXT("b: A callback remains exactly once"), CallsA, 1);
	TestTrue(TEXT("b: B survives stale A answer"), F.Owner->IsPending(B));
	TestEqual(TEXT("b: B callback is still zero"), CallsB, 0);
	F.Owner->Answer(B, EVBConfirmationResult::Declined);
	TestEqual(TEXT("B resolves once"), CallsB, 1);
	TestTrue(TEXT("B declined"), LastB == EVBConfirmationResult::Declined);

	int32 DeadCalls = 0;
	// UObject 는 abstract 라 가벼운 구체 요청자를 쓴다.
	UObject* Requester = NewObject<UTextBlock>(F.Row.LocalPlayer);
	const FVBConfirmationHandle Dead = F.Owner->ShowConfirmation(Request(TEXT("weak requester")),
		FVBConfirmationResultDelegate::CreateWeakLambda(Requester, [&DeadCalls](EVBConfirmationResult) { ++DeadCalls; }));
	// 호스트의 다른 객체를 수집하지 않고 weak delegate 만 무효로 만든다.
	Requester->MarkAsGarbage();
	F.Owner->Answer(Dead, EVBConfirmationResult::Confirmed);
	TestEqual(TEXT("lost weak requester receives no callback"), DeadCalls, 0);
	TestFalse(TEXT("lost requester does not strand queue"), F.Owner->IsPending(Dead));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingQueueTest, "VB.Unit.UIMessaging.Queue", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingQueueTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.Owner->NotifyDialogActivated();
	FVBConfirmationHandle A, B, C;
	int32 CallsA = 0, CallsB = 0;
	A = F.Owner->ShowConfirmation(Request(TEXT("A")), FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult)
	{
		++CallsA;
		TestFalse(TEXT("a: resolved A is absent inside callback"), F.Owner->IsPending(A));
		TestTrue(TEXT("a: B remains FIFO head"), Presented(*F.Owner) == B);
		C = F.Owner->ShowConfirmation(Request(TEXT("C from A callback")), {});
	}));
	B = F.Owner->ShowConfirmation(Request(TEXT("B")), FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult) { ++CallsB; }));
	TestTrue(TEXT("only A is presented"), Presented(*F.Owner) == A);
	F.Owner->Answer(B, EVBConfirmationResult::Confirmed);
	TestEqual(TEXT("queued B cannot be answered"), CallsB, 0);
	F.Owner->Answer(A, EVBConfirmationResult::Confirmed);
	TestEqual(TEXT("A resolved once during reentrancy"), CallsA, 1);
	TestTrue(TEXT("B remains before reentrant C"), Presented(*F.Owner) == B && F.Owner->IsPending(C));
	F.Owner->Answer(B, EVBConfirmationResult::Confirmed);
	TestTrue(TEXT("C becomes head after B"), Presented(*F.Owner) == C);
	F.Owner->Cancel(C);
	TestFalse(TEXT("queue drains"), Presented(*F.Owner).IsValid());

	FVBUIMessagingTestAccess::AwaitPush(*F.Owner);
	int32 PendingCalls = 0;
	const FVBConfirmationHandle Pending = F.Owner->ShowConfirmation(Request(TEXT("awaiting first activation")),
		FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult) { ++PendingCalls; }));
	F.Owner->Answer(Pending, EVBConfirmationResult::Confirmed);
	TestEqual(TEXT("pending push cannot be answered"), PendingCalls, 0);
	TestTrue(TEXT("pending push keeps its handle"), F.Owner->IsPending(Pending));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingCancelTest, "VB.Unit.UIMessaging.Cancel", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingCancelTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.Owner->NotifyDialogActivated();
	int32 CallsA = 0, CallsB = 0, CallsC = 0;
	EVBConfirmationResult LastA = EVBConfirmationResult::Failed, LastB = LastA, LastC = LastA;
	const auto A = F.Owner->ShowConfirmation(Request(TEXT("A")), CountResult(CallsA, LastA));
	const auto B = F.Owner->ShowConfirmation(Request(TEXT("B")), CountResult(CallsB, LastB));
	const auto C = F.Owner->ShowConfirmation(Request(TEXT("C")), CountResult(CallsC, LastC));
	int32 Changed = 0;
	F.Owner->OnPresentedChanged.AddLambda([&]() { ++Changed; });
	F.Owner->Cancel(B);
	TestEqual(TEXT("queued cancellation is delivered once"), CallsB, 1);
	TestTrue(TEXT("queued cancellation result"), LastB == EVBConfirmationResult::Cancelled);
	TestTrue(TEXT("queued cancellation preserves A"), Presented(*F.Owner) == A);
	TestEqual(TEXT("queued cancellation does not refresh front"), Changed, 0);
	F.Owner->Cancel(A);
	TestTrue(TEXT("active cancellation advances to C"), Presented(*F.Owner) == C);
	TestTrue(TEXT("active cancellation result"), LastA == EVBConfirmationResult::Cancelled);
	F.Owner->Cancel(A);
	F.Owner->Cancel(B);
	TestEqual(TEXT("active Cancel is exactly once"), CallsA, 1);
	TestEqual(TEXT("queued Cancel is exactly once"), CallsB, 1);
	FVBConfirmationRequest Notice = Request(TEXT("no decline"));
	Notice.bOfferDecline = false;
	F.Owner->Answer(C, EVBConfirmationResult::Confirmed);
	EVBConfirmationResult NoticeResult = EVBConfirmationResult::Failed;
	int32 NoticeCalls = 0;
	const auto N = F.Owner->ShowConfirmation(Notice, CountResult(NoticeCalls, NoticeResult));
	F.Owner->Answer(N, EVBConfirmationResult::Declined);
	TestTrue(TEXT("one-button request normalizes decline to Cancelled"), NoticeResult == EVBConfirmationResult::Cancelled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingFailedTest, "VB.Unit.UIMessaging.Failed", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingFailedTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	int32 Calls = 0;
	EVBConfirmationResult Last = EVBConfirmationResult::Confirmed;
	const auto H = F.Owner->ShowConfirmation(Request(TEXT("no viewport/layout")), CountResult(Calls, Last));
	TestEqual(TEXT("Failed callback is synchronous"), Calls, 1);
	TestTrue(TEXT("no layout produces Failed"), Last == EVBConfirmationResult::Failed);
	TestFalse(TEXT("synchronous Failed returns invalid handle"), H.IsValid());
	TestFalse(TEXT("requester must not lock on returned handle"), F.Owner->IsPending(H));

	// 실패 콜백은 정확히 한 번 다시 열고, 이 동기 호출 밖으로 빠져나가야 한다.
	int32 FirstCalls = 0, RetryCalls = 0;
	EVBConfirmationResult RetryLast = EVBConfirmationResult::Confirmed;
	FVBConfirmationHandle Retry;
	const auto First = F.Owner->ShowConfirmation(Request(TEXT("first failure")),
		FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult Result)
		{
			++FirstCalls;
			TestTrue(TEXT("first failure result"), Result == EVBConfirmationResult::Failed);
			Retry = F.Owner->ShowConfirmation(Request(TEXT("retry")), CountResult(RetryCalls, RetryLast));
		}));
	TestFalse(TEXT("original failed request returns invalid"), First.IsValid());
	TestEqual(TEXT("failure callback executes once"), FirstCalls, 1);
	TestEqual(TEXT("reentrant failure is not recursively completed"), RetryCalls, 0);
	TestTrue(TEXT("reentrant retry remains pending"), Retry.IsValid() && F.Owner->IsPending(Retry));
	TestTrue(TEXT("failure retry scheduled"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	TestTrue(TEXT("one owned retry tick runs"), FVBUIMessagingTestAccess::RunOwnedPresentTick(*F.Owner));
	TestEqual(TEXT("retry reaches actual missing-layout failure on next turn"), RetryCalls, 1);
	TestTrue(TEXT("retry failure result"), RetryLast == EVBConfirmationResult::Failed);
	TestFalse(TEXT("retry ticker is one-shot"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));

	FVBUIMessagingTestAccess::AwaitPush(*F.Owner);
	int32 BatchCalls = 0;
	EVBConfirmationResult BatchLast = EVBConfirmationResult::Confirmed;
	const auto A = F.Owner->ShowConfirmation(Request(TEXT("failed batch A")), CountResult(BatchCalls, BatchLast));
	const auto B = F.Owner->ShowConfirmation(Request(TEXT("failed batch B")), CountResult(BatchCalls, BatchLast));
	F.UI->OnScreenPushEnded.Broadcast(VBGameplayTags::UI_Screen_Confirmation, EVBScreenPushEnd::Failed);
	TestEqual(TEXT("failed push resolves entire queue"), BatchCalls, 2);
	TestTrue(TEXT("batch result Failed"), BatchLast == EVBConfirmationResult::Failed);
	TestFalse(TEXT("failed batch A removed"), F.Owner->IsPending(A));
	TestFalse(TEXT("failed batch B removed"), F.Owner->IsPending(B));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingPushCancelledTest, "VB.Unit.UIMessaging.PushEndedCancelled", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingPushCancelledTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	FVBUIMessagingTestAccess::AwaitPush(*F.Owner);
	int32 Calls = 0;
	EVBConfirmationResult Last = EVBConfirmationResult::Confirmed;
	const auto H = F.Owner->ShowConfirmation(Request(TEXT("cancelled pending load")), CountResult(Calls, Last));
	F.UI->OnScreenPushEnded.Broadcast(VBGameplayTags::UI_Screen_Options, EVBScreenPushEnd::Cancelled);
	TestFalse(TEXT("unrelated screen does not schedule retry"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	F.UI->OnScreenPushEnded.Broadcast(VBGameplayTags::UI_Screen_Confirmation, EVBScreenPushEnd::Cancelled);
	TestEqual(TEXT("cancelled load does not resolve request"), Calls, 0);
	TestTrue(TEXT("cancelled load preserves handle"), F.Owner->IsPending(H));
	TestTrue(TEXT("cancelled load schedules next turn"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	TestTrue(TEXT("cancelled load retries once"), FVBUIMessagingTestAccess::RunOwnedPresentTick(*F.Owner));
	TestEqual(TEXT("retry attempts real PushScreen and fails without layout"), Calls, 1);
	TestTrue(TEXT("retry failure is explicit"), Last == EVBConfirmationResult::Failed);
	TestFalse(TEXT("retry cannot strand request"), F.Owner->IsPending(H));

	// 보이는 중 Layout 교체: 지금 머리는 취소되고 나머지는 새 Layout 으로 대기한다.
	F.Owner->NotifyDialogActivated();
	int32 CallsA = 0, CallsB = 0;
	EVBConfirmationResult LastA = EVBConfirmationResult::Failed, LastB = LastA;
	const auto A = F.Owner->ShowConfirmation(Request(TEXT("visible before replacement")), CountResult(CallsA, LastA));
	const auto B = F.Owner->ShowConfirmation(Request(TEXT("queued before replacement")), CountResult(CallsB, LastB));
	F.Owner->NotifyDialogDeactivated();
	TestEqual(TEXT("replacement cancels visible head once"), CallsA, 1);
	TestTrue(TEXT("replacement head result Cancelled"), LastA == EVBConfirmationResult::Cancelled);
	TestFalse(TEXT("replacement removes old head"), F.Owner->IsPending(A));
	TestTrue(TEXT("replacement preserves queued request"), F.Owner->IsPending(B));
	TestEqual(TEXT("replacement does not prematurely resolve next request"), CallsB, 0);
	TestTrue(TEXT("replacement schedules next presentation"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingTeardownTest, "VB.Unit.UIMessaging.Teardown", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingTeardownTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	FVBUIMessagingTestAccess::AwaitPush(*F.Owner);
	int32 Calls = 0;
	EVBConfirmationResult Last = EVBConfirmationResult::Confirmed;
	const auto A = F.Owner->ShowConfirmation(Request(TEXT("A")), CountResult(Calls, Last));
	const auto B = F.Owner->ShowConfirmation(Request(TEXT("B")), CountResult(Calls, Last));
	F.UI->OnScreenPushEnded.Broadcast(VBGameplayTags::UI_Screen_Confirmation, EVBScreenPushEnd::Cancelled);
	TestTrue(TEXT("teardown fixture has a scheduled retry"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	F.Owner->Deinitialize();
	TestEqual(TEXT("teardown is silent"), Calls, 0);
	TestFalse(TEXT("teardown removes A"), F.Owner->IsPending(A));
	TestFalse(TEXT("teardown removes B"), F.Owner->IsPending(B));
	TestFalse(TEXT("teardown removes owned ticker"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	F.Owner->Answer(A, EVBConfirmationResult::Confirmed);
	F.Owner->Cancel(B);
	F.UI->OnScreenPushEnded.Broadcast(VBGameplayTags::UI_Screen_Confirmation, EVBScreenPushEnd::Failed);
	const auto After = F.Owner->ShowConfirmation(Request(TEXT("after teardown")), CountResult(Calls, Last));
	TestFalse(TEXT("teardown rejects new request with invalid handle"), After.IsValid());
	TestEqual(TEXT("late answers/push events/reopens remain silent"), Calls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingReopenResultTest, "VB.Unit.UIMessaging.ReopenAfterClose.ResultCallback", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingReopenResultTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.Owner->NotifyDialogActivated();
	// 위젯이 정한 빈 대기열 닫힘 알림 순서만 흉내 낸다.
	F.Owner->OnPresentedChanged.AddLambda([&]()
	{
		if (!Presented(*F.Owner).IsValid())
		{
			F.Owner->NotifyDialogDeactivated();
		}
	});
	int32 ReopenCalls = 0;
	EVBConfirmationResult ReopenLast = EVBConfirmationResult::Confirmed;
	FVBConfirmationHandle Reopen;
	const auto A = F.Owner->ShowConfirmation(Request(TEXT("last visible request")),
		FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult)
		{
			Reopen = F.Owner->ShowConfirmation(Request(TEXT("new request from result")), CountResult(ReopenCalls, ReopenLast));
		}));
	F.Owner->Answer(A, EVBConfirmationResult::Confirmed);
	TestTrue(TEXT("callback reopen stays pending before next tick"), Reopen.IsValid() && F.Owner->IsPending(Reopen));
	TestEqual(TEXT("callback reopen has no synchronous result"), ReopenCalls, 0);
	TestTrue(TEXT("one retry scheduled"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	if (FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner))
	{
		TestTrue(TEXT("callback reopen runs on owned next turn"), FVBUIMessagingTestAccess::RunOwnedPresentTick(*F.Owner));
		TestEqual(TEXT("no-layout failure occurs only on next turn"), ReopenCalls, 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingZeroStackTest, "VB.Unit.UIMessaging.ReopenAfterClose.ZeroDurationStack", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingZeroStackTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this) || !F.MakeLayout(*this, false))
	{
		return false;
	}

	auto* Layer = F.DialogLayer(*this);
	if (!Layer)
	{
		return false;
	}

	int32 TransitionEvents = 0, ACalls = 0, BCalls = 0;
	EVBConfirmationResult ALast = EVBConfirmationResult::Failed, BLast = ALast;
	FVBConfirmationHandle B;
	Layer->OnTransitioningChanged.AddLambda([&](UCommonActivatableWidgetContainerBase*, bool) { ++TransitionEvents; });
	UCommonActivatableWidget* Screen = Layer->AddWidget<UCommonActivatableWidget>(UCommonActivatableWidget::StaticClass(),
		[&](UCommonActivatableWidget& Instance)
		{
			// 스택이 자기 OnDeactivated 핸들러를 걸기 전에 건다 - NativeOnDeactivated 와 같은 순서.
			Instance.OnDeactivated().AddLambda([&]() { F.Owner->NotifyDialogDeactivated(); });
		});
	if (!TestNotNull(TEXT("real stack screen"), Screen))
	{
		return false;
	}

	TestTrue(TEXT("real stack activates screen"), Screen->IsActivated());
	F.Owner->NotifyDialogActivated();
	const auto A = F.Owner->ShowConfirmation(Request(TEXT("A")),
		FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult Result)
		{
			++ACalls;
			ALast = Result;
			B = F.Owner->ShowConfirmation(Request(TEXT("B from close callback")), CountResult(BCalls, BLast));
		}));
	Screen->DeactivateWidget();
	TestEqual(TEXT("close cancels A exactly once"), ACalls, 1);
	TestTrue(TEXT("close cancellation result"), ALast == EVBConfirmationResult::Cancelled);
	TestFalse(TEXT("A removed during close"), F.Owner->IsPending(A));
	TestFalse(TEXT("zero-duration stack removes screen synchronously"), Layer->GetWidgetList().Contains(Screen));
	TestTrue(TEXT("new close-callback request survives current turn"), B.IsValid() && F.Owner->IsPending(B));
	TestEqual(TEXT("close-callback request not resolved synchronously"), BCalls, 0);
	TestTrue(TEXT("close-callback request scheduled once"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	TestEqual(TEXT("zero duration needs no transition-end event"), TransitionEvents, 0);
	TestTrue(TEXT("close-callback request retries on owned next turn"), FVBUIMessagingTestAccess::RunOwnedPresentTick(*F.Owner));
	TestEqual(TEXT("retry reaches missing viewport failure"), BCalls, 1);
	Screen->OnDeactivated().Clear();
	Layer->OnTransitioningChanged.Clear();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingWidgetTest, "VB.Unit.UIMessaging.WidgetQueueAndBody", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingWidgetTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UClass* Class = F.RequiredDialogClass(*this);
	if (!Class || !F.MakeLayout(*this, true))
	{
		return false;
	}

	auto* Layer = F.DialogLayer(*this);
	if (!Layer)
	{
		return false;
	}

	FVBUIMessagingTestAccess::AwaitPush(*F.Owner); // Install real widget below without asynchronous loading.
	int32 ACalls = 0, BCalls = 0;
	EVBConfirmationResult ALast = EVBConfirmationResult::Failed, BLast = ALast;
	FVBConfirmationRequest ARequest = Request(TEXT("A"));
	ARequest.bFocusDecline = true;
	const auto A = F.Owner->ShowConfirmation(ARequest, CountResult(ACalls, ALast));
	const auto B = F.Owner->ShowConfirmation(Request(TEXT("B")), CountResult(BCalls, BLast));
	auto* Widget = Layer->AddWidget<UVBConfirmationDialogWidget>(Class);
	if (!TestNotNull(TEXT("real confirmation widget"), Widget))
	{
		return false;
	}

	UButton* Confirm = FVBUIMessagingTestAccess::Confirm(*Widget);
	UButton* Decline = FVBUIMessagingTestAccess::Decline(*Widget);
	UTextBlock* Body = FVBUIMessagingTestAccess::Body(*Widget);
	if (!TestNotNull(TEXT("bound Confirm button"), Confirm) || !TestNotNull(TEXT("bound Decline button"), Decline)
		|| !TestNotNull(TEXT("bound Body text"), Body))
	{
		return false;
	}

	TestTrue(TEXT("real widget initially presents A"), FVBUIMessagingTestAccess::Shown(*Widget) == A);
	TestEqual(TEXT("native new-request input guard defaults to 0.2 seconds"),
		FVBUIMessagingTestAccess::GuardSeconds(*GetDefault<UVBConfirmationDialogWidget>()), 0.2f);
	if (!TestTrue(TEXT("configured real dialog enables new-request guard"), FVBUIMessagingTestAccess::GuardSeconds(*Widget) > 0.0f))
	{
		return false;
	}

	FVBUIMessagingTestAccess::SetRequestAge(*Widget, 0.0);
	Confirm->OnClicked.Broadcast();
	FVBUIMessagingTestAccess::SetRequestAge(*Widget, 0.0);
	Decline->OnClicked.Broadcast();
	FVBUIMessagingTestAccess::SetRequestAge(*Widget, 0.0);
	TestTrue(TEXT("guarded Back is consumed"), FVBUIMessagingTestAccess::Back(*Widget));
	TestEqual(TEXT("new-request guard blocks immediate confirm/decline/back"), ACalls, 0);
	TestTrue(TEXT("guarded input preserves A"), F.Owner->IsPending(A));
	FVBUIMessagingTestAccess::SetRequestAge(*Widget, FVBUIMessagingTestAccess::GuardSeconds(*Widget) + 1.0);
	const double AShownAt = FVBUIMessagingTestAccess::ShownAt(*Widget);
	TestTrue(TEXT("decline is desired focus for safe-default request"), FVBUIMessagingTestAccess::DesiredFocus(*Widget) == Decline);
	const auto ConfirmSlate = Confirm->GetCachedWidget();
	const auto DeclineSlate = Decline->GetCachedWidget();
	TestTrue(TEXT("real button Slate exists"), ConfirmSlate.IsValid() && DeclineSlate.IsValid());
	for (int32 Index = 3; Index > 0; --Index)
	{
		const FText Countdown = FText::AsNumber(Index);
		F.Owner->SetBody(A, Countdown);
		TestTrue(TEXT("body-only update reaches real text"), Body->GetText().EqualTo(Countdown));
		TestTrue(TEXT("body-only update preserves handle"), FVBUIMessagingTestAccess::Shown(*Widget) == A);
		TestEqual(TEXT("body-only update does not restart input guard"), FVBUIMessagingTestAccess::ShownAt(*Widget), AShownAt);
		TestTrue(TEXT("body-only update preserves desired focus"), FVBUIMessagingTestAccess::DesiredFocus(*Widget) == Decline);
		TestTrue(TEXT("body-only update preserves button identities"), FVBUIMessagingTestAccess::Confirm(*Widget) == Confirm
			&& FVBUIMessagingTestAccess::Decline(*Widget) == Decline);
		TestTrue(TEXT("body-only update preserves button Slate"), Confirm->GetCachedWidget() == ConfirmSlate
			&& Decline->GetCachedWidget() == DeclineSlate);
	}

	F.Owner->SetBody(B, FText::FromString(TEXT("queued body")));
	TestTrue(TEXT("queued body does not replace front text"), Body->GetText().EqualTo(FText::AsNumber(1)));
	Confirm->OnClicked.Broadcast(); // Real production button binding, not Owner.Answer.
	TestEqual(TEXT("real confirm resolves A once"), ACalls, 1);
	TestTrue(TEXT("same real widget advances to B"), Widget->IsActivated() && FVBUIMessagingTestAccess::Shown(*Widget) == B);
	TestEqual(TEXT("queue keeps one widget instance"), Layer->GetWidgetList().Num(), 1);
	TestTrue(TEXT("B reads updated queued body"), Body->GetText().EqualTo(FText::FromString(TEXT("queued body"))));
	TestTrue(TEXT("B's default desired focus is Confirm"), FVBUIMessagingTestAccess::DesiredFocus(*Widget) == Confirm);
	TestTrue(TEXT("advancing to B restarts presentation timestamp"), FVBUIMessagingTestAccess::ShownAt(*Widget) > AShownAt);
	FVBUIMessagingTestAccess::SetRequestAge(*Widget, 0.0);
	Confirm->OnClicked.Broadcast();
	TestEqual(TEXT("late immediate double-click cannot answer B"), BCalls, 0);
	TestTrue(TEXT("new guard preserves B after double-click"), F.Owner->IsPending(B));
	F.Owner->Answer(A, EVBConfirmationResult::Declined); // Old-token late click; not raw stale Slate input.
	F.Owner->Cancel(A);
	TestEqual(TEXT("old-token late click does not answer B"), BCalls, 0);
	TestTrue(TEXT("old-token late click preserves B"), F.Owner->IsPending(B));
	FVBUIMessagingTestAccess::SetRequestAge(*Widget, FVBUIMessagingTestAccess::GuardSeconds(*Widget) + 1.0);
	Decline->OnClicked.Broadcast();
	TestEqual(TEXT("real decline resolves B once"), BCalls, 1);
	TestTrue(TEXT("real decline result"), BLast == EVBConfirmationResult::Declined);
	TestFalse(TEXT("empty queue deactivates real dialog"), Widget->IsActivated());
	TestFalse(TEXT("empty dialog leaves real stack immediately"), Layer->GetWidgetList().Contains(Widget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingStillListedTest, "VB.Unit.UIMessaging.ReopenAfterClose.StillListedFails", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingStillListedTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UClass* Class = F.RequiredDialogClass(*this);
	if (!Class || !F.MakeLayout(*this, true))
	{
		return false;
	}

	auto* Layer = F.DialogLayer(*this);
	if (!Layer)
	{
		return false;
	}

	FVBUIMessagingTestAccess::AwaitPush(*F.Owner);
	int32 CallsA = 0, CallsB = 0;
	EVBConfirmationResult LastA = EVBConfirmationResult::Failed, LastB = EVBConfirmationResult::Confirmed;
	F.Owner->ShowConfirmation(Request(TEXT("A")), CountResult(CallsA, LastA));
	const auto B = F.Owner->ShowConfirmation(Request(TEXT("B")), CountResult(CallsB, LastB));
	auto* Widget = Layer->AddWidget<UVBConfirmationDialogWidget>(Class);
	if (!TestNotNull(TEXT("listed real confirmation widget"), Widget))
	{
		return false;
	}

	TestTrue(TEXT("real UIManager sees listed confirmation"), F.UI->IsScreenOpen(VBGameplayTags::UI_Screen_Confirmation));
	// 실제 위젯을 빼지 않고 닫힘만 알려 해제, 전환 전제가 깨진 상황을 만든다. 길이가 있는 애니메이션은 돌리지 않는다.
	F.Owner->NotifyDialogDeactivated();
	TestTrue(TEXT("B survives until scheduled check"), F.Owner->IsPending(B));
	TestEqual(TEXT("listed next request not failed synchronously"), CallsB, 0);
	TestTrue(TEXT("listed-widget check runs next turn"), FVBUIMessagingTestAccess::RunOwnedPresentTick(*F.Owner));
	TestEqual(TEXT("still-listed request fails once instead of hanging"), CallsB, 1);
	TestTrue(TEXT("still-listed result is Failed"), LastB == EVBConfirmationResult::Failed);
	TestFalse(TEXT("still-listed request removed"), F.Owner->IsPending(B));
	TestFalse(TEXT("still-listed check does not poll"), FVBUIMessagingTestAccess::HasScheduledPresent(*F.Owner));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingRejectedAsyncLoadTest,
	"VB.Unit.UIMessaging.RejectedAsyncLoad", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingRejectedAsyncLoadTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	FConfirmationFixture F;
	if (!F.Initialize(*this) || !F.MakeLayout(*this, true))
	{
		return false;
	}
    // 주입한 실패가 아니라 실제 StreamableManager 의 null handle 경로를 돈다.
	struct FScopedRoute
	{
		UVBUISettings* Settings = GetMutableDefault<UVBUISettings>();
		FGameplayTag Tag = VBGameplayTags::UI_Screen_Confirmation;
		TOptional<FVBScreenRoute> Previous;
		FScopedRoute()
		{
			if (const FVBScreenRoute* Existing = Settings->Screens.Find(Tag))
			{
				Previous = *Existing;
			}

	FVBScreenRoute Invalid;
	Invalid.Layer = VBGameplayTags::UI_Layer_Dialog;
            // FName 생성자는 망가진 패키지 이름을 그대로 받는다 - 문자열 파서처럼 빈 경로로 바꾸지 않는다.
	Invalid.ScreenClass = TSoftClassPtr<UVBScreenWidget>(FSoftObjectPath(
			FTopLevelAssetPath(FName(TEXT("VBRejectedPackage")), FName(TEXT("InvalidClass")))));
	Settings->Screens.Add(Tag, Invalid);
	}

	~FScopedRoute()
	{
		if (Previous.IsSet())
		{
			Settings->Screens.Add(Tag, Previous.GetValue());
		}
		else
		{
			Settings->Screens.Remove(Tag);
		}
	}
	} ScopedRoute;
	AddExpectedError(TEXT("called with invalid package name VBRejectedPackage.InvalidClass"),
		EAutomationExpectedErrorFlags::Contains, 1);
	int32 Calls = 0, FailedPushes = 0;
	EVBConfirmationResult Last = EVBConfirmationResult::Confirmed;
	const FDelegateHandle PushEnd = F.UI->OnScreenPushEnded.AddLambda(
		[&](FGameplayTag Tag, EVBScreenPushEnd Reason)
		{
			if (Tag != VBGameplayTags::UI_Screen_Confirmation)
			{
				return;
			}

	++FailedPushes;
	TestTrue(TEXT("rejected async push reports Failed"), Reason == EVBScreenPushEnd::Failed);
	TestFalse(TEXT("rejected async push releases input before notification"),
		FVBUIMessagingTestAccess::HasPushInputReason(*F.UI, Tag));
	});
	const FVBConfirmationHandle Handle = F.Owner->ShowConfirmation(
		Request(TEXT("rejected load")), CountResult(Calls, Last));
	F.UI->OnScreenPushEnded.Remove(PushEnd);
	TestEqual(TEXT("rejected async push emits exactly one failure"), FailedPushes, 1);
	TestEqual(TEXT("rejected async confirmation resolves synchronously once"), Calls, 1);
	TestTrue(TEXT("rejected async confirmation result Failed"), Last == EVBConfirmationResult::Failed);
	TestFalse(TEXT("rejected async confirmation returns invalid handle"), Handle.IsValid());
	TestFalse(TEXT("rejected async push leaves no input reason"),
		FVBUIMessagingTestAccess::HasPushInputReason(*F.UI, VBGameplayTags::UI_Screen_Confirmation));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingReentrantTeardownTest,
	"VB.Unit.UIMessaging.ReentrantTeardown", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingReentrantTeardownTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	{
		FConfirmationFixture F;
		if (!F.Initialize(*this))
		{
			return false;
		}

	F.Owner->NotifyDialogActivated();
	int32 Calls = 0;
	EVBConfirmationResult Last = EVBConfirmationResult::Failed;
	const auto H = F.Owner->ShowConfirmation(Request(TEXT("observer teardown")), CountResult(Calls, Last));
	F.Owner->OnPresentedChanged.AddLambda([&] { F.Owner->Deinitialize(); });
	F.Owner->Answer(H, EVBConfirmationResult::Confirmed);
	TestEqual(TEXT("observer teardown silences detached result"), Calls, 0);
	TestFalse(TEXT("observer teardown drops accepted request"), F.Owner->IsPending(H));
	}

	{
		FConfirmationFixture F;
		if (!F.Initialize(*this))
		{
			return false;
		}

	FVBUIMessagingTestAccess::AwaitPush(*F.Owner);
	int32 FirstCalls = 0, RemainingCalls = 0;
	EVBConfirmationResult RemainingLast = EVBConfirmationResult::Confirmed;
	F.Owner->ShowConfirmation(Request(TEXT("first failure")),
		FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult Result)
			{
				++FirstCalls;
				TestTrue(TEXT("first detached failure delivered"), Result == EVBConfirmationResult::Failed);
				F.Owner->Deinitialize();
			}));
	const auto Remaining = F.Owner->ShowConfirmation(Request(TEXT("remaining failure")),
		CountResult(RemainingCalls, RemainingLast));
	F.UI->OnScreenPushEnded.Broadcast(VBGameplayTags::UI_Screen_Confirmation, EVBScreenPushEnd::Failed);
	TestEqual(TEXT("first detached failure executes once"), FirstCalls, 1);
	TestEqual(TEXT("failure callback teardown silences remaining detached results"), RemainingCalls, 0);
	TestFalse(TEXT("failure callback teardown drops remaining request"), F.Owner->IsPending(Remaining));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBUIMessagingRequesterTest,
	"VB.Unit.UIMessaging.Requester", VBUIMessagingTests::MessagingFlags)
bool FVBUIMessagingRequesterTest::RunTest(const FString& Parameters)
{
	using namespace VBUIMessagingTests;
	{
		FConfirmationFixture F;
		if (!F.Initialize(*this))
		{
			return false;
		}

	FVBConfirmationRequester Ask;
	int32 FirstCalls = 0;
	FVBConfirmationHandle Retry;
	const bool Accepted = Ask.Ask(F.Row.LocalPlayer, Request(TEXT("sync failure")),
		FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult Result)
			{
				++FirstCalls;
				TestTrue(TEXT("requester sees synchronous Failed"), Result == EVBConfirmationResult::Failed);
				TestTrue(TEXT("failure callback accepts deferred retry"),
					Ask.Ask(F.Row.LocalPlayer, Request(TEXT("retry")), {}));
				Retry = Ask.GetHandle();
			}));
	TestFalse(TEXT("failed original Ask returns false"), Accepted);
	TestEqual(TEXT("failed original Ask calls back once"), FirstCalls, 1);
	TestTrue(TEXT("synchronous failure does not overwrite reentrant requester handle"), Ask.GetHandle() == Retry);
	TestTrue(TEXT("reentrant requester remains pending"), Ask.IsPending(F.Row.LocalPlayer));
	Ask.Cancel(F.Row.LocalPlayer);
	TestFalse(TEXT("reentrant requester can withdraw retry"), F.Owner->IsPending(Retry));

	F.Owner->NotifyDialogActivated();
	FVBConfirmationHandle FromCancel;
	TestTrue(TEXT("requester accepts first shown question"),
		Ask.Ask(F.Row.LocalPlayer, Request(TEXT("cancel then reopen")),
			FVBConfirmationResultDelegate::CreateLambda([&](EVBConfirmationResult Result)
				{
					TestTrue(TEXT("requester cancel reports Cancelled"), Result == EVBConfirmationResult::Cancelled);
					TestTrue(TEXT("cancel callback accepts new question"),
						Ask.Ask(F.Row.LocalPlayer, Request(TEXT("from cancel")), {}));
					FromCancel = Ask.GetHandle();
				})));
	Ask.Cancel(F.Row.LocalPlayer);
	TestTrue(TEXT("cancel does not erase reentrant requester handle"), Ask.GetHandle() == FromCancel);
	TestTrue(TEXT("cancel callback question stays pending"), Ask.IsPending(F.Row.LocalPlayer));
	Ask.Cancel(F.Row.LocalPlayer);
	}

	{
		FConfirmationFixture A, B;
		if (!A.Initialize(*this) || !B.Initialize(*this))
		{
			return false;
		}

	A.Owner->NotifyDialogActivated();
	B.Owner->NotifyDialogActivated();
	FVBConfirmationRequester Ask;
	int32 CallsA = 0, CallsB = 0;
	EVBConfirmationResult LastA = EVBConfirmationResult::Failed, LastB = LastA;
	if (!TestTrue(TEXT("first seat requester accepted"), Ask.Ask(A.Row.LocalPlayer,
				Request(TEXT("seat A")), CountResult(CallsA, LastA))))
	{
		return false;
	}

	const auto HandleA = Ask.GetHandle();
	const auto HandleB = B.Owner->ShowConfirmation(Request(TEXT("seat B")), CountResult(CallsB, LastB));
	TestEqual(TEXT("two owners can issue the same local numeric ID"), HandleA.Id, HandleB.Id);
	Ask.SetBody(B.Row.LocalPlayer, FText::FromString(TEXT("A updated after seat change")));
	FVBConfirmationHandle PresentedA, PresentedB;
	const auto* RequestA = A.Owner->GetPresented(PresentedA);
	const auto* RequestB = B.Owner->GetPresented(PresentedB);
	TestTrue(TEXT("body update stays on captured owner"), RequestA && RequestA->Body.EqualTo(
			FText::FromString(TEXT("A updated after seat change"))));
	TestTrue(TEXT("body update cannot overwrite another seat"), RequestB && RequestB->Body.EqualTo(
			FText::FromString(TEXT("seat B"))));
	Ask.Cancel(B.Row.LocalPlayer);
	TestEqual(TEXT("withdraw after seat change cancels captured owner"), CallsA, 1);
	TestTrue(TEXT("captured owner receives Cancelled"), LastA == EVBConfirmationResult::Cancelled);
	TestEqual(TEXT("withdraw cannot cancel another seat's same ID"), CallsB, 0);
	TestTrue(TEXT("other seat request remains registered"), B.Owner->IsPending(HandleB));
	}

	return true;
}

#endif
