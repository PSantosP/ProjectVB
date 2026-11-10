// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Unit/VBStoryTestWorld.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "Components/ActorComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"

namespace
{
	FGameplayTag Fact()
	{
		return FGameplayTag::RequestGameplayTag(TEXT("Story.Collection.Document.TestNote"), false);
	}

	UVBQuestLogComponent* SpawnLog(UWorld& World)
	{
		APlayerState* Player = World.SpawnActor<APlayerState>();
		if (!Player)
		{
			return nullptr;
		}

		UVBQuestLogComponent* Log = NewObject<UVBQuestLogComponent>(Player);
		Player->AddInstanceComponent(Log);
		Log->RegisterComponent();
		return Log;
	}

	UVBQuestDefinition* MakeFactQuest()
	{
		UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>();
		Quest->QuestId = FGameplayTag::RequestGameplayTag(TEXT("Quest.Prologue.Opening"), false);
		Quest->Title = FText::FromString(TEXT("Story binding fixture"));
		FVBCondition_Fact Condition;
		Condition.Fact = Fact();
		FVBQuestObjective_Condition Objective;
		Objective.Description = FText::FromString(TEXT("Wait for the fact"));
		Objective.Conditions.Add(FInstancedStruct::Make(Condition));
		Quest->Stages.AddDefaulted_GetRef().Objectives.Add(FInstancedStruct::Make(Objective));
		return Quest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestStorySnapshotTest, "VB.Unit.Quest.StoryBinding.CurrentSnapshot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestStorySnapshotTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this) || !TestTrue(TEXT("fixture fact registered"), Fact().IsValid()))
	{
		return false;
	}

	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(*Fixture.World);
	UVBQuestLogComponent* Log = SpawnLog(*Fixture.World);
	if (!TestNotNull(TEXT("story"), Story) || !TestNotNull(TEXT("quest log"), Log))
	{
		return false;
	}

	UVBQuestDefinition* Quest = MakeFactQuest();
	if (!TestTrue(TEXT("fixture quest tag registered"), Quest->QuestId.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("quest starts before component BeginPlay"), Log->StartQuest(Quest));
	Story->SetFact(Fact(), true);
	TestEqual(TEXT("not subscribed until BeginPlay"), Log->GetStatus(Quest), EVBQuestStatus::Active);
	int32 Completed = 0;
	const FDelegateHandle Notice = Log->OnQuestNotice.AddLambda([&](const UVBQuestDefinition*, EVBQuestNotice Kind)
	{
		if (Kind == EVBQuestNotice::Completed)
		{
			++Completed;
		}
	});
	Log->GetOwner()->DispatchBeginPlay();
	TestEqual(TEXT("bind-time snapshot completes a pre-existing objective"), Log->GetStatus(Quest), EVBQuestStatus::Completed);
	TestEqual(TEXT("initial requery grants completion once"), Completed, 1);
	Story->OnStoryStateChanged.Broadcast();
	TestEqual(TEXT("same state cannot repeat completion"), Completed, 1);
	Log->OnQuestNotice.Remove(Notice);
	UVBQuestLogComponent* Empty = SpawnLog(*Fixture.World);
	if (!TestNotNull(TEXT("fresh empty log"), Empty))
	{
		return false;
	}

	int32 Changes = 0;
	const FDelegateHandle Changed = Empty->OnQuestLogChanged.AddLambda([&]() { ++Changes; });
	Empty->GetOwner()->DispatchBeginPlay();
	TestTrue(TEXT("initial requery does not create quests"), Empty->GetEntries().IsEmpty());
	TestEqual(TEXT("empty initial requery is silent"), Changes, 0);
	Empty->OnQuestLogChanged.Remove(Changed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestStoryReplacementTest, "VB.Unit.Quest.StoryBinding.LateAndReplacedProvider",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestStoryReplacementTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this) || !TestTrue(TEXT("fixture fact registered"), Fact().IsValid()))
	{
		return false;
	}

	UVBQuestLogComponent* LateLog = SpawnLog(*Fixture.World);
	if (!TestNotNull(TEXT("log before GameState"), LateLog))
	{
		return false;
	}

	LateLog->GetOwner()->DispatchBeginPlay();
	UVBStoryStateComponent* First = VBStoryTestWorld::SpawnStory(*Fixture.World);
	if (!TestNotNull(TEXT("late GameState"), First))
	{
		return false;
	}

	UVBQuestDefinition* LateQuest = MakeFactQuest();
	LateLog->StartQuest(LateQuest);
	First->SetFact(Fact(), true);
	TestEqual(TEXT("provider arriving after BeginPlay is observed"), LateLog->GetStatus(LateQuest), EVBQuestStatus::Completed);
	First->SetFact(Fact(), false);
	UVBQuestLogComponent* Log = SpawnLog(*Fixture.World);
	if (!TestNotNull(TEXT("replacement log"), Log))
	{
		return false;
	}

	Log->GetOwner()->DispatchBeginPlay();
	UVBQuestDefinition* Quest = MakeFactQuest();
	Log->StartQuest(Quest);
	UVBStoryStateComponent* Second = VBStoryTestWorld::SpawnStory(*Fixture.World);
	if (!TestNotNull(TEXT("replacement provider"), Second))
	{
		return false;
	}

	First->SetFact(Fact(), true);
	TestEqual(TEXT("old provider no longer advances objective"), Log->GetStatus(Quest), EVBQuestStatus::Active);
	Fixture.World->SetGameState(nullptr);
	Second->SetFact(Fact(), true);
	TestEqual(TEXT("source removed from the world cannot advance objective"), Log->GetStatus(Quest), EVBQuestStatus::Active);
	Fixture.World->SetGameState(CastChecked<AGameStateBase>(Second->GetOwner()));
	TestEqual(TEXT("replacement already-true snapshot is immediately evaluated"), Log->GetStatus(Quest), EVBQuestStatus::Completed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestStoryEndPlayTest, "VB.Unit.Quest.StoryBinding.EndPlay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestStoryEndPlayTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	UVBStoryStateComponent* First = VBStoryTestWorld::SpawnStory(*Fixture.World);
	UVBQuestLogComponent* Log = SpawnLog(*Fixture.World);
	if (!TestNotNull(TEXT("story"), First) || !TestNotNull(TEXT("log"), Log))
	{
		return false;
	}

	Log->GetOwner()->DispatchBeginPlay();
	UVBQuestDefinition* Quest = MakeFactQuest();
	Log->StartQuest(Quest);
	UVBStoryStateComponent* Second = VBStoryTestWorld::SpawnStory(*Fixture.World);
	if (!TestNotNull(TEXT("replacement provider"), Second))
	{
		return false;
	}

	int32 Changes = 0;
	const FDelegateHandle Changed = Log->OnQuestLogChanged.AddLambda([&]() { ++Changes; });
	Log->GetOwner()->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	TestTrue(TEXT("ended component remains alive for teardown assertions"), IsValid(Log));
	First->SetFact(Fact(), true);
	Second->SetFact(Fact(), true);
	TestEqual(TEXT("ended log does not hear current or old source"), Log->GetStatus(Quest), EVBQuestStatus::Active);
	UVBStoryStateComponent* Third = VBStoryTestWorld::SpawnStory(*Fixture.World);
	if (!TestNotNull(TEXT("provider after EndPlay"), Third))
	{
		return false;
	}

	Third->SetFact(Fact(), true);
	TestEqual(TEXT("world replacement cannot rebind an ended log"), Log->GetStatus(Quest), EVBQuestStatus::Active);
	TestEqual(TEXT("teardown prevents observer effects"), Changes, 0);
	Log->OnQuestLogChanged.Remove(Changed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBQuestStorySaveTest, "VB.Unit.Quest.StoryBinding.SaveDeferral",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBQuestStorySaveTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(*Fixture.World);
	UVBQuestLogComponent* Log = SpawnLog(*Fixture.World);
	UVBSaveParticipantSubsystem* Registry = Fixture.World->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (!TestNotNull(TEXT("story"), Story) || !TestNotNull(TEXT("log"), Log) || !TestNotNull(TEXT("save registry"), Registry))
	{
		return false;
	}

	Log->GetOwner()->DispatchBeginPlay();
	UVBQuestDefinition* Quest = MakeFactQuest();
	Log->StartQuest(Quest);
	// 합성 정의를 메모리에 둔다. 이 시험은 관찰을 보고 asset id 해석은 보지 않는다
	UVBSaveParticipantSubsystem::Unregister(Log);
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	Save->Progress.Story.Facts.AddTag(Fact());
	FVBSaveContext Context;
	Context.World = Fixture.World;
	Context.PlayerState = CastChecked<APlayerState>(Log->GetOwner());
	bool bInjected = false;
	const int32 Delivered = Registry->ReadAll(*Save, Context, [&]()
	{
		if (Registry->IsApplyingSave() && !bInjected)
		{
			bInjected = true;
			Story->SetFact(Fact(), true);
			TestEqual(TEXT("state objective waits while other save participants are applying"), Log->GetStatus(Quest),
				EVBQuestStatus::Active);
		}

		return true;
	});
	TestTrue(TEXT("real read cycle delivered and exercised application phase"), Delivered > 0 && bInjected);
	TestFalse(TEXT("application phase finished"), Registry->IsApplyingSave());
	TestEqual(TEXT("OnSaveApplied reevaluates the deferred fact"), Log->GetStatus(Quest), EVBQuestStatus::Completed);
	return true;
}

#endif
