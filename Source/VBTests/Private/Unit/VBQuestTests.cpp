#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Narrative/Dialogue/VBDialogueAsset.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Quest/VBQuestRules.h"
#include "Narrative/Quest/VBQuestText.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "SaveSystem/VBSaveGame.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 퀘스트 단계의 단위 시험.
// 주인 액터 없는 일지 = 권한 검사가 통과하는 자리라 서버 경로가 돌고, 주인 클라 RPC(알림)도 이 자리에서 바로 실행된다.
// 사건, 퀘스트 태그(Quest.*, Enemy.*)는 ini 내용이라 시험은 네이티브 태그를 빌려 쓴다 - 연산은 태그 이름과 무관하다.

#if WITH_AUTOMATION_TESTS
namespace
{
	FInstancedStruct MakeEventObjective(FGameplayTag EventTag, int32 Required, FGameplayTag Target = FGameplayTag(), bool bOptional = false)
	{
		FVBQuestObjective_Event Objective;
		Objective.Description = FText::FromString(TEXT("objective"));
		Objective.EventTag = EventTag;
		Objective.RequiredCount = Required;
		Objective.bOptional = bOptional;
		if (Target.IsValid())
		{
			Objective.TargetQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(Target));
		}

		return FInstancedStruct::Make(Objective);
	}

	FVBGameEvent MakeEvent(FGameplayTag EventTag, FGameplayTag Target = FGameplayTag(), int32 Count = 1)
	{
		FVBGameEvent Event;
		Event.EventTag = EventTag;
		if (Target.IsValid())
		{
			Event.TargetTags.AddTag(Target);
		}

		Event.Count = Count;
		return Event;
	}

	// 두 단계: 1) 처치 2번(대상 = 병사 표지) + 선택 목표 하나 2) 줍기 1번.
	UVBQuestDefinition* MakeTwoStageQuest()
	{
		UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
		Quest->QuestId = VBGameplayTags::Melee_Window;
		Quest->Title = FText::FromString(TEXT("test quest"));
		FVBQuestStage& Kill = Quest->Stages.AddDefaulted_GetRef();
		Kill.Description = FText::FromString(TEXT("kill"));
		Kill.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Enemy_Killed, 2, VBGameplayTags::Melee_Shape_Weapon));
		Kill.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Item_Used, 1, FGameplayTag(), /*bOptional=*/true));
		FVBQuestStage& Pick = Quest->Stages.AddDefaulted_GetRef();
		Pick.Description = FText::FromString(TEXT("pick"));
		Pick.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Item_Acquired, 1));
		return Quest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestProgressTest, "VB.Unit.Quest.Progress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestProgressTest::RunTest(const FString& Parameters)
{
	const UVBQuestDefinition* Quest = MakeTwoStageQuest();
	UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
	TArray<EVBQuestNotice> Notices;
	Log->OnQuestNotice.AddLambda([&Notices](const UVBQuestDefinition*, EVBQuestNotice Notice) { Notices.Add(Notice); });

	TestEqual(TEXT("not started before StartQuest"), Log->GetStatus(Quest), EVBQuestStatus::NotStarted);
	TestTrue(TEXT("start"), Log->StartQuest(Quest));
	TestEqual(TEXT("active at stage 0"), Log->GetStageIndex(Quest), 0);
	TestFalse(TEXT("starting twice is refused"), Log->StartQuest(Quest));

	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_HandL));
	TestEqual(TEXT("kill of another kind does not count"), Log->GetEntries()[0].Progress[0], 0);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_Weapon));
	TestEqual(TEXT("matching kill counts"), Log->GetEntries()[0].Progress[0], 1);
	TestTrue(TEXT("tracker text shows n/m"), VBQuestText::DescribeObjectives(Log->GetEntries()[0]).ToString().Contains(TEXT("(1/2)")));
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_Weapon));
	TestEqual(TEXT("required objectives done -> next stage (optional one is not needed)"), Log->GetStageIndex(Quest), 1);
	TestEqual(TEXT("progress restarts for the new stage"), Log->GetEntries()[0].Progress.Num(), 1);

	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_Weapon));
	TestEqual(TEXT("events for a finished stage do not leak into the next"), Log->GetEntries()[0].Progress[0], 0);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Item_Acquired));
	TestEqual(TEXT("last stage done -> completed"), Log->GetStatus(Quest), EVBQuestStatus::Completed);
	TestEqual(TEXT("completed quest has no stage"), Log->GetStageIndex(Quest), static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("completed quest cannot be restarted"), Log->StartQuest(Quest));
	TestNull(TEXT("nothing to track once completed"), Log->GetTrackedEntry());

	TestEqual(TEXT("notices: started, advanced, completed"), Notices.Num(), 3);
	if (Notices.Num() == 3)
	{
		TestTrue(TEXT("notice order"), Notices[0] == EVBQuestNotice::Started && Notices[1] == EVBQuestNotice::StageAdvanced
			&& Notices[2] == EVBQuestNotice::Completed);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestCountTest, "VB.Unit.Quest.EventCount",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestCountTest::RunTest(const FString& Parameters)
{
	// 사건의 수(주운 개수)만큼 세고, 상한에서 멈춘다. 계층 태그: 목표 Event.Item 은 Event.Item.Acquired 를 품는다.
	UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
	Quest->QuestId = VBGameplayTags::Melee_Window;
	FVBQuestStage& Stage = Quest->Stages.AddDefaulted_GetRef();
	Stage.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Item_Acquired.GetTag().RequestDirectParent(), 5));
	Stage.Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Enemy_Killed, 1));

	UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
	Log->StartQuest(Quest);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Item_Acquired, FGameplayTag(), 3));
	TestEqual(TEXT("event count is added"), Log->GetEntries()[0].Progress[0], 3);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Item_Acquired, FGameplayTag(), 9));
	TestEqual(TEXT("progress stops at the required count"), Log->GetEntries()[0].Progress[0], 5);
	TestEqual(TEXT("one objective left keeps the stage"), Log->GetStageIndex(Quest), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestSaveWriteTest, "VB.Unit.Quest.SaveWrite",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestSaveWriteTest::RunTest(const FString& Parameters)
{
	// 읽기(ReadSave)는 에셋 매니저가 실제 퀘스트 자산을 되찾아야 해서 여기서 못 돈다 - 쓰는 모양만 본다.
	const UVBQuestDefinition* Quest = MakeTwoStageQuest();
	UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
	Log->StartQuest(Quest);
	Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed, VBGameplayTags::Melee_Shape_Weapon));

	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	Log->WriteSave(*Save, FVBSaveContext());
	TestEqual(TEXT("one quest written"), Save->Quests.Quests.Num(), 1);
	if (Save->Quests.Quests.Num() == 1)
	{
		const FVBSavedQuest& Saved = Save->Quests.Quests[0];
		TestTrue(TEXT("saved by QuestId tag"), Saved.QuestId == Quest->QuestId);
		TestFalse(TEXT("not completed"), Saved.bCompleted);
		TestEqual(TEXT("stage"), Saved.StageIndex, 0);
		TestTrue(TEXT("progress kept"), Saved.Progress.Num() == 2 && Saved.Progress[0] == 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestSavedResolutionTest, "VB.Unit.Quest.SavedResolution",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestSavedResolutionTest::RunTest(const FString& Parameters)
{
	UVBQuestDefinition* Quest = MakeTwoStageQuest();
	UVBQuestDefinition* Empty = NewObject<UVBQuestDefinition>();
	const FGameplayTag MissingId = VBGameplayTags::Event_Enemy_Killed;
	const FGameplayTag EmptyId = VBGameplayTags::Event_Item_Used;
	TArray<FVBSavedQuest> Saved;
	FVBSavedQuest Row;
	Row.QuestId = MissingId;
	Row.StageIndex = 8;
	Row.Progress = {4};
	Saved.Add(Row);
	Row.QuestId = EmptyId;
	Saved.Add(Row);
	Row.QuestId = Quest->QuestId;
	Row.StageIndex = 0;
	Row.Progress = {1, 3};
	Saved.Add(Row);
	Row.StageIndex = 99;
	Row.Progress = {7};
	Saved.Add(Row);
	Row.StageIndex = 0;
	Row.Progress = {8};
	Saved.Add(Row);
	Row.bCompleted = true;
	Row.StageIndex = -5;
	Row.Progress = {-2, 20, 7};
	Saved.Add(Row);
	TArray<FVBQuestLogEntry> Entries;
	TArray<FVBSavedQuest> Unresolved;
	Entries.AddDefaulted();
	Unresolved.AddDefaulted();
	int32 NextOrder = 7;
	int32 Resolved = 0;
	const auto Resolve = [&](FGameplayTag Id) -> const UVBQuestDefinition*
	{
		++Resolved;
		if (Id == Quest->QuestId)
		{
			return Quest;
		}

		return Id == EmptyId ? Empty : nullptr;
	};
	AddExpectedMessagePlain(MissingId.ToString(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(EmptyId.ToString(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(GetNameSafe(Quest), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	UVBQuestLogComponent::ResolveSavedQuests(Saved, Resolve, Entries, Unresolved, NextOrder);
	TestEqual(TEXT("one lookup per saved row"), Resolved, Saved.Num());
	TestEqual(TEXT("missing and empty definitions retained"), Unresolved.Num(), 2);
	TestEqual(TEXT("resolved rows replace output"), Entries.Num(), 4);
	if (Unresolved.Num() != 2 || Entries.Num() != 4)
	{
		return false;
	}

	TestTrue(TEXT("missing row kept unchanged"), Unresolved[0].QuestId == MissingId
		&& Unresolved[0].StageIndex == 8 && Unresolved[0].Progress == TArray<int32>{4});
	TestEqual(TEXT("empty-stage definition remains unresolved"), Unresolved[1].QuestId, EmptyId);
	TestTrue(TEXT("matching progress kept"), Entries[0].Progress == TArray<int32>({1, 3}));
	TestEqual(TEXT("shrunken stage uses last stage"), Entries[1].StageIndex, 1);
	TestTrue(TEXT("matching last-stage progress kept"), Entries[1].Progress == TArray<int32>{7});
	TestTrue(TEXT("changed objective count resets all progress"), Entries[2].Progress == TArray<int32>({0, 0}));
	TestTrue(TEXT("completed row keeps old stage and progress"), Entries[3].Status == EVBQuestStatus::Completed
		&& Entries[3].StageIndex == -5 && Entries[3].Progress == TArray<int32>({-2, 20, 7}));
	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		TestEqual(TEXT("recent order follows successful row order"), Entries[Index].RecentOrder, 7 + Index);
	}

	TestEqual(TEXT("next order advanced only for resolved rows"), NextOrder, 11);
	UVBQuestLogComponent::ResolveSavedQuests({}, Resolve, Entries, Unresolved, NextOrder);
	TestTrue(TEXT("empty input clears both outputs"), Entries.IsEmpty() && Unresolved.IsEmpty());
	TestEqual(TEXT("empty input does not reset order counter"), NextOrder, 11);
	UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	Save->Quests.Quests.Add(Saved[0]);
	AddExpectedMessagePlain(MissingId.ToString(), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Log->ReadSave(*Save, FVBSaveContext());
	UVBSaveGame* Written = NewObject<UVBSaveGame>();
	Log->WriteSave(*Written, FVBSaveContext());
	TestTrue(TEXT("unresolved row survives real read-write"), Written->Quests.Quests.Num() == 1
		&& Written->Quests.Quests[0].QuestId == MissingId
		&& Written->Quests.Quests[0].StageIndex == Saved[0].StageIndex
		&& Written->Quests.Quests[0].Progress == Saved[0].Progress);
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestValidateTest, "VB.Unit.Quest.Validate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestValidateTest::RunTest(const FString& Parameters)
{
	{
		FDataValidationContext Context;
		TestEqual(TEXT("well-formed quest is valid"), MakeTwoStageQuest()->IsDataValid(Context), EDataValidationResult::Valid);
	}

	{
		UVBQuestDefinition* OnlyOptional = MakeTwoStageQuest();
		OnlyOptional->Stages[0].Objectives.RemoveAt(0);
		FDataValidationContext Context;
		TestEqual(TEXT("a stage with only optional objectives is invalid"), OnlyOptional->IsDataValid(Context),
			EDataValidationResult::Invalid);
	}

	{
		UVBQuestDefinition* NoTag = MakeTwoStageQuest();
		NoTag->Stages[1].Objectives[0] = MakeEventObjective(FGameplayTag(), 1);
		FDataValidationContext Context;
		TestEqual(TEXT("an event objective without a tag is invalid"), NoTag->IsDataValid(Context), EDataValidationResult::Invalid);
	}

	{
		UVBQuestDefinition* NoId = MakeTwoStageQuest();
		NoId->QuestId = FGameplayTag();
		FDataValidationContext Context;
		TestEqual(TEXT("missing QuestId is invalid"), NoId->IsDataValid(Context), EDataValidationResult::Invalid);
	}
	// 이미 찬 목표의 확인: 확인할 길이 없는 저작은 저장 전에 잡는다.
	{
		UVBDialogueAsset* Dialogue = NewObject<UVBDialogueAsset>();
		FVBDialogueNode& First = Dialogue->Nodes.AddDefaulted_GetRef();
		First.NodeId = TEXT("Line");
		First.NextNodeId = TEXT("Ack");
		FVBDialogueNode& Ack = Dialogue->Nodes.AddDefaulted_GetRef();
		Ack.NodeId = TEXT("Ack");
		Ack.ReachedTag = VBGameplayTags::Melee_Shape_Weapon;
		Dialogue->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("Line");
		auto WithAck = [](const UVBDialogueAsset* InDialogue, FGameplayTag Tag)
		{
			UVBQuestDefinition* Quest = MakeTwoStageQuest();
			FVBQuestObjective& Objective = Quest->Stages[1].Objectives[0].GetMutable<FVBQuestObjective>();
			Objective.AlreadyMetDialogue = InDialogue;
			Objective.AlreadyMetAckTag = Tag;
			return Quest;
		};
		FDataValidationContext Good, Half, NoNode, OnStart;
		TestEqual(TEXT("dialogue with a reachable ack node is valid"),
			WithAck(Dialogue, VBGameplayTags::Melee_Shape_Weapon)->IsDataValid(Good), EDataValidationResult::Valid);
		TestEqual(TEXT("ack dialogue without ack tag is invalid"), WithAck(Dialogue, FGameplayTag())->IsDataValid(Half),
			EDataValidationResult::Invalid);
		TestEqual(TEXT("ack tag no node reaches is invalid"), WithAck(Dialogue, VBGameplayTags::Melee_Window)->IsDataValid(NoNode),
			EDataValidationResult::Invalid);
		Dialogue->EntryPoints[0].StartNodeId = TEXT("Ack");
		TestEqual(TEXT("ack node as the start node is invalid"),
			WithAck(Dialogue, VBGameplayTags::Melee_Shape_Weapon)->IsDataValid(OnStart), EDataValidationResult::Invalid);
	}

	return true;
}
#endif

// 단계가 시작될 때 이미 찬 목표는 확인(대화 확인 노드 도착)까지 단계를 넘기지 않는다.
// 주인 없는 일지라 대화가 거절된 채 기다리므로, 확인 사건이 와야만 넘기고 다른 사건엔 그대로인지 본다.
// 실제 대화 화면과 확인 노드 도달은 여기서 못 보고 PIE 몫이다.
namespace
{
	// 단계 1 = 조건 없는 조건 목표(늘 참 = 시작할 때 이미 찬다) + 확인 칸, 단계 2 = 줍기 1번.
	UVBQuestDefinition* MakeAlreadyMetQuest(const UVBDialogueAsset* Dialogue, FGameplayTag AckTag)
	{
		UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
		Quest->QuestId = VBGameplayTags::Melee_Window;
		FVBQuestObjective_Condition Owned;
		Owned.Description = FText::FromString(TEXT("own it"));
		Owned.AlreadyMetDialogue = Dialogue;
		Owned.AlreadyMetAckTag = AckTag;
		Quest->Stages.AddDefaulted_GetRef().Objectives.Add(FInstancedStruct::Make(Owned));
		Quest->Stages.AddDefaulted_GetRef().Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Item_Acquired, 1));
		return Quest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestAlreadyMetAckTest, "VB.Unit.Quest.AlreadyMetAcknowledgement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestAlreadyMetAckTest::RunTest(const FString& Parameters)
{
	const FGameplayTag AckTag = VBGameplayTags::Melee_Shape_Weapon;
	const UVBDialogueAsset* Dialogue = NewObject<UVBDialogueAsset>();

	// 시작할 때 이미 찼고 확인 칸이 있다 - 기다린다. 다른 사건, 다른 노드는 넘기지 않고, 확인 노드 도착만 넘긴다.
	{
		const UVBQuestDefinition* Quest = MakeAlreadyMetQuest(Dialogue, AckTag);
		UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
		Log->StartQuest(Quest);
		TestEqual(TEXT("already met at start waits on stage 0"), Log->GetStageIndex(Quest), 0);
		TestEqual(TEXT("pending objective is the met one"), Log->GetEntries()[0].PendingAckObjective, 0);
		Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Item_Acquired));
		TestEqual(TEXT("an unrelated event does not acknowledge"), Log->GetStageIndex(Quest), 0);
		Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Dialogue_NodeReached, VBGameplayTags::Melee_Window));
		TestEqual(TEXT("another dialogue node does not acknowledge"), Log->GetStageIndex(Quest), 0);
		Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Dialogue_NodeReached, AckTag));
		TestEqual(TEXT("the acknowledgement node advances the stage"), Log->GetStageIndex(Quest), 1);
		TestEqual(TEXT("pending cleared"), Log->GetEntries()[0].PendingAckObjective, static_cast<int32>(INDEX_NONE));
	}
	// 확인 칸이 비면 지금처럼 바로 넘긴다.
	{
		const UVBQuestDefinition* Quest = MakeAlreadyMetQuest(nullptr, FGameplayTag());
		UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
		Log->StartQuest(Quest);
		TestEqual(TEXT("no acknowledgement data advances at once"), Log->GetStageIndex(Quest), 1);
	}
	// 반만 채운 저작은 기다리지 않고 말한다 - 확인할 길이 없어 영영 멈추면 안 된다.
	{
		AddExpectedMessagePlain(TEXT("확인 대화·확인 태그 중 하나가 비었다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		const UVBQuestDefinition* Quest = MakeAlreadyMetQuest(nullptr, AckTag);
		UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
		Log->StartQuest(Quest);
		TestEqual(TEXT("half-authored acknowledgement does not block"), Log->GetStageIndex(Quest), 1);
	}
	// 시작할 때 안 찼다가 나중에 차면 확인 없이 넘긴다(받고 줍기 - 놀랄 일이 아니다).
	{
		UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
		Quest->QuestId = VBGameplayTags::Melee_Window;
		FInstancedStruct Pick = MakeEventObjective(VBGameplayTags::Event_Item_Acquired, 1);
		Pick.GetMutable<FVBQuestObjective_Event>().AlreadyMetDialogue = Dialogue;
		Pick.GetMutable<FVBQuestObjective_Event>().AlreadyMetAckTag = AckTag;
		Quest->Stages.AddDefaulted_GetRef().Objectives.Add(Pick);
		Quest->Stages.AddDefaulted_GetRef().Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Enemy_Killed, 1));
		UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>();
		Log->StartQuest(Quest);
		Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Item_Acquired));
		TestEqual(TEXT("met later advances without acknowledgement"), Log->GetStageIndex(Quest), 1);
	}
	// 대기는 저장하지 않는다 - 저장 줄에서 되살린 줄은 기다리지 않는다(불러온 뒤에는 확인한 것으로 친다).
	{
		UVBQuestDefinition* Quest = MakeAlreadyMetQuest(Dialogue, AckTag);
		FVBSavedQuest Row;
		Row.QuestId = Quest->QuestId;
		Row.StageIndex = 0;
		Row.Progress = {1};
		TArray<FVBQuestLogEntry> Entries;
		TArray<FVBSavedQuest> Unresolved;
		int32 NextOrder = 1;
		UVBQuestLogComponent::ResolveSavedQuests({Row}, [Quest](FGameplayTag) { return Quest; }, Entries, Unresolved, NextOrder);
		if (TestEqual(TEXT("saved row resolves"), Entries.Num(), 1))
		{
			TestEqual(TEXT("a loaded row is not waiting"), Entries[0].PendingAckObjective, static_cast<int32>(INDEX_NONE));
		}
	}

	return true;
}

// 퀘스트 일지 처리 구간 시험. 위 시험은 주인 없는 일지라 Announce 가 돌아오지 않아 큐가 비워지지 않는다.
// 여기서는 controller 가 가진 PlayerState 에 일지를 두고 실제 UVBGameEventSubsystem 으로 사건을 듣게 해서
// 처리 중 큐잉, FVBEffect_StartQuest 의 일지 찾기, controller 위 대화 열기를 함께 본다.
namespace
{
	struct FQuestWorld
	{
		UWorld* World = nullptr;
		APlayerController* Controller = nullptr;
		APlayerState* PlayerState = nullptr;
		UVBQuestLogComponent* Log = nullptr;

		bool Make(FAutomationTestBase& Test)
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			Controller = World ? World->SpawnActor<APlayerController>() : nullptr;
			FActorSpawnParameters Params;
			Params.Owner = Controller;
			PlayerState = Controller ? World->SpawnActor<APlayerState>(APlayerState::StaticClass(), Params) : nullptr;
			UVBGameEventSubsystem* Events = World ? UVBGameEventSubsystem::Get(World) : nullptr;
			if (!Test.TestNotNull(TEXT("quest world"), World) || !Test.TestNotNull(TEXT("controller"), Controller)
				|| !Test.TestNotNull(TEXT("player state"), PlayerState) || !Test.TestNotNull(TEXT("game event subsystem"), Events))
			{
				return false;
			}

			Controller->PlayerState = PlayerState;
			Log = NewObject<UVBQuestLogComponent>(PlayerState);
			Log->RegisterComponent();
			Events->OnGameEvent.AddUObject(Log, &UVBQuestLogComponent::HandleGameEvent);
			return true;
		}

		~FQuestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}
	};

	UVBQuestDefinition* MakeOneStageQuest(FGameplayTag QuestId, FGameplayTag EventTag, int32 Required, FGameplayTag Target = FGameplayTag())
	{
		UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
		Quest->QuestId = QuestId;
		Quest->Stages.AddDefaulted_GetRef().Objectives.Add(MakeEventObjective(EventTag, Required, Target));
		return Quest;
	}

	void AddStartQuestEffect(UVBQuestDefinition* Quest, const UVBQuestDefinition* ToStart)
	{
		FVBEffect_StartQuest Effect;
		Effect.Quest = ToStart;
		Quest->Stages[0].OnCompleteEffects.Add(FInstancedStruct::Make(Effect));
	}

	const FVBQuestLogEntry* FindLogEntry(const UVBQuestLogComponent* Log, const UVBQuestDefinition* Quest)
	{
		return Log->GetEntries().FindByPredicate([Quest](const FVBQuestLogEntry& Entry) { return Entry.Definition == Quest; });
	}

	int32 FirstProgress(const UVBQuestLogComponent* Log, const UVBQuestDefinition* Quest)
	{
		const FVBQuestLogEntry* Entry = FindLogEntry(Log, Quest);
		return Entry && Entry->Progress.IsValidIndex(0) ? Entry->Progress[0] : INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestChainedEventsTest, "VB.Unit.Quest.ChainedEventsDrainInSection",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestChainedEventsTest::RunTest(const FString& Parameters)
{
	FQuestWorld Fixture;
	if (!Fixture.Make(*this))
	{
		return false;
	}
	// B 는 A 의 완료 알림을 센다. 그 알림은 A 처리 중에 나서 큐에 들어가고 같은 호출이 비운다.
	const UVBQuestDefinition* A = MakeOneStageQuest(VBGameplayTags::Melee_Window, VBGameplayTags::Event_Enemy_Killed, 1);
	const UVBQuestDefinition* B = MakeOneStageQuest(VBGameplayTags::Melee_Shape_HandL, VBGameplayTags::Event_Quest_Completed, 1,
		A->QuestId);
	Fixture.Log->StartQuest(A);
	Fixture.Log->StartQuest(B);
	Fixture.Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed));
	TestEqual(TEXT("the event completes A"), Fixture.Log->GetStatus(A), EVBQuestStatus::Completed);
	TestEqual(TEXT("A's queued completion notice completes B in the same call"), Fixture.Log->GetStatus(B), EVBQuestStatus::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestNestedStartTest, "VB.Unit.Quest.NestedStartQuestDrainsOuter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestNestedStartTest::RunTest(const FString& Parameters)
{
	FQuestWorld Fixture;
	if (!Fixture.Make(*this))
	{
		return false;
	}
	// A 의 단계 효과가 A 처리 중에 C 를 시작한다. 중첩 StartQuest 는 C 의 시작 알림을 큐에 남기고 바깥 구간이 돌아가기 전에 비운다.
	UVBQuestDefinition* A = MakeOneStageQuest(VBGameplayTags::Melee_Window, VBGameplayTags::Event_Enemy_Killed, 1);
	UVBQuestDefinition* C = NewObject<UVBQuestDefinition>();
	C->QuestId = VBGameplayTags::Melee_Shape_HandL;
	C->Stages.AddDefaulted_GetRef().Objectives.Add(MakeEventObjective(VBGameplayTags::Event_Quest_Started, 1, C->QuestId));
	AddStartQuestEffect(A, C);
	Fixture.Log->StartQuest(A);
	int32 ProgressInsideOuter = -2;
	const FDelegateHandle Watch = Fixture.Log->OnQuestLogChanged.AddLambda([&]()
	{
		if (ProgressInsideOuter == -2 && FindLogEntry(Fixture.Log, C))
		{
			ProgressInsideOuter = FirstProgress(Fixture.Log, C);
		}
	});
	Fixture.Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed));
	Fixture.Log->OnQuestLogChanged.Remove(Watch);
	TestEqual(TEXT("inside the outer body C has not counted its queued start notice"), ProgressInsideOuter, 0);
	TestEqual(TEXT("the outer section drained it: C completed"), Fixture.Log->GetStatus(C), EVBQuestStatus::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestChainGuardTest, "VB.Unit.Quest.ChainGuard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestChainGuardTest::RunTest(const FString& Parameters)
{
	// 폭주 사슬: 처리 중 사건 100개가 한꺼번에 쌓인다. 두 입구 모두 정확히 64개만 비우고 나머지는 경고 한 번과 함께 버린다.
	constexpr int32 Flood = 100;
	const FGameplayTag FloodTag = VBGameplayTags::Event_Item_Used;
	AddExpectedMessagePlain(TEXT("꼬리를 물었다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	const auto ArmFlood = [FloodTag](UVBQuestLogComponent* Log, bool& bArmed)
	{
		return Log->OnQuestLogChanged.AddLambda([Log, FloodTag, &bArmed]()
		{
			if (bArmed)
			{
				bArmed = false;
				for (int32 Index = 0; Index < Flood; ++Index)
				{
					Log->HandleGameEvent(MakeEvent(FloodTag));
				}
			}
		});
	};
	// 1. HandleGameEvent: 본문이 Trigger 진행을 바꾸고 그 구간 안 방송이 넘친다. 큐에는 넘친 사건만 있다.
	{
		FQuestWorld Fixture;
		if (!Fixture.Make(*this))
		{
			return false;
		}

		const UVBQuestDefinition* Trigger = MakeOneStageQuest(VBGameplayTags::Melee_Window, VBGameplayTags::Event_Enemy_Killed, 2);
		const UVBQuestDefinition* Counter = MakeOneStageQuest(VBGameplayTags::Melee_Shape_HandL, FloodTag, 1000);
		Fixture.Log->StartQuest(Trigger);
		Fixture.Log->StartQuest(Counter);
		bool bArmed = true;
		const FDelegateHandle Handle = ArmFlood(Fixture.Log, bArmed);
		Fixture.Log->HandleGameEvent(MakeEvent(VBGameplayTags::Event_Enemy_Killed));
		Fixture.Log->OnQuestLogChanged.Remove(Handle);
		TestFalse(TEXT("HandleGameEvent: the flood happened inside the section"), bArmed);
		TestEqual(TEXT("HandleGameEvent drains 64 queued events (all flood)"), FirstProgress(Fixture.Log, Counter), 64);
	}
	// 2. StartQuest: S 의 첫 단계가 이미 차서 효과가 S 본문 안에서 F 를 시작하고 F 의 중첩 방송이 넘친다.
	//   큐 = S 시작, F 시작, 넘침 100, S 완료 -> 64개 비움 = 알림 2 + 넘침 62.
	{
		FQuestWorld Fixture;
		if (!Fixture.Make(*this))
		{
			return false;
		}

		const UVBQuestDefinition* Counter = MakeOneStageQuest(VBGameplayTags::Melee_Shape_HandL, FloodTag, 1000);
		UVBQuestDefinition* F = MakeOneStageQuest(VBGameplayTags::Melee_Shape_Weapon, VBGameplayTags::Event_Enemy_Killed, 1);
		UVBQuestDefinition* S = NewObject<UVBQuestDefinition>();
		S->QuestId = VBGameplayTags::Melee_Window;
		FVBQuestObjective_Condition Met;
		Met.Description = FText::FromString(TEXT("met"));
		S->Stages.AddDefaulted_GetRef().Objectives.Add(FInstancedStruct::Make(Met));
		AddStartQuestEffect(S, F);
		Fixture.Log->StartQuest(Counter);
		bool bArmed = true;
		const FDelegateHandle Handle = ArmFlood(Fixture.Log, bArmed);
		Fixture.Log->StartQuest(S);
		Fixture.Log->OnQuestLogChanged.Remove(Handle);
		TestFalse(TEXT("StartQuest: the flood happened inside the section"), bArmed);
		TestEqual(TEXT("StartQuest drains 64 queued events (2 notices + 62 flood)"), FirstProgress(Fixture.Log, Counter), 62);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestAckDialogueTest, "VB.Unit.Quest.AckDialogueReentry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestAckDialogueTest::RunTest(const FString& Parameters)
{
	// 실제 확인 경로: 구간이 닫히면 Offer 가 controller 에 대화를 열고, 첫 노드 사건이 처리 밖에서 일지로 다시 들어오며
	//   Continue 가 확인 노드에 닿는다.
	FQuestWorld Fixture;
	if (!Fixture.Make(*this))
	{
		return false;
	}

	UVBDialogueComponent* Dialogue = NewObject<UVBDialogueComponent>(Fixture.Controller);
	Dialogue->RegisterComponent();
	const FGameplayTag LineTag = VBGameplayTags::Melee_Shape_HandL;
	const FGameplayTag AckTag = VBGameplayTags::Melee_Shape_Weapon;
	UVBDialogueAsset* Asset = NewObject<UVBDialogueAsset>();
	FVBDialogueNode& Line = Asset->Nodes.AddDefaulted_GetRef();
	Line.NodeId = TEXT("Line");
	Line.ReachedTag = LineTag;
	Line.NextNodeId = TEXT("Ack");
	FVBDialogueNode& Ack = Asset->Nodes.AddDefaulted_GetRef();
	Ack.NodeId = TEXT("Ack");
	Ack.ReachedTag = AckTag;
	Asset->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("Line");
	const UVBQuestDefinition* Quest = MakeAlreadyMetQuest(Asset, AckTag);
	int32 LineEvents = 0;
	const FDelegateHandle Count = UVBGameEventSubsystem::Get(Fixture.World)->OnGameEvent.AddLambda([&](const FVBGameEvent& Event)
	{
		LineEvents += Event.EventTag == VBGameplayTags::Event_Dialogue_NodeReached && Event.TargetTags.HasTagExact(LineTag);
	});
	Fixture.Log->StartQuest(Quest);
	TestTrue(TEXT("the ack dialogue opened after the section closed"), Dialogue->IsInDialogue());
	TestEqual(TEXT("its first node re-entered the log once"), LineEvents, 1);
	TestEqual(TEXT("still waiting on stage 0"), Fixture.Log->GetStageIndex(Quest), 0);
	Dialogue->RequestContinue();
	TestEqual(TEXT("continue to the ack node advances the stage"), Fixture.Log->GetStageIndex(Quest), 1);
	UVBGameEventSubsystem::Get(Fixture.World)->OnGameEvent.Remove(Count);
	return true;
}
#endif
