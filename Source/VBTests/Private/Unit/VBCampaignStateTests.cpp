#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameModes/VBGameState.h"
#include "Misc/ScopeExit.h"
#include "Narrative/Campaign/VBCampaignRules.h"
#include "Narrative/Campaign/VBCampaignStateComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 회차와 앞 회차 조건.
namespace
{
	FGameplayTag CampaignTag(const TCHAR* Name) { return FGameplayTag::RequestGameplayTag(FName(Name), /*ErrorIfNotFound=*/false); }

	bool PreviousEnding(const FVBRuleContext& Context, FGameplayTag Ending, bool bNegate = false)
	{
		FVBCondition_PreviousEnding Condition;
		Condition.Ending = Ending;
		Condition.bNegate = bNegate;
		return Condition.Evaluate(Context);
	}

	bool PreviousFact(const FVBRuleContext& Context, FGameplayTag Fact, bool bNegate = false)
	{
		FVBCondition_PreviousFact Condition;
		Condition.Fact = Fact;
		Condition.bNegate = bNegate;
		return Condition.Evaluate(Context);
	}
}

// 격리 월드: 실제 GameState 클래스가 주인을 갖고, 이름 붙이기, 세이브 읽기, 조건이 앞 회차만 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCampaignConditionsTest, "VB.Unit.Campaign.Conditions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCampaignConditionsTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Root = CampaignTag(TEXT("Ending"));
	const FGameplayTag Alone = CampaignTag(TEXT("Ending.ReturnAlone"));
	const FGameplayTag Shared = CampaignTag(TEXT("Ending.SharedRecovery"));
	const FGameplayTag Talked = CampaignTag(TEXT("Story.Test.Talked"));
	const FGameplayTag Never = CampaignTag(TEXT("Story.Test.Never"));
	if (!TestTrue(TEXT("ending and story tags registered"), Root.IsValid() && Alone.IsValid() && Shared.IsValid()
		&& Talked.IsValid() && Never.IsValid()))
	{
		return false;
	}

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};

	FVBRuleContext Context;
	Context.World = World;
	AddExpectedMessagePlain(TEXT("회차 주인이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("no game state yet: previous-ending is false"), PreviousEnding(Context, Root));
	TestFalse(TEXT("no game state yet: negated previous-ending is also false (unknown is not «first run»)"),
		PreviousEnding(Context, Root, true));

	{
		// 회차를 아직 모르는 주인(클라 복제 전과 같은 모양): InitializeActorsForPlay 전이라 InitializeComponent 가 안 돌아 이름이 없다.
		AGameStateBase* Early = World->SpawnActor<AGameStateBase>();
		if (!TestNotNull(TEXT("early game state"), Early))
		{
			return false;
		}

		World->SetGameState(Early);
		UVBCampaignStateComponent* Unnamed = NewObject<UVBCampaignStateComponent>(Early);
		Early->AddInstanceComponent(Unnamed);
		Unnamed->RegisterComponent();
		TestEqual(TEXT("component before its first read is unnamed"), Unnamed->GetRunIndex(), 0);
		TestFalse(TEXT("unknown run: negated previous-ending is false (not «first run»)"), PreviousEnding(Context, Root, true));
		TestFalse(TEXT("unknown run: negated previous-fact is false"), PreviousFact(Context, Never, true));

		// 권한 없는 쪽(클라)의 ReadSave 는 아무것도 안 바꾼다.
		Early->SetRole(ROLE_SimulatedProxy);
		const TStrongObjectPtr<UVBSaveGame> Named(NewObject<UVBSaveGame>(GetTransientPackage()));
		Named->Campaign.CampaignId = FGuid::NewGuid();
		Named->Campaign.RunId = FGuid::NewGuid();
		Named->Campaign.RunIndex = 2;
		AddExpectedMessagePlain(TEXT("권한 없는 쪽"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		Unnamed->ReadSave(*Named.Get(), FVBSaveContext());
		TestEqual(TEXT("client ReadSave is ignored"), Unnamed->GetRunIndex(), 0);
		Early->Destroy();
	}

	// 이것 없이는 액터가 초기화되지 않아 RegisterComponent 가 InitializeComponent 를 건너뛴다.
	World->InitializeActorsForPlay(FURL());
	AVBGameState* GameState = World->SpawnActor<AVBGameState>();
	if (!TestNotNull(TEXT("game state"), GameState))
	{
		return false;
	}

	World->SetGameState(GameState);
	UVBCampaignStateComponent* Campaign = UVBCampaignStateComponent::Get(World);
	if (!TestNotNull(TEXT("AVBGameState owns a campaign component"), Campaign))
	{
		return false;
	}

	// 세이브 없는 월드(PIE 바로 시작) = 1회차, 이름이 붙고 앞 회차는 없다.
	TestEqual(TEXT("fresh world is run 1"), Campaign->GetRunIndex(), 1);
	TestTrue(TEXT("fresh world gets a campaign and run name"), Campaign->GetState().CampaignId.IsValid()
		&& Campaign->GetState().RunId.IsValid());
	TestFalse(TEXT("run 1 has no previous clear"), Campaign->GetPrevious().IsSet());
	TestFalse(TEXT("run 1: «any previous ending» is false"), PreviousEnding(Context, Root));
	TestTrue(TEXT("run 1: negated «any previous ending» is true"), PreviousEnding(Context, Root, true));
	TestFalse(TEXT("run 1: previous fact is false"), PreviousFact(Context, Talked));

	// 2회차 세이브: 앞 회차가 "혼자 돌아옴" 으로 끝났고 "대화했다" 를 기억한다.
	const TStrongObjectPtr<UVBSaveGame> Save(NewObject<UVBSaveGame>(GetTransientPackage()));
	Save->Campaign.CampaignId = FGuid::NewGuid();
	Save->Campaign.RunId = FGuid::NewGuid();
	Save->Campaign.RunIndex = 2;
	Save->Campaign.Parent.ClearId = FGuid::NewGuid();
	Save->Campaign.Parent.Ending = Alone;
	Save->Campaign.Parent.Facts.AddTagFast(Talked);
	Campaign->ReadSave(*Save.Get(), FVBSaveContext());
	TestEqual(TEXT("read run 2"), Campaign->GetRunIndex(), 2);
	TestEqual(TEXT("read campaign id"), Campaign->GetState().CampaignId, Save->Campaign.CampaignId);
	TestTrue(TEXT("previous ending matches its own tag"), PreviousEnding(Context, Alone));
	TestFalse(TEXT("previous ending does not match another ending"), PreviousEnding(Context, Shared));
	TestTrue(TEXT("negated other ending is true"), PreviousEnding(Context, Shared, true));
	TestTrue(TEXT("«Ending» root matches any previous ending"), PreviousEnding(Context, Root));
	TestTrue(TEXT("remembered fact is true"), PreviousFact(Context, Talked));
	TestFalse(TEXT("unremembered fact is false"), PreviousFact(Context, Never));
	TestTrue(TEXT("negated unremembered fact is true"), PreviousFact(Context, Never, true));
	{
		AddExpectedMessagePlain(TEXT("이름표가 비었다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
		TestFalse(TEXT("empty ending tag is false"), PreviousEnding(Context, FGameplayTag()));
		TestFalse(TEXT("empty ending tag is false even negated"), PreviousEnding(Context, FGameplayTag(), true));
	}

	{
		// 조건 배열(대화, 적 존재 조건이 쓰는 모양)로도 같은 답.
		TArray<FInstancedStruct> Conditions;
		Conditions.Add(FInstancedStruct::Make(FVBCondition_PreviousEnding{}));
		Conditions[0].GetMutable<FVBCondition_PreviousEnding>().Ending = Alone;
		TestTrue(TEXT("condition array sees the previous ending"), VBRules::EvaluateAll(Conditions, Context));
	}

	// 저장 왕복: 쓰기는 지금 회차를 그대로.
	const TStrongObjectPtr<UVBSaveGame> Out(NewObject<UVBSaveGame>(GetTransientPackage()));
	Campaign->WriteSave(*Out.Get(), FVBSaveContext());
	TestEqual(TEXT("write keeps the run id"), Out->Campaign.RunId, Save->Campaign.RunId);
	TestEqual(TEXT("write keeps the parent clear"), Out->Campaign.Parent.ClearId, Save->Campaign.Parent.ClearId);
	TestTrue(TEXT("write keeps the parent facts"), Out->Campaign.Parent.Facts.HasTagExact(Talked));

	// 옛 슬롯(빈 회차 구역)을 이미 이름 붙은 월드에 읽어도 이름이 지워지지 않는다.
	const TStrongObjectPtr<UVBSaveGame> Legacy(NewObject<UVBSaveGame>(GetTransientPackage()));
	Campaign->ReadSave(*Legacy.Get(), FVBSaveContext());
	TestEqual(TEXT("empty campaign zone does not erase the current run"), Campaign->GetRunIndex(), 2);
	TestTrue(TEXT("and keeps the previous ending"), PreviousEnding(Context, Alone));
	return true;
}

// 이름 없는 회차에 앞 회차 기록만 있는 손상 세이브: 1회차로 다시 시작하고 기록은 버린다("몇 회차" 를 지어내지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCampaignCorruptTest, "VB.Unit.Campaign.CorruptSave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCampaignCorruptTest::RunTest(const FString& Parameters)
{
	const FGameplayTag Alone = CampaignTag(TEXT("Ending.ReturnAlone"));
	if (!TestTrue(TEXT("tag registered"), Alone.IsValid()))
	{
		return false;
	}

	// 소유 액터 없는 컴포넌트 = 단위 시험의 서버(쓰기 허용). InitializeComponent 를 안 지나 이름이 아직 없다.
	UVBCampaignStateComponent* Campaign = NewObject<UVBCampaignStateComponent>(GetTransientPackage());
	const TStrongObjectPtr<UVBCampaignStateComponent> Keep(Campaign);
	TestEqual(TEXT("unnamed before any read"), Campaign->GetRunIndex(), 0);

	const TStrongObjectPtr<UVBSaveGame> Save(NewObject<UVBSaveGame>(GetTransientPackage()));
	Save->Campaign.RunIndex = 3; // 이름(Guid)이 없다
	Save->Campaign.Parent.ClearId = FGuid::NewGuid();
	Save->Campaign.Parent.Ending = Alone;
	AddExpectedMessagePlain(TEXT("회차 이름이 비었는데"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Campaign->ReadSave(*Save.Get(), FVBSaveContext());
	TestEqual(TEXT("corrupt save restarts as run 1"), Campaign->GetRunIndex(), 1);
	TestFalse(TEXT("corrupt parent is dropped"), Campaign->GetPrevious().IsSet());
	TestTrue(TEXT("and the run gets a real name"), Campaign->GetState().RunId.IsValid());

	// 이름은 있는데 앞 회차 결말 이름이 빈 기록: 회차는 받아들이고 앞 회차 기록만 버린다.
	const TStrongObjectPtr<UVBSaveGame> NoEnding(NewObject<UVBSaveGame>(GetTransientPackage()));
	NoEnding->Campaign.CampaignId = FGuid::NewGuid();
	NoEnding->Campaign.RunId = FGuid::NewGuid();
	NoEnding->Campaign.RunIndex = 2;
	NoEnding->Campaign.Parent.ClearId = FGuid::NewGuid();
	AddExpectedMessagePlain(TEXT("결말 이름이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	Campaign->ReadSave(*NoEnding.Get(), FVBSaveContext());
	TestEqual(TEXT("named run is adopted"), Campaign->GetRunIndex(), 2);
	TestFalse(TEXT("parent without an ending is dropped"), Campaign->GetPrevious().IsSet());
	return true;
}

// 실제 시작 길: 저장 서브시스템의 새 게임 뒤 생기는 GameState 가 ReadSave 없이 1회차로 이름을 붙이고, 그 이름을 활성 세이브에 바로 적는다
//   (저장 없이 레벨을 옮겨도 같은 회차). 새 게임을 또 하면 다른 줄기다. 난이도 NewGameStart 와 같은 대가(이 실행의 활성 세이브가 바뀐다, 디스크는 안 씀).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCampaignNewGameTest, "VB.Unit.Campaign.NewGameStart",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCampaignNewGameTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		if ((WorldContext.WorldType == EWorldType::Game || WorldContext.WorldType == EWorldType::PIE)
			&& WorldContext.OwningGameInstance && WorldContext.World())
		{
			World = WorldContext.World();
			break;
		}
	}

	if (!TestNotNull(TEXT("a game world owned by a game instance (run headless -game or in PIE)"), World))
	{
		return false;
	}

	UVBSaveGameSubsystem* SaveSubsystem = World->GetGameInstance()->GetSubsystem<UVBSaveGameSubsystem>();
	if (!TestNotNull(TEXT("save subsystem"), SaveSubsystem))
	{
		return false;
	}

	auto StartAndRead = [&](FVBCampaignSaveData& OutState) -> bool
	{
		if (!TestTrue(TEXT("new game accepted"), SaveSubsystem->StartNewGameWithDifficulty(FGameplayTag())))
		{
			return false;
		}

		TestFalse(TEXT("blank new save has no run name yet"), SaveSubsystem->GetActiveGameSave()->Campaign.RunId.IsValid());
		AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
		if (!TestNotNull(TEXT("game state"), GameState))
		{
			return false;
		}

		ON_SCOPE_EXIT { GameState->Destroy(); };
		UVBCampaignStateComponent* Campaign = NewObject<UVBCampaignStateComponent>(GameState);
		GameState->AddInstanceComponent(Campaign);
		Campaign->RegisterComponent(); // InitializeComponent - ReadSave 를 부르지 않는다
		OutState = Campaign->GetState();
		TestEqual(TEXT("new game is run 1"), OutState.RunIndex, 1);
		TestFalse(TEXT("new game has no previous clear"), OutState.Parent.IsSet());
		TestEqual(TEXT("the run name is written to the active save at once"), SaveSubsystem->GetActiveGameSave()->Campaign.RunId,
			OutState.RunId);
		FVBClearSummary Clear;
		Clear.ClearId = FGuid::NewGuid();
		Clear.Ending = CampaignTag(TEXT("Ending.ReturnAlone"));
		TestTrue(TEXT("record clear accepted on authority"), Campaign->RecordClear(Clear));
		TestEqual(TEXT("clear reaches active save in memory"), SaveSubsystem->GetActiveGameSave()->Campaign.LastClear.ClearId,
			Clear.ClearId);
		TestTrue(TEXT("seen ending reaches active save"),
			SaveSubsystem->GetActiveGameSave()->Campaign.SeenEndings.HasTagExact(Clear.Ending));
		TestFalse(TEXT("clear never replaces run 1 parent"), SaveSubsystem->GetActiveGameSave()->Campaign.Parent.IsSet());
		return true;
	};
	FVBCampaignSaveData First, Second;
	if (!StartAndRead(First) || !StartAndRead(Second))
	{
		return false;
	}

	TestNotEqual(TEXT("a second new game is a different campaign"), Second.CampaignId, First.CampaignId);
	return true;
}
#endif
