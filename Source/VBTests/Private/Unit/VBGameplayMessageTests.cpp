// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 메시지 버스 시험. GI 를 초기화하지 않고 버스를 직접 Initialize/Deinitialize 하며, AsyncActivation 만 떠 있는 게임 GI 를 빌린다.
// K2 노드, 와일드카드 복사, BP 핀은 에디터 모듈의 몫이다. 채널별 스냅숏 계약만 보고 듣기 사이의 순서는 단언하지 않는다.
// 음성 대조는 각 시험 이름(Handle/, Dispatch/, TypeCleanup/, Weak/, Depth/, Async/, Activate/)의 단언이 실패하는 것으로 본다.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "Messages/VBAsyncAction_ListenForGameplayMessage.h"
#include "Subsystems/SubsystemCollection.h"
#include "Unit/VBTestIniTag.h"
#include "UObject/StrongObjectPtr.h"
#include "VBGameplayMessageTestProbe.h"

// 버스의 손상 입력과 비동기 액션의 빌린 값 수명만 준비한다. 핸들은 공개 함수로 시험한다.
class FVBGameplayMessageTestAccess
{
public:
    static int32 Count(const UVBGameplayMessageSubsystem& Bus, FGameplayTag Channel)
    {
        const auto* Entries = Bus.ListenerMap.Find(Channel);
        return Entries ? Entries->Num() : 0;
    }

    static bool LoseOnlyListenerType(UVBGameplayMessageSubsystem& Bus, FGameplayTag Channel)
    {
        auto* Entries = Bus.ListenerMap.Find(Channel);
        if (!Entries || Entries->Num() != 1)
        {
            return false;
        }

        const auto& Entry = (*Entries)[0];
        if (!Entry->bHadValidType || !Entry->StructType.IsValid())
        {
            return false;
        }
        // 이 시험 채널의 유일한 형만 만료시킨다. 전역 GC나 핸들의 내부 번호는 필요 없다.
        Entry->StructType.Reset();
        return true;
    }

    static TArray<int64> EntryIds(const UVBGameplayMessageSubsystem& Bus, FGameplayTag Channel)
    {
        TArray<int64> Result;
        if (const auto* Entries = Bus.ListenerMap.Find(Channel))
        {
            for (const auto& Entry : *Entries)
            {
                Result.Add(Entry->Id);
            }
        }

        return Result;
    }

    static void RemoveAddedEntries(UVBGameplayMessageSubsystem& Bus, FGameplayTag Channel, const TArray<int64>& Baseline)
    {
        // 중복 Activate 음성 대조에서도 이 시험이 흘린 등록을 정리한다.
        // 시험 전부터 있던 등록은 지우지 않는다.
        const TArray<int64> Current = EntryIds(Bus, Channel);
        for (int64 Id : Current)
        {
            if (!Baseline.Contains(Id))
            {
                Bus.UnregisterListenerInternal(Channel, Id);
            }
        }
    }

    static void ConfigureCore(UVBAsyncAction_ListenForGameplayMessage& Action, UScriptStruct* Type)
    {
        Action.MessageStructType = Type;
    }

    static void RegisterCore(UVBGameplayMessageSubsystem& Bus, UVBAsyncAction_ListenForGameplayMessage& Action,
                             FGameplayTag Channel, UScriptStruct* Type)
    {
        Action.MessageStructType = Type;
        const TWeakObjectPtr<UVBAsyncAction_ListenForGameplayMessage> WeakAction(&Action);
        Action.ListenerHandle = Bus.RegisterListenerInternal(Channel,
            [WeakAction](FGameplayTag Actual, const UScriptStruct* ActualType, const void* Bytes)
            {
                if (auto* Receiver = WeakAction.Get())
                {
                    Receiver->HandleMessageReceived(Actual, ActualType, Bytes);
                }
            }, Type, EVBMessageMatch::ExactMatch, &Action);
    }

    static const void* Payload(const UVBAsyncAction_ListenForGameplayMessage& Action)
    {
        return Action.ReceivedMessagePayloadPtr;
    }

    // 엔진 5.8 에서 protected - 이 접근자가 그 액션의 친구라 액션을 통해 부른다.
    static void RegisterWithGameInstance(UVBAsyncAction_ListenForGameplayMessage& Action, UGameInstance* GameInstance)
    {
        Action.RegisterWithGameInstance(GameInstance);
    }

    static void Deliver(UVBAsyncAction_ListenForGameplayMessage& Action, FGameplayTag Channel,
                        const UScriptStruct* Type, const void* Payload)
    {
        Action.HandleMessageReceived(Channel, Type, Payload);
    }
};

namespace VBGameplayMessageTests
{
    // 시험 채널은 ini 태그다(VBTestIniTag.h).
    const FVBTestIniTag TAG_Parent{TEXT("VB.Test.Messages")};
    const FVBTestIniTag TAG_Child{TEXT("VB.Test.Messages.Child")};
    const FVBTestIniTag TAG_Grandchild{TEXT("VB.Test.Messages.Child.Deep")};
    const FVBTestIniTag TAG_AsyncActivation{TEXT("VB.Test.Messages.AsyncActivation")};

    constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
    using Access = FVBGameplayMessageTestAccess;

    struct FBusFixture
    {
        TStrongObjectPtr<UGameInstance> GameInstance;
        TStrongObjectPtr<UVBGameplayMessageSubsystem> Bus;
        FSubsystemCollection<UGameInstanceSubsystem> Collection;

        bool Initialize(FAutomationTestBase& Test)
        {
            GameInstance.Reset(NewObject<UGameInstance>(GetTransientPackage(), NAME_None, RF_Transient));
            if (!Test.TestNotNull(TEXT("Fixture/GI outer without Init"), GameInstance.Get()))
            {
                return false;
            }

            Bus.Reset(NewObject<UVBGameplayMessageSubsystem>(GameInstance.Get(), NAME_None, RF_Transient));
            if (!Test.TestNotNull(TEXT("Fixture/Manual bus"), Bus.Get()))
            {
                return false;
            }
            // Collection 은 Initialize 에 필요한 인자일 뿐 GI.GetSubsystem 흉내가 아니다. static Get 은 여기서 안 본다.
            Bus->Initialize(Collection);
            return Test.TestFalse(TEXT("Fixture/Bus initially running"), Bus->IsDeinitialized());
        }

        ~FBusFixture()
        {
            if (IsValid(Bus.Get()) && !Bus->IsDeinitialized())
            {
                Bus->Deinitialize();
            }

            Bus.Reset();
            GameInstance.Reset();
        }
    };

    UWorld* FindInitializedGameWorld()
    {
        if (!GEngine)
        {
            return nullptr;
        }

        for (const FWorldContext& Context : GEngine->GetWorldContexts())
        {
            if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
                && Context.OwningGameInstance && Context.World()
                && Context.World()->GetGameInstance() == Context.OwningGameInstance)
            {
                return Context.World();
            }
        }

        return nullptr;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesExactAndPartialTest, "VB.Unit.Messages.ExactAndPartial",
    VBGameplayMessageTests::Flags)
bool FVBMessagesExactAndPartialTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    FBusFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }

    auto& Bus = *Fixture.Bus;
    const FGameplayTag Parent = TAG_Parent;
    const FGameplayTag Child = TAG_Child;
    const FGameplayTag Grandchild = TAG_Grandchild;
    int32 Exact = 0, PartialOnSelf = 0, ParentPartial = 0, ParentExact = 0, Descendant = 0;
    FGameplayTag Actual;
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(Child, [&](FGameplayTag Channel, const FVBGameplayMessageTestPayload& Value)
    {
        ++Exact;
        Actual = Channel;
        TestEqual(TEXT("Match/PayloadValue"), Value.Value, 23);
    });
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(Child,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++PartialOnSelf; }, EVBMessageMatch::PartialMatch);
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(Parent,
        [&](FGameplayTag Channel, const FVBGameplayMessageTestPayload&) { ++ParentPartial; Actual = Channel; }, EVBMessageMatch::PartialMatch);
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(Parent,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++ParentExact; });
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(Grandchild,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++Descendant; }, EVBMessageMatch::PartialMatch);
    FVBGameplayMessageTestPayload Payload;
    Payload.Value = 23;
    Bus.BroadcastMessage(Child, Payload);
    TestEqual(TEXT("Match/ExactSelf"), Exact, 1);
    TestEqual(TEXT("Match/PartialSelf"), PartialOnSelf, 1);
    TestEqual(TEXT("Match/ParentPartial"), ParentPartial, 1);
    TestEqual(TEXT("Match/ParentExactExcluded"), ParentExact, 0);
    TestEqual(TEXT("Match/DescendantExcluded"), Descendant, 0);
    TestTrue(TEXT("Match/OriginalChannel"), Actual == Child);

    FVBMessageListenerParams<FVBGameplayMessageTestPayload> Empty;
    const int32 Before = Access::Count(Bus, Child);
    const auto Invalid = Bus.RegisterListener<FVBGameplayMessageTestPayload>(Child, Empty);
    TestFalse(TEXT("Match/EmptyCallbackInvalid"), Invalid.IsValid());
    TestEqual(TEXT("Match/EmptyCallbackNotStored"), Access::Count(Bus, Child), Before);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesTypeCheckTest, "VB.Unit.Messages.TypeCheck",
    VBGameplayMessageTests::Flags)
bool FVBMessagesTypeCheckTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    FBusFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }

    auto& Bus = *Fixture.Bus;
    int32 BaseCalls = 0, UnrelatedCalls = 0, LaterParentCalls = 0;
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload& Value)
        {
            ++BaseCalls;
            TestEqual(TEXT("Type/DerivedBaseValue"), Value.Value, 41);
        });
    Bus.RegisterListener<FVBGameplayMessageTestOtherPayload>(TAG_Child,
        [&](FGameplayTag, const FVBGameplayMessageTestOtherPayload&) { ++UnrelatedCalls; });
    // 같은 채널 callback 순서가 아니라 채널 순회가 이것이 mismatch 뒤에 오는 것을 보장한다
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Parent,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++LaterParentCalls; }, EVBMessageMatch::PartialMatch);
    FVBGameplayMessageTestDerivedPayload Payload;
    Payload.Value = 41;
    Payload.Extra = 99;
    AddExpectedError(TEXT("VBGameplayMessageTestOtherPayload"), EAutomationExpectedErrorFlags::Contains, 1);
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Type/ChildAcceptedByBase"), BaseCalls, 1);
    TestEqual(TEXT("Type/UnrelatedNotCalled"), UnrelatedCalls, 0);
    TestEqual(TEXT("Type/NextListenerStillCalled"), LaterParentCalls, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesHandleLifetimeTest, "VB.Unit.Messages.HandleLifetime",
    VBGameplayMessageTests::Flags)
bool FVBMessagesHandleLifetimeTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    FBusFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }

    auto& Bus = *Fixture.Bus;
    FVBGameplayMessageTestPayload Payload;
    int32 OldCalls = 0, FreshCalls = 0;
    auto Old = Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++OldCalls; });
    if (!TestTrue(TEXT("Handle/RegistrationValid"), Old.IsValid()))
    {
        return false;
    }
    // 각 버스의 첫 듣기는 같은 채널이어도 독립적이다.
    FBusFixture OtherFixture;
    if (!OtherFixture.Initialize(*this))
    {
        return false;
    }

    int32 OtherBusCalls = 0;
    auto OtherBusHandle = OtherFixture.Bus->RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++OtherBusCalls; });
    if (!TestTrue(TEXT("Handle/OtherBusRegistrationValid"), OtherBusHandle.IsValid()))
    {
        return false;
    }

    OtherFixture.Bus->UnregisterListener(Old);
    OtherFixture.Bus->BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Handle/ForeignHandleCannotRemoveOtherBusListener"), OtherBusCalls, 1);
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Handle/ListenerDeliveredBeforeRemoval"), OldCalls, 1);
    auto StaleAlias = Old;
    Old.Unregister();
    TestFalse(TEXT("Handle/UnregisterInvalidatesThisCopy"), Old.IsValid());
    Old.Unregister();
    TestEqual(TEXT("Handle/LastRemovalEmptiesChannel"), Access::Count(Bus, TAG_Child), 0);
    auto Fresh = Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++FreshCalls; });
    StaleAlias.Unregister();
    Old.Unregister();
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Handle/RemovedListenerNotCalledAgain"), OldCalls, 1);
    TestEqual(TEXT("Handle/StaleAliasKeepsNewListener"), FreshCalls, 1);
    Fresh.Unregister();

    // 어느 callback 이 먼저 돌든, 도는 쪽이 지금 dispatch 를 끝내야 한다
    int32 DuringDeinitCalls = 0;
    auto OnDispatch = [&](FGameplayTag, const FVBGameplayMessageTestPayload&)
    {
        ++DuringDeinitCalls;
        Bus.Deinitialize();
    };
    auto HeldHandle = Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child, OnDispatch);
    auto Other = Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child, OnDispatch);
    const TWeakObjectPtr<UVBGameplayMessageSubsystem> BusLifetime(&Bus);
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Handle/DeinitStopsRemainingCallbacks"), DuringDeinitCalls, 1);
    TestTrue(TEXT("Handle/FixtureBusStillAlive"), BusLifetime.IsValid());
    TestTrue(TEXT("Handle/BusIsDeinitialized"), Bus.IsDeinitialized());
    TestFalse(TEXT("Handle/DeinitInvalidatesHandleBeforeGC"), HeldHandle.IsValid());
    TestFalse(TEXT("Handle/DeinitInvalidatesOtherHandle"), Other.IsValid());
    TestEqual(TEXT("Handle/DeinitEmptiesEntries"), Access::Count(Bus, TAG_Child), 0);

    // 자기 버스만 쓰레기로 표시한다. 강한 fixture 참조와 별개로 약한 조회는 실패해야 한다.
    Bus.MarkAsGarbage();
    TestFalse(TEXT("Handle/GoneBusWeakLookupFails"), BusLifetime.IsValid());
    HeldHandle.Unregister();
    TestFalse(TEXT("Handle/UnregisterInvalidWhenBusGone"), HeldHandle.IsValid());
    HeldHandle.Unregister();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesDispatchTest, "VB.Unit.Messages.UnregisterDuringDispatch",
    VBGameplayMessageTests::Flags)
bool FVBMessagesDispatchTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    FVBGameplayMessageTestPayload Payload;
    {
        FBusFixture Fixture;
        if (!Fixture.Initialize(*this))
        {
            return false;
        }

        auto& Bus = *Fixture.Bus;
        FVBMessageListenerHandle Left, Right;
        int32 LeftCalls = 0, RightCalls = 0;
        Left = Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
            [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++LeftCalls; Right.Unregister(); });
        Right = Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
            [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++RightCalls; Left.Unregister(); });
        Bus.BroadcastMessage(TAG_Child, Payload);
        TestEqual(TEXT("Dispatch/ExactlyOneSurvivesMutualCancel"), LeftCalls + RightCalls, 1);
        TestEqual(TEXT("Dispatch/CancelledEntryRemoved"), Access::Count(Bus, TAG_Child), 1);
    }

    {
        FBusFixture Fixture;
        if (!Fixture.Initialize(*this))
        {
            return false;
        }

        auto& Bus = *Fixture.Bus;
        bool bRegistered = false;
        int32 AddedCalls = 0;
        Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
            [&](FGameplayTag, const FVBGameplayMessageTestPayload&)
            {
                if (!bRegistered)
                {
                    bRegistered = true;
                    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
                        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++AddedCalls; });
                }
            });
        Bus.BroadcastMessage(TAG_Child, Payload);
        TestEqual(TEXT("Dispatch/SameChannelNewNotInOuterSnapshot"), AddedCalls, 0);
        Bus.BroadcastMessage(TAG_Child, Payload);
        TestEqual(TEXT("Dispatch/SameChannelNewInNextBroadcast"), AddedCalls, 1);
    }

    {
        FBusFixture Fixture;
        if (!Fixture.Initialize(*this))
        {
            return false;
        }

        auto& Bus = *Fixture.Bus;
        bool bRegistered = false;
        int32 AddedCalls = 0;
        FGameplayTag Actual;
        Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
            [&](FGameplayTag, const FVBGameplayMessageTestPayload&)
            {
                if (!bRegistered)
                {
                    bRegistered = true;
                    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Parent,
                        [&](FGameplayTag Channel, const FVBGameplayMessageTestPayload&) { ++AddedCalls; Actual = Channel; },
                        EVBMessageMatch::PartialMatch);
                }
            });
        Bus.BroadcastMessage(TAG_Child, Payload);
        TestEqual(TEXT("Dispatch/UnvisitedAncestorNewCalledInOuter"), AddedCalls, 1);
        TestTrue(TEXT("Dispatch/AncestorStillSeesOriginalChannel"), Actual == FGameplayTag(TAG_Child));
    }

    {
        FBusFixture Fixture;
        if (!Fixture.Initialize(*this))
        {
            return false;
        }

        auto& Bus = *Fixture.Bus;
        bool bRegistered = false;
        TArray<int32> AddedValues;
        Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
            [&](FGameplayTag, const FVBGameplayMessageTestPayload&)
            {
                if (!bRegistered)
                {
                    bRegistered = true;
                    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
                        [&](FGameplayTag, const FVBGameplayMessageTestPayload& Value) { AddedValues.Add(Value.Value); });
                    FVBGameplayMessageTestPayload Nested;
                    Nested.Value = 202;
                    Bus.BroadcastMessage(TAG_Child, Nested);
                }
            });
        Payload.Value = 101;
        Bus.BroadcastMessage(TAG_Child, Payload);
        TestEqual(TEXT("Dispatch/NestedBroadcastSeesNewListener"), AddedValues.Num(), 1);
        if (AddedValues.Num() == 1)
        {
            TestEqual(TEXT("Dispatch/NewListenerGetsOnlyNestedValue"), AddedValues[0], 202);
        }
    }

    {
        FBusFixture Fixture;
        if (!Fixture.Initialize(*this))
        {
            return false;
        }

        auto& Bus = *Fixture.Bus;
        FVBMessageListenerHandle Self;
        int32 OldCalls = 0, NewCalls = 0;
        Self = Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
            [&](FGameplayTag, const FVBGameplayMessageTestPayload&)
            {
                ++OldCalls;
                Self.Unregister();
                Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
                    [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++NewCalls; });
            });
        Bus.BroadcastMessage(TAG_Child, Payload);
        TestEqual(TEXT("Dispatch/RecreatedChannelOldCalledOnce"), OldCalls, 1);
        TestEqual(TEXT("Dispatch/RecreatedChannelNotInOldSnapshot"), NewCalls, 0);
        TestEqual(TEXT("Dispatch/RecreatedChannelHasOneEntry"), Access::Count(Bus, TAG_Child), 1);
        Bus.BroadcastMessage(TAG_Child, Payload);
        TestEqual(TEXT("Dispatch/RecreatedChannelNextBroadcastWorks"), NewCalls, 1);
        TestEqual(TEXT("Dispatch/RecreatedChannelOldDoesNotReturn"), OldCalls, 1);
    }

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesWeakMemberTest, "VB.Unit.Messages.WeakMemberPrune",
    VBGameplayMessageTests::Flags)
bool FVBMessagesWeakMemberTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    FBusFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }

    auto& Bus = *Fixture.Bus;
    FVBGameplayMessageTestPayload Payload;
    Payload.Value = 31;
    TStrongObjectPtr<UVBGameplayMessageTestProbe> Direct(NewObject<UVBGameplayMessageTestProbe>());
    TStrongObjectPtr<UVBGameplayMessageTestProbe> ParamsOwner(NewObject<UVBGameplayMessageTestProbe>());
    if (!TestNotNull(TEXT("Weak/DirectOwner"), Direct.Get())
        || !TestNotNull(TEXT("Weak/ParamsOwner"), ParamsOwner.Get()))
    {
        return false;
    }

    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child, Direct.Get(), &UVBGameplayMessageTestProbe::ReceiveMember);
    FVBMessageListenerParams<FVBGameplayMessageTestPayload> Params;
    Params.MatchType = EVBMessageMatch::PartialMatch;
    Params.SetMessageReceivedCallback(ParamsOwner.Get(), &UVBGameplayMessageTestProbe::ReceiveMember);
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Parent, Params);
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Weak/DirectActuallyDeliveredBeforeDeath"), Direct->MemberCalls, 1);
    TestEqual(TEXT("Weak/ParamsActuallyDeliveredBeforeDeath"), ParamsOwner->MemberCalls, 1);
    TestEqual(TEXT("Weak/ParamsPayloadCopied"), ParamsOwner->LastValue, 31);
    TestTrue(TEXT("Weak/ParamsOriginalChannel"), ParamsOwner->LastChannel == FGameplayTag(TAG_Child));
    TestEqual(TEXT("Weak/DirectEntryBeforeDeath"), Access::Count(Bus, TAG_Child), 1);
    TestEqual(TEXT("Weak/ParamsEntryBeforeDeath"), Access::Count(Bus, TAG_Parent), 1);
    Direct->MarkAsGarbage();
    ParamsOwner->MarkAsGarbage();
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Weak/DirectNoCallAfterDeath"), Direct->MemberCalls, 1);
    TestEqual(TEXT("Weak/ParamsNoCallAfterDeath"), ParamsOwner->MemberCalls, 1);
    TestEqual(TEXT("Weak/DirectEntryRemoved"), Access::Count(Bus, TAG_Child), 0);
    TestEqual(TEXT("Weak/ParamsEntryRemoved"), Access::Count(Bus, TAG_Parent), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesAncestorTypeCleanupTest, "VB.Unit.Messages.AncestorTypeCleanup",
    VBGameplayMessageTests::Flags)
bool FVBMessagesAncestorTypeCleanupTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    FBusFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }

    auto& Bus = *Fixture.Bus;
    int32 DeadTypeCalls = 0, ChildCalls = 0;
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Parent,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++DeadTypeCalls; }, EVBMessageMatch::PartialMatch);
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload&) { ++ChildCalls; });
    if (!TestTrue(TEXT("TypeCleanup/ExistingValidTypeInvalidated"), Access::LoseOnlyListenerType(Bus, TAG_Parent)))
    {
        return false;
    }

    FVBGameplayMessageTestPayload Payload;
    // 잘못된 타입 경로는 막아야 할 Error 가 아니라 Warning 이다
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("TypeCleanup/ExpiredTypeNeverCalled"), DeadTypeCalls, 0);
    TestEqual(TEXT("TypeCleanup/AncestorRemoved"), Access::Count(Bus, TAG_Parent), 0);
    TestEqual(TEXT("TypeCleanup/UnrelatedChildEntryRetained"), Access::Count(Bus, TAG_Child), 1);
    TestEqual(TEXT("TypeCleanup/ChildDelivered"), ChildCalls, 1);
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("TypeCleanup/ChildStillDeliveredNextTime"), ChildCalls, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesRecursionTest, "VB.Unit.Messages.RecursionGuard",
    VBGameplayMessageTests::Flags)
bool FVBMessagesRecursionTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    FBusFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }

    auto& Bus = *Fixture.Bus;
    int32 Calls = 0;
    int32 RequestedDepth = 16;
    auto SendNext = [&](FGameplayTag Next, const FVBGameplayMessageTestPayload& Value)
    {
        ++Calls;
        // 두 listener 가 서로 보낸다. 가드를 빼도 유한하다(17 번 호출)
        if (Value.Value < RequestedDepth)
        {
            FVBGameplayMessageTestPayload Nested;
            Nested.Value = Value.Value + 1;
            Bus.BroadcastMessage(Next, Nested);
        }
    };
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Child,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload& Value) { SendNext(TAG_Grandchild, Value); });
    Bus.RegisterListener<FVBGameplayMessageTestPayload>(TAG_Grandchild,
        [&](FGameplayTag, const FVBGameplayMessageTestPayload& Value) { SendNext(TAG_Child, Value); });
    FVBGameplayMessageTestPayload Payload;
    Payload.Value = 1;
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Depth/SixteenAccepted"), Calls, 16);
    TestEqual(TEXT("Depth/ConfiguredLimitIsSixteen"), UVBGameplayMessageSubsystem::MaxBroadcastDepth, 16);
    Calls = 0;
    RequestedDepth = 17;
    AddExpectedError(TEXT("16"), EAutomationExpectedErrorFlags::Contains, 1);
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Depth/SeventeenthDropped"), Calls, 16);
    Calls = 0;
    RequestedDepth = 1;
    Bus.BroadcastMessage(TAG_Child, Payload);
    TestEqual(TEXT("Depth/IndependentWorks"), Calls, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesAsyncCoreTest, "VB.Unit.Messages.AsyncAction",
    VBGameplayMessageTests::Flags)
bool FVBMessagesAsyncCoreTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    FBusFixture Fixture;
    if (!Fixture.Initialize(*this))
    {
        return false;
    }

    TStrongObjectPtr<UVBAsyncAction_ListenForGameplayMessage> Action(NewObject<UVBAsyncAction_ListenForGameplayMessage>());
    TStrongObjectPtr<UVBGameplayMessageTestProbe> Probe(NewObject<UVBGameplayMessageTestProbe>());
    if (!TestNotNull(TEXT("Async/Action"), Action.Get()) || !TestNotNull(TEXT("Async/Probe"), Probe.Get()))
    {
        return false;
    }

    ON_SCOPE_EXIT { Action->Cancel(); };
    Access::ConfigureCore(*Action, FVBGameplayMessageTestPayload::StaticStruct());
    int32 CallbackCount = 0;
    FVBGameplayMessageTestPayload Outer, Inner;
    Outer.Value = 10;
    Inner.Value = 20;
    Probe->OnAsync = [&](UVBAsyncAction_ListenForGameplayMessage* Proxy, FGameplayTag Channel)
    {
        ++CallbackCount;
        TestTrue(TEXT("Async/ProxyIsAction"), Proxy == Action.Get());
        TestTrue(TEXT("Async/OriginalChannel"), Channel == FGameplayTag(TAG_Child));
        const auto* Current = static_cast<const FVBGameplayMessageTestPayload*>(Access::Payload(*Proxy));
        if (!TestNotNull(TEXT("Async/PayloadExistsOnlyDuringCallback"), Current))
        {
            return;
        }

        if (Current->Value == Outer.Value)
        {
            TestTrue(TEXT("Async/OuterPointerBorrowed"), Current == &Outer);
            Fixture.Bus->BroadcastMessage(TAG_Child, Inner);
            TestTrue(TEXT("Async/OuterRestored"), Access::Payload(*Proxy) == &Outer);
            if (Access::Payload(*Proxy))
            {
                TestEqual(TEXT("Async/OuterValueRestored"), static_cast<const FVBGameplayMessageTestPayload*>(Access::Payload(*Proxy))->Value, 10);
            }
        }
        else if (Current->Value == Inner.Value)
        {
            TestTrue(TEXT("Async/NestedPointerBorrowed"), Current == &Inner);
        }
        else
        {
            TestEqual(TEXT("Async/DerivedBaseValue"), Current->Value, 30);
        }
    };
    Action->OnMessageReceived.AddDynamic(Probe.Get(), &UVBGameplayMessageTestProbe::ReceiveAsync);
    TestFalse(TEXT("Async/NotActiveBeforeGIRegistration"), Action->IsActive());
    Access::Deliver(*Action, TAG_Child, FVBGameplayMessageTestPayload::StaticStruct(), &Inner);
    TestEqual(TEXT("Async/UnregisteredDeliveryIgnored"), CallbackCount, 0);
    Access::RegisterWithGameInstance(*Action, Fixture.GameInstance.Get());
    TestTrue(TEXT("Async/ActiveAfterGIRegistration"), Action->IsActive());
    Access::RegisterCore(*Fixture.Bus, *Action, TAG_Child, FVBGameplayMessageTestPayload::StaticStruct());
    TestEqual(TEXT("Async/NativeCoreRegisteredOnManualBus"), Access::Count(*Fixture.Bus, TAG_Child), 1);
    TestTrue(TEXT("Async/NoPayloadBeforeDelivery"), Access::Payload(*Action) == nullptr);
    Fixture.Bus->BroadcastMessage(TAG_Child, Outer);
    TestEqual(TEXT("Async/OuterAndNestedCallbacks"), CallbackCount, 2);
    TestTrue(TEXT("Async/NoPayloadAfterDelivery"), Access::Payload(*Action) == nullptr);
    FVBGameplayMessageTestDerivedPayload Derived;
    Derived.Value = 30;
    Derived.Extra = 777;
    Fixture.Bus->BroadcastMessage(TAG_Child, Derived);
    TestEqual(TEXT("Async/DerivedAcceptedByConfiguredBase"), CallbackCount, 3);
    FVBGameplayMessageTestOtherPayload Unrelated;
    Access::Deliver(*Action, TAG_Child, FVBGameplayMessageTestOtherPayload::StaticStruct(), &Unrelated);
    TestEqual(TEXT("Async/UnrelatedCoreDeliveryIgnored"), CallbackCount, 3);
    TestTrue(TEXT("Async/StillActiveWhileDelegateBound"), Action->IsActive());
    Action->Cancel();
    TestFalse(TEXT("Async/CancelReleasesGIRegistration"), Action->IsActive());
    TestEqual(TEXT("Async/CancelRemovesManualBusListener"), Access::Count(*Fixture.Bus, TAG_Child), 0);
    // Cancel 전에 이미 복사된 router callback 을 흉내낸다(UObject 는 강하게 쥔다)
    Access::Deliver(*Action, TAG_Child, FVBGameplayMessageTestPayload::StaticStruct(), &Inner);
    TestEqual(TEXT("Async/CancelledDeliveryIgnored"), CallbackCount, 3);
    TestTrue(TEXT("Async/CancelledDeliveryHasNoBorrowedPayload"), Access::Payload(*Action) == nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBMessagesAsyncActivationTest, "VB.Unit.Messages.AsyncActivation",
    VBGameplayMessageTests::Flags)
bool FVBMessagesAsyncActivationTest::RunTest(const FString& Parameters)
{
    using namespace VBGameplayMessageTests;
    UWorld* World = FindInitializedGameWorld();
    if (!TestNotNull(TEXT("Activate/Existing initialized GI world required; run headless -game or PIE"), World))
    {
        return false;
    }

    UVBGameplayMessageSubsystem* Bus = World->GetGameInstance()->GetSubsystem<UVBGameplayMessageSubsystem>();
    if (!TestNotNull(TEXT("Activate/Real GI collection contains production bus"), Bus))
    {
        return false;
    }

    if (!TestFalse(TEXT("Activate/Real bus running"), Bus->IsDeinitialized()))
    {
        return false;
    }

    const FGameplayTag Channel = TAG_AsyncActivation;
    const TArray<int64> Baseline = Access::EntryIds(*Bus, Channel);
    TStrongObjectPtr<UVBAsyncAction_ListenForGameplayMessage> Action(
        UVBAsyncAction_ListenForGameplayMessage::ListenForGameplayMessages(World, Channel, FVBGameplayMessageTestPayload::StaticStruct()));
    TStrongObjectPtr<UVBGameplayMessageTestProbe> Probe(NewObject<UVBGameplayMessageTestProbe>());
    if (!TestNotNull(TEXT("Activate/Public factory action"), Action.Get()) || !TestNotNull(TEXT("Activate/Probe"), Probe.Get()))
    {
        return false;
    }

    ON_SCOPE_EXIT
    {
        Action->Cancel();
        Access::RemoveAddedEntries(*Bus, Channel, Baseline);
    };
    int32 Calls = 0;
    Probe->OnAsync = [&](UVBAsyncAction_ListenForGameplayMessage* Proxy, FGameplayTag Actual)
    {
        ++Calls;
        TestTrue(TEXT("Activate/ActualChannel"), Actual == Channel);
        const auto* Value = static_cast<const FVBGameplayMessageTestPayload*>(Access::Payload(*Proxy));
        if (TestNotNull(TEXT("Activate/BusDeliversBorrowedPayload"), Value))
        {
            TestEqual(TEXT("Activate/BusPayloadValue"), Value->Value, 57);
        }
    };
    Action->OnMessageReceived.AddDynamic(Probe.Get(), &UVBGameplayMessageTestProbe::ReceiveAsync);
    TestTrue(TEXT("Activate/FactoryRetainsInGI"), Action->IsActive());
    Action->Activate();
    TestEqual(TEXT("Activate/FirstRegistersOne"), Access::Count(*Bus, Channel), Baseline.Num() + 1);
    Action->Activate();
    TestEqual(TEXT("Activate/SecondDoesNotRegister"), Access::Count(*Bus, Channel), Baseline.Num() + 1);
    FVBGameplayMessageTestPayload Payload;
    Payload.Value = 57;
    Bus->BroadcastMessage(Channel, Payload);
    TestEqual(TEXT("Activate/OneCallbackAfterTwoActivations"), Calls, 1);
    Action->Cancel();
    TestFalse(TEXT("Activate/CancelInactive"), Action->IsActive());
    TestEqual(TEXT("Activate/CancelRemovesItsEntry"), Access::Count(*Bus, Channel), Baseline.Num());
    Bus->BroadcastMessage(Channel, Payload);
    TestEqual(TEXT("Activate/NoCallbackAfterCancel"), Calls, 1);
    Action->Activate();
    TestEqual(TEXT("Activate/CancelledActionCannotActivateAgain"), Access::Count(*Bus, Channel), Baseline.Num());
    TestFalse(TEXT("Activate/CancelledActionStaysInactive"), Action->IsActive());
    return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
