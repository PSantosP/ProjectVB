// 로딩 화면 subsystem 의 episode 판단 시험 - 초기화하지 않은 GI 를 outer 로 쓰고, headless 모드는 Show 성공을 흉내 낸다
// 위젯 부착, 입력 가로채기, fade, 렌더링, movie thread 는 증명하지 않는다. 실제 전역 delegate 를 쏘므로 한가한 headless -game 에서만 돌린다
// 각 행은 해당 결함을 넣으면 실패하는 반대 대조이고, Settings CDO 덮어쓰기는 범위 안에서 되돌리며 config 에 저장하지 않는다

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Data/VBGameFlowSettings.h"
#include "Player/VBPlayerController.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Subsystems/SubsystemCollection.h"
#include "UI/Loading/VBLoadingScreenSubsystem.h"
#include "UI/VBUISettings.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "VBLoadingScreenTestProbe.h"

namespace VBLoadingScreenMovieTests
{
    // 엔진 계약 - Wait 는 prepared 와 playing 이 둘 다일 때만 thread 를 없애고 Finished 를 낸다 (DefaultGameMoviePlayer.cpp)
    // Clear 는 일부러 재생을 멈추지 않으므로, 무조건 Clear 하는 결함은 흉내 thread 를 새게 해 정리 검사에서 실패한다
    struct FMovieState
    {
        bool bAvailable = false;
        bool bPrepared = false;
        bool bPlaying = false;
        bool bThreadAlive = false;
        int32 Installs = 0;
        int32 Clears = 0;
        int32 Starts = 0;
        int32 Waits = 0;
        int32 CleanupBranches = 0;
        int32 WindowOnlyBranches = 0;
        int32 Finishes = 0;
        int32 Unbinds = 0;
        TFunction<void()> OnStarted;
        TFunction<void()> OnFinished;
        TSharedPtr<SWidget> PreparedWidget;
        TSharedPtr<SWidget> PlayingWidget;

        void EnginePre()
        {
            if (!bAvailable || !bPrepared || bPlaying)
            {
                return;
            }

            bPlaying = bThreadAlive = true;
            PlayingWidget = PreparedWidget;
            ++Starts;
            if (OnStarted)
            {
                OnStarted();
            }
        }

        void EnginePost()
        {
            ++Waits;
            if (bPrepared && bPlaying)
            {
                ++CleanupBranches;
                bThreadAlive = bPlaying = bPrepared = false;
                PlayingWidget.Reset();
                PreparedWidget.Reset();
                ++Finishes;
                if (OnFinished)
                {
                    OnFinished();
                }
            }
            else
            {
                ++WindowOnlyBranches; // does not clean up the thread or emit Finished
            }
        }
    };

    class FFakeMoviePort final : public IVBLoadingMoviePort
    {
    public:
        explicit FFakeMoviePort(TSharedRef<FMovieState> InState) : State(InState) {}
        virtual bool IsAvailable() const override { return State->bAvailable; }
        virtual bool IsPlaying() const override { return State->bPlaying; }
        virtual bool IsPrepared() const override { return State->bPrepared; }
        virtual void Install(const TSharedRef<SWidget>& Widget) override
        {
            ++State->Installs;
            State->PreparedWidget = Widget;
            State->bPrepared = true;
        }

        virtual void Clear() override
        {
            ++State->Clears;
            State->bPrepared = false;
            State->PreparedWidget.Reset();
        }

        virtual void Bind(TFunction<void()> Started, TFunction<void()> Finished) override
        {
            State->OnStarted = MoveTemp(Started);
            State->OnFinished = MoveTemp(Finished);
        }

        virtual void Unbind() override
        {
            ++State->Unbinds;
            State->OnStarted = {};
            State->OnFinished = {};
        }
    private:
        TSharedRef<FMovieState> State;
    };
}

// 제품 쪽 friend 선언이 이 이름을 그대로 가리키므로 전역에 둔다
class FVBLoadingScreenTestAccess
{
public:
    static void Configure(UVBLoadingScreenSubsystem& Owner, TFunction<double()> Clock)
    {
        Owner.NowForTests = MoveTemp(Clock);
        Owner.bHeadlessForTests = true;
        // episode 시험은 이 프로세스를 띄운 명령줄과 무관해야 한다
        Owner.NoLoadingScreenForTests = false;
    }

    static void SetHeadless(UVBLoadingScreenSubsystem& Owner, bool bHeadless)
    {
        Owner.bHeadlessForTests = bHeadless;
    }

    static bool CanDisplay(const UVBLoadingScreenSubsystem& Owner) { return Owner.CanDisplayNow(); }
    static void SuppressPresentation(UVBLoadingScreenSubsystem& Owner, bool bSuppress)
    {
        Owner.NoLoadingScreenForTests = bSuppress;
    }

    static bool MapLoading(const UVBLoadingScreenSubsystem& Owner) { return Owner.bMapLoading; }
    static bool EpisodeActive(const UVBLoadingScreenSubsystem& Owner) { return Owner.bEpisodeActive; }
    static bool EpisodeShown(const UVBLoadingScreenSubsystem& Owner) { return Owner.bEpisodeShown; }
    static bool Abandoned(const UVBLoadingScreenSubsystem& Owner) { return Owner.bEpisodeAbandoned; }
    static double HoldUntil(const UVBLoadingScreenSubsystem& Owner) { return Owner.HoldUntilSeconds; }
    static double EpisodeStart(const UVBLoadingScreenSubsystem& Owner) { return Owner.EpisodeStartSeconds; }
    static int32 ProcessCount(const UVBLoadingScreenSubsystem& Owner) { return Owner.Processes.Num(); }
    static bool HasWidget(const UVBLoadingScreenSubsystem& Owner) { return Owner.Widget.IsValid(); }
    static bool HasInputBlocker(const UVBLoadingScreenSubsystem& Owner) { return Owner.InputBlocker.IsValid(); }
    static bool StartPhase(const UVBLoadingScreenSubsystem& Owner) { return Owner.bStartPhase; }
    static bool MovieInstalled(const UVBLoadingScreenSubsystem& Owner) { return Owner.bMovieInstalledByMe; }
    static bool MovieStarted(const UVBLoadingScreenSubsystem& Owner) { return Owner.bMovieStarted; }
    static TSharedPtr<SWidget> Widget(const UVBLoadingScreenSubsystem& Owner) { return Owner.Widget; }
    static void SetMoviePort(UVBLoadingScreenSubsystem& Owner, TUniquePtr<IVBLoadingMoviePort> Port)
    {
        if (Owner.MoviePort)
        {
            Owner.MoviePort->Unbind();
        }

        Owner.MoviePort = MoveTemp(Port);
        Owner.BindMoviePort();
    }
};

namespace VBLoadingScreenTests
{
    using Access = FVBLoadingScreenTestAccess;
    constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

    struct FSettingsOverride
    {
        UVBUISettings* Settings = GetMutableDefault<UVBUISettings>();
        float Hold = Settings->LoadingHoldSeconds;
        float Max = Settings->LoadingMaxShowSeconds;
        bool bEditorDisplay = Settings->bShowLoadingScreenInEditor;
        float Fade = Settings->LoadingFadeInSeconds;
        FSettingsOverride()
        {
            Settings->LoadingHoldSeconds = 0.0f;
            Settings->LoadingMaxShowSeconds = 60.0f;
            Settings->bShowLoadingScreenInEditor = false;
        }

        ~FSettingsOverride()
        {
            Settings->LoadingHoldSeconds = Hold;
            Settings->LoadingMaxShowSeconds = Max;
            Settings->bShowLoadingScreenInEditor = bEditorDisplay;
            Settings->LoadingFadeInSeconds = Fade;
        }
    };

    struct FFixture
    {
        FSettingsOverride Settings;
        double Time = 100.0;
        TStrongObjectPtr<UGameInstance> GI;
        TStrongObjectPtr<UVBLoadingScreenSubsystem> Owner;
        FSubsystemCollection<UGameInstanceSubsystem> Collection;
        bool bInitialized = false;
        TSharedRef<VBLoadingScreenMovieTests::FMovieState> Movie = MakeShared<VBLoadingScreenMovieTests::FMovieState>();

        bool Initialize(FAutomationTestBase& Test)
        {
            if (!Test.TestTrue(TEXT("Fixture/GameThread"), IsInGameThread()))
            {
                return false;
            }

            GI.Reset(NewObject<UVBLoadingScreenTestGameInstance>(GetTransientPackage(), NAME_None, RF_Transient));
            Owner.Reset(NewObject<UVBLoadingScreenSubsystem>(GI.Get(), NAME_None, RF_Transient));
            if (!Test.TestNotNull(TEXT("Fixture/ManualOwner"), Owner.Get()))
            {
                return false;
            }

            Access::Configure(*Owner, [this]() { return Time; });
            // Initialize 인자일 뿐 GI 에 등록된 subsystem collection 이 아니다
            Owner->Initialize(Collection);
            Access::SetMoviePort(*Owner, MakeUnique<VBLoadingScreenMovieTests::FFakeMoviePort>(Movie));
            bInitialized = true;
            return Test.TestFalse(TEXT("Fixture/InitiallyIdle"), Owner->IsTickable());
        }

        void Deinitialize()
        {
            if (bInitialized)
            {
                Owner->Deinitialize();
                bInitialized = false;
            }
        }

        ~FFixture()
        {
            Deinitialize();
            if (Owner) Access::Configure(*Owner, {}); // no lambda may outlive Time
        }

        void At(double NewTime)
        {
            Time = NewTime;
            Owner->Tick(0.0f); // production Tick; clock is independent of DeltaTime
        }
    };

    TStrongObjectPtr<UVBLoadingScreenTestProbe> MakeProbe(const TCHAR* Name)
    {
        return TStrongObjectPtr<UVBLoadingScreenTestProbe>(
            NewObject<UVBLoadingScreenTestProbe>(GetTransientPackage(),
                MakeUniqueObjectName(GetTransientPackage(), UVBLoadingScreenTestProbe::StaticClass(), FName(Name)), RF_Transient));
    }

    TStrongObjectPtr<UWorld> MakeIdentityWorld(UGameInstance* GI)
    {
        TStrongObjectPtr<UWorld> World(NewObject<UWorld>(GetTransientPackage(), NAME_None, RF_Transient));
        World->SetGameInstance(GI);
        return World; // never InitWorld/CreateWorld/BeginPlay or insert in GEngine contexts
    }

    void BroadcastPre(UGameInstance* GI, const FString& MapName = TEXT("/Game/U06aTest_NotLoaded"))
    {
        FWorldContext Context;
        Context.WorldType = EWorldType::Game;
        Context.OwningGameInstance = GI;
        FCoreUObjectDelegates::PreLoadMapWithContext.Broadcast(Context, MapName);
    }

    bool CanBroadcastMapSignals(FAutomationTestBase& Test)
    {
        if (!GEngine)
        {
            return true;
        }

        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if (Context.WorldType == EWorldType::PIE)
            {
                Test.AddError(TEXT("Reasons requires an isolated headless -game process; global map signals must not run during PIE."));
                return false;
            }

            if (const UGameInstance* GI = Context.OwningGameInstance)
            {
                if (const auto* LiveOwner = GI->GetSubsystem<UVBLoadingScreenSubsystem>())
                {
                    if (Access::MapLoading(*LiveOwner))
                    {
                        Test.AddError(TEXT("Reasons refuses global null Post while another loading owner is opening a map."));
                        return false;
                    }
                }
            }
        }

        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingReasonsTest, "VB.Unit.LoadingScreen.Reasons",
    VBLoadingScreenTests::Flags)
bool FVBLoadingReasonsTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    TStrongObjectPtr<UGameInstance> OtherGI(NewObject<UGameInstance>());
    auto MyWorld = MakeIdentityWorld(F.GI.Get());
    auto OtherWorld = MakeIdentityWorld(OtherGI.Get());

    BroadcastPre(OtherGI.Get());
    TestFalse(TEXT("Reasons/ForeignPreIgnored"), Access::MapLoading(*F.Owner));
    TestFalse(TEXT("Reasons/ForeignPreDoesNotShow"), F.Owner->IsShowing());
    TestFalse(TEXT("Reasons/ForeignPreRemainsIdle"), F.Owner->IsTickable());
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(OtherWorld.Get());
    TestFalse(TEXT("Reasons/ForeignPostWhileIdleIgnored"), F.Owner->IsShowing());

    BroadcastPre(F.GI.Get());
    TestTrue(TEXT("Reasons/OwnPreStartsLoading"), Access::MapLoading(*F.Owner));
    TestTrue(TEXT("Reasons/OwnPreShowsSynchronously"), F.Owner->IsShowing());
    TestTrue(TEXT("Reasons/MapReasonReported"), !F.Owner->GetCurrentReason().IsEmpty());
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(OtherWorld.Get());
    TestTrue(TEXT("Reasons/ForeignPostCannotEndOwnLoad"), Access::MapLoading(*F.Owner));
    TestTrue(TEXT("Reasons/ForeignPostKeepsShowing"), F.Owner->IsShowing());
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(MyWorld.Get());
    TestFalse(TEXT("Reasons/OwnPostEndsLoading"), Access::MapLoading(*F.Owner));
    TestFalse(TEXT("Reasons/OwnPostHidesWithoutHold"), F.Owner->IsShowing());
    TestTrue(TEXT("Reasons/OwnPostClearsReason"), F.Owner->GetCurrentReason().IsEmpty());

    // 실제 null broadcast - 심각도 Warning, 정확히 한 번인지 본다
    BroadcastPre(F.GI.Get());
    AddExpectedMessagePlain(TEXT("Post"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 1);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(nullptr);
    TestFalse(TEXT("Reasons/NullPostEndsLoading"), Access::MapLoading(*F.Owner));
    TestFalse(TEXT("Reasons/NullPostHides"), F.Owner->IsShowing());
    TestFalse(TEXT("Reasons/NullPostReturnsIdle"), F.Owner->IsTickable());
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(nullptr); // no second warning while idle

    TestTrue(TEXT("Reasons/PreDelegateWasBound"), FCoreUObjectDelegates::PreLoadMapWithContext.IsBoundToObject(F.Owner.Get()));
    TestTrue(TEXT("Reasons/PostDelegateWasBound"), FCoreUObjectDelegates::PostLoadMapWithWorld.IsBoundToObject(F.Owner.Get()));
    F.Deinitialize();
    TestFalse(TEXT("Reasons/PreDelegateRemoved"), FCoreUObjectDelegates::PreLoadMapWithContext.IsBoundToObject(F.Owner.Get()));
    TestFalse(TEXT("Reasons/PostDelegateRemoved"), FCoreUObjectDelegates::PostLoadMapWithWorld.IsBoundToObject(F.Owner.Get()));
    BroadcastPre(F.GI.Get());
    TestFalse(TEXT("Reasons/DeinitUnbindsActualPreDelegate"), Access::MapLoading(*F.Owner));
    TestFalse(TEXT("Reasons/DeinitCannotReshow"), F.Owner->IsShowing());
    TestFalse(TEXT("Reasons/DeinitIdle"), F.Owner->IsTickable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingReasonsHoldTest, "VB.Unit.LoadingScreen.ReasonsHold",
    VBLoadingScreenTests::Flags)
bool FVBLoadingReasonsHoldTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Settings.Settings->LoadingHoldSeconds = 2.0f;
    auto Probe = MakeProbe(TEXT("ShownHoldProbe"));
    F.Owner->RegisterProcess(Probe.Get());
    F.At(100.0);
    TestTrue(TEXT("Hold/ShownWhileWanted"), F.Owner->IsShowing());
    TestTrue(TEXT("Hold/EpisodeActuallyMarkedShown"), Access::EpisodeShown(*F.Owner));
    TestTrue(TEXT("Hold/NamesProcessAndReason"),
        F.Owner->GetCurrentReason().Contains(Probe->GetName()) && F.Owner->GetCurrentReason().Contains(Probe->Reason));
    Probe->bWanted = false;
    F.At(101.0);
    TestTrue(TEXT("Hold/StillShownAfterReasonEnds"), F.Owner->IsShowing());
    TestEqual(TEXT("Hold/UsesInjectedRealTime"), Access::HoldUntil(*F.Owner), 103.0);
    TestTrue(TEXT("Hold/ReasonClearedDuringHold"), F.Owner->GetCurrentReason().IsEmpty());
    F.At(102.9);
    TestTrue(TEXT("Hold/BeforeDeadlineShown"), F.Owner->IsShowing());
    F.At(103.0);
    TestFalse(TEXT("Hold/AtDeadlineHidden"), F.Owner->IsShowing());
    F.Owner->UnregisterProcess(Probe.Get());
    TestFalse(TEXT("Hold/IdleAfterUnregister"), F.Owner->IsTickable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingUnshownHoldTest, "VB.Unit.LoadingScreen.UnshownHold",
    VBLoadingScreenTests::Flags)
bool FVBLoadingUnshownHoldTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Settings.Settings->LoadingHoldSeconds = 5.0f;
    F.Settings.Settings->bShowLoadingScreenInEditor = true;
    Access::SetHeadless(*F.Owner, false);
    TestFalse(TEXT("Unshown/NoViewportAvailable"), Access::CanDisplay(*F.Owner));
    auto Probe = MakeProbe(TEXT("UnshownProbe"));
    F.Owner->RegisterProcess(Probe.Get());
    F.At(100.0);
    TestTrue(TEXT("Unshown/ReasonEpisodeExists"), Access::EpisodeActive(*F.Owner));
    TestFalse(TEXT("Unshown/NoViewportCannotShow"), F.Owner->IsShowing());
    TestFalse(TEXT("Unshown/EpisodeNeverShown"), Access::EpisodeShown(*F.Owner));
    Probe->bWanted = false;
    F.At(101.0);
    TestFalse(TEXT("Unshown/EpisodeEnded"), Access::EpisodeActive(*F.Owner));
    TestEqual(TEXT("Unshown/NoHoldScheduled"), Access::HoldUntil(*F.Owner), 0.0);
    TestFalse(TEXT("Unshown/NoWidget"), Access::HasWidget(*F.Owner));
    TestFalse(TEXT("Unshown/NoInputBlocker"), Access::HasInputBlocker(*F.Owner));
    F.Owner->UnregisterProcess(Probe.Get());
    TestFalse(TEXT("Unshown/Idle"), F.Owner->IsTickable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingMaxShowTest, "VB.Unit.LoadingScreen.MaxShow",
    VBLoadingScreenTests::Flags)
bool FVBLoadingMaxShowTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Settings.Settings->LoadingMaxShowSeconds = 10.0f;
    F.Settings.Settings->LoadingHoldSeconds = 5.0f;
    auto Probe = MakeProbe(TEXT("MaxShowProbe"));
    Probe->Reason = TEXT("U06aMaxShowReason_FirstEpisode");
    F.Owner->RegisterProcess(Probe.Get());
    // 버려진 episode 가 tick 을 여러 번 더 받아도 Error 는 episode 마다 하나다
    AddExpectedMessagePlain(Probe->Reason, ELogVerbosity::Error,
        EAutomationExpectedMessageFlags::Contains, 1);
    F.At(100.0);
    TestTrue(TEXT("Max/InitiallyShown"), F.Owner->IsShowing());
    F.At(110.0);
    TestTrue(TEXT("Max/ExactLimitStillShown"), F.Owner->IsShowing());
    F.At(111.0);
    TestFalse(TEXT("Max/OverLimitHidden"), F.Owner->IsShowing());
    TestTrue(TEXT("Max/EpisodeAbandoned"), Access::Abandoned(*F.Owner));
    TestTrue(TEXT("Max/ReasonStillReported"), F.Owner->GetCurrentReason().Contains(Probe->Reason));
    F.At(112.0);
    F.At(115.0);
    TestFalse(TEXT("Max/SameEpisodeStaysHidden"), F.Owner->IsShowing());
    Probe->bWanted = false;
    F.At(116.0);
    TestFalse(TEXT("Max/EndedEpisodeInactive"), Access::EpisodeActive(*F.Owner));
    TestEqual(TEXT("Max/AbandonedEpisodeGetsNoHold"), Access::HoldUntil(*F.Owner), 0.0);

    Probe->bWanted = true;
    Probe->Reason = TEXT("U06aMaxShowReason_SecondEpisode");
    AddExpectedMessagePlain(Probe->Reason, ELogVerbosity::Error,
        EAutomationExpectedMessageFlags::Contains, 1);
    F.At(200.0);
    TestTrue(TEXT("Max/NewEpisodeReshows"), F.Owner->IsShowing());
    TestFalse(TEXT("Max/NewEpisodeNotAbandoned"), Access::Abandoned(*F.Owner));
    TestEqual(TEXT("Max/NewEpisodeHasFreshClock"), Access::EpisodeStart(*F.Owner), 200.0);
    F.At(205.0);
    TestTrue(TEXT("Max/NewEpisodeSurvivesWithinLimit"), F.Owner->IsShowing());
    F.At(211.0);
    TestFalse(TEXT("Max/NewEpisodeCanExpireIndependently"), F.Owner->IsShowing());
    F.At(212.0);
    Probe->bWanted = false;
    F.Owner->ReevaluateNow(true);
    F.Owner->UnregisterProcess(Probe.Get());
    TestFalse(TEXT("Max/IdleAfterEpisodeEnds"), F.Owner->IsTickable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingProcessLifetimeTest, "VB.Unit.LoadingScreen.ProcessLifetime",
    VBLoadingScreenTests::Flags)
bool FVBLoadingProcessLifetimeTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    auto Probe = MakeProbe(TEXT("WeakLoadingProbe"));
    const TWeakObjectPtr<UVBLoadingScreenTestProbe> WeakProbe(Probe.Get());
    F.Owner->RegisterProcess(Probe.Get());
    F.Owner->RegisterProcess(Probe.Get());
    TestEqual(TEXT("Process/DuplicateRegistrationIsUnique"), Access::ProcessCount(*F.Owner), 1);
    F.At(100.0);
    TestTrue(TEXT("Process/LiveObjectHolds"), F.Owner->IsShowing());

    // 강한 참조 반대 대조에서는 살아 있는 주인이 GC 를 버텨야 강한 참조가 process 를 붙잡는다 (명시적 파괴 꼼수 없음)
    Probe.Reset();
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("Process/Collected"), WeakProbe.IsValid());
    F.At(101.0);
    TestFalse(TEXT("Process/CollectedStopsHolding"), F.Owner->IsShowing());
    TestEqual(TEXT("Process/ExpiredEntryPruned"), Access::ProcessCount(*F.Owner), 0);
    TestFalse(TEXT("Process/CollectedReturnsIdle"), F.Owner->IsTickable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingProcessQueryTest, "VB.Unit.LoadingScreen.ProcessQuery",
    VBLoadingScreenTests::Flags)
bool FVBLoadingProcessQueryTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    auto World = MakeIdentityWorld(F.GI.Get());
    auto OldProcess = MakeProbe(TEXT("TearingDownProbe"));
    OldProcess->World = World.Get();
    World->bIsTearingDown = true;
    F.Owner->RegisterProcess(OldProcess.Get());
    F.At(100.0);
    TestFalse(TEXT("Process/TearingDownWorldCannotHold"), F.Owner->IsShowing());
    TestEqual(TEXT("Process/TearingDownWorldIsNotQueried"), OldProcess->QueryCalls, 0);
    World->bIsTearingDown = false;
    F.At(101.0);
    TestTrue(TEXT("Process/LiveWorldCanHold"), F.Owner->IsShowing());
    TestEqual(TEXT("Process/LiveWorldIsQueried"), OldProcess->QueryCalls, 1);
    F.Owner->UnregisterProcess(OldProcess.Get());
    F.Owner->ReevaluateNow(true);
    TestFalse(TEXT("Process/ExplicitUnregisterEndsHold"), F.Owner->IsShowing());

    auto Registering = MakeProbe(TEXT("RegisteringProbe"));
    Registering->bWanted = false;
    TArray<TStrongObjectPtr<UVBLoadingScreenTestProbe>> NewProcesses;
    for (int32 Index = 0; Index < 32; ++Index)
    {
        NewProcesses.Add(MakeProbe(TEXT("RegisteredInsideQuery")));
    }

    bool bRegistered = false;
    Registering->OnQuery = [&F, &NewProcesses, &bRegistered](FString& OutReason)
    {
        if (!bRegistered)
        {
            bRegistered = true;
            for (const auto& NewProcess : NewProcesses)
            {
                F.Owner->RegisterProcess(NewProcess.Get());
            }
        }

        return false;
    };
    F.Owner->RegisterProcess(Registering.Get());
    F.At(102.0);
    TestTrue(TEXT("Query/RegistrationCallbackRan"), bRegistered);
    TestEqual(TEXT("Query/RegistrationRetained"), Access::ProcessCount(*F.Owner), 33);
    TestFalse(TEXT("Query/NewEntryWaitsForNextQuery"), F.Owner->IsShowing());
    int32 NewQueries = 0;
    for (const auto& NewProcess : NewProcesses)
    {
        NewQueries += NewProcess->QueryCalls;
    }

    TestEqual(TEXT("Query/NewEntriesNotQueriedInOldSnapshot"), NewQueries, 0);
    F.At(103.0);
    TestTrue(TEXT("Query/NewEntryHoldsOnNextQuery"), F.Owner->IsShowing());
    TestEqual(TEXT("Query/FirstNewProcessQueried"), NewProcesses[0]->QueryCalls, 1);
    Registering->OnQuery = {}; // remove stack captures before fixture/probes leave scope
    F.Owner->UnregisterProcess(Registering.Get());
    for (const auto& NewProcess : NewProcesses)
    {
        F.Owner->UnregisterProcess(NewProcess.Get());
    }

    F.Owner->ReevaluateNow(true);
    TestFalse(TEXT("Query/AllRemovedIdle"), F.Owner->IsTickable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingReevaluateNowTest, "VB.Unit.LoadingScreen.ReevaluateNow",
    VBLoadingScreenTests::Flags)
bool FVBLoadingReevaluateNowTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Settings.Settings->LoadingHoldSeconds = 10.0f;
    auto Probe = MakeProbe(TEXT("ImmediateReevaluationProbe"));
    F.Owner->RegisterProcess(Probe.Get());
    F.Owner->ReevaluateNow();
    TestTrue(TEXT("Immediate/ShownSynchronously"), F.Owner->IsShowing());
    Probe->bWanted = false;
    F.Time = 101.0;
    F.Owner->ReevaluateNow(false);
    TestTrue(TEXT("Immediate/NormalReevaluationHonorsHold"), F.Owner->IsShowing());
    TestEqual(TEXT("Immediate/NormalHoldScheduled"), Access::HoldUntil(*F.Owner), 111.0);
    F.Owner->ReevaluateNow(true);
    TestFalse(TEXT("Immediate/SkipHoldHidesInSameCall"), F.Owner->IsShowing());
    TestFalse(TEXT("Immediate/NoEpisodeAfterHide"), Access::EpisodeActive(*F.Owner));

    // 이미 hold 중일 때뿐 아니라 이유 없는 첫 평가에서도 건너뛴다
    Probe->bWanted = true;
    F.Time = 102.0;
    F.Owner->ReevaluateNow();
    Probe->bWanted = false;
    F.Time = 103.0;
    F.Owner->ReevaluateNow(true);
    TestFalse(TEXT("Immediate/SkipAtReasonEndHidesInSameCall"), F.Owner->IsShowing());
    TestEqual(TEXT("Immediate/SkipAtReasonEndDoesNotScheduleHold"), Access::HoldUntil(*F.Owner), 0.0);

    // SkipHold 가 아직 필요한 화면을 숨기면 안 된다
    Probe->bWanted = true;
    F.Time = 104.0;
    F.Owner->ReevaluateNow(true);
    TestTrue(TEXT("Immediate/SkipHoldStillRespectsActiveReason"), F.Owner->IsShowing());
    F.Owner->UnregisterProcess(Probe.Get());
    F.Owner->ReevaluateNow(true);
    TestFalse(TEXT("Immediate/IdleAfterUnregister"), F.Owner->IsTickable());
    // headless 에서 만들지 않았는지만 본다 - 실제 Slate 자원을 지웠다는 증거가 아니다
    TestFalse(TEXT("Immediate/HeadlessNeverCreatedWidget"), Access::HasWidget(*F.Owner));
    TestFalse(TEXT("Immediate/HeadlessNeverCreatedBlocker"), Access::HasInputBlocker(*F.Owner));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingTickGatingTest, "VB.Unit.LoadingScreen.TickGating",
    VBLoadingScreenTests::Flags)
bool FVBLoadingTickGatingTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    TestFalse(TEXT("Tick/CDONeverTickable"), GetDefault<UVBLoadingScreenSubsystem>()->IsTickable());
    TestFalse(TEXT("Tick/IdleOwnerNotTickable"), F.Owner->IsTickable());
    TestTrue(TEXT("Tick/ConditionalTickType"), F.Owner->GetTickableTickType() == ETickableTickType::Conditional);
    TestTrue(TEXT("Tick/TicksWhenPaused"), F.Owner->IsTickableWhenPaused());
    auto Probe = MakeProbe(TEXT("TickGatingProbe"));
    Probe->bWanted = false;
    F.Owner->RegisterProcess(Probe.Get());
    TestTrue(TEXT("Tick/RegisterWakesIdleOwner"), F.Owner->IsTickable());
    F.At(100.0);
    TestFalse(TEXT("Tick/FalseProcessDoesNotShow"), F.Owner->IsShowing());
    TestTrue(TEXT("Tick/RegisteredFalseProcessStillPolled"), F.Owner->IsTickable());
    Probe->bWanted = true;
    F.At(101.0);
    TestTrue(TEXT("Tick/LaterReadinessChangeObserved"), F.Owner->IsShowing());
    F.Settings.Settings->LoadingHoldSeconds = 2.0f;
    F.Owner->UnregisterProcess(Probe.Get());
    TestTrue(TEXT("Tick/ShownOwnerRemainsTickableWithoutProcesses"), F.Owner->IsTickable());
    F.At(102.0);
    TestTrue(TEXT("Tick/HoldKeepsTickable"), F.Owner->IsTickable());
    F.At(104.0);
    TestFalse(TEXT("Tick/HoldExpiryHides"), F.Owner->IsShowing());
    TestFalse(TEXT("Tick/FullyIdleStopsTicking"), F.Owner->IsTickable());

    F.Owner->RegisterProcess(Probe.Get());
    F.Owner->ReevaluateNow();
    F.Deinitialize();
    TestFalse(TEXT("Tick/DeinitHides"), F.Owner->IsShowing());
    TestEqual(TEXT("Tick/DeinitClearsProcesses"), Access::ProcessCount(*F.Owner), 0);
    TestFalse(TEXT("Tick/DeinitStopsTicking"), F.Owner->IsTickable());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingMoviePostOrderTest, "VB.Unit.LoadingScreen.MoviePostOrder",
    VBLoadingScreenTests::Flags)
bool FVBLoadingMoviePostOrderTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    for (const bool bEngineFirst : {false, true})
    {
        FFixture F;
        F.Movie->bAvailable = true;
        FDelegateHandle EnginePostHandle;
        bool bEngineSawMapLoading = false;
        const auto RegisterEnginePost = [&]()
        {
            EnginePostHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda(
                [State = F.Movie, &F, &bEngineSawMapLoading](UWorld*)
                {
                    bEngineSawMapLoading = Access::MapLoading(*F.Owner);
                    State->EnginePost();
                });
        };
        // 지금 UE 는 multicast snapshot 을 등록 역순으로 부른다
        if (!bEngineFirst)
        {
            RegisterEnginePost();
        }

        ON_SCOPE_EXIT { FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(EnginePostHandle); };
        if (!F.Initialize(*this))
        {
            return false;
        }

        if (bEngineFirst)
        {
            RegisterEnginePost();
        }

        int32 TrueSignals = 0;
        int32 FalseSignals = 0;
        const FDelegateHandle ShowingHandle = F.Owner->OnShowingChanged.AddLambda([&](bool bShowing)
        {
            if (bShowing) ++TrueSignals; else ++FalseSignals;
        });
        ON_SCOPE_EXIT { F.Owner->OnShowingChanged.Remove(ShowingHandle); };
        auto World = MakeIdentityWorld(F.GI.Get());
        BroadcastPre(F.GI.Get());
        TestEqual(TEXT("MovieOrder/InstalledOnce"), F.Movie->Installs, 1);
        TestTrue(TEXT("MovieOrder/OwnershipSet"), Access::MovieInstalled(*F.Owner));
        F.Movie->EnginePre();
        TestTrue(TEXT("MovieOrder/StartNotificationObserved"), Access::MovieStarted(*F.Owner));
        TestTrue(TEXT("MovieOrder/ThreadAliveBeforePost"), F.Movie->bThreadAlive);
        FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World.Get());
        const FString Prefix = bEngineFirst ? TEXT("MovieOrder/EngineFirst/") : TEXT("MovieOrder/GIFirst/");
        TestEqual(Prefix + TEXT("ActualCallbackOrder"), bEngineSawMapLoading, bEngineFirst);
        TestEqual(Prefix + TEXT("CleanupBranchRuns"), F.Movie->CleanupBranches, 1);
        TestEqual(Prefix + TEXT("FinishedOnce"), F.Movie->Finishes, 1);
        TestEqual(Prefix + TEXT("NoOwnerClear"), F.Movie->Clears, 0);
        TestFalse(Prefix + TEXT("ThreadCleaned"), F.Movie->bThreadAlive);
        TestFalse(Prefix + TEXT("AttributesConsumed"), F.Movie->bPrepared);
        TestFalse(Prefix + TEXT("OwnershipReleased"), Access::MovieInstalled(*F.Owner));
        TestFalse(Prefix + TEXT("MapReasonEnded"), Access::MapLoading(*F.Owner));
        TestFalse(Prefix + TEXT("NoRemainingReasonHides"), F.Owner->IsShowing());
        TestEqual(Prefix + TEXT("OneTrueAcrossMovieStart"), TrueSignals, 1);
        TestEqual(Prefix + TEXT("OneFalseAcrossMovieFinish"), FalseSignals, 1);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingMovieNeverStartedTest, "VB.Unit.LoadingScreen.MovieNeverStarted",
    VBLoadingScreenTests::Flags)
bool FVBLoadingMovieNeverStartedTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Movie->bAvailable = true;
    auto World = MakeIdentityWorld(F.GI.Get());
    BroadcastPre(F.GI.Get());
    TestEqual(TEXT("NeverStarted/PreparedByOwner"), F.Movie->Installs, 1);
    AddExpectedMessagePlain(TEXT("\ubb34\ube44 \uae38\uc744 \uc124\uce58\ud588\ub294\ub370"), ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 1);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World.Get());
    TestEqual(TEXT("NeverStarted/ClearOnce"), F.Movie->Clears, 1);
    TestFalse(TEXT("NeverStarted/PreparedCleared"), F.Movie->bPrepared);
    TestFalse(TEXT("NeverStarted/OwnershipReleased"), Access::MovieInstalled(*F.Owner));
    F.Movie->EnginePost();
    TestEqual(TEXT("NeverStarted/NoCleanupForUnstartedMovie"), F.Movie->CleanupBranches, 0);
    TestEqual(TEXT("NeverStarted/WindowBranchOnly"), F.Movie->WindowOnlyBranches, 1);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World.Get());
    F.Owner->ReevaluateNow();
    F.Deinitialize();
    TestEqual(TEXT("NeverStarted/NoRepeatedClearOrWarning"), F.Movie->Clears, 1);
    TestFalse(TEXT("NeverStarted/NoThreadCreated"), F.Movie->bThreadAlive);
    TestFalse(TEXT("NeverStarted/CallbacksUnbound"), bool(F.Movie->OnStarted) || bool(F.Movie->OnFinished));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingMovieForeignTest, "VB.Unit.LoadingScreen.MovieForeign",
    VBLoadingScreenTests::Flags)
bool FVBLoadingMovieForeignTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    for (const bool bPlaying : {false, true})
    {
        FFixture F;
        if (!F.Initialize(*this))
        {
            return false;
        }

        F.Movie->bAvailable = true;
        F.Movie->bPrepared = true;
        F.Movie->bPlaying = bPlaying;
        F.Movie->bThreadAlive = bPlaying;
        auto World = MakeIdentityWorld(F.GI.Get());
        BroadcastPre(F.GI.Get());
        TestEqual(TEXT("ForeignMovie/NoInstall"), F.Movie->Installs, 0);
        TestFalse(TEXT("ForeignMovie/NotClaimed"), Access::MovieInstalled(*F.Owner));
        FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World.Get());
        F.Deinitialize();
        TestEqual(TEXT("ForeignMovie/NoClear"), F.Movie->Clears, 0);
        TestTrue(TEXT("ForeignMovie/PreparedPreserved"), F.Movie->bPrepared);
        TestEqual(TEXT("ForeignMovie/PlaybackPreserved"), F.Movie->bPlaying, bPlaying);
        TestEqual(TEXT("ForeignMovie/ThreadPreserved"), F.Movie->bThreadAlive, bPlaying);
        if (bPlaying)
        {
            F.Movie->EnginePost(); // cleanup belongs to the foreign engine owner
        }
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingMovieConsecutiveTest, "VB.Unit.LoadingScreen.MovieConsecutive",
    VBLoadingScreenTests::Flags)
bool FVBLoadingMovieConsecutiveTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Movie->bAvailable = true;
    auto World = MakeIdentityWorld(F.GI.Get());
    TSharedPtr<SWidget> PreviousMovieWidget;
    for (int32 Load = 1; Load <= 2; ++Load)
    {
        F.Time += 1.0;
        BroadcastPre(F.GI.Get());
        TestEqual(TEXT("Consecutive/EachLoadInstalls"), F.Movie->Installs, Load);
        TestTrue(TEXT("Consecutive/FreshMovieInstance"), F.Movie->PreparedWidget != PreviousMovieWidget);
        PreviousMovieWidget = F.Movie->PreparedWidget;
        F.Movie->EnginePre();
        FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World.Get()); // GI first
        F.Movie->EnginePost();
        TestEqual(TEXT("Consecutive/EachLoadCleans"), F.Movie->CleanupBranches, Load);
        TestEqual(TEXT("Consecutive/EachLoadFinishes"), F.Movie->Finishes, Load);
        TestFalse(TEXT("Consecutive/OwnershipResetForNextLoad"), Access::MovieInstalled(*F.Owner));
        TestFalse(TEXT("Consecutive/PlayingResetForNextLoad"), Access::MovieStarted(*F.Owner));
        TestFalse(TEXT("Consecutive/ReturnsIdle"), F.Owner->IsTickable());
    }

    TestEqual(TEXT("Consecutive/NoOwnerClear"), F.Movie->Clears, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingMovieFailureTeardownTest, "VB.Unit.LoadingScreen.MovieFailureTeardown",
    VBLoadingScreenTests::Flags)
bool FVBLoadingMovieFailureTeardownTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    for (const bool bStarted : {false, true})
    {
        FFixture F;
        if (!F.Initialize(*this))
        {
            return false;
        }

        F.Movie->bAvailable = true;
        BroadcastPre(F.GI.Get());
        if (bStarted)
        {
            F.Movie->EnginePre();
        }

        if (!bStarted)
        {
            AddExpectedMessagePlain(TEXT("\ubb34\ube44 \uae38\uc744 \uc124\uce58\ud588\ub294\ub370"), ELogVerbosity::Warning,
            EAutomationExpectedMessageFlags::Contains, 1);
        }

        AddExpectedMessagePlain(TEXT("Post"), ELogVerbosity::Warning,
            EAutomationExpectedMessageFlags::Contains, 1);
        FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(nullptr);
        TestFalse(TEXT("MovieFailure/NullPostEndsMap"), Access::MapLoading(*F.Owner));
        TestFalse(TEXT("MovieFailure/NullPostEndsStartPhase"), Access::StartPhase(*F.Owner));
        TestEqual(TEXT("MovieFailure/OnlyNeverStartedCleared"), F.Movie->Clears, bStarted ? 0 : 1);
        if (bStarted)
        {
            F.Movie->EnginePost();
        }

        TestEqual(TEXT("MovieFailure/StartedEngineCleanupRuns"), F.Movie->CleanupBranches, bStarted ? 1 : 0);
        TestFalse(TEXT("MovieFailure/ThreadAbsentAfterEnginePost"), F.Movie->bThreadAlive);
    }

    for (const bool bStarted : {false, true})
    {
        FFixture F;
        if (!F.Initialize(*this))
        {
            return false;
        }

        F.Movie->bAvailable = true;
        BroadcastPre(F.GI.Get());
        if (bStarted)
        {
            F.Movie->EnginePre();
        }

        if (!bStarted)
        {
            AddExpectedMessagePlain(TEXT("\ubb34\ube44 \uae38\uc744 \uc124\uce58\ud588\ub294\ub370"), ELogVerbosity::Warning,
            EAutomationExpectedMessageFlags::Contains, 1);
        }

        F.Deinitialize();
        TestEqual(TEXT("MovieTeardown/OnlyNeverStartedCleared"), F.Movie->Clears, bStarted ? 0 : 1);
        TestFalse(TEXT("MovieTeardown/CallbacksDetached"), bool(F.Movie->OnStarted) || bool(F.Movie->OnFinished));
        if (bStarted)
        {
            F.Movie->EnginePost();
        }

        TestEqual(TEXT("MovieTeardown/StartedEngineStillCleans"), F.Movie->CleanupBranches, bStarted ? 1 : 0);
        TestFalse(TEXT("MovieTeardown/OwnerNotShown"), F.Owner->IsShowing());
        TestFalse(TEXT("MovieTeardown/NoThreadRemaining"), F.Movie->bThreadAlive);
        BroadcastPre(F.GI.Get());
        TestEqual(TEXT("MovieTeardown/DelegatesCannotReinstall"), F.Movie->Installs, 1);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingShowingChangesTest, "VB.Unit.LoadingScreen.ShowingChanges",
    VBLoadingScreenTests::Flags)
bool FVBLoadingShowingChangesTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Settings.Settings->LoadingHoldSeconds = 5.0f;
    F.Settings.Settings->LoadingMaxShowSeconds = 10.0f;
    TArray<bool> Changes;
    bool bObservedCurrentState = true;
    const FDelegateHandle Handle = F.Owner->OnShowingChanged.AddLambda([&](bool bNow)
    {
        bObservedCurrentState &= F.Owner->IsShowing() == bNow;
        Changes.Add(bNow);
    });
    ON_SCOPE_EXIT { F.Owner->OnShowingChanged.Remove(Handle); };
    auto Probe = MakeProbe(TEXT("ShowingChangesProbe"));
    Probe->Reason = TEXT("U06cShowingChangeMaxReason");
    F.Owner->RegisterProcess(Probe.Get());
    F.At(100.0);
    F.At(101.0);
    F.Owner->ReevaluateNow();
    TestEqual(TEXT("Showing/OneTrueDespiteRepeatedEvaluate"), Changes.Num(), 1);
    TestTrue(TEXT("Showing/FirstIsTrue"), Changes.Num() == 1 && Changes[0]);
    Probe->bWanted = false;
    F.At(102.0);
    TestEqual(TEXT("Showing/HoldDoesNotSignalFalse"), Changes.Num(), 1);
    F.Owner->ReevaluateNow(true);
    F.Owner->ReevaluateNow(true);
    TestEqual(TEXT("Showing/SkipHoldSignalsFalseOnce"), Changes.Num(), 2);
    TestTrue(TEXT("Showing/SkipHoldFalse"), Changes.Num() == 2 && !Changes.Last());
    Probe->bWanted = true;
    F.At(200.0);
    AddExpectedMessagePlain(Probe->Reason, ELogVerbosity::Error,
        EAutomationExpectedMessageFlags::Contains, 1);
    F.At(211.0);
    F.At(212.0);
    TestEqual(TEXT("Showing/MaxShowSignalsFalseOnce"), Changes.Num(), 4);
    TestTrue(TEXT("Showing/MaxShowFalse"), Changes.Num() == 4 && !Changes.Last());
    Probe->bWanted = false;
    F.At(213.0);
    Probe->bWanted = true;
    F.At(300.0);
    F.Deinitialize();
    TestEqual(TEXT("Showing/DeinitSignalsFalseOnce"), Changes.Num(), 6);
    TestTrue(TEXT("Showing/DeinitFalse"), Changes.Num() == 6 && !Changes.Last());
    TestTrue(TEXT("Showing/StateAlreadyChangedAtNotification"), bObservedCurrentState);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingMovieViewportHandoverTest, "VB.Unit.LoadingScreen.MovieViewportHandover",
    VBLoadingScreenTests::Flags | EAutomationTestFlags::NonNullRHI)
bool FVBLoadingMovieViewportHandoverTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    if (!TestTrue(TEXT("Handover/RenderingProcessRequired"), FApp::CanEverRender()))
    {
        return false;
    }

    if (!TestTrue(TEXT("Handover/SlateRequired"), FSlateApplication::IsInitialized()))
    {
        return false;
    }

    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    FWorldContext Context;
    Context.WorldType = EWorldType::Game;
    Context.OwningGameInstance = F.GI.Get();
    if (!TestNotNull(TEXT("Handover/EngineOuter"), GEngine))
    {
        return false;
    }

    TStrongObjectPtr<UVBLoadingScreenTestViewport> Viewport(NewObject<UVBLoadingScreenTestViewport>(GEngine));
    auto* TestGI = CastChecked<UVBLoadingScreenTestGameInstance>(F.GI.Get());
    Context.GameViewport = Viewport.Get();
    TestGI->SetContextForTests(&Context);
    ON_SCOPE_EXIT { F.Deinitialize(); TestGI->SetContextForTests(nullptr); };
    F.Settings.Settings->bShowLoadingScreenInEditor = true;
    F.Settings.Settings->LoadingFadeInSeconds = 1000000.0f;
    Access::SetHeadless(*F.Owner, false);
    F.Movie->bAvailable = true;
    auto World = MakeIdentityWorld(F.GI.Get());
    auto Process = MakeProbe(TEXT("PostMoviePreparation"));
    F.Owner->RegisterProcess(Process.Get());
    BroadcastPre(F.GI.Get());
    TestEqual(TEXT("Handover/RealShowAttachedOnce"), Viewport->Adds, 1);
    TestTrue(TEXT("Handover/SeparateMovieAndViewportInstances"),
        Viewport->Attached.IsValid() && F.Movie->PreparedWidget.IsValid()
        && Viewport->Attached != F.Movie->PreparedWidget);
    // 공개 SBorder 색 attribute 를 실제로 평가한다 - 그리기, Slate 시간 진행, private header, fade 상태 조작 없이
    const auto ReadBackgroundAlpha = [&](TSharedPtr<SWidget> Widget, float& OutAlpha)
    {
        if (!TestTrue(TEXT("Handover/WidgetExists"), Widget.IsValid()))
        {
            return false;
        }

        FChildren* Children = Widget->GetChildren();
        if (!TestTrue(TEXT("Handover/RootChildExists"), Children && Children->Num() == 1))
        {
            return false;
        }

        TSharedRef<SWidget> Child = Children->GetChildAt(0);
        if (!TestTrue(TEXT("Handover/ActualBorderRoot"), Child->GetType() == FName(TEXT("SBorder"))))
        {
            return false;
        }

        Child->UpdateAllAttributes(); // managed attributes cache values; a getter alone does not re-evaluate
        OutAlpha = StaticCastSharedRef<SBorder>(Child)->GetBorderBackgroundColor().GetSpecifiedColor().A;
        return true;
    };
    float ViewportAlpha = -1.0f;
    float MovieAlpha = -1.0f;
    if (!ReadBackgroundAlpha(Viewport->Attached, ViewportAlpha)
        || !ReadBackgroundAlpha(F.Movie->PreparedWidget, MovieAlpha))
    {
        return false;
    }

    TestEqual(TEXT("Handover/ViewportHasNoFadeUnderMovie"), ViewportAlpha,
        F.Settings.Settings->LoadingBackgroundColor.A);
    TestTrue(TEXT("Handover/MovieHasItsOwnFadeClock"), MovieAlpha < 0.01f);
    const TSharedPtr<SWidget> OriginalViewportWidget = Viewport->Attached;
    F.Movie->EnginePre();
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World.Get());
    F.Movie->EnginePost();
    TestTrue(TEXT("Handover/PreparationStillHoldsViewport"), F.Owner->IsShowing());
    TestTrue(TEXT("Handover/ViewportInstanceReusedAfterMovie"), Viewport->Attached == OriginalViewportWidget);
    TestEqual(TEXT("Handover/NoDuplicateAttachment"), Viewport->Adds, 1);
    F.Owner->UnregisterProcess(Process.Get());
    F.Owner->ReevaluateNow(true);
    TestEqual(TEXT("Handover/RealHideRemovesOnce"), Viewport->Removes, 1);
    TestFalse(TEXT("Handover/WidgetReleasedByOwner"), Access::HasWidget(*F.Owner));
    TestFalse(TEXT("Handover/InputBlockerReleasedByOwner"), Access::HasInputBlocker(*F.Owner));
    // movie 없는 별개 episode 는 설정된 fade 를 그대로 쓴다
    F.Owner->RegisterProcess(Process.Get());
    F.Owner->ReevaluateNow();
    float OrdinaryAlpha = -1.0f;
    if (!ReadBackgroundAlpha(Viewport->Attached, OrdinaryAlpha))
    {
        return false;
    }

    TestTrue(TEXT("Handover/IndependentViewportEpisodeUsesConfiguredFade"), OrdinaryAlpha < 0.01f);
    F.Owner->UnregisterProcess(Process.Get());
    F.Owner->ReevaluateNow(true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingMapLifetimeTest, "VB.Unit.LoadingScreen.MapLifetime",
    VBLoadingScreenTests::Flags)
bool FVBLoadingMapLifetimeTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    auto* Flow = GetMutableDefault<UVBGameFlowSettings>();
    const TSoftObjectPtr<UWorld> OldGameplay = Flow->GameplayLevel;
    Flow->GameplayLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/U06cTest_Gameplay.U06cTest_Gameplay")));
    ON_SCOPE_EXIT { Flow->GameplayLevel = OldGameplay; };
    TStrongObjectPtr<UGameInstance> OtherGI(NewObject<UGameInstance>());
    auto ForeignWorld = MakeIdentityWorld(OtherGI.Get());
    UWorld::InitializationValues Init;
    Init.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* OldWorld = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("MapLifetime/IsolatedWorld"), OldWorld))
    {
        return false;
    }

    ON_SCOPE_EXIT
    {
        OldWorld->bIsTearingDown = false;
        const TStrongObjectPtr<UPackage> Package(OldWorld->GetPackage());
        OldWorld->DestroyWorld(false);
        Package->SetDirtyFlag(false);
    };
    OldWorld->SetGameInstance(F.GI.Get());
    auto NewWorld = MakeIdentityWorld(F.GI.Get());
    FWorldContext Context;
    Context.WorldType = EWorldType::Game;
    Context.OwningGameInstance = F.GI.Get();
    Context.SetCurrentWorld(OldWorld);
    auto* TestGI = CastChecked<UVBLoadingScreenTestGameInstance>(F.GI.Get());
    TestGI->SetContextForTests(&Context);
    ON_SCOPE_EXIT { F.Deinitialize(); TestGI->SetContextForTests(nullptr); };
    auto* PC = OldWorld->SpawnActor<AVBLoadingScreenTestPlayerProcess>();
    if (!TestNotNull(TEXT("MapLifetime/PCPresenceProbe"), PC))
    {
        return false;
    }

    BroadcastPre(OtherGI.Get(), TEXT("/Game/U06cTest_Gameplay"));
    TestFalse(TEXT("MapLifetime/ForeignPreDoesNotOpenStartPhase"), Access::StartPhase(*F.Owner));
    BroadcastPre(F.GI.Get(), TEXT("/Game/U06cTest_Gameplay"));
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(ForeignWorld.Get());
    TestTrue(TEXT("MapLifetime/ForeignPostDoesNotEndOwnLoad"), Access::MapLoading(*F.Owner));
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(OldWorld);
    TestTrue(TEXT("MapLifetime/NoPCRegistrationGapStillShows"), F.Owner->IsShowing());
    TestTrue(TEXT("MapLifetime/StartPhaseWaitsForPC"), Access::StartPhase(*F.Owner));
    F.Owner->RegisterProcess(PC);
    F.Owner->ReevaluateNow();
    TestFalse(TEXT("MapLifetime/ReadyRegisteredPCEndsPhase"), Access::StartPhase(*F.Owner));
    TestFalse(TEXT("MapLifetime/ReadyRegisteredPCHides"), F.Owner->IsShowing());

    // 사라지는 옛 맵에 등록된 PC 가 새 맵의 존재 조건을 채우면 안 된다
    OldWorld->bIsTearingDown = true;
    Context.SetCurrentWorld(NewWorld.Get());
    BroadcastPre(F.GI.Get(), TEXT("/Game/U06cTest_Gameplay"));
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(NewWorld.Get());
    TestTrue(TEXT("MapLifetime/OldTearingDownPCDoesNotFillRegistrationGap"), F.Owner->IsShowing());
    TestTrue(TEXT("MapLifetime/NewMapStillAwaitsLivePC"), Access::StartPhase(*F.Owner));
    F.Owner->UnregisterProcess(PC);
    BroadcastPre(F.GI.Get(), TEXT("/Game/U06cTest_Gameplay"));
    AddExpectedMessagePlain(TEXT("Post"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(nullptr);
    TestFalse(TEXT("MapLifetime/FailedTravelDoesNotWaitForAbsentPC"), Access::StartPhase(*F.Owner));
    TestFalse(TEXT("MapLifetime/NullPostHides"), F.Owner->IsShowing());
    BroadcastPre(F.GI.Get(), TEXT("/Game/U06cTest_Gameplay"));
    F.Deinitialize();
    TestFalse(TEXT("MapLifetime/DeinitEndsStartPhase"), Access::StartPhase(*F.Owner));
    TestFalse(TEXT("MapLifetime/DeinitEndsMapReason"), Access::MapLoading(*F.Owner));
    TestFalse(TEXT("MapLifetime/DeinitIdle"), F.Owner->IsTickable());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingPlayerProcessReasonsTest, "VB.Unit.LoadingScreen.PlayerProcessReasons",
    VBLoadingScreenTests::Flags)
bool FVBLoadingPlayerProcessReasonsTest::RunTest(const FString& Parameters)
{
    const uint32 Allowed = static_cast<uint32>(EVBPlayerPreparationReason::Initialization)
        | static_cast<uint32>(EVBPlayerPreparationReason::SaveApplication)
        | static_cast<uint32>(EVBPlayerPreparationReason::Restore)
        | static_cast<uint32>(EVBPlayerPreparationReason::AbilitySystem);
    // ShouldShowLoadingScreen 이 쓰는 제품 selector 를 그대로 부른다 (PC BeginPlay 수락, Readiness.Query 는 흉내 내지 않는다)
    for (const bool bUnavailable : {false, true})
    {
        for (uint32 Index = 0; Index < 32; ++Index)
        {
            FVBPlayerPreparationSnapshot Snapshot;
            const uint32 Bit = 1u << Index;
            Snapshot.BlockedReasons = bUnavailable ? 0 : Bit;
            Snapshot.UnavailableReasons = bUnavailable ? Bit : 0;
            Snapshot.State = bUnavailable ? EVBPlayerPreparationState::Unavailable : EVBPlayerPreparationState::Blocked;
            FString Reason;
            const FString Label = FString::Printf(TEXT("PlayerProcess/%s/Bit%u"), bUnavailable ? TEXT("Unavailable") : TEXT("Blocked"), Index);
            TestEqual(Label, VBPlayerLoadingRules::SelectLoadingReason(Snapshot, Reason), (Bit & Allowed) != 0);
            if (Bit & Allowed)
            {
                TestFalse(Label + TEXT("/NamesAllowedReason"), Reason.IsEmpty());
            }
        }
    }

    FVBPlayerPreparationSnapshot Snapshot;
    Snapshot.BlockedReasons = ~Allowed;
    Snapshot.UnavailableReasons = ~Allowed;
    FString Reason;
    TestFalse(TEXT("PlayerProcess/ExcludedReasonsTogetherAreNotLoading"),
        VBPlayerLoadingRules::SelectLoadingReason(Snapshot, Reason));
    Snapshot.BlockedReasons |= static_cast<uint32>(EVBPlayerPreparationReason::Restore);
    TestTrue(TEXT("PlayerProcess/AllowedSurvivesExcludedReasons"), VBPlayerLoadingRules::SelectLoadingReason(Snapshot, Reason));
    TestEqual(TEXT("PlayerProcess/RestoreReasonNamed"), Reason, FString(TEXT("Restore")));
    Snapshot.UnavailableReasons |= static_cast<uint32>(EVBPlayerPreparationReason::Initialization);
    TestTrue(TEXT("PlayerProcess/UnavailabilityIsNotDropped"), VBPlayerLoadingRules::SelectLoadingReason(Snapshot, Reason));
    TestEqual(TEXT("PlayerProcess/InitializationFirstInAllowlist"), Reason, FString(TEXT("Initialization")));
    Snapshot.BlockedReasons = Snapshot.UnavailableReasons = 0;
    Snapshot.State = EVBPlayerPreparationState::Unavailable; // summary state alone is not a reason
    TestFalse(TEXT("PlayerProcess/NoGenericUnavailableLoading"), VBPlayerLoadingRules::SelectLoadingReason(Snapshot, Reason));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingPlayerProcessQueryTest, "VB.Unit.LoadingScreen.PlayerProcessQuery",
    VBLoadingScreenTests::Flags)
bool FVBLoadingPlayerProcessQueryTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Init;
    Init.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("PlayerQuery/World"), World))
    {
        return false;
    }

    ON_SCOPE_EXIT
    {
        const TStrongObjectPtr<UPackage> Package(World->GetPackage());
        World->DestroyWorld(false);
        Package->SetDirtyFlag(false);
    };
    auto* PC = World->SpawnActor<AVBLoadingScreenTestVBController>();
    auto* Pawn = World->SpawnActor<AVBLoadingScreenTestAbilityPawn>();
    if (!TestNotNull(TEXT("PlayerQuery/PC"), PC) || !TestNotNull(TEXT("PlayerQuery/Pawn"), Pawn))
    {
        return false;
    }

    FString Reason;
    TestTrue(TEXT("PlayerQuery/NoPawnHolds"), PC->ShouldShowLoadingScreen(Reason));
    PC->SetPawnForTests(Pawn);
    TestTrue(TEXT("PlayerQuery/NoReadinessHolds"), PC->ShouldShowLoadingScreen(Reason));
    auto* Readiness = NewObject<UVBPlayerReadinessComponent>(Pawn);
    Pawn->AddInstanceComponent(Readiness);
    Readiness->RegisterComponent();
    const FGuid Attempt = FGuid::NewGuid();
    Readiness->BeginInitialization(Attempt, EVBGameStartIntent::NewGame);
    Pawn->AbilitySystem->InitAbilityActorInfo(Pawn, Pawn);
    TestTrue(TEXT("PlayerQuery/RealInitializationBlocks"), PC->ShouldShowLoadingScreen(Reason));
    TestEqual(TEXT("PlayerQuery/InitializationReason"), Reason, FString(TEXT("Initialization")));
    Readiness->CompleteInitialization(Attempt, false);
    TestFalse(TEXT("PlayerQuery/NonAllowlistedMissingProvidersDoNotLoad"), PC->ShouldShowLoadingScreen(Reason));
    Pawn->AbilitySystem->AddLooseGameplayTag(VBGameplayTags::State_Interacting);
    TestFalse(TEXT("PlayerQuery/InteractionDoesNotLoad"), PC->ShouldShowLoadingScreen(Reason));
    Pawn->AbilitySystem->AddLooseGameplayTag(VBGameplayTags::State_Dead);
    TestFalse(TEXT("PlayerQuery/DeathDoesNotLoad"), PC->ShouldShowLoadingScreen(Reason));
    const FVBPlayerPreparationHoldToken Hold = Readiness->AcquirePreparationHold(PC, TEXT("LoadingTestExternalHold"));
    TestTrue(TEXT("PlayerQuery/RealExternalHoldCreated"), Hold.Id.IsValid());
    TestFalse(TEXT("PlayerQuery/ExternalHoldDoesNotLoad"), PC->ShouldShowLoadingScreen(Reason));
    Readiness->ReleasePreparationHold(Hold);
    Pawn->AbilitySystem->ClearActorInfo();
    TestTrue(TEXT("PlayerQuery/LostAbilitySystemHolds"), PC->ShouldShowLoadingScreen(Reason));
    TestEqual(TEXT("PlayerQuery/LostAbilitySystemReason"), Reason, FString(TEXT("AbilitySystem")));
    PC->SetPawnForTests(nullptr);
    TestTrue(TEXT("PlayerQuery/PawnReplacementGapHolds"), PC->ShouldShowLoadingScreen(Reason));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingUnshownMaxShowTest, "VB.Unit.LoadingScreen.UnshownMaxShow",
    VBLoadingScreenTests::Flags)
bool FVBLoadingUnshownMaxShowTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Settings.Settings->LoadingHoldSeconds = 5.0f;
    F.Settings.Settings->LoadingMaxShowSeconds = 10.0f;
    F.Settings.Settings->bShowLoadingScreenInEditor = true;
    Access::SetHeadless(*F.Owner, false);
    if (!TestFalse(TEXT("UnshownMax/NoViewport"), Access::CanDisplay(*F.Owner)))
    {
        return false;
    }

    int32 Signals = 0;
    const FDelegateHandle Handle = F.Owner->OnShowingChanged.AddLambda([&](bool) { ++Signals; });
    ON_SCOPE_EXIT { F.Owner->OnShowingChanged.Remove(Handle); };
    auto Probe = MakeProbe(TEXT("UnshownMaxProbe"));
    Probe->Reason = TEXT("U06cUnshownMaxReason");
    F.Owner->RegisterProcess(Probe.Get());
    F.At(100.0);
    F.At(110.0);
    TestTrue(TEXT("UnshownMax/ReasonEpisodeExists"), Access::EpisodeActive(*F.Owner));
    TestFalse(TEXT("UnshownMax/ExactLimitNotAbandoned"), Access::Abandoned(*F.Owner));
    TestFalse(TEXT("UnshownMax/NeverShown"), Access::EpisodeShown(*F.Owner));
    // 여기서 Error 는 예상 밖이라 automation 을 실패시키고, Warning 하나만 허용한다
    AddExpectedMessagePlain(Probe->Reason, ELogVerbosity::Warning,
        EAutomationExpectedMessageFlags::Contains, 1);
    F.At(111.0);
    F.At(112.0);
    F.At(130.0);
    TestTrue(TEXT("UnshownMax/OverLimitAbandoned"), Access::Abandoned(*F.Owner));
    TestFalse(TEXT("UnshownMax/StillHidden"), F.Owner->IsShowing());
    TestEqual(TEXT("UnshownMax/NoVisibilityChanges"), Signals, 0);
    TestFalse(TEXT("UnshownMax/NoWidget"), Access::HasWidget(*F.Owner));
    TestFalse(TEXT("UnshownMax/NoInputBlocker"), Access::HasInputBlocker(*F.Owner));
    // 나중에 viewport 가 생겨도 버려진 이 episode 는 숨은 채다
    Access::SetHeadless(*F.Owner, true);
    F.At(131.0);
    TestFalse(TEXT("UnshownMax/AvailabilityCannotReviveAbandonedEpisode"), F.Owner->IsShowing());
    Probe->bWanted = false;
    F.At(132.0);
    TestFalse(TEXT("UnshownMax/ReasonEndClosesEpisode"), Access::EpisodeActive(*F.Owner));
    TestEqual(TEXT("UnshownMax/NoHoldAfterNeverShownEpisode"), Access::HoldUntil(*F.Owner), 0.0);
    TestEqual(TEXT("UnshownMax/NoFalseHideSignal"), Signals, 0);
    Probe->bWanted = true;
    F.At(200.0);
    TestTrue(TEXT("UnshownMax/NewEpisodeCanShow"), F.Owner->IsShowing());
    TestFalse(TEXT("UnshownMax/NewEpisodeNotAbandoned"), Access::Abandoned(*F.Owner));
    TestEqual(TEXT("UnshownMax/NewEpisodeFreshClock"), Access::EpisodeStart(*F.Owner), 200.0);
    TestEqual(TEXT("UnshownMax/NewEpisodeSignalsTrueOnce"), Signals, 1);
    F.Owner->UnregisterProcess(Probe.Get());
    F.Owner->ReevaluateNow(true);
    TestEqual(TEXT("UnshownMax/ActualShownEpisodeSignalsFalseOnce"), Signals, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingRenderGateTest, "VB.Unit.LoadingScreen.RenderGate",
    VBLoadingScreenTests::Flags)
bool FVBLoadingRenderGateTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!TestTrue(TEXT("RenderGate/SlateRequired"), FSlateApplication::IsInitialized()))
    {
        return false;
    }

    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    FWorldContext Context;
    Context.WorldType = EWorldType::Game;
    Context.OwningGameInstance = F.GI.Get();
    if (!TestNotNull(TEXT("RenderGate/EngineOuter"), GEngine))
    {
        return false;
    }

    TStrongObjectPtr<UVBLoadingScreenTestViewport> Viewport(NewObject<UVBLoadingScreenTestViewport>(GEngine));
    Context.GameViewport = Viewport.Get();
    auto* TestGI = CastChecked<UVBLoadingScreenTestGameInstance>(F.GI.Get());
    TestGI->SetContextForTests(&Context);
    ON_SCOPE_EXIT { F.Deinitialize(); TestGI->SetContextForTests(nullptr); };
    Access::SetHeadless(*F.Owner, false);
    // CanDisplayNow 의 다른 전제는 모두 실제로 있다 - viewport 없는 UnshownHold 와 달리 NullRHI 에서 FApp gate 를 빼면 실패한다
    TestEqual(TEXT("RenderGate/ViewportDoesNotBypassRenderingRequirement"),
        Access::CanDisplay(*F.Owner), FApp::CanEverRender());
    TestEqual(TEXT("RenderGate/NoShowAttempt"), Viewport->Adds, 0);
    return true;
}

#if !UE_BUILD_SHIPPING
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBLoadingNoLoadingScreenTest, "VB.Unit.LoadingScreen.NoLoadingScreen",
    VBLoadingScreenTests::Flags)
bool FVBLoadingNoLoadingScreenTest::RunTest(const FString& Parameters)
{
    using namespace VBLoadingScreenTests;
    if (!CanBroadcastMapSignals(*this))
    {
        return false;
    }

    FFixture F;
    if (!F.Initialize(*this))
    {
        return false;
    }

    F.Settings.Settings->LoadingHoldSeconds = 5.0f;
    Access::SuppressPresentation(*F.Owner, true);
    F.Movie->bAvailable = true;
    int32 Signals = 0;
    const FDelegateHandle Handle = F.Owner->OnShowingChanged.AddLambda([&](bool) { ++Signals; });
    ON_SCOPE_EXIT { F.Owner->OnShowingChanged.Remove(Handle); };
    auto Probe = MakeProbe(TEXT("NoLoadingScreenProbe"));
    F.Owner->RegisterProcess(Probe.Get());
    auto World = MakeIdentityWorld(F.GI.Get());

    // 실제 맵 이벤트와 episode 판단은 그대로 돌고 표시만 막는다
    BroadcastPre(F.GI.Get());
    TestTrue(TEXT("NoLoadingScreen/MapReasonRetained"), Access::MapLoading(*F.Owner));
    TestTrue(TEXT("NoLoadingScreen/EpisodeRetained"), Access::EpisodeActive(*F.Owner));
    TestFalse(TEXT("NoLoadingScreen/NoShow"), F.Owner->IsShowing());
    TestFalse(TEXT("NoLoadingScreen/NeverCountedAsShown"), Access::EpisodeShown(*F.Owner));
    TestFalse(TEXT("NoLoadingScreen/NoWidget"), Access::HasWidget(*F.Owner));
    TestFalse(TEXT("NoLoadingScreen/NoInputBlocker"), Access::HasInputBlocker(*F.Owner));
    TestEqual(TEXT("NoLoadingScreen/NoMovieInstall"), F.Movie->Installs, 0);
    TestFalse(TEXT("NoLoadingScreen/NoOwnedMovie"), Access::MovieInstalled(*F.Owner));
    // 무시 flag 반대 대조에서도 정리를 결정적으로 하려고 설치된 movie 를 끝낸다
    F.Movie->EnginePre();
    FCoreUObjectDelegates::PostLoadMapWithWorld.Broadcast(World.Get());
    F.Movie->EnginePost();
    TestFalse(TEXT("NoLoadingScreen/PostEndsMapReason"), Access::MapLoading(*F.Owner));
    TestTrue(TEXT("NoLoadingScreen/RegisteredProcessStillHoldsEpisode"), Access::EpisodeActive(*F.Owner));
    Probe->bWanted = false;
    F.At(101.0);
    TestFalse(TEXT("NoLoadingScreen/ProcessReleaseEndsEpisode"), Access::EpisodeActive(*F.Owner));
    TestEqual(TEXT("NoLoadingScreen/NoHoldAfterUnshownEpisode"), Access::HoldUntil(*F.Owner), 0.0);
    TestEqual(TEXT("NoLoadingScreen/NoVisibilitySignals"), Signals, 0);

    // 양성 대조 - 표시 정책이 허락하면 같은 fixture 와 process 로 보인다
    Access::SuppressPresentation(*F.Owner, false);
    Probe->bWanted = true;
    F.At(102.0);
    TestTrue(TEXT("NoLoadingScreen/AllowingPresentationShows"), F.Owner->IsShowing());
    TestEqual(TEXT("NoLoadingScreen/AllowedEpisodeSignalsOnce"), Signals, 1);
    Probe->bWanted = false;
    F.Owner->ReevaluateNow(true);
    TestFalse(TEXT("NoLoadingScreen/AllowedEpisodeHides"), F.Owner->IsShowing());
    TestEqual(TEXT("NoLoadingScreen/AllowedHideSignalsOnce"), Signals, 2);
    F.Owner->UnregisterProcess(Probe.Get());
    TestFalse(TEXT("NoLoadingScreen/IdleAfterUnregister"), F.Owner->IsTickable());
    return true;
}
#endif // !UE_BUILD_SHIPPING

#endif // WITH_DEV_AUTOMATION_TESTS
