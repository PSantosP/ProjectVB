#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/ScopeExit.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "VBSaveGateFixtureParticipant.h"
#include "VBSaveGateTestAccess.h"


namespace
{
	struct FSaveFixture
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
		TStrongObjectPtr<UVBSaveGameSubsystem> Save{NewObject<UVBSaveGameSubsystem>(Instance.Get())};
		int32 Dispatches = 0;
		TArray<FAsyncSaveGameToSlotDelegate> Pending;
		TArray<FString> DeletedSlots;
		int32 DeletedUserIndex = INDEX_NONE;
		FSaveFixture()
		{
			// 플랫폼 디스크에 닿지 않는다. 예상 밖 gate 누락도 실제 슬롯을 덮지 못한다.
			FVBSaveGateTestAccess::SetDispatch(*Save.Get(), [this](UVBSaveGame*, const FString&, int32,
				const FAsyncSaveGameToSlotDelegate& Completion) { ++Dispatches; Pending.Add(Completion); });
			FVBSaveGateTestAccess::SetDeleteDispatch(*Save.Get(), [this](const FString& Slot, int32 UserIndex)
			{
				DeletedSlots.Add(Slot);
				DeletedUserIndex = UserIndex;
			});
			Save->StartNewGame();
		}

		~FSaveFixture()
		{
			FVBSaveGateTestAccess::RestoreDisk(*Save.Get());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSavePublicGateTest, "VB.Unit.Save.Gate.PublicAndReentry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSavePublicGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	UVBSaveGame* Before = F.Save->GetActiveGameSave();
	Before->Progress.PlayTimeSeconds = 7.0;
	const FVBSaveHoldToken Hold = F.Save->BeginSaveHold(TEXT("Fixture"));
	if (!TestTrue(TEXT("Hold admitted"), Hold.IsValid()))
	{
		return false;
	}

	int32 Refusals = 0;
	const FDelegateHandle Handle = F.Save->OnGameSaveRefused.AddLambda([&](int32 Slot, FName Reason)
	{
		++Refusals;
		TestEqual(TEXT("Refusal is not disk failure"), Reason, FName(TEXT("Held")));
		F.Save->SaveGameSlot(Slot);
	});
	F.Save->SaveGameSlot(0);
	TestEqual(TEXT("Nested refusal does not recurse"), Refusals, 1);
	TestEqual(TEXT("No transport while held"), F.Dispatches, 0);
	TestTrue(TEXT("Active object preserved"), F.Save->GetActiveGameSave() == Before);
	TestEqual(TEXT("Active data preserved"), Before->Progress.PlayTimeSeconds, 7.0);
	TestEqual(TEXT("Slot not selected"), F.Save->GetActiveSlotIndex(), INDEX_NONE);
	F.Save->OnGameSaveRefused.Remove(Handle);
	F.Save->EndSaveHold(Hold);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSavePendingGateTest, "VB.Unit.Save.Gate.PendingAndSyncCompletion",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSavePendingGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	F.Save->SaveGameSlot(0);
	F.Save->SaveGameSlot(0);
	TestEqual(TEXT("Same slot is not submitted twice"), F.Dispatches, 1);
	TestFalse(TEXT("Scene cannot protect an already submitted write"), F.Save->BeginSaveHold(TEXT("Scene")).IsValid());
	F.Pending[0].ExecuteIfBound(TEXT("Mock"), 0, false);
	FVBSaveHoldToken Hold = F.Save->BeginSaveHold(TEXT("Scene"));
	TestTrue(TEXT("False completion still ends in-flight admission"), Hold.IsValid());
	F.Save->EndSaveHold(Hold);
	FVBSaveGateTestAccess::SetDispatch(*F.Save.Get(), [&](UVBSaveGame*, const FString& Slot, int32 User,
		const FAsyncSaveGameToSlotDelegate& Completion)
	{
		TestFalse(TEXT("Transport call has already reserved its ID"), F.Save->BeginSaveHold(TEXT("InsideDispatch")).IsValid());
		Completion.ExecuteIfBound(Slot, User, false);
	});
	F.Save->SaveGameSlot(1);
	TestTrue(TEXT("Synchronous completion leaves no orphaned request"), F.Save->BeginSaveHold(TEXT("AfterSync")).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveDeleteGateTest, "VB.Unit.Save.Gate.DeleteProtection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveDeleteGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	F.Save->StartNewGameInSlot(0);
	if (!TestEqual(TEXT("Slot seed has an in-flight request"), F.Pending.Num(), 1))
	{
		return false;
	}

	F.Save->DeleteGameSave(0);
	TestEqual(TEXT("Pending seed cannot be deleted"), F.DeletedSlots.Num(), 0);
	F.Pending[0].ExecuteIfBound(TEXT("Mock"), 0, false);
	const FVBSaveHoldToken Hold = F.Save->BeginSaveHold(TEXT("Scene"));
	if (!TestTrue(TEXT("Scene hold admitted after transport completion"), Hold.IsValid()))
	{
		return false;
	}

	F.Save->DeleteGameSave(0);
	TestEqual(TEXT("Held active slot cannot be deleted"), F.DeletedSlots.Num(), 0);
	TestTrue(TEXT("Rejected deletion preserves hold"), F.Save->IsSaveHoldActive(Hold));
	TestEqual(TEXT("Rejected deletion preserves active slot"), F.Save->GetActiveSlotIndex(), 0);
	F.Save->DeleteGameSave(1);
	TestEqual(TEXT("Held inactive slot can be deleted"), F.DeletedSlots.Num(), 1);
	if (F.DeletedSlots.Num() == 1)
	{
		TestEqual(TEXT("Delete retains slot naming"), F.DeletedSlots[0], UVBSaveGameSubsystem::MakeGameSlotName(1));
	}

	TestEqual(TEXT("Delete retains user index"), F.DeletedUserIndex, 0);
	F.Save->DeleteGameSave(-1);
	F.Save->DeleteGameSave(F.Save->GetMaxSlotCount());
	TestEqual(TEXT("Invalid slots never reach transport"), F.DeletedSlots.Num(), 1);
	const FDelegateHandle Refusal = F.Save->OnGameSaveRefused.AddLambda([&](int32, FName)
	{
		F.Save->DeleteGameSave(2);
	});
	F.Save->SaveGameSlot(0);
	F.Save->OnGameSaveRefused.Remove(Refusal);
	TestEqual(TEXT("Refusal callback cannot reenter delete transport"), F.DeletedSlots.Num(), 1);
	F.Save->EndSaveHold(Hold);
	F.Save->DeleteGameSave(0);
	TestEqual(TEXT("Released active slot can be deleted"), F.DeletedSlots.Num(), 2);
	F.Save->SaveGameSlot(0);
	if (!TestEqual(TEXT("Second write captured"), F.Pending.Num(), 2))
	{
		return false;
	}

	F.Save->DeleteGameSave(0);
	TestEqual(TEXT("Same-slot write prevents deletion"), F.DeletedSlots.Num(), 2);
	F.Save->DeleteGameSave(2);
	TestEqual(TEXT("Other-slot write does not block unrelated delete"), F.DeletedSlots.Num(), 3);
	const FDelegateHandle Changing = F.Save->OnGameStartContextChanged.AddLambda([&]()
	{
		F.Save->DeleteGameSave(1);
	});
	F.Save->StartNewGame();
	F.Save->OnGameStartContextChanged.Remove(Changing);
	TestEqual(TEXT("Start-context callback cannot reenter delete transport"), F.DeletedSlots.Num(), 3);
	F.Pending[1].ExecuteIfBound(TEXT("Mock"), 0, false);
	F.Save->Deinitialize();
	F.Save->DeleteGameSave(1);
	TestEqual(TEXT("Stopped subsystem cannot delete"), F.DeletedSlots.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveGenerationGateTest, "VB.Unit.Save.Gate.GenerationAndBootReceipt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveGenerationGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	const FGuid OldAttempt = F.Save->GetGameStartContext().AttemptId;
	const FVBSaveHoldToken OldHold = F.Save->BeginSaveHold(TEXT("Old"));
	TestTrue(TEXT("First explicit new game bootstrap"), F.Save->ConsumeInitialNewGameStart(OldAttempt));
	TestFalse(TEXT("Same attempt does not reboot"), F.Save->ConsumeInitialNewGameStart(OldAttempt));
	F.Save->StartNewGame();
	TestFalse(TEXT("New game discards previous holds"), F.Save->IsGameSaveHeld());
	const FVBSaveHoldToken NewHold = F.Save->BeginSaveHold(TEXT("New"));
	TestFalse(TEXT("Old token cannot release new hold"), F.Save->EndSaveHold(OldHold));
	TestTrue(TEXT("New hold survives old release"), F.Save->IsSaveHoldActive(NewHold));
	TestFalse(TEXT("Old bootstrap receipt is rejected"), F.Save->ConsumeInitialNewGameStart(OldAttempt));
	TestTrue(TEXT("New attempt has its own receipt"), F.Save->ConsumeInitialNewGameStart(F.Save->GetGameStartContext().AttemptId));
	TestEqual(TEXT("New game itself never writes disk"), F.Dispatches, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveSnapshotGateTest, "VB.Unit.Save.Gate.SnapshotReentry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveSnapshotGateTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Owned world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	World->SetGameInstance(F.Instance.Get());
	APlayerController* PC = World->SpawnActor<APlayerController>();
	APawn* Pawn = World->SpawnActor<APawn>();
	APlayerState* State = World->SpawnActor<APlayerState>();
	if (!PC || !Pawn || !State)
	{
		World->DestroyWorld(false);
		Package->SetDirtyFlag(false);
		return false;
	}

	PC->PlayerState = State;
	Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
	PC->Possess(Pawn);
	UVBSaveGateFixtureParticipant* Writer = NewObject<UVBSaveGateFixtureParticipant>(Pawn);
	Writer->RegisterComponent();
	UVBSaveParticipantSubsystem::Register(Writer);
	UVBSaveGame* Before = F.Save->GetActiveGameSave();
	Before->Progress.PlayTimeSeconds = 7.0;
	int32 Writes = 0;
	Writer->OnWrite = [&](UVBSaveGame& Snapshot, const FVBSaveContext&)
	{
		++Writes;
		Snapshot.Progress.PlayTimeSeconds = 99.0;
		F.Save->DeleteGameSave(0);
		F.Save->BeginSaveHold(TEXT("DuringCollection"));
	};
	TestFalse(TEXT("New hold aborts current collection"), F.Save->SaveCurrentGameToSlot(Pawn, 0));
	TestEqual(TEXT("Actual participant callback entered"), Writes, 1);
	TestTrue(TEXT("Published object is unchanged"), F.Save->GetActiveGameSave() == Before);
	TestEqual(TEXT("Partial snapshot never published"), Before->Progress.PlayTimeSeconds, 7.0);
	TestEqual(TEXT("Aborted snapshot never dispatches"), F.Dispatches, 0);
	TestEqual(TEXT("Snapshot writer cannot reenter delete transport"), F.DeletedSlots.Num(), 0);
	F.Save->StartNewGame();
	Writer->OnWrite = [&](UVBSaveGame& Snapshot, const FVBSaveContext&)
	{
		++Writes;
		Snapshot.Progress.PlayTimeSeconds = 99.0;
		F.Save->StartNewGame();
	};
	TestFalse(TEXT("New generation aborts current collection"), F.Save->SaveCurrentGameToSlot(Pawn, 0));
	TestEqual(TEXT("Second actual participant callback entered"), Writes, 2);
	TestEqual(TEXT("New generation stays clean"), F.Save->GetActiveGameSave()->Progress.PlayTimeSeconds, 0.0);
	TestEqual(TEXT("No transport from either aborted generation"), F.Dispatches, 0);
	Writer->OnWrite = {};
	UVBSaveParticipantSubsystem::Unregister(Writer);
	World->DestroyWorld(false);
	// 직접 만든 월드 패키지 하나만 정리한다. 다른 패키지/프로젝트 자산에는 쓰지 않는다.
	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveCaptureBoundaryTest, "VB.Unit.Save.Gate.CaptureBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveCaptureBoundaryTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
		ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("owned capture world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->SetGameInstance(F.Instance.Get());
	APlayerController* PC = World->SpawnActor<APlayerController>();
	APawn* Pawn = World->SpawnActor<APawn>();
	APlayerState* State = World->SpawnActor<APlayerState>();
	if (!PC || !Pawn || !State)
	{
		return false;
	}

	PC->PlayerState = State;
	Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
	PC->Possess(Pawn);
	UVBSaveParticipantSubsystem* Registry = World->GetSubsystem<UVBSaveParticipantSubsystem>();
	UVBSaveGateFixtureParticipant* First = NewObject<UVBSaveGateFixtureParticipant>(Pawn);
	UVBSaveGateFixtureParticipant* Second = NewObject<UVBSaveGateFixtureParticipant>(Pawn);
	First->RegisterComponent();
	Second->RegisterComponent();
	UVBSaveParticipantSubsystem::Register(First);
	UVBSaveParticipantSubsystem::Register(Second);
	if (!TestNotNull(TEXT("capture registry"), Registry))
	{
		return false;
	}

	const TArray<IVBSaveParticipant*> Participants = Registry->GetParticipants();
	const int32 FirstIndex = Participants.IndexOfByKey(static_cast<IVBSaveParticipant*>(First));
	const int32 SecondIndex = Participants.IndexOfByKey(static_cast<IVBSaveParticipant*>(Second));
	if (!TestTrue(TEXT("capture writers registered in order"), FirstIndex != INDEX_NONE && SecondIndex > FirstIndex))
	{
		return false;
	}

	UVBSaveGame* Active = F.Save->GetActiveGameSave();
	Active->Progress.PlayTimeSeconds = 7.0;
	const int32 BeforeSlot = FVBSaveGateTestAccess::GetActiveSlot(*F.Save.Get());
	const int32 TargetSlot = BeforeSlot == 2 ? 1 : 2;
	int32 FirstWrites = 0;
	int32 SecondWrites = 0;
	First->OnWrite = [&](UVBSaveGame& Snapshot, const FVBSaveContext&)
	{
		++FirstWrites;
		Snapshot.Progress.PlayTimeSeconds = 99.0;
		UVBSaveParticipantSubsystem::Unregister(Second);
	};
	Second->OnWrite = [&](UVBSaveGame&, const FVBSaveContext&) { ++SecondWrites; };
	TestFalse(TEXT("registry change aborts capture"), F.Save->SaveCurrentGameToSlot(Pawn, TargetSlot));
	TestEqual(TEXT("first writer entered"), FirstWrites, 1);
	TestEqual(TEXT("original registered-component snapshot still visited"), SecondWrites, 1);
	TestTrue(TEXT("aborted capture preserves active object"), F.Save->GetActiveGameSave() == Active);
	TestEqual(TEXT("aborted capture preserves published properties"), Active->Progress.PlayTimeSeconds, 7.0);
	TestEqual(TEXT("aborted capture preserves active slot"), FVBSaveGateTestAccess::GetActiveSlot(*F.Save.Get()), BeforeSlot);
	TestEqual(TEXT("aborted capture submits nothing"), F.Dispatches, 0);
	UVBSaveParticipantSubsystem::Register(Second);
	First->OnWrite = [&](UVBSaveGame& Snapshot, const FVBSaveContext&) { Snapshot.Progress.PlayTimeSeconds = 99.0; };
	TestTrue(TEXT("unchanged registry capture submits"), F.Save->SaveCurrentGameToSlot(Pawn, TargetSlot));
	TestTrue(TEXT("success preserves active object"), F.Save->GetActiveGameSave() == Active);
	TestEqual(TEXT("success publishes captured property"), Active->Progress.PlayTimeSeconds, 99.0);
	TestEqual(TEXT("success updates active slot"), FVBSaveGateTestAccess::GetActiveSlot(*F.Save.Get()), TargetSlot);
	TestEqual(TEXT("success submits once"), F.Dispatches, 1);
	First->OnWrite = {};
	Second->OnWrite = {};
	UVBSaveParticipantSubsystem::Unregister(First);
	UVBSaveParticipantSubsystem::Unregister(Second);
	return true;
}

// 메뉴 없이 열린 월드 = 새 게임, 첫 시네마틱 없음. 메뉴의 새 게임, 이어하기는 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveDirectStartTest, "VB.Unit.Save.DirectStart.FreshWorld",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveDirectStartTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
	TStrongObjectPtr<UVBSaveGameSubsystem> Save{NewObject<UVBSaveGameSubsystem>(Instance.Get())};
	TestFalse(TEXT("No start record before"), Save->GetGameStartContext().AttemptId.IsValid());
	TestFalse(TEXT("No save hold without a start record (the elevator symptom)"), Save->BeginSaveHold(TEXT("Before")).IsValid());

	// 공개 순서: 시작 알림 안에서 이미 첫 시네마틱이 없는 상태로 보여야 한다.
	int32 Notifications = 0;
	bool bOpeningSeenInNotification = false;
	const FDelegateHandle Handle = Save->OnGameStartContextChanged.AddLambda([&]()
	{
		++Notifications;
		bOpeningSeenInNotification |= Save->ConsumeInitialNewGameStart(Save->GetGameStartContext().AttemptId);
	});
	TestTrue(TEXT("Direct start is recorded"), Save->EnsureDirectStartContext());
	Save->OnGameStartContextChanged.Remove(Handle);
	TestEqual(TEXT("One start notification"), Notifications, 1);
	TestFalse(TEXT("No opening cinematic even inside the notification"), bOpeningSeenInNotification);
	const FVBGameStartContext Start = Save->GetGameStartContext();
	TestTrue(TEXT("Valid attempt"), Start.AttemptId.IsValid());
	TestTrue(TEXT("Same intent as a menu new game"), Start.Intent == EVBGameStartIntent::NewGame);
	const FVBSaveHoldToken Hold = Save->BeginSaveHold(TEXT("Elevator"));
	TestTrue(TEXT("Save hold works after a direct start"), Hold.IsValid());
	Save->EndSaveHold(Hold);
	TestFalse(TEXT("No opening cinematic for a direct start"), Save->ConsumeInitialNewGameStart(Start.AttemptId));
	TestFalse(TEXT("Second call is a no-op"), Save->EnsureDirectStartContext());
	TestTrue(TEXT("Attempt unchanged by the second call"), Save->GetGameStartContext().AttemptId == Start.AttemptId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveDirectStartMenuTest, "VB.Unit.Save.DirectStart.MenuStartsKept",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveDirectStartMenuTest::RunTest(const FString& Parameters)
{
	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
		TStrongObjectPtr<UVBSaveGameSubsystem> Save{NewObject<UVBSaveGameSubsystem>(Instance.Get())};
		Save->StartNewGame();
		const FGuid Menu = Save->GetGameStartContext().AttemptId;
		const UVBSaveGame* MenuSave = Save->GetActiveGameSave();
		const int32 MenuSlot = FVBSaveGateTestAccess::GetActiveSlot(*Save.Get());
		TestFalse(TEXT("Menu new game: nothing to record"), Save->EnsureDirectStartContext());
		TestTrue(TEXT("Menu new game attempt kept"), Save->GetGameStartContext().AttemptId == Menu);
		TestTrue(TEXT("Menu new game active save, slot and pending state kept"), Save->GetActiveGameSave() == MenuSave
			&& FVBSaveGateTestAccess::GetActiveSlot(*Save.Get()) == MenuSlot && !FVBSaveGateTestAccess::GetPendingContinue(*Save.Get()));
		TestTrue(TEXT("Menu new game keeps its opening cinematic"), Save->ConsumeInitialNewGameStart(Menu));
	}

	{
		TStrongObjectPtr<UGameInstance> Instance{NewObject<UGameInstance>()};
		TStrongObjectPtr<UVBSaveGameSubsystem> Save{NewObject<UVBSaveGameSubsystem>(Instance.Get())};
		const FVBGameStartContext Continue{FGuid::NewGuid(), EVBGameStartIntent::Continue};
		FVBSaveGateTestAccess::SetStartContext(*Save.Get(), Continue);
		const UVBSaveGame* ContinueSave = Save->GetActiveGameSave();
		TestFalse(TEXT("Continue: nothing to record"), Save->EnsureDirectStartContext());
		TestTrue(TEXT("Continue active save kept"), Save->GetActiveGameSave() == ContinueSave);
		TestTrue(TEXT("Continue attempt and intent kept"), Save->GetGameStartContext().AttemptId == Continue.AttemptId
			&& Save->GetGameStartContext().Intent == EVBGameStartIntent::Continue);
	}

	return true;
}

namespace
{
	USaveGame* ReadNothing(const FString&, int32) { return nullptr; }
}

// 파일이 있는데 읽지 못한 슬롯은 빈 슬롯이 아니고, 그 사실이 로그에 남는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveUnreadableSlotListTest, "VB.Unit.Save.Unreadable.SlotList",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveUnreadableSlotListTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	FVBSaveGateTestAccess::SetReadDisk(*F.Save.Get(), [](const FString&, int32) { return true; }, ReadNothing);
	AddExpectedError(TEXT("저장 파일을 읽지 못했다"), EAutomationExpectedErrorFlags::Contains, 1);

	FVBSaveSlotMeta Meta;
	TestFalse(TEXT("An unreadable slot has nothing to show"), F.Save->GetSlotMetadata(1, Meta));
	TestTrue(TEXT("The slot says a file is there"), Meta.bUnreadable);
	TestFalse(TEXT("The slot is not free"), Meta.IsFree());
	return true;
}

// 파일이 없는 슬롯은 정상이라 오류를 남기지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveUnreadableMissingFileTest, "VB.Unit.Save.Unreadable.MissingFileIsQuiet",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveUnreadableMissingFileTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	FVBSaveGateTestAccess::SetReadDisk(*F.Save.Get(), [](const FString&, int32) { return false; }, ReadNothing);

	FVBSaveSlotMeta Meta;
	TestFalse(TEXT("A missing file has nothing to show"), F.Save->GetSlotMetadata(1, Meta));
	TestFalse(TEXT("A missing file is not unreadable"), Meta.bUnreadable);
	TestTrue(TEXT("A missing file leaves the slot free"), Meta.IsFree());
	return true;
}

// 체크포인트는 읽지 못한 슬롯을 지나 진짜 빈 슬롯을 고르고, 빈 슬롯이 없으면 거절한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveUnreadableCheckpointTest, "VB.Unit.Save.Unreadable.CheckpointSkipsIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveUnreadableCheckpointTest::RunTest(const FString& Parameters)
{
	FVBSaveSlotMeta Unreadable;
	Unreadable.SlotIndex = 0;
	Unreadable.bUnreadable = true;
	FVBSaveSlotMeta Empty;
	Empty.SlotIndex = 1;

	TestEqual(TEXT("Skips the unreadable slot"), VBSaveSlotPolicy::ResolveCheckpointSlot(INDEX_NONE, {Unreadable, Empty}), 1);
	TestEqual(TEXT("Refuses when nothing is free"), VBSaveSlotPolicy::ResolveCheckpointSlot(INDEX_NONE, {Unreadable}), INDEX_NONE);
	TestEqual(TEXT("An empty slot stays free"), VBSaveSlotPolicy::ResolveCheckpointSlot(INDEX_NONE, {Empty}), 1);
	return true;
}

// 일반 저장의 슬롯 선택도 읽지 못한 슬롯을 지나간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveUnreadableResolveTest, "VB.Unit.Save.Unreadable.SaveSlotSkipsIt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveUnreadableResolveTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	FVBSaveGateTestAccess::SetReadDisk(*F.Save.Get(), [](const FString& Slot, int32) { return Slot.EndsWith(TEXT("_0")); }, ReadNothing);
	AddExpectedError(TEXT("저장 파일을 읽지 못했다"), EAutomationExpectedErrorFlags::Contains, 1);

	TestEqual(TEXT("Slot 0 holds an unreadable file, so slot 1 is chosen"), FVBSaveGateTestAccess::ResolveSlot(*F.Save.Get()), 1);
	return true;
}

// 이어하기 로드가 빈손으로 돌아오면 이어하기를 시작하지 않고 로그에 남긴다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSaveUnreadableContinueTest, "VB.Unit.Save.Unreadable.ContinueLoad",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSaveUnreadableContinueTest::RunTest(const FString& Parameters)
{
	FSaveFixture F;
	TArray<FAsyncLoadGameFromSlotDelegate> Pending;
	FVBSaveGateTestAccess::SetReadDisk(*F.Save.Get(), [](const FString&, int32) { return true; }, ReadNothing);
	FVBSaveGateTestAccess::SetReadAsync(*F.Save.Get(), [&Pending](const FString&, int32, const FAsyncLoadGameFromSlotDelegate& Completed)
	{
		Pending.Add(Completed);
	});

	F.Save->LoadGameSlot(1);
	if (!TestEqual(TEXT("The load goes through the disk seam"), Pending.Num(), 1))
	{
		return true;
	}

	AddExpectedError(TEXT("저장 파일을 읽지 못했다"), EAutomationExpectedErrorFlags::Contains, 1);
	Pending[0].ExecuteIfBound(TEXT("Mock"), 0, nullptr);
	TestFalse(TEXT("A failed load starts no Continue"), FVBSaveGateTestAccess::GetPendingContinue(*F.Save.Get()));
	return true;
}
#endif
