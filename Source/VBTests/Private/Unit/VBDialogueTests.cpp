#include "Misc/AutomationTest.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AI/VBEnemyCombatComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "Narrative/Dialogue/VBDialogueAsset.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

// 대화 단계의 단위 시험.
// 주인 액터 없는 대화 컴포넌트 = 권한 검사가 통과하는 자리라 서버 경로가 돌고, 서버, 클라 RPC 도 이 자리에서 바로 실행된다.
// 사실 저장소(GameState)가 없는 자리라 "잠긴 조건" 은 빈 조건 칸으로 만든다 - 빈 칸은 거짓이다(VBRules::EvaluateAll).

#if WITH_AUTOMATION_TESTS
namespace
{
	FVBDialogueChoice MakeChoice(const TCHAR* Text, FName Next, bool bLocked = false)
	{
		FVBDialogueChoice Choice;
		Choice.Text = FText::FromString(Text);
		Choice.NextNodeId = Next;
		if (bLocked)
		{
			Choice.Conditions.AddDefaulted();
		}

		return Choice;
	}

	FVBDialogueNode& AddNode(UVBDialogueAsset* Dialogue, FName NodeId, FName Next = NAME_None)
	{
		FVBDialogueNode& Node = Dialogue->Nodes.AddDefaulted_GetRef();
		Node.NodeId = NodeId;
		Node.Line = FText::FromName(NodeId);
		Node.NextNodeId = Next;
		return Node;
	}

	// First -(선택 0)-> Who -(계속)-> Last -(계속)-> 끝. First 의 선택지 1 은 잠겼고 2 는 바로 끝.
	UVBDialogueAsset* MakeTestDialogue()
	{
		UVBDialogueAsset* Dialogue = NewObject<UVBDialogueAsset>();
		FVBDialogueEntryPoint& Locked = Dialogue->EntryPoints.AddDefaulted_GetRef();
		Locked.Conditions.AddDefaulted();
		Locked.StartNodeId = TEXT("Last");
		Dialogue->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("First");

		FVBDialogueNode& First = AddNode(Dialogue, TEXT("First"));
		First.Choices.Add(MakeChoice(TEXT("who"), TEXT("Who")));
		First.Choices.Add(MakeChoice(TEXT("locked"), TEXT("Last"), /*bLocked=*/true));
		First.Choices.Add(MakeChoice(TEXT("bye"), NAME_None));
		AddNode(Dialogue, TEXT("Who"), TEXT("Last"));
		AddNode(Dialogue, TEXT("Last"));
		return Dialogue;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueEntryTest, "VB.Unit.Dialogue.Entry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueEntryTest::RunTest(const FString& Parameters)
{
	const UVBDialogueAsset* Dialogue = MakeTestDialogue();
	AddExpectedMessagePlain(TEXT("조건이 아닌 원소"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestEqual(TEXT("first true entry wins; the locked one is skipped"), Dialogue->ChooseEntry(FVBRuleContext()), FName(TEXT("First")));
	TestNotNull(TEXT("node found by id"), Dialogue->FindNode(TEXT("Who")));
	TestNull(TEXT("missing node is null"), Dialogue->FindNode(TEXT("Nope")));
	TestNull(TEXT("empty id is null"), Dialogue->FindNode(NAME_None));

	const UVBDialogueAsset* Empty = NewObject<UVBDialogueAsset>();
	TestTrue(TEXT("no entry points = nothing to say"), Empty->ChooseEntry(FVBRuleContext()).IsNone());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueFlowTest, "VB.Unit.Dialogue.Flow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueFlowTest::RunTest(const FString& Parameters)
{
	const UVBDialogueAsset* Dialogue = MakeTestDialogue();
	UVBDialogueComponent* Component = NewObject<UVBDialogueComponent>();
	int32 ViewChanges = 0;
	Component->OnViewChanged.AddLambda([&ViewChanges]() { ++ViewChanges; });

	// 진입점 판정 1 + 첫 노드 선택지 판정 1 + 잠긴 선택 요청 재판정 1 = 빈 조건 칸 경고 3.
	AddExpectedMessagePlain(TEXT("조건이 아닌 원소"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 3);
	AddExpectedMessagePlain(TEXT("선택지 5 번이 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);

	TestTrue(TEXT("start"), Component->StartDialogue(Dialogue, nullptr));
	TestTrue(TEXT("server is in dialogue"), Component->IsInDialogue());
	TestEqual(TEXT("view shows the entry node"), Component->GetView().NodeId, FName(TEXT("First")));
	TestEqual(TEXT("availability per choice"), Component->GetView().ChoiceAvailable.Num(), 3);
	TestTrue(TEXT("open choice is available"), Component->GetView().ChoiceAvailable[0]);
	TestFalse(TEXT("locked choice is not"), Component->GetView().ChoiceAvailable[1]);
	TestFalse(TEXT("second start while talking is refused"), Component->StartDialogue(Dialogue, nullptr));

	Component->RequestChoose(1);
	TestEqual(TEXT("locked choice does not move"), Component->GetView().NodeId, FName(TEXT("First")));
	Component->RequestChoose(5);
	TestEqual(TEXT("bad index does not move"), Component->GetView().NodeId, FName(TEXT("First")));

	Component->RequestChoose(0);
	TestEqual(TEXT("choice follows its link"), Component->GetView().NodeId, FName(TEXT("Who")));
	Component->RequestContinue();
	TestEqual(TEXT("continue follows NextNodeId"), Component->GetView().NodeId, FName(TEXT("Last")));
	Component->RequestContinue();
	TestFalse(TEXT("continue past the last node ends"), Component->IsInDialogue());
	TestFalse(TEXT("view is cleared on end"), Component->GetView().IsActive());
	// 보기 바뀜: First, Who, Last, 끝.
	TestEqual(TEXT("one view change per node plus the end"), ViewChanges, 4);

	Component->RequestContinue();
	TestFalse(TEXT("request after the end is ignored"), Component->IsInDialogue());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueLeaveTest, "VB.Unit.Dialogue.Leave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueLeaveTest::RunTest(const FString& Parameters)
{
	const UVBDialogueAsset* Dialogue = MakeTestDialogue();
	UVBDialogueComponent* Component = NewObject<UVBDialogueComponent>();
	AddExpectedMessagePlain(TEXT("조건이 아닌 원소"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 4);

	Component->StartDialogue(Dialogue, nullptr);
	Component->RequestLeave();
	TestFalse(TEXT("leave ends the dialogue"), Component->IsInDialogue());

	Component->StartDialogue(Dialogue, nullptr);
	Component->RequestChoose(2);
	TestFalse(TEXT("a choice with no link ends the dialogue"), Component->IsInDialogue());
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueValidateTest, "VB.Unit.Dialogue.Validate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueValidateTest::RunTest(const FString& Parameters)
{
	// 시험용 잠금(빈 조건 칸)을 걷어낸 것이 "멀쩡한" 대화다. 걷어내기 전 것은 검증기가 저작 실수로 본다.
	auto MakeClean = []()
	{
		UVBDialogueAsset* Dialogue = MakeTestDialogue();
		Dialogue->EntryPoints[0].Conditions.Reset();
		Dialogue->Nodes[0].Choices[1].Conditions.Reset();
		return Dialogue;
	};
	{
		FDataValidationContext Context;
		TestEqual(TEXT("well-formed dialogue is valid"), MakeClean()->IsDataValid(Context), EDataValidationResult::Valid);
	}

	{
		FDataValidationContext Context;
		TestEqual(TEXT("empty condition slot is invalid"), MakeTestDialogue()->IsDataValid(Context), EDataValidationResult::Invalid);
	}

	{
		UVBDialogueAsset* Broken = MakeClean();
		Broken->Nodes[1].NextNodeId = TEXT("Nowhere");
		FDataValidationContext Context;
		TestEqual(TEXT("dangling link is invalid"), Broken->IsDataValid(Context), EDataValidationResult::Invalid);
	}

	{
		// 끝으로 가는 길이 없는 고리: A -> B -> A. 진입점에서 닿으니 "닿지 않음" 경고가 아니라 오류여야 한다.
		UVBDialogueAsset* Trapped = NewObject<UVBDialogueAsset>();
		Trapped->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("A");
		AddNode(Trapped, TEXT("A"), TEXT("B"));
		AddNode(Trapped, TEXT("B"), TEXT("A"));
		FDataValidationContext Context;
		TestEqual(TEXT("a loop with no way to the end is invalid"), Trapped->IsDataValid(Context), EDataValidationResult::Invalid);

		// 같은 고리에 끝으로 나가는 선택지 하나를 달면 유효하다(대조).
		Trapped->Nodes[1].Choices.Add(MakeChoice(TEXT("back"), TEXT("A")));
		Trapped->Nodes[1].Choices.Add(MakeChoice(TEXT("leave"), NAME_None));
		Trapped->Nodes[1].NextNodeId = NAME_None;
		FDataValidationContext ExitContext;
		TestEqual(TEXT("the same loop with an exit is valid"), Trapped->IsDataValid(ExitContext), EDataValidationResult::Valid);
	}

	{
		UVBDialogueAsset* Duplicate = MakeClean();
		AddNode(Duplicate, TEXT("Who"));
		FDataValidationContext Context;
		TestEqual(TEXT("duplicate node id is invalid"), Duplicate->IsDataValid(Context), EDataValidationResult::Invalid);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueValidationMessagesTest, "VB.Unit.Dialogue.ValidationMessages",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueValidationMessagesTest::RunTest(const FString& Parameters)
{
	auto Check = [this](const TCHAR* Case, UVBDialogueAsset* Dialogue, EDataValidationResult ExpectedResult,
		TArray<FString> ExpectedIssueMessages, TArray<EMessageSeverity::Type> ExpectedSeverities)
	{
		FDataValidationContext Context;
		TestEqual(Case, Dialogue->IsDataValid(Context), ExpectedResult);
		const auto& Issues = Context.GetIssues();
		TestEqual(TEXT("exact issue count"), Issues.Num(), ExpectedIssueMessages.Num());
		for (int32 Index = 0; Index < FMath::Min(Issues.Num(), ExpectedIssueMessages.Num()); ++Index)
		{
			TestEqual(TEXT("issue source text and order"), Issues[Index].Message.BuildSourceString(), ExpectedIssueMessages[Index]);
			TestEqual(TEXT("issue severity and order"), static_cast<int32>(Issues[Index].Severity),
				static_cast<int32>(ExpectedSeverities[Index]));
		}
	};
	UVBDialogueAsset* Clean = NewObject<UVBDialogueAsset>();
	Clean->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("A");
	AddNode(Clean, TEXT("A"));
	Check(TEXT("clean"), Clean, EDataValidationResult::Valid, {}, {});

	UVBDialogueAsset* Empty = NewObject<UVBDialogueAsset>();
	Check(TEXT("empty"), Empty, EDataValidationResult::Invalid,
		{TEXT("진입점이 없다 - 이 대화는 시작할 수 없다.")}, {EMessageSeverity::Error});

	UVBDialogueAsset* Broken = NewObject<UVBDialogueAsset>();
	Broken->Speakers.Add(nullptr);
	Broken->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("A");
	AddNode(Broken, TEXT("A"), TEXT("Missing"));
	AddNode(Broken, TEXT("A"), TEXT("Missing"));
	AddNode(Broken, TEXT("Orphan"));
	Check(TEXT("multiple errors and warning preserve order"), Broken, EDataValidationResult::Invalid,
		{TEXT("화자 0 번이 비었다."), TEXT("노드 A 이 두 번 있다 - 연결이 앞의 것만 찾는다."),
		TEXT("노드 A: 없는 노드 Missing 를 가리킨다."), TEXT("노드 A: 없는 노드 Missing 를 가리킨다."),
		TEXT("노드 Orphan 에는 어느 진입점에서도 닿지 않는다."),
		TEXT("노드 A 에서는 끝으로 가는 길이 없다 - 고리에 갇힌다."),
		TEXT("노드 A 에서는 끝으로 가는 길이 없다 - 고리에 갇힌다.")},
		{EMessageSeverity::Error, EMessageSeverity::Error, EMessageSeverity::Error, EMessageSeverity::Error,
		EMessageSeverity::Warning, EMessageSeverity::Error, EMessageSeverity::Error});

	UVBDialogueAsset* WarningOnly = NewObject<UVBDialogueAsset>();
	WarningOnly->EntryPoints.AddDefaulted_GetRef().StartNodeId = TEXT("A");
	FVBDialogueNode& ChoiceNode = AddNode(WarningOnly, TEXT("A"), TEXT("Orphan"));
	ChoiceNode.Choices.Add(MakeChoice(TEXT("exit"), NAME_None));
	AddNode(WarningOnly, TEXT("Orphan"));
	Check(TEXT("choices ignore Next and warnings stay valid"), WarningOnly, EDataValidationResult::Valid,
		{TEXT("노드 A: 선택지가 있어 NextNodeId(Orphan)는 쓰이지 않는다."),
		TEXT("노드 Orphan 에는 어느 진입점에서도 닿지 않는다.")},
		{EMessageSeverity::Warning, EMessageSeverity::Warning});
	return true;
}

#endif

// 전투 중 대화 거절의 판정. 막는 경우와 여는 경우를 같이 본다 - 막기만 보면 늘 거절하는 판정도 통과한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueCombatGateTest, "VB.Unit.Dialogue.CombatGate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBDialogueCombatGateTest::RunTest(const FString& Parameters)
{
	UVBDialogueAsset* Normal = NewObject<UVBDialogueAsset>();
	UVBDialogueAsset* Taunt = NewObject<UVBDialogueAsset>();
	Taunt->bAllowDuringCombat = true;

	TestTrue(TEXT("engaged player cannot open a normal dialogue"), VBDialogueCombatGate::ShouldRefuse(Normal, true));
	TestFalse(TEXT("free player opens a normal dialogue"), VBDialogueCombatGate::ShouldRefuse(Normal, false));
	TestFalse(TEXT("a dialogue allowed during combat opens while engaged"), VBDialogueCombatGate::ShouldRefuse(Taunt, true));
	TestFalse(TEXT("an empty dialogue is not refused here (StartDialogue refuses it separately)"),
		VBDialogueCombatGate::ShouldRefuse(nullptr, true));

	// 주인(컨트롤러)이 없는 컴포넌트는 폰이 없다 - 노릴 몸이 없으니 막지 않는다. 엔진 경고 없이 끝나야 한다(월드 조회를 폰으로 한다).
	UVBDialogueComponent* Orphan = NewObject<UVBDialogueComponent>();
	TestFalse(TEXT("no pawn means not engaged"), Orphan->IsBlockedByCombat(Normal));
	return true;
}

// "전투 중" 의 진실 = 코디네이터 그룹. 적 컴포넌트의 SetCombatTarget 이 실제 등록 길이다(BTService 가 매 주기 부른다).
//   EngineFilter 인 이유: 월드를 만든다(최소 월드).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBDialogueCombatEngagedTest, "VB.Unit.Dialogue.CombatEngaged",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBDialogueCombatEngagedTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	UVBCombatCoordinatorSubsystem* Coordinator = World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
	if (!TestNotNull(TEXT("coordinator lives in the world"), Coordinator))
	{
		return false;
	}

	AActor* Player = World->SpawnActor<AActor>();
	AActor* Bystander = World->SpawnActor<AActor>();
	AActor* Enemy = World->SpawnActor<AActor>();
	UVBEnemyCombatComponent* EnemyCombat = NewObject<UVBEnemyCombatComponent>(Enemy);

	TestFalse(TEXT("nobody targets the player yet"), Coordinator->IsEngaged(Player));
	EnemyCombat->SetCombatTarget(Player);
	TestTrue(TEXT("an enemy targeting the player makes the player engaged"), Coordinator->IsEngaged(Player));
	TestFalse(TEXT("another actor is not engaged by that enemy"), Coordinator->IsEngaged(Bystander));
	EnemyCombat->LeaveCombat();
	TestFalse(TEXT("leaving combat ends the engagement"), Coordinator->IsEngaged(Player));
	TestFalse(TEXT("null target is never engaged"), Coordinator->IsEngaged(nullptr));
	return true;
}

#endif
