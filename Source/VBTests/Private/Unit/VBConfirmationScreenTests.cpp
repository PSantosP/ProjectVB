// ProjectVB Copyright (c) 2025. All Rights Reserved.
// 실제 Options, KeyBindings 위젯(WBP_Options, Slate 수명)의 확인 질문 흐름을 한 fixture 로 시험한다. 자산이 없으면 실패다.
// 디스크 쓰기, 해상도 적용, GI.Init 없이, Video/commit/autodetect test seam 이 엔진 변경을 가로챈다.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Settings/VBOptionsSubsystem.h"
#include "UI/Settings/VBOptionsScreenWidget.h"
#include "UI/Settings/VBKeyBindingScreenWidget.h"
#include "UI/VBUIMessagingSubsystem.h"
#include "UI/VBUIManagerSubsystem.h"
#include "VBUITestFixture.h"

#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

class FVBConfirmationScreensTestAccess
{
public:
    static FVBConfirmationHandle OptionHandle(const UVBOptionsScreenWidget& Screen) { return Screen.ConfirmAsk.GetHandle(); }
    static bool Asking(const UVBOptionsScreenWidget& Screen) { return Screen.IsAsking(); }
    static void Back(UVBOptionsScreenWidget& Screen) { Screen.HandleBack(); }
    static void AskVideo(UVBOptionsScreenWidget& Screen) { Screen.AskKeepVideo(); }
    static void Tick(UVBOptionsScreenWidget& Screen) { Screen.NativeTick(FGeometry(), 0.0f); }
    static void Dirty(UVBOptionsScreenWidget& Screen)
    {
        Screen.Draft = Screen.Committed;
        Screen.Draft.User.MasterVolume = Screen.Committed.User.MasterVolume == 0.25f ? 0.75f : 0.25f;
    }

    static FVBOptionsDraft Draft(const UVBOptionsScreenWidget& Screen) { return Screen.Draft; }
    static EVBOptionTab Tab(const UVBOptionsScreenWidget& Screen) { return Screen.CurrentTab; }
    static void SelectTab(UVBOptionsScreenWidget& Screen, EVBOptionTab Tab) { Screen.SelectTab(static_cast<int32>(Tab)); }
    static void StepTab(UVBOptionsScreenWidget& Screen) { Screen.StepTab(1); }
    static void RowValue(UVBOptionsScreenWidget& Screen) { Screen.HandleRowValueChanged(0, 0.9f); }
    static void RowAction(UVBOptionsScreenWidget& Screen) { Screen.HandleRowAction(0); }
    static void Apply(UVBOptionsScreenWidget& Screen) { Screen.HandleApply(); }
    static void Reset(UVBOptionsScreenWidget& Screen) { Screen.HandleReset(); }
    static void Entry(UVBOptionsScreenWidget& Screen, FVBOptionEntry Entry)
    {
        Screen.Rows.Reset();
        Screen.EntryBox->ClearChildren();
        Screen.Entries = {MoveTemp(Entry)};
    }

    static bool HasMultipleVisibleTabs(const UVBOptionsScreenWidget& Screen)
    {
        return Screen.TabBox && Screen.TabBox->GetChildrenCount() > 1
            && Screen.TabBox->GetChildAt(1)->GetVisibility() != ESlateVisibility::Collapsed;
    }

    static void ObserveVideo(UVBOptionsSubsystem& Options, TFunction<void(bool)> Callback)
    {
        Options.MachineActions.DecideVideoMode = MoveTemp(Callback);
    }

    static void ObserveCommit(UVBOptionsSubsystem& Options, TFunction<bool(const FVBMachineSettings&)> Callback)
    {
        Options.MachineActions.Commit = MoveTemp(Callback);
    }

    static void ObserveAuto(UVBOptionsSubsystem& Options, TFunction<void()> Callback)
    {
        Options.MachineActions.RunAutoDetect = MoveTemp(Callback);
    }

    static void Preview(UVBOptionsSubsystem& Options, double Seconds = 30.0)
    {
        Options.PendingVideoDeadline = FPlatformTime::Seconds() + Seconds;
    }

    static void Expire(UVBOptionsSubsystem& Options)
    {
        Options.PendingVideoDeadline = FPlatformTime::Seconds() - 1.0;
        Options.TickPendingVideo(0.0f);
    }

    static void Clear(UVBOptionsSubsystem& Options)
    {
        Options.ClearPendingVideo();
        Options.MachineActions = FVBOptionsMachineActions::Engine();
    }

    static bool Bound(const UVBOptionsScreenWidget& Screen) { return Screen.TabBox && Screen.EntryBox && Screen.BtnApply && Screen.BtnReset; }
    static FVBConfirmationHandle KeyHandle(const UVBKeyBindingScreenWidget& Screen) { return Screen.ConflictAsk.GetHandle(); }
    static void KeyboardForFixture(UVBKeyBindingScreenWidget& Screen) { Screen.ShownDevice = EVBKeyDevice::KeyboardMouse; }
    static void KeyRows(UVBKeyBindingScreenWidget& Screen, TArray<FVBKeyBindingRow> Rows) { Screen.Rows = MoveTemp(Rows); }
    static void SelectRow(UVBKeyBindingScreenWidget& Screen) { Screen.SelectRow(0); }
    static bool Capturing(const UVBKeyBindingScreenWidget& Screen) { return Screen.CaptureProcessor.IsValid(); }
    static bool Confirming(const UVBKeyBindingScreenWidget& Screen) { return Screen.State == UVBKeyBindingScreenWidget::EState::Confirming; }
    static bool Browsing(const UVBKeyBindingScreenWidget& Screen) { return Screen.State == UVBKeyBindingScreenWidget::EState::Browsing; }
    static bool HasSettings(const UVBKeyBindingScreenWidget& Screen) { return Screen.GetSettings() != nullptr; }
    static void ObserveRebind(UVBKeyBindingScreenWidget& Screen, TFunction<void(FName, EVBKeyDevice, const FKey&)> Callback)
    {
        Screen.ApplyRebindForTests = MoveTemp(Callback);
    }

    static bool Back(UVBKeyBindingScreenWidget& Screen) { return Screen.NativeOnHandleBackAction(); }
    static void StopCapture(UVBKeyBindingScreenWidget& Screen) { Screen.StopCapture(); }
    static void DifferentDeviceWithoutWithdraw(UVBKeyBindingScreenWidget& Screen) { Screen.ShownDevice = EVBKeyDevice::Gamepad; }
    static bool Bound(const UVBKeyBindingScreenWidget& Screen)
    {
        return Screen.EntryBox && Screen.TxtStatus && Screen.BtnKeyboardTab && Screen.BtnGamepadTab;
    }
};


namespace VBConfirmationScreenTests
{
    using Access = FVBConfirmationScreensTestAccess;
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
    struct FFixture
    {
        VBUITest::FRowFixture Row;
        UVBUIMessagingSubsystem* Bus = nullptr;
        UVBOptionsSubsystem* Options = nullptr;
        UVBUIManagerSubsystem* UI = nullptr;
        UVBOptionsScreenWidget* OptionsScreen = nullptr;
        UVBKeyBindingScreenWidget* KeyScreen = nullptr;
        bool bKeySlateConstructed = false;
        VBUITest::FSlateLife ScreenLife;
        TStrongObjectPtr<UGameInstance> WidgetGI;
        TStrongObjectPtr<UGameViewportClient> WidgetViewport;
        TArray<bool> VideoDecisions;
        int32 Commits = 0, AutoDetects = 0, Rebinds = 0;
        FName LastMapping;
        EVBKeyDevice LastDevice = EVBKeyDevice::KeyboardMouse;
        FKey LastKey;
        bool bDecisionWhileInactive = false;

        bool Initialize(FAutomationTestBase& Test, bool bKeyScreen = false)
        {
            if (!Row.Initialize(Test))
            {
                return false;
            }
            // CommonUI 는 렌더 viewport 나 UGameInstance::Init 없이 GI.LocalPlayers 만 있으면 된다.
            WidgetGI.Reset(NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient));
            // CommonUI NativeDestruct 는 viewport 만이 아니라 UWidget 의 world 로 GI 를 읽는다.
            Row.World->SetGameInstance(WidgetGI.Get());
            WidgetViewport.Reset(NewObject<UGameViewportClient>(GEngine, NAME_None, RF_Transient));
            auto* GIProperty = FindFProperty<FObjectProperty>(UGameViewportClient::StaticClass(), TEXT("GameInstance"));
            auto* WorldProperty = FindFProperty<FObjectProperty>(UGameViewportClient::StaticClass(), TEXT("World"));
            auto* PlayersProperty = FindFProperty<FArrayProperty>(UGameInstance::StaticClass(), TEXT("LocalPlayers"));
            if (!GIProperty || !WorldProperty || !PlayersProperty)
            {
                Test.AddError(TEXT("Fixture/CommonUI identity properties unavailable"));
                return false;
            }

            auto* Inner = CastField<FObjectProperty>(PlayersProperty->Inner);
            if (!Test.TestNotNull(TEXT("Fixture/CommonUI player element"), Inner))
            {
                return false;
            }

            GIProperty->SetObjectPropertyValue_InContainer(WidgetViewport.Get(), WidgetGI.Get());
            WorldProperty->SetObjectPropertyValue_InContainer(WidgetViewport.Get(), Row.World);
            FScriptArrayHelper Players(PlayersProperty, PlayersProperty->ContainerPtrToValuePtr<void>(WidgetGI.Get()));
            Inner->SetObjectPropertyValue(Players.GetRawPtr(Players.AddValue()), Row.LocalPlayer);
            Row.LocalPlayer->ViewportClient = WidgetViewport.Get();
            if (!Test.TestEqual(TEXT("Fixture/CommonUI index"), Row.LocalPlayer->GetLocalPlayerIndex(), 0))
            {
                return false;
            }

            Bus = Row.LocalPlayer->GetSubsystem<UVBUIMessagingSubsystem>();
            Options = Row.LocalPlayer->GetSubsystem<UVBOptionsSubsystem>();
            UI = Row.LocalPlayer->GetSubsystem<UVBUIManagerSubsystem>();
            if (!Test.TestNotNull(TEXT("Fixture/Messaging"), Bus) || !Test.TestNotNull(TEXT("Fixture/Options"), Options)
                || !Test.TestNotNull(TEXT("Fixture/UIManager"), UI))
            {
                return false;
            }

            Access::ObserveVideo(*Options, [this](bool bKeep)
            {
                VideoDecisions.Add(bKeep);
                bDecisionWhileInactive |= OptionsScreen && !OptionsScreen->IsActivated();
            });
            Access::ObserveCommit(*Options, [this](const FVBMachineSettings&) { ++Commits; return false; });
            Access::ObserveAuto(*Options, [this]() { ++AutoDetects; });
            // 공개 dialog adapter 만 쓴다. 실제 확인 위젯은 다른 시험이 본다.
            Bus->NotifyDialogActivated();
            if (bKeyScreen)
            {
                KeyScreen = Row.MakeRow<UVBKeyBindingScreenWidget>(Test, TEXT("/Game/VB/UI/Settings/WBP_KeyBindings.WBP_KeyBindings_C"));
                if (!KeyScreen || !Test.TestTrue(TEXT("Fixture/KeyBindWidgets"), Access::Bound(*KeyScreen)))
                {
                    return false;
                }

                if (!Test.TestTrue(TEXT("Fixture/KeyWorldGameInstance"), KeyScreen->GetGameInstance() == WidgetGI.Get()))
                {
                    return false;
                }

                Access::ObserveRebind(*KeyScreen, [this](FName Mapping, EVBKeyDevice Device, const FKey& Key)
                {
                    ++Rebinds; LastMapping = Mapping; LastDevice = Device; LastKey = Key;
                });
                ExpectRebuild(Test);
                ScreenLife.Construct(*KeyScreen);
                bKeySlateConstructed = true;
                KeyScreen->ActivateWidget();
            }
            else
            {
                OptionsScreen = Row.MakeRow<UVBOptionsScreenWidget>(Test,
                    TEXT("/Game/VB/UI/Settings/WBP_Options.WBP_Options_C"));
                if (!OptionsScreen || !Test.TestTrue(TEXT("Fixture/OptionsBindWidgets"), Access::Bound(*OptionsScreen)))
                {
                    return false;
                }

                ScreenLife.Construct(*OptionsScreen);
                OptionsScreen->ActivateWidget();
            }

            return true;
        }

        void ExpectRebuild(FAutomationTestBase& Test)
        {
            if (KeyScreen && !Access::HasSettings(*KeyScreen))
            {
                Test.AddExpectedMessagePlain(TEXT("사용자 설정이 UVBInputUserSettings"), ELogVerbosity::Warning,
                    EAutomationExpectedMessageFlags::Contains, 1);
            }
        }

        bool AskConflict(FAutomationTestBase& Test)
        {
            // fixture 행만 쓴다 - 실제 사용자 매핑은 바꾸지 않는다.
            FVBKeyBindingRow Editing;
            Editing.MappingName = TEXT("U04_TestEdit"); Editing.Key = EKeys::V; Editing.DefaultKey = EKeys::V;
            FVBKeyBindingRow Occupied;
            Occupied.MappingName = TEXT("U04_TestOccupied"); Occupied.Key = EKeys::B; Occupied.DefaultKey = EKeys::B;
            Access::KeyboardForFixture(*KeyScreen);
            Access::KeyRows(*KeyScreen, {Editing, Occupied});
            Access::SelectRow(*KeyScreen);
            if (!Test.TestTrue(TEXT("Key/RealCaptureStarted"), Access::Capturing(*KeyScreen)))
            {
                return false;
            }

            KeyScreen->HandleCapturedKey(EKeys::B);
            return Test.TestTrue(TEXT("Key/ConflictQuestionPending"), Bus->IsPending(Access::KeyHandle(*KeyScreen)));
        }

        ~FFixture()
        {
            // 클로저가 이 fixture 를 가리키므로 위젯 해체 전에 결과 알림을 끊는다.
            if (Bus)
            {
                Bus->Deinitialize();
                Bus->OnPresentedChanged.Clear();
            }

            if (Options)
            {
                Access::Clear(*Options);
            }

            if (KeyScreen)
            {
                Access::StopCapture(*KeyScreen);
                Access::ObserveRebind(*KeyScreen, {});
                KeyScreen->DeactivateWidget();
                if (bKeySlateConstructed)
                {
                    ScreenLife.Destruct(*KeyScreen);
                }
            }

            if (OptionsScreen)
            {
                OptionsScreen->DeactivateWidget();
                ScreenLife.Destruct(*OptionsScreen);
            }

            Row.Shutdown();
            if (Row.LocalPlayer)
            {
                Row.LocalPlayer->ViewportClient = nullptr;
            }

            WidgetViewport.Reset();
            WidgetGI.Reset();
        }

        void DestructKeySlate()
        {
            ScreenLife.Destruct(*KeyScreen);
            bKeySlateConstructed = false;
        }
    };
    void Reply(UVBUIMessagingSubsystem& Bus, FVBConfirmationHandle Handle, EVBConfirmationResult Result)
    {
        if (Result == EVBConfirmationResult::Cancelled)
        {
            Bus.Cancel(Handle);
        }
        else
        {
            Bus.Answer(Handle, Result);
        }
    }

    FVBConfirmationRequest Request(const TCHAR* Body)
    {
        FVBConfirmationRequest Value; Value.Body = FText::FromString(Body); return Value;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationDiscardTest, "VB.Unit.UIMessaging.Screens.Discard", VBConfirmationScreenTests::Flags)
bool FVBConfirmationDiscardTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    for (auto Result : {EVBConfirmationResult::Confirmed, EVBConfirmationResult::Declined,
        EVBConfirmationResult::Cancelled, EVBConfirmationResult::Failed})
    {
        FFixture F;
        if (!F.Initialize(*this))
        {
            return false;
        }

        Access::Dirty(*F.OptionsScreen);
        const auto Dirty = Access::Draft(*F.OptionsScreen);
        Access::Back(*F.OptionsScreen);
        const auto Handle = Access::OptionHandle(*F.OptionsScreen);
        if (!TestTrue(TEXT("Discard/SharedQuestionPending"), F.Bus->IsPending(Handle)))
        {
            return false;
        }

        Reply(*F.Bus, Handle, Result);
        TestFalse(TEXT("Discard/QuestionResolved"), F.Bus->IsPending(Handle));
        TestFalse(TEXT("Discard/RequesterUnlocked"), Access::Asking(*F.OptionsScreen));
        TestEqual(TEXT("Discard/OnlyConfirmLeaves"), F.OptionsScreen->IsActivated(), Result != EVBConfirmationResult::Confirmed);
        TestTrue(TEXT("Discard/DoesNotCommitOrRewriteDraft"), Access::Draft(*F.OptionsScreen) == Dirty);
        TestEqual(TEXT("Discard/NoSettingsCommit"), F.Commits, 0);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationOptionsLocksTest, "VB.Unit.UIMessaging.Screens.OptionsLocks", VBConfirmationScreenTests::Flags)
bool FVBConfirmationOptionsLocksTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    Access::Dirty(*F.OptionsScreen);
    Access::Back(*F.OptionsScreen);
    if (!TestTrue(TEXT("Locks/Asking"), Access::Asking(*F.OptionsScreen)))
    {
        return false;
    }

    const auto Handle = Access::OptionHandle(*F.OptionsScreen);
    const auto OriginalTab = Access::Tab(*F.OptionsScreen);
    Access::SelectTab(*F.OptionsScreen, EVBOptionTab::Graphics);
    TestTrue(TEXT("Locks/TabUnchanged"), Access::Tab(*F.OptionsScreen) == OriginalTab);
    if (!TestTrue(TEXT("Locks/StepHasAnotherVisibleTab"), Access::HasMultipleVisibleTabs(*F.OptionsScreen)))
    {
        return false;
    }

    Access::StepTab(*F.OptionsScreen);
    TestTrue(TEXT("Locks/StepUnchanged"), Access::Tab(*F.OptionsScreen) == OriginalTab);
    int32 Sets = 0;
    FVBOptionEntry Entry;
    Entry.Id = TEXT("U04_LockSentinel");
    Entry.Get = [](const FVBOptionsDraft& Draft) { return Draft.User.MasterVolume; };
    Entry.Set = [&Sets](FVBOptionsDraft& Draft, float Value) { ++Sets; Draft.User.MasterVolume = Value; };
    Access::Entry(*F.OptionsScreen, Entry);
    Access::RowValue(*F.OptionsScreen);
    TestEqual(TEXT("Locks/RowSetterNotCalled"), Sets, 0);
    Access::Reset(*F.OptionsScreen);
    TestEqual(TEXT("Locks/ResetSetterNotCalled"), Sets, 0);
    Access::Apply(*F.OptionsScreen);
    TestEqual(TEXT("Locks/NoCommit"), F.Commits, 0);
    Entry.Id = VBOptionCatalog::AutoDetect;
    Access::Entry(*F.OptionsScreen, Entry);
    Access::RowAction(*F.OptionsScreen);
    TestEqual(TEXT("Locks/NoAutoDetect"), F.AutoDetects, 0);
    int32 KeyBindingPushes = 0;
    const auto PushListener = F.UI->OnScreenPushEnded.AddLambda([&](FGameplayTag Tag, EVBScreenPushEnd)
    {
        if (Tag == VBGameplayTags::UI_Screen_KeyBindings)
        {
            ++KeyBindingPushes;
        }
    });
    ON_SCOPE_EXIT { F.UI->OnScreenPushEnded.Remove(PushListener); };
    Entry.Id = VBOptionCatalog::KeyBindings;
    Access::Entry(*F.OptionsScreen, Entry);
    Access::RowAction(*F.OptionsScreen);
    TestEqual(TEXT("Locks/NoKeyBindingsPush"), KeyBindingPushes, 0);
    Access::Back(*F.OptionsScreen);
    TestTrue(TEXT("Locks/BackKeepsSameQuestion"), Access::OptionHandle(*F.OptionsScreen) == Handle && F.Bus->IsPending(Handle));
    F.Bus->Cancel(Handle);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationVideoTest, "VB.Unit.UIMessaging.Screens.KeepVideo", VBConfirmationScreenTests::Flags)
bool FVBConfirmationVideoTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    for (auto Result : {EVBConfirmationResult::Confirmed, EVBConfirmationResult::Declined,
        EVBConfirmationResult::Cancelled, EVBConfirmationResult::Failed})
    {
        FFixture F;
        if (!F.Initialize(*this))
        {
            return false;
        }

        Access::Preview(*F.Options);
        Access::AskVideo(*F.OptionsScreen);
        const auto Handle = Access::OptionHandle(*F.OptionsScreen);
        if (!TestTrue(TEXT("Video/SharedQuestionPending"), F.Bus->IsPending(Handle)))
        {
            return false;
        }

        Reply(*F.Bus, Handle, Result);
        if (!TestEqual(TEXT("Video/ExactlyOnePendingPreviewDecision"), F.VideoDecisions.Num(), 1))
        {
            return false;
        }

        if (Result == EVBConfirmationResult::Confirmed)
        {
            TestTrue(TEXT("Video/ConfirmKeeps"), F.VideoDecisions[0]);
        }
        else
        {
            TestFalse(TEXT("Video/RevertOnNonConfirm"), F.VideoDecisions[0]);
        }

        TestFalse(TEXT("Video/DecisionClearsPendingPreview"), F.Options->HasPendingVideoMode());
    }

    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    Access::Preview(*F.Options);
    Access::AskVideo(*F.OptionsScreen);
    const auto Handle = Access::OptionHandle(*F.OptionsScreen);
    Access::Dirty(*F.OptionsScreen);
    const auto DraftBeforeClose = Access::Draft(*F.OptionsScreen);
    F.OptionsScreen->DeactivateWidget();
    TestFalse(TEXT("Video/DeactivationCancelsQuestion"), F.Bus->IsPending(Handle));
    TestTrue(TEXT("Video/DeactivationRevertsWhileInactive"),
        F.VideoDecisions.Num() == 1 && !F.VideoDecisions[0] && F.bDecisionWhileInactive);
    TestTrue(TEXT("Video/InactiveDraftNotRefreshed"), Access::Draft(*F.OptionsScreen) == DraftBeforeClose);
    F.Bus->Answer(Handle, EVBConfirmationResult::Confirmed);
    TestEqual(TEXT("Video/LateConfirmCannotKeepAfterClose"), F.VideoDecisions.Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationDeadlineTest, "VB.Unit.UIMessaging.Screens.QueuedVideoDeadline", VBConfirmationScreenTests::Flags)
bool FVBConfirmationDeadlineTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    const auto First = F.Bus->ShowConfirmation(Request(TEXT("Another owner's question")), FVBConfirmationResultDelegate());
    Access::Preview(*F.Options);
    Access::AskVideo(*F.OptionsScreen);
    const auto Queued = Access::OptionHandle(*F.OptionsScreen);
    if (!TestTrue(TEXT("Deadline/PreviewQuestionQueued"), Queued.IsValid() && F.Bus->IsPending(Queued)))
    {
        return false;
    }

    FVBConfirmationHandle Presented;
    F.Bus->GetPresented(Presented);
    TestTrue(TEXT("Deadline/OtherOwnerStillHead"), Presented == First && !(Presented == Queued));
    Access::Tick(*F.OptionsScreen); // update body while queued, without restarting preview
    Access::Expire(*F.Options); // owner's actual deadline callback, engine apply/save intercepted
    TestFalse(TEXT("Deadline/OwnerExpiredPreview"), F.Options->HasPendingVideoMode());
    Access::Tick(*F.OptionsScreen);
    TestFalse(TEXT("Deadline/QueuedQuestionWithdrawn"), F.Bus->IsPending(Queued));
    TestFalse(TEXT("Deadline/ScreenUnlocked"), Access::Asking(*F.OptionsScreen));
    TestTrue(TEXT("Deadline/OneRealPreviewRevert"), F.VideoDecisions.Num() == 1 && !F.VideoDecisions[0]);
    F.Bus->GetPresented(Presented);
    TestTrue(TEXT("Deadline/OtherOwnerUnchanged"), Presented == First && F.Bus->IsPending(First));
    F.Bus->Answer(Queued, EVBConfirmationResult::Confirmed);
    TestEqual(TEXT("Deadline/LateQueuedAnswerCannotKeep"), F.VideoDecisions.Num(), 1);
    F.Bus->Cancel(First);
    F.Bus->GetPresented(Presented);
    TestFalse(TEXT("Deadline/NoExpiredQuestionLeftToPresent"), Presented.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationFailedVideoTest, "VB.Unit.UIMessaging.Screens.FailedKeepVideo", VBConfirmationScreenTests::Flags)
bool FVBConfirmationFailedVideoTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Bus->NotifyDialogDeactivated();
    Access::Preview(*F.Options);
    int32 FailureObservations = 0;
    const auto FailureObserver = F.Bus->OnPresentedChanged.AddLambda([&]
    {
        ++FailureObservations;
        TestTrue(TEXT("FailedVideo/LockedBeforeSynchronousAskReturns"), Access::Asking(*F.OptionsScreen));
        Access::Apply(*F.OptionsScreen);
        Access::Reset(*F.OptionsScreen);
        Access::Back(*F.OptionsScreen);
    });
    ON_SCOPE_EXIT { F.Bus->OnPresentedChanged.Remove(FailureObserver); };
    AddExpectedMessagePlain(TEXT("[VBUI]"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
    Access::AskVideo(*F.OptionsScreen);
    TestEqual(TEXT("FailedVideo/OneSynchronousFailureObservation"), FailureObservations, 1);
    TestEqual(TEXT("FailedVideo/NoCommitDuringSynchronousAsk"), F.Commits, 0);
    TestFalse(TEXT("FailedVideo/SynchronousFailureReturnsInvalidHandle"), Access::OptionHandle(*F.OptionsScreen).IsValid());
    TestFalse(TEXT("FailedVideo/NotLocked"), Access::Asking(*F.OptionsScreen));
    TestFalse(TEXT("FailedVideo/PreviewReverted"), F.Options->HasPendingVideoMode());
    TestTrue(TEXT("FailedVideo/RevertedExactlyOnce"), F.VideoDecisions.Num() == 1 && !F.VideoDecisions[0]);
    TestTrue(TEXT("FailedVideo/OptionsScreenStaysActive"), F.OptionsScreen->IsActivated());
    return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationKeyRequestTest, "VB.Unit.UIMessaging.Screens.KeyConflict", VBConfirmationScreenTests::Flags)
bool FVBConfirmationKeyRequestTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    FFixture F;
    if (!F.Initialize(*this, true) || !F.AskConflict(*this))
    {
        return false;
    }

    TestFalse(TEXT("Key/CaptureStopped"), Access::Capturing(*F.KeyScreen));
    TestTrue(TEXT("Key/StateConfirming"), Access::Confirming(*F.KeyScreen));
    FVBConfirmationHandle Presented;
    const auto* Asked = F.Bus->GetPresented(Presented);
    if (!TestNotNull(TEXT("Key/SharedRequest"), Asked))
    {
        return false;
    }

    TestTrue(TEXT("Key/SafeDefaultDecline"), Asked->bFocusDecline);
    TestTrue(TEXT("Key/ScreenHandleMatchesBroker"), Presented == Access::KeyHandle(*F.KeyScreen));
    F.ExpectRebuild(*this);
    F.Bus->Answer(Presented, EVBConfirmationResult::Declined);
    TestEqual(TEXT("Key/DeclineNeverRebinds"), F.Rebinds, 0);
    TestTrue(TEXT("Key/DeclineReturnsBrowsing"), Access::Browsing(*F.KeyScreen));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationCaptureOrderTest, "VB.Unit.UIMessaging.Screens.CaptureBeforeFailedAsk", VBConfirmationScreenTests::Flags)
bool FVBConfirmationCaptureOrderTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    FFixture F;
    if (!F.Initialize(*this, true))
    {
        return false;
    }

    F.Bus->NotifyDialogDeactivated(); // no requests: ends the stand-in adapter, leaves Idle
    bool bSawFailure = false, bStoppedDuringAsk = false;
    const auto Listener = F.UI->OnScreenPushEnded.AddLambda([&](FGameplayTag Tag, EVBScreenPushEnd Reason)
    {
        if (Tag == VBGameplayTags::UI_Screen_Confirmation && Reason == EVBScreenPushEnd::Failed)
        {
            bSawFailure = true;
            bStoppedDuringAsk = !Access::Capturing(*F.KeyScreen);
        }
    });
    ON_SCOPE_EXIT { F.UI->OnScreenPushEnded.Remove(Listener); };
    // 실제 route 누락 또는 viewport-layout 누락 경고, 그다음 broker FailAll 경고.
    AddExpectedMessagePlain(TEXT("[VBUI]"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
    F.ExpectRebuild(*this);
    // AskConflict 는 성공을 단언하므로 그 단언 없이 같은 실제 capture 경로를 만든다.
    FVBKeyBindingRow A; A.MappingName = TEXT("U04_TestEdit"); A.Key = EKeys::V;
    FVBKeyBindingRow B; B.MappingName = TEXT("U04_TestOccupied"); B.Key = EKeys::B;
    Access::KeyboardForFixture(*F.KeyScreen);
    Access::KeyRows(*F.KeyScreen, {A, B});
    Access::SelectRow(*F.KeyScreen);
    if (!TestTrue(TEXT("Capture/RealCaptureStarted"), Access::Capturing(*F.KeyScreen)))
    {
        return false;
    }

    F.KeyScreen->HandleCapturedKey(EKeys::B);
    TestTrue(TEXT("Capture/RealPushFailedSynchronously"), bSawFailure);
    TestTrue(TEXT("Capture/StoppedAtRealPushFailure"), bStoppedDuringAsk);
    TestFalse(TEXT("Capture/FailedAskNoPendingHandle"), F.Bus->IsPending(Access::KeyHandle(*F.KeyScreen)));
    TestTrue(TEXT("Capture/FailedAskReturnsBrowsing"), Access::Browsing(*F.KeyScreen));
    TestEqual(TEXT("Capture/FailedAskDoesNotRebind"), F.Rebinds, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationWithdrawTest, "VB.Unit.UIMessaging.Screens.KeyWithdraw", VBConfirmationScreenTests::Flags)
bool FVBConfirmationWithdrawTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    for (int32 Path = 0; Path < 5; ++Path)
    {
        FFixture F;
        if (!F.Initialize(*this, true) || !F.AskConflict(*this))
        {
            return false;
        }

        const auto Handle = Access::KeyHandle(*F.KeyScreen);
        if (Path == 0)
        {
            F.ExpectRebuild(*this);
            UButton* Button = Cast<UButton>(F.KeyScreen->GetWidgetFromName(TEXT("BtnGamepadTab")));
            if (!TestNotNull(TEXT("Withdraw/BoundTabButton"), Button))
            {
                return false;
            }

            Button->OnClicked.Broadcast();
        }
        else if (Path == 1)
        {
            F.ExpectRebuild(*this);
            TestTrue(TEXT("Withdraw/BackConsumed"), Access::Back(*F.KeyScreen));
        }
        else if (Path == 2)
        {
            UButton* Button = Cast<UButton>(F.KeyScreen->GetWidgetFromName(TEXT("BtnClose")));
            if (!TestNotNull(TEXT("Withdraw/BoundCloseButton"), Button))
            {
                return false;
            }

            Button->OnClicked.Broadcast();
        }
        else if (Path == 3)
        {
            F.KeyScreen->DeactivateWidget();
        }
        else
        {
            F.DestructKeySlate();
        }

        TestFalse(FString::Printf(TEXT("Withdraw/QuestionCancelled/Path%d"), Path), F.Bus->IsPending(Handle));
        TestFalse(FString::Printf(TEXT("Withdraw/CaptureGone/Path%d"), Path), Access::Capturing(*F.KeyScreen));
        TestTrue(FString::Printf(TEXT("Withdraw/Browsing/Path%d"), Path), Access::Browsing(*F.KeyScreen));
        F.Bus->Answer(Handle, EVBConfirmationResult::Confirmed);
        TestEqual(FString::Printf(TEXT("Withdraw/LateClickCannotRebind/Path%d"), Path), F.Rebinds, 0);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationRecheckTest, "VB.Unit.UIMessaging.Screens.KeyRecheck", VBConfirmationScreenTests::Flags)
bool FVBConfirmationRecheckTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    for (int32 Change = 0; Change < 3; ++Change)
    {
        FFixture F;
        if (!F.Initialize(*this, true) || !F.AskConflict(*this))
        {
            return false;
        }

        const auto Handle = Access::KeyHandle(*F.KeyScreen);
        if (Change == 0)
        {
            Access::DifferentDeviceWithoutWithdraw(*F.KeyScreen);
        }
        else if (Change == 1) Access::KeyRows(*F.KeyScreen, {});
        else
        {
            FVBKeyBindingRow Other; Other.MappingName = TEXT("U04_NewIndexZero"); Other.Key = EKeys::N;
            FVBKeyBindingRow Saved; Saved.MappingName = TEXT("U04_TestEdit"); Saved.Key = EKeys::V;
            Access::KeyRows(*F.KeyScreen, {Other, Saved}); // original EditingRow now names another row
        }
        // 받는 시점의 가드만 보려고 Change 0 에서 tab-withdraw 를 일부러 건너뛴다.
        F.ExpectRebuild(*this);
        F.Bus->Answer(Handle, EVBConfirmationResult::Confirmed);
        if (Change == 0)
        {
            TestEqual(TEXT("Recheck/ChangedDeviceIgnored"), F.Rebinds, 0);
        }
        else if (Change == 1)
        {
            TestEqual(TEXT("Recheck/MissingNameIgnored"), F.Rebinds, 0);
        }
        else
        {
            TestEqual(TEXT("Recheck/ExactlyOneRebind"), F.Rebinds, 1);
            TestTrue(TEXT("Recheck/UsesSavedMappingName"), F.LastMapping == TEXT("U04_TestEdit"));
            TestTrue(TEXT("Recheck/UsesSavedKeyAndDevice"), F.LastKey == EKeys::B && F.LastDevice == EVBKeyDevice::KeyboardMouse);
            F.Bus->Answer(Handle, EVBConfirmationResult::Confirmed);
            TestEqual(TEXT("Recheck/RepeatedAnswerDoesNotRebind"), F.Rebinds, 1);
        }
    }

    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationKeyNonConfirmTest, "VB.Unit.UIMessaging.Screens.KeyNonConfirm", VBConfirmationScreenTests::Flags)
bool FVBConfirmationKeyNonConfirmTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    for (auto Result : {EVBConfirmationResult::Cancelled, EVBConfirmationResult::Failed})
    {
        FFixture F;
        if (!F.Initialize(*this, true) || !F.AskConflict(*this))
        {
            return false;
        }

        const auto Handle = Access::KeyHandle(*F.KeyScreen);
        F.ExpectRebuild(*this);
        Reply(*F.Bus, Handle, Result);
        TestEqual(TEXT("KeyNonConfirm/NoRebind"), F.Rebinds, 0);
        TestTrue(TEXT("KeyNonConfirm/Browsing"), Access::Browsing(*F.KeyScreen));
        TestFalse(TEXT("KeyNonConfirm/QuestionResolved"), F.Bus->IsPending(Handle));
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBConfirmationKeyQueuedWithdrawTest, "VB.Unit.UIMessaging.Screens.KeyQueuedWithdraw", VBConfirmationScreenTests::Flags)
bool FVBConfirmationKeyQueuedWithdrawTest::RunTest(const FString& Parameters)
{
    using namespace VBConfirmationScreenTests;
    FFixture F;
    if (!F.Initialize(*this, true))
    {
        return false;
    }

    const auto First = F.Bus->ShowConfirmation(Request(TEXT("Another owner's question")), FVBConfirmationResultDelegate());
    if (!TestTrue(TEXT("KeyQueue/OtherQuestionPending"), F.Bus->IsPending(First)))
    {
        return false;
    }

    if (!F.AskConflict(*this))
    {
        return false;
    }

    const auto Queued = Access::KeyHandle(*F.KeyScreen);
    FVBConfirmationHandle Presented;
    F.Bus->GetPresented(Presented);
    TestTrue(TEXT("KeyQueue/ConflictWaitsBehindOther"), Presented == First && !(Presented == Queued));
    F.KeyScreen->DeactivateWidget();
    TestFalse(TEXT("KeyQueue/ConflictWithdrawn"), F.Bus->IsPending(Queued));
    F.Bus->GetPresented(Presented);
    TestTrue(TEXT("KeyQueue/OtherOwnerUnchanged"), Presented == First && F.Bus->IsPending(First));
    F.Bus->Answer(Queued, EVBConfirmationResult::Confirmed);
    TestEqual(TEXT("KeyQueue/LateConfirmCannotRebind"), F.Rebinds, 0);
    F.Bus->Cancel(First);
    F.Bus->GetPresented(Presented);
    TestFalse(TEXT("KeyQueue/NoWithdrawnQuestionLeftToPresent"), Presented.IsValid());
    return true;
}

#endif
