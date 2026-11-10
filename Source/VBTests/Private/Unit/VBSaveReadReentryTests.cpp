#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "VBSaveGateFixtureParticipant.h"

namespace VBSaveReadReentryFixture
{
struct FFixture
{
    TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
    TStrongObjectPtr<UVBSaveGameSubsystem> Save{NewObject<UVBSaveGameSubsystem>(Instance.Get())};
    UWorld* World = nullptr;
    UVBSaveParticipantSubsystem* Registry = nullptr;
    UVBSaveGateFixtureParticipant* First = nullptr;
    UVBSaveGateFixtureParticipant* Second = nullptr;
    FVBSaveContext Context;
    FFixture()
    {
        const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
            .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
            .ShouldSimulatePhysics(false).SetTransactional(false);
        World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
            ERHIFeatureLevel::Num, &Init);
        if (!World)
        {
            return;
        }

        World->SetGameInstance(Instance.Get());
        AActor* Owner = World->SpawnActor<AActor>();
        if (!Owner)
        {
            return;
        }

        Registry = World->GetSubsystem<UVBSaveParticipantSubsystem>();
        First = NewObject<UVBSaveGateFixtureParticipant>(Owner);
        Second = NewObject<UVBSaveGateFixtureParticipant>(Owner);
        First->RegisterComponent();
        Second->RegisterComponent();
        UVBSaveParticipantSubsystem::Register(First);
        UVBSaveParticipantSubsystem::Register(Second);
        Context.World = World;
        Save->StartNewGame();
    }

    bool IsValid() const
    {
        if (!Registry || !First || !Second)
        {
            return false;
        }

        const TArray<IVBSaveParticipant*> Readers = Registry->GetParticipants();
        const int32 FirstIndex = Readers.IndexOfByKey(static_cast<IVBSaveParticipant*>(First));
        const int32 SecondIndex = Readers.IndexOfByKey(static_cast<IVBSaveParticipant*>(Second));
        return FirstIndex != INDEX_NONE && SecondIndex > FirstIndex;
    }

    ~FFixture()
    {
        if (::IsValid(First)) { First->OnRead = {}; UVBSaveParticipantSubsystem::Unregister(First); }
        if (::IsValid(Second)) { Second->OnRead = {}; UVBSaveParticipantSubsystem::Unregister(Second); }
        if (World)
        {
            UPackage* Package = World->GetPackage();
            World->DestroyWorld(false);
            Package->SetDirtyFlag(false);
        }
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveReadUnregisterReproTest, "VB.Unit.Save.Read.UnregisterReentry",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveReadUnregisterReproTest::RunTest(const FString& Parameters)
{
    VBSaveReadReentryFixture::FFixture F;
    if (!TestTrue(TEXT("fixture readers registered in order"), F.IsValid()))
    {
        return false;
    }

    const int32 BaselineReaders = F.Registry->GetParticipants().Num();
    int32 FirstReads = 0;
    int32 SecondReads = 0;
    int32 AppliedSignals = 0;
    F.Registry->OnSaveApplied.AddLambda([&] { ++AppliedSignals; });
    F.First->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&)
    {
        ++FirstReads;
        UVBSaveParticipantSubsystem::Unregister(F.Second);
    };
    F.Second->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&) { ++SecondReads; };
    const int32 Delivered = F.Registry->ReadAll(*F.Save->GetActiveGameSave(), F.Context);
    TestEqual(TEXT("first reader entered"), FirstReads, 1);
    TestFalse(TEXT("second reader was unregistered"), F.Registry->GetParticipants().Contains(static_cast<IVBSaveParticipant*>(F.Second)));
    TestEqual(TEXT("unregistered later reader is skipped"), SecondReads, 0);
    TestEqual(TEXT("delivery excludes only the unregistered reader"), Delivered, BaselineReaders - 1);
    TestEqual(TEXT("valid delivery cycle completes once"), AppliedSignals, 1);
    F.Registry->OnSaveApplied.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveReadGenerationReproTest, "VB.Unit.Save.Read.GenerationReentry",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveReadGenerationReproTest::RunTest(const FString& Parameters)
{
    VBSaveReadReentryFixture::FFixture F;
    if (!TestTrue(TEXT("fixture readers registered in order"), F.IsValid()))
    {
        return false;
    }

    TStrongObjectPtr<UVBSaveGame> OldSave(F.Save->GetActiveGameSave());
    const FGuid OldAttempt = F.Save->GetGameStartContext().AttemptId;
    int32 FirstReads = 0;
    int32 StaleReads = 0;
    int32 AppliedSignals = 0;
    F.Registry->OnSaveApplied.AddLambda([&] { ++AppliedSignals; });
    F.First->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&)
    {
        ++FirstReads;
        F.Save->StartNewGame();
    };
    F.Second->OnRead = [&](const UVBSaveGame& Read, const FVBSaveContext&)
    {
        if (&Read == OldSave.Get() && &Read != F.Save->GetActiveGameSave())
        {
            ++StaleReads;
        }
    };
    const int32 Delivered = F.Save->ApplyActiveSave(F.Context);
    TestEqual(TEXT("first reader entered"), FirstReads, 1);
    TestNotEqual(TEXT("attempt really changed"), F.Save->GetGameStartContext().AttemptId, OldAttempt);
    TestTrue(TEXT("active save really changed"), F.Save->GetActiveGameSave() != OldSave.Get());
    TestEqual(TEXT("new attempt never receives stale later reads"), StaleReads, 0);
    TestEqual(TEXT("aborted old attempt cannot report prepared delivery"), Delivered, 0);
    TestEqual(TEXT("aborted old attempt emits no applied signal"), AppliedSignals, 0);
    F.Registry->OnSaveApplied.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveReadNestedTest, "VB.Unit.Save.Read.NestedCycle",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveReadNestedTest::RunTest(const FString& Parameters)
{
    VBSaveReadReentryFixture::FFixture F;
    if (!TestTrue(TEXT("fixture readers registered in order"), F.IsValid()))
    {
        return false;
    }

    const int32 BaselineReaders = F.Registry->GetParticipants().Num();
    int32 FirstReads = 0;
    int32 SecondReads = 0;
    int32 AppliedSignals = 0;
    int32 NestedDuringRead = INDEX_NONE;
    int32 NestedDuringCompletion = INDEX_NONE;
    F.First->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&)
    {
        ++FirstReads;
        TestTrue(TEXT("application phase is active inside reader"), F.Registry->IsApplyingSave());
        NestedDuringRead = F.Save->ApplyActiveSave(F.Context);
    };
    F.Second->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&) { ++SecondReads; };
    F.Registry->OnSaveApplied.AddLambda([&]
    {
        ++AppliedSignals;
        TestFalse(TEXT("completion consumers evaluate outside application"), F.Registry->IsApplyingSave());
        NestedDuringCompletion = F.Registry->ReadAll(*F.Save->GetActiveGameSave(), F.Context);
    });
    const int32 Delivered = F.Save->ApplyActiveSave(F.Context);
    TestEqual(TEXT("normal delivery count includes world participants"), Delivered, BaselineReaders);
    TestEqual(TEXT("first reader called once"), FirstReads, 1);
    TestEqual(TEXT("second reader called once"), SecondReads, 1);
    TestEqual(TEXT("nested read is rejected"), NestedDuringRead, 0);
    TestEqual(TEXT("nested completion read is rejected"), NestedDuringCompletion, 0);
    TestEqual(TEXT("only the outer cycle completes"), AppliedSignals, 1);
    TestFalse(TEXT("application phase is restored after cycle"), F.Registry->IsApplyingSave());
    F.Registry->OnSaveApplied.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveReadDestroyedComponentTest, "VB.Unit.Save.Read.DestroyedComponent",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveReadDestroyedComponentTest::RunTest(const FString& Parameters)
{
    VBSaveReadReentryFixture::FFixture F;
    if (!TestTrue(TEXT("fixture readers registered in order"), F.IsValid()))
    {
        return false;
    }

    const int32 BaselineReaders = F.Registry->GetParticipants().Num();
    int32 FirstReads = 0;
    int32 SecondReads = 0;
    int32 AppliedSignals = 0;
    F.First->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&)
    {
        ++FirstReads;
        F.Second->DestroyComponent();
    };
    F.Second->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&) { ++SecondReads; };
    F.Registry->OnSaveApplied.AddLambda([&] { ++AppliedSignals; });
    const int32 Delivered = F.Registry->ReadAll(*F.Save->GetActiveGameSave(), F.Context);
    TestEqual(TEXT("first reader really destroys the later component"), FirstReads, 1);
    TestFalse(TEXT("destroyed component is no longer registered"), F.Second->IsRegistered());
    TestEqual(TEXT("destroyed component is never called"), SecondReads, 0);
    TestEqual(TEXT("destroyed component is excluded from delivery"), Delivered, BaselineReaders - 1);
    TestEqual(TEXT("remaining valid readers complete normally"), AppliedSignals, 1);
    F.Registry->OnSaveApplied.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveReadWorldAbortTest, "VB.Unit.Save.Read.WorldAbort",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveReadWorldAbortTest::RunTest(const FString& Parameters)
{
    VBSaveReadReentryFixture::FFixture F;
    if (!TestTrue(TEXT("fixture readers registered in order"), F.IsValid()))
    {
        return false;
    }

    int32 FirstReads = 0;
    int32 SecondReads = 0;
    int32 AppliedSignals = 0;
    F.First->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&)
    {
        ++FirstReads;
        F.World->bIsTearingDown = true;
    };
    F.Second->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&) { ++SecondReads; };
    F.Registry->OnSaveApplied.AddLambda([&] { ++AppliedSignals; });
    const int32 Delivered = F.Save->ApplyActiveSave(F.Context);
    TestEqual(TEXT("first reader enters before world shutdown"), FirstReads, 1);
    TestEqual(TEXT("world shutdown stops further delivery"), SecondReads, 0);
    TestEqual(TEXT("world shutdown cannot prepare the player"), Delivered, 0);
    TestEqual(TEXT("world shutdown emits no completion"), AppliedSignals, 0);
    TestFalse(TEXT("application guard unwinds on abort"), F.Registry->IsApplyingSave());
    F.World->bIsTearingDown = false;
    F.Registry->OnSaveApplied.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveReadCompletionAttemptTest, "VB.Unit.Save.Read.CompletionAttemptChange",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveReadCompletionAttemptTest::RunTest(const FString& Parameters)
{
    VBSaveReadReentryFixture::FFixture F;
    if (!TestTrue(TEXT("fixture readers registered in order"), F.IsValid()))
    {
        return false;
    }

    const FGuid Attempt = F.Save->GetGameStartContext().AttemptId;
    int32 FirstReads = 0;
    int32 SecondReads = 0;
    int32 AppliedSignals = 0;
    F.First->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&) { ++FirstReads; };
    F.Second->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&) { ++SecondReads; };
    F.Registry->OnSaveApplied.AddLambda([&]
    {
        ++AppliedSignals;
        F.Save->StartNewGame();
    });
    const int32 Delivered = F.Save->ApplyActiveSave(F.Context);
    TestEqual(TEXT("all fixture readers ran before completion"), FirstReads + SecondReads, 2);
    TestEqual(TEXT("valid old cycle already emitted completion"), AppliedSignals, 1);
    TestNotEqual(TEXT("completion subscriber really replaces the attempt"), F.Save->GetGameStartContext().AttemptId, Attempt);
    TestEqual(TEXT("old result cannot prepare the replaced attempt"), Delivered, 0);
    F.Registry->OnSaveApplied.Clear();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveReadPossessionChangeTest, "VB.Unit.Save.Read.PossessionChange",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveReadPossessionChangeTest::RunTest(const FString& Parameters)
{
    VBSaveReadReentryFixture::FFixture F;
    if (!TestTrue(TEXT("fixture readers registered in order"), F.IsValid()))
    {
        return false;
    }

    APawn* Pawn = F.World->SpawnActor<APawn>();
    APlayerController* Controller = F.World->SpawnActor<APlayerController>();
    APlayerState* PlayerState = F.World->SpawnActor<APlayerState>();
    if (!Pawn || !Controller || !PlayerState)
    {
        return false;
    }

    Controller->PlayerState = PlayerState;
    Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
    Controller->Possess(Pawn);
    if (!TestTrue(TEXT("fixture pawn really belongs to controller"), Controller->GetPawn() == Pawn
        && Pawn->GetController() == Controller && Pawn->GetPlayerState() == PlayerState))
    {
        return false;
    }

    F.Context.Pawn = Pawn;
    F.Context.PlayerController = Controller;
    F.Context.PlayerState = PlayerState;
    int32 FirstReads = 0;
    int32 SecondReads = 0;
    int32 AppliedSignals = 0;
    F.First->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&)
    {
        ++FirstReads;
        Controller->UnPossess();
    };
    F.Second->OnRead = [&](const UVBSaveGame&, const FVBSaveContext&) { ++SecondReads; };
    F.Registry->OnSaveApplied.AddLambda([&] { ++AppliedSignals; });
    const int32 Delivered = F.Save->ApplyActiveSave(F.Context);
    TestEqual(TEXT("first reader really changes possession"), FirstReads, 1);
    TestNull(TEXT("controller really loses its pawn"), Controller->GetPawn());
    TestEqual(TEXT("old pawn context receives no later reads"), SecondReads, 0);
    TestEqual(TEXT("old pawn context cannot prepare a replacement"), Delivered, 0);
    TestEqual(TEXT("old pawn context emits no completion"), AppliedSignals, 0);
    F.Registry->OnSaveApplied.Clear();
    return true;
}
#endif
