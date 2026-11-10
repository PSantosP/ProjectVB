#include "Misc/AutomationTest.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagContainer.h"
#include "Net/UnrealNetwork.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "Inventory/VBItemDefinition.h"
#include "Inventory/VBItemRules.h"
#include "Narrative/Cinematic/VBCinematicRules.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestRules.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Narrative/Tutorial/VBTutorialDefinition.h"
#include "Narrative/Tutorial/VBTutorialComponent.h"
#include "Narrative/Tutorial/VBTutorialTypes.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_AUTOMATION_TESTS
namespace VBTutorialUnitTests
{
	constexpr const TCHAR* TutorialA = TEXT("Tutorial.Unit.A");
	constexpr const TCHAR* TutorialB = TEXT("Tutorial.Unit.B");
	constexpr const TCHAR* MissingTutorial = TEXT("Tutorial.Unit.MissingSave");
	constexpr const TCHAR* StepFirst = TEXT("Tutorial.Step.Unit.First");
	constexpr const TCHAR* StepSecond = TEXT("Tutorial.Step.Unit.Second");
	constexpr const TCHAR* StepThird = TEXT("Tutorial.Step.Unit.Third");
	constexpr const TCHAR* StepUnknown = TEXT("Tutorial.Step.Unit.Unknown");
	constexpr const TCHAR* AdvanceEvent = TEXT("Event.Tutorial.Unit.Advance");
	constexpr const TCHAR* OtherEvent = TEXT("Event.Tutorial.Unit.Other");
	constexpr const TCHAR* TargetA = TEXT("Item.Tutorial.Unit.TargetA");
	constexpr const TCHAR* TargetB = TEXT("Item.Tutorial.Unit.TargetB");
	constexpr const TCHAR* ReadyFact = TEXT("Story.Tutorial.Unit.Ready");

	FGameplayTag TutorialTag(const TCHAR* Name)
	{
		// UncookedOnly 모듈은 native tag 를 갖지 않고 시험 시점에 ini 태그를 찾는다
		return FGameplayTag::RequestGameplayTag(FName(Name), false);
	}

	bool TutorialTagsReady(FAutomationTestBase& Test)
	{
		bool bReady = true;
		for (const TCHAR* Name : { TutorialA, TutorialB, MissingTutorial, StepFirst,
			StepSecond, StepThird, StepUnknown, AdvanceEvent, OtherEvent, TargetA, TargetB, ReadyFact })
		{
			bReady &= Test.TestTrue(*FString::Printf(TEXT("registered tutorial test tag: %s"), Name), TutorialTag(Name).IsValid());
		}

		return bReady;
	}

	struct FTutorialFixture
	{
		UWorld* World = nullptr;
		UVBStoryStateComponent* Story = nullptr;
		APlayerState* Player = nullptr;
		APlayerState* OtherPlayer = nullptr;

		FTutorialFixture()
		{
			const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!World)
			{
				return;
			}

			AGameStateBase* GS = World->SpawnActor<AGameStateBase>();
			Player = World->SpawnActor<APlayerState>();
			OtherPlayer = World->SpawnActor<APlayerState>();
			if (!GS || !Player || !OtherPlayer)
			{
				return;
			}

			World->SetGameState(GS);
			Story = NewObject<UVBStoryStateComponent>(GS);
			GS->AddInstanceComponent(Story);
			Story->RegisterComponent();
		}

		~FTutorialFixture() { if (World) { World->DestroyWorld(false); } }
		bool IsValid() const { return World && Story && Player && OtherPlayer; }
		FVBRuleContext Context() const { return FVBRuleContext::Make(Story, Player); }
	};

	FVBTutorialStep MakeEventStep(FGameplayTag Id, int32 Required = 1)
	{
		FVBTutorialStep Step;
		Step.StepId = Id;
		Step.Title = FText::AsCultureInvariant(TEXT("Neutral step"));
		Step.Body = FText::AsCultureInvariant(TEXT("Neutral unit test content."));
		FVBQuestObjective_Event Goal;
		Goal.EventTag = TutorialTag(AdvanceEvent);
		Goal.RequiredCount = Required;
		Step.Completion = FInstancedStruct::Make(Goal);
		return Step;
	}

	FVBTutorialStep MakeConditionStep(FGameplayTag Id)
	{
		FVBTutorialStep Step = MakeEventStep(Id);
		FVBCondition_Fact Fact;
		Fact.Fact = TutorialTag(ReadyFact);
		FVBQuestObjective_Condition Goal;
		Goal.Conditions.Add(FInstancedStruct::Make(Fact));
		Step.Completion = FInstancedStruct::Make(Goal);
		return Step;
	}

	UVBTutorialDefinition* MakeDefinition(FGameplayTag Id = TutorialTag(TutorialA))
	{
		UVBTutorialDefinition* Definition = NewObject<UVBTutorialDefinition>();
		Definition->TutorialId = Id;
		Definition->Title = FText::AsCultureInvariant(TEXT("Neutral tutorial"));
		Definition->Summary = FText::AsCultureInvariant(TEXT("Unit test only."));
		Definition->Steps = { MakeEventStep(TutorialTag(StepFirst), 2), MakeEventStep(TutorialTag(StepSecond)) };
		return Definition;
	}

	FVBGameEvent MakeEvent(APlayerState* Player, int32 Count = 1)
	{
		FVBGameEvent Event;
		Event.EventTag = TutorialTag(AdvanceEvent);
		Event.Instigator = Player;
		Event.Count = Count;
		return Event;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialActivationTest, "VB.Unit.Tutorial.Activation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialActivationTest::RunTest(const FString& Parameters)
{
	if (!TutorialTagsReady(*this))
	{
		return false;
	}

	FTutorialFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	UVBTutorialDefinition* Definition = MakeDefinition();
	const FVBRuleContext Context = F.Context();
	FVBGameEvent Event = MakeEvent(F.Player);
	TestFalse(TEXT("empty activation is explicit-start only"), VBTutorial::CanActivate(*Definition, &Event, Context));
	Definition->ActivationEvent = TutorialTag(AdvanceEvent);
	TestFalse(TEXT("event activation cannot start on a state refresh"), VBTutorial::CanActivate(*Definition, nullptr, Context));
	TestTrue(TEXT("matching event activates"), VBTutorial::CanActivate(*Definition, &Event, Context));
	Event.Instigator = F.OtherPlayer;
	TestFalse(TEXT("other player's activation is refused"), VBTutorial::CanActivate(*Definition, &Event, Context));
	Event.Instigator = nullptr;
	TestTrue(TEXT("world event may activate"), VBTutorial::CanActivate(*Definition, &Event, Context));
	Definition->ActivationTargetQuery = FGameplayTagQuery::MakeQuery_MatchTag(TutorialTag(TargetA));
	Event.TargetTags.AddTag(TutorialTag(TargetB));
	TestFalse(TEXT("wrong target does not activate"), VBTutorial::CanActivate(*Definition, &Event, Context));
	Event.TargetTags.AddTag(TutorialTag(TargetA));
	TestTrue(TEXT("matching target activates"), VBTutorial::CanActivate(*Definition, &Event, Context));
	Definition->ActivationEvent = FGameplayTag::RequestGameplayTag(FName(TEXT("Event.Tutorial.Unit")), false);
	TestTrue(TEXT("existing matcher preserves parent-event matching"), VBTutorial::CanActivate(*Definition, &Event, Context));
	FVBCondition_Fact Fact;
	Fact.Fact = TutorialTag(ReadyFact);
	Definition->ActivationConditions.Add(FInstancedStruct::Make(Fact));
	TestFalse(TEXT("activation also requires condition"), VBTutorial::CanActivate(*Definition, &Event, Context));
	F.Story->SetFact(TutorialTag(ReadyFact), true);
	TestTrue(TEXT("event and condition together activate"), VBTutorial::CanActivate(*Definition, &Event, Context));
	Definition->ActivationEvent = FGameplayTag();
	Definition->ActivationTargetQuery = FGameplayTagQuery();
	TestTrue(TEXT("condition-only activation works on refresh"), VBTutorial::CanActivate(*Definition, nullptr, Context));
	FVBRuleContext MissingStory = Context;
	MissingStory.Story = nullptr;
	Definition->ActivationConditions[0].GetMutable<FVBCondition_Fact>().bNegate = true;
	TestFalse(TEXT("missing story is not an empty fact state"), VBTutorial::CanActivate(*Definition, nullptr, MissingStory));
	FVBTutorialProgress Progress;
	TestTrue(TEXT("explicit start"), VBTutorial::Start(*Definition, Progress));
	TestEqual(TEXT("start does not count the activation event"), Progress.EventCount, 0);
	TestFalse(TEXT("active tutorial is not reset by start"), VBTutorial::Start(*Definition, Progress));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialEventProgressTest, "VB.Unit.Tutorial.EventProgress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialEventProgressTest::RunTest(const FString& Parameters)
{
	if (!TutorialTagsReady(*this))
	{
		return false;
	}

	FTutorialFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	UVBTutorialDefinition* Definition = MakeDefinition();
	FVBTutorialProgress Progress;
	if (!TestTrue(TEXT("start valid definition"), VBTutorial::Start(*Definition, Progress)))
	{
		return false;
	}

	const FVBRuleContext Context = F.Context();
	FVBGameEvent Event = MakeEvent(F.OtherPlayer);
	TestFalse(TEXT("other player's event is ignored"), VBTutorial::ApplyEvent(*Definition, Event, Context, Progress));
	Event.Instigator = F.Player;
	Event.EventTag = TutorialTag(OtherEvent);
	TestFalse(TEXT("different event is ignored"), VBTutorial::ApplyEvent(*Definition, Event, Context, Progress));
	Event.EventTag = TutorialTag(AdvanceEvent);
	Definition->Steps[0].Completion.GetMutable<FVBQuestObjective_Event>().TargetQuery = FGameplayTagQuery::MakeQuery_MatchTag(TutorialTag(TargetA));
	TestFalse(TEXT("wrong target is ignored"), VBTutorial::ApplyEvent(*Definition, Event, Context, Progress));
	Event.TargetTags.AddTag(TutorialTag(TargetA));
	TestTrue(TEXT("first counted event"), VBTutorial::ApplyEvent(*Definition, Event, Context, Progress));
	TestEqual(TEXT("partial progress"), Progress.EventCount, 1);
	Event.Count = MAX_int32;
	TestTrue(TEXT("large count saturates without overflow"), VBTutorial::ApplyEvent(*Definition, Event, Context, Progress));
	TestTrue(TEXT("advances exactly one event step"), Progress.CurrentStepId == TutorialTag(StepSecond));
	TestEqual(TEXT("surplus does not spill to next step"), Progress.EventCount, 0);
	TestFalse(TEXT("next event step is not complete"), Progress.bCompleted);
	TestEqual(TEXT("two steps are unlocked"), VBTutorial::BuildReviewStepIndices(*Definition, Progress).Num(), 2);
	Event.Instigator = nullptr;
	Event.Count = 0;
	TestTrue(TEXT("existing quest matcher treats zero-count world event as one"),
		VBTutorial::ApplyEvent(*Definition, Event, Context, Progress));
	TestTrue(TEXT("last step completes"), Progress.bCompleted);
	TestFalse(TEXT("completed tutorial ignores events"), VBTutorial::ApplyEvent(*Definition, Event, Context, Progress));
	TestFalse(TEXT("completed tutorial cannot restart"), VBTutorial::Start(*Definition, Progress));
	TestTrue(TEXT("completed has no HUD hint"), VBTutorial::GetHint(*Definition, Progress) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialConditionProgressTest, "VB.Unit.Tutorial.ConditionProgress",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialConditionProgressTest::RunTest(const FString& Parameters)
{
	if (!TutorialTagsReady(*this))
	{
		return false;
	}

	FTutorialFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	UVBTutorialDefinition* Definition = MakeDefinition();
	Definition->Steps = { MakeConditionStep(TutorialTag(StepFirst)), MakeConditionStep(TutorialTag(StepSecond)), MakeEventStep(TutorialTag(StepThird)) };
	FVBTutorialProgress Progress;
	VBTutorial::Start(*Definition, Progress);
	TestFalse(TEXT("false condition waits"), VBTutorial::RefreshConditions(*Definition, F.Context(), Progress));
	F.Story->SetFact(TutorialTag(ReadyFact), true);
	FVBRuleContext Missing = F.Context();
	Missing.Story = nullptr;
	TestFalse(TEXT("no missing-context completion"), VBTutorial::RefreshConditions(*Definition, Missing, Progress));
	TestTrue(TEXT("true state advances consecutive condition steps"), VBTutorial::RefreshConditions(*Definition, F.Context(), Progress));
	TestTrue(TEXT("state evaluation stops at event step"), Progress.CurrentStepId == TutorialTag(StepThird) && !Progress.bCompleted);
	TestEqual(TEXT("all reached steps are available for review"), VBTutorial::BuildReviewStepIndices(*Definition, Progress).Num(), 3);
	TestFalse(TEXT("repeat refresh is idempotent"), VBTutorial::RefreshConditions(*Definition, F.Context(), Progress));
	Definition->Steps[2] = MakeConditionStep(TutorialTag(StepThird));
	TestTrue(TEXT("bounded final state step completes"), VBTutorial::RefreshConditions(*Definition, F.Context(), Progress));
	TestTrue(TEXT("all-state sequence completes"), Progress.bCompleted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialIdentityTest, "VB.Unit.Tutorial.IdentityAndReview",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialIdentityTest::RunTest(const FString& Parameters)
{
	if (!TutorialTagsReady(*this))
	{
		return false;
	}

	UVBTutorialDefinition* Definition = MakeDefinition();
	FVBTutorialProgress Progress;
	VBTutorial::Start(*Definition, Progress);
	TestTrue(TEXT("primary asset identity"),
		Definition->GetPrimaryAssetId() == UVBTutorialDefinition::MakeTutorialAssetId(TutorialTag(TutorialA)));
	TestEqual(TEXT("future step is hidden"), VBTutorial::BuildReviewStepIndices(*Definition, Progress).Num(), 1);
	Definition->Steps.Swap(0, 1);
	const FVBTutorialStep* Hint = VBTutorial::GetHint(*Definition, Progress);
	TestTrue(TEXT("current step follows stable ID after reorder"), Hint && Hint->StepId == TutorialTag(StepFirst));
	const FVBTutorialProgress Before = Progress;
	VBTutorial::BuildReviewStepIndices(*Definition, Progress);
	TestTrue(TEXT("review cannot mutate progress"), Progress.CurrentStepId == Before.CurrentStepId
		&& Progress.EventCount == Before.EventCount);
	Progress.CurrentStepId = TutorialTag(StepUnknown);
	TestFalse(TEXT("unknown saved step is not guessed"), VBTutorial::IsProgressCompatible(*Definition, Progress));
	TestTrue(TEXT("unknown progress is not exposed"), VBTutorial::GetHint(*Definition, Progress) == nullptr);
	Progress = Before;
	Progress.EventCount = -1;
	TestFalse(TEXT("negative restored count rejected"), VBTutorial::IsProgressCompatible(*Definition, Progress));
	Progress = Before;
	Progress.EventCount = 2;
	TestFalse(TEXT("already-full active count requires restore handling"), VBTutorial::IsProgressCompatible(*Definition, Progress));
	Progress = Before;
	Progress.UnlockedSteps.Add(TutorialTag(StepUnknown));
	TestFalse(TEXT("unknown unlocked step preserved as unresolved, not exposed"), VBTutorial::IsProgressCompatible(*Definition, Progress));
	Progress = Before;
	const FGameplayTag Copy = Progress.UnlockedSteps[0];
	Progress.UnlockedSteps.Add(Copy);
	TestFalse(TEXT("duplicate unlocked IDs rejected"), VBTutorial::IsProgressCompatible(*Definition, Progress));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialHintOrderTest, "VB.Unit.Tutorial.HintOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialHintOrderTest::RunTest(const FString& Parameters)
{
	if (!TutorialTagsReady(*this))
	{
		return false;
	}

	UVBTutorialDefinition* A = MakeDefinition(TutorialTag(TutorialA));
	UVBTutorialDefinition* B = MakeDefinition(TutorialTag(TutorialB));
	FVBTutorialProgress PA, PB;
	VBTutorial::Start(*A, PA);
	VBTutorial::Start(*B, PB);
	const TArray<const UVBTutorialDefinition*> Definitions = { nullptr, B, A, B };
	const TArray<FVBTutorialProgress> Entries = { PB, PA };
	TestTrue(TEXT("lexical ID breaks complete tie"), VBTutorial::SelectHint(Definitions, Entries) == A);
	B->SortOrder = -1;
	TestTrue(TEXT("sort order beats ID"), VBTutorial::SelectHint(Definitions, Entries) == B);
	A->HintPriority = 1;
	TestTrue(TEXT("higher priority beats sort order"), VBTutorial::SelectHint(Definitions, Entries) == A);
	PA.bCompleted = true;
	TestTrue(TEXT("completed entries excluded"), VBTutorial::SelectHint(Definitions, { PA, PB }) == B);
	TestTrue(TEXT("empty entries yield no hint"), VBTutorial::SelectHint(Definitions, {}) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialDefinitionTest, "VB.Unit.Tutorial.DefinitionValidation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialDefinitionTest::RunTest(const FString& Parameters)
{
	if (!TutorialTagsReady(*this))
	{
		return false;
	}

	UVBTutorialDefinition* Definition = MakeDefinition();
	TestTrue(TEXT("valid definition usable"), Definition->IsUsable());
	TestTrue(TEXT("fact refresh signal known"), UVBTutorialDefinition::HasRefreshSignal(FVBCondition_Fact::StaticStruct()));
	TestTrue(TEXT("counter refresh signal known"), UVBTutorialDefinition::HasRefreshSignal(FVBCondition_Counter::StaticStruct()));
	TestTrue(TEXT("quest refresh signal known"), UVBTutorialDefinition::HasRefreshSignal(FVBCondition_QuestState::StaticStruct()));
	TestTrue(TEXT("cinematic refresh signal known"),
		UVBTutorialDefinition::HasRefreshSignal(FVBCondition_CinematicCompleted::StaticStruct()));
	TestFalse(TEXT("base condition has no known signal"), UVBTutorialDefinition::HasRefreshSignal(FVBCondition::StaticStruct()));
	Definition->Steps[1].StepId = TutorialTag(StepFirst);
	TestFalse(TEXT("duplicate stable IDs rejected"), Definition->IsUsable());
	Definition->Steps[1].StepId = TutorialTag(StepSecond);
	Definition->Steps[0].Completion = FInstancedStruct::Make(FVBQuestObjective());
	FVBTutorialProgress Progress;
	TestFalse(TEXT("base goal does not start"), VBTutorial::Start(*Definition, Progress));
	Definition->Steps[0] = MakeEventStep(TutorialTag(StepFirst), 0);
	TestFalse(TEXT("invalid authored count does not silently clamp"), Definition->IsUsable());
	Definition->Steps[0] = MakeEventStep(TutorialTag(StepFirst));
	Definition->Steps[0].Completion.GetMutable<FVBQuestObjective_Event>().bOptional = true;
	TestFalse(TEXT("optional goal rejected"), Definition->IsUsable());
	Definition->Steps[0] = MakeEventStep(TutorialTag(StepFirst));
	Definition->Steps[0].Completion.GetMutable<FVBQuestObjective_Event>().MarkerTag = TutorialTag(TargetA);
	TestFalse(TEXT("quest marker rejected"), Definition->IsUsable());
	Definition->Steps[0] = MakeConditionStep(TutorialTag(StepFirst));
	Definition->Steps[0].Completion.GetMutable<FVBQuestObjective_Condition>().Conditions.Reset();
	TestFalse(TEXT("empty condition is not automatic completion"), Definition->IsUsable());
	Definition->Steps[0] = MakeEventStep(TutorialTag(StepFirst));
	Definition->ActivationConditions.Add(FInstancedStruct::Make(FVBCondition()));
	TestFalse(TEXT("base activation condition rejected"), Definition->IsUsable());
	Definition->ActivationConditions.Reset();
	FVBCondition_Fact InvalidFact;
	InvalidFact.bNegate = true;
	Definition->ActivationConditions.Add(FInstancedStruct::Make(InvalidFact));
	TestFalse(TEXT("empty negated fact cannot auto-activate"), Definition->IsUsable());
	Definition->ActivationConditions.Reset();
#if WITH_EDITOR
	FDataValidationContext ValidContext;
	TestTrue(TEXT("valid asset passes editor validation"), Definition->IsDataValid(ValidContext) == EDataValidationResult::Valid);
	FVBCondition_ItemCount Unsignaled;
	Unsignaled.Item = NewObject<UVBItemDefinition>();
	Definition->ActivationConditions.Add(FInstancedStruct::Make(Unsignaled));
	FDataValidationContext UnsignaledContext;
	TestTrue(TEXT("unsupported refresh condition remains usable with warning"),
		Definition->IsDataValid(UnsignaledContext) == EDataValidationResult::Valid);
	TestEqual(TEXT("unsupported condition emits one authoring warning"), UnsignaledContext.GetNumWarnings(), uint32(1));
	Definition->ActivationConditions.Reset();
	Definition->Steps[0].Body = FText::GetEmpty();
	FDataValidationContext InvalidContext;
	TestTrue(TEXT("missing player copy fails editor validation"),
		Definition->IsDataValid(InvalidContext) == EDataValidationResult::Invalid);
	Definition->Steps[0].Body = INVTEXT("Body");
	Definition->Steps[0].DeviceBodies.Add(ECommonInputType::Gamepad, FText::GetEmpty());
	FDataValidationContext EmptyDeviceBodyContext;
	TestTrue(TEXT("empty device text fails editor validation"),
		Definition->IsDataValid(EmptyDeviceBodyContext) == EDataValidationResult::Invalid);
	Definition->Steps[0].DeviceBodies.Add(ECommonInputType::Gamepad, INVTEXT("  "));
	FDataValidationContext BlankDeviceBodyContext;
	TestTrue(TEXT("whitespace device text fails editor validation"),
		Definition->IsDataValid(BlankDeviceBodyContext) == EDataValidationResult::Invalid);
	Definition->Steps[0].DeviceBodies.Reset();
	// 상호작용 완료는 EventTags 가 있는 대상만 낸다 - 대상을 이름 짓지 않은 단계는 영영 끝나지 않으므로 검증이 막는다.
	const FGameplayTag DoorTag = FGameplayTag::RequestGameplayTag(TEXT("Interaction.Door"), false);
	if (TestTrue(TEXT("Interaction.Door registered"), DoorTag.IsValid()))
	{
		FVBQuestObjective_Event Interaction;
		Interaction.EventTag = FGameplayTag::RequestGameplayTag(TEXT("Event.Interaction.Completed"));
		Definition->Steps[0].Completion = FInstancedStruct::Make(Interaction);
		FDataValidationContext UntaggedContext;
		TestTrue(TEXT("interaction completion without a target query fails"),
			Definition->IsDataValid(UntaggedContext) == EDataValidationResult::Invalid);
		Interaction.TargetQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(DoorTag));
		Definition->Steps[0].Completion = FInstancedStruct::Make(Interaction);
		FDataValidationContext TaggedContext;
		TestTrue(TEXT("interaction completion naming its targets passes"),
			Definition->IsDataValid(TaggedContext) == EDataValidationResult::Valid);
	}
#endif
	return true;
}

// 실린 안내 정의가 모두 검증을 통과한다 - 저작 실수(끝날 수 없는 단계 등)가 PIE 까지 가지 않게.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialShippedDefinitionsTest, "VB.Unit.Tutorial.ShippedDefinitionsValid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialShippedDefinitionsTest::RunTest(const FString& Parameters)
{
#if WITH_EDITOR
	UAssetManager& Manager = UAssetManager::Get();
	TArray<FPrimaryAssetId> Ids;
	Manager.GetPrimaryAssetIdList(UVBTutorialDefinition::GetTutorialAssetType(), Ids);
	TestTrue(TEXT("tutorial definitions are registered"), Ids.Num() > 0);
	for (const FPrimaryAssetId& Id : Ids)
	{
		const UVBTutorialDefinition* Definition = Cast<UVBTutorialDefinition>(Manager.GetPrimaryAssetPath(Id).TryLoad());
		if (!TestNotNull(*Id.ToString(), Definition))
		{
			continue;
		}

		FDataValidationContext Context;
		TestTrue(*FString::Printf(TEXT("%s passes validation"), *Id.ToString()),
			Definition->IsDataValid(Context) != EDataValidationResult::Invalid);
	}
#endif
	return true;
}

// 단계는 플레이어의 현재 장치 항목이 있으면 그것을, 없는 장치에는 Body 를 보인다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialDeviceBodyTest, "VB.Unit.Tutorial.DeviceBody",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialDeviceBodyTest::RunTest(const FString& Parameters)
{
	FVBTutorialStep Step;
	Step.Body = INVTEXT("WASD");
	Step.DeviceBodies.Add(ECommonInputType::Gamepad, INVTEXT("Left stick"));
	TestEqual(TEXT("gamepad uses its entry"), VBTutorial::BodyFor(Step, ECommonInputType::Gamepad).ToString(), FString(TEXT("Left stick")));
	TestEqual(TEXT("keyboard falls back to Body"), VBTutorial::BodyFor(Step, ECommonInputType::MouseAndKeyboard).ToString(),
		FString(TEXT("WASD")));
	TestEqual(TEXT("touch falls back to Body"), VBTutorial::BodyFor(Step, ECommonInputType::Touch).ToString(), FString(TEXT("WASD")));
	TestEqual(TEXT("unknown device shows Body, not a guessed device"), VBTutorial::BodyFor(Step, {}).ToString(), FString(TEXT("WASD")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialSaveBoundaryTest, "VB.Unit.Tutorial.SaveBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialSaveBoundaryTest::RunTest(const FString& Parameters)
{
	if (!TutorialTagsReady(*this))
	{
		return false;
	}

	FTutorialFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	UVBTutorialComponent* Tutorial = NewObject<UVBTutorialComponent>(F.Player);
	F.Player->AddInstanceComponent(Tutorial);
	Tutorial->RegisterComponent();
	UActorComponent* Base = Tutorial;
	if (!Base->HasBeenInitialized())
	{
		Base->InitializeComponent();
	}

	UVBSaveParticipantSubsystem* Registry = F.World->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (!TestNotNull(TEXT("save registry"), Registry))
	{
		return false;
	}

	TestTrue(TEXT("registered before BeginPlay"), Registry->GetParticipants().Contains(static_cast<IVBSaveParticipant*>(Tutorial)));
	TestFalse(TEXT("fixture has not begun play"), Tutorial->HasBegunPlay());
	TestFalse(TEXT("catalog alone is not an initial progress snapshot"), Tutorial->IsReady());
	UClass* TutorialClass = Tutorial->GetClass();
	// 보통은 RepLayout 이 이 callback 을 부르기 전에 RepIndex 를 정한다
	TutorialClass->SetUpRuntimeReplicationData();
	TArray<FLifetimeProperty> Replication;
	Tutorial->GetLifetimeReplicatedProps(Replication);
	for (const FName PropertyName : { FName(TEXT("Progress")), FName(TEXT("bProgressReady")) })
	{
		const FProperty* ReflectedProperty = FindFProperty<FProperty>(TutorialClass, PropertyName);
		if (!TestNotNull(*FString::Printf(TEXT("reflection: %s"), *PropertyName.ToString()), ReflectedProperty))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("owner-only: %s"), *PropertyName.ToString()),
			Replication.ContainsByPredicate([ReflectedProperty](const FLifetimeProperty& Property)
		{
			return Property.RepIndex == ReflectedProperty->RepIndex && Property.Condition == COND_OwnerOnly;
		}));
	}

	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	FVBSavedTutorial Raw;
	Raw.TutorialId = TutorialTag(MissingTutorial);
	Raw.CurrentStepId = TutorialTag(StepUnknown);
	Raw.EventCount = 7;
	Raw.UnlockedSteps = { TutorialTag(StepUnknown) };
	Save->Tutorials.Entries.Add(Raw);
	FVBSaveContext OtherContext;
	OtherContext.World = F.World;
	OtherContext.PlayerState = F.OtherPlayer;
	Tutorial->ReadSave(*Save, OtherContext);
	TestTrue(TEXT("another player's restore is ignored"), Tutorial->GetProgress().IsEmpty());
	Tutorial->WriteSave(*Save, OtherContext);
	TestEqual(TEXT("another player's save section is untouched"), Save->Tutorials.Entries.Num(), 1);
	FVBSaveContext Context = OtherContext;
	Context.PlayerState = F.Player;
	if (Tutorial->IsCatalogReady())
	{
		AddExpectedMessagePlain(TEXT("Preserving unresolved tutorial save"), ELogVerbosity::Warning,
			EAutomationExpectedMessageFlags::Contains, 1);
	}

	Tutorial->ReadSave(*Save, Context);
	TestTrue(TEXT("restore never queues a completion popup"), Tutorial->TakeCompletionNotices().IsEmpty());
	UVBSaveGame* Again = NewObject<UVBSaveGame>();
	Tutorial->WriteSave(*Again, Context);
	TestEqual(TEXT("unresolved entry survives write-back"), Again->Tutorials.Entries.Num(), 1);
	if (Again->Tutorials.Entries.Num() == 1)
	{
		const FVBSavedTutorial& Kept = Again->Tutorials.Entries[0];
		TestTrue(TEXT("unresolved IDs survive exactly"), Kept.TutorialId == TutorialTag(MissingTutorial)
			&& Kept.CurrentStepId == TutorialTag(StepUnknown));
		TestEqual(TEXT("unresolved count is not reinterpreted"), Kept.EventCount, 7);
		TestTrue(TEXT("unresolved review allowance survives"), Kept.UnlockedSteps == Raw.UnlockedSteps);
	}

	UVBSaveGame* EmptySlot = NewObject<UVBSaveGame>();
	Tutorial->ReadSave(*EmptySlot, Context);
	Tutorial->WriteSave(*Again, Context);
	TestTrue(TEXT("old or different empty slot has no previous progress"), Again->Tutorials.Entries.IsEmpty());
	Base->UninitializeComponent();
	TestFalse(TEXT("uninitialize unregisters save participant"),
		Registry->GetParticipants().Contains(static_cast<IVBSaveParticipant*>(Tutorial)));
	return true;
}
// 안내는 BeginPlay 에서 사실 저장소를 한 번 묶는다(평가 경로에서 다시 묶지 않는다). GameState 가 바뀌면 새 저장소를 듣고, 끝나면 놓는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialStorySourceTest, "VB.Unit.Tutorial.StorySource",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBTutorialStorySourceTest::RunTest(const FString& Parameters)
{
	FTutorialFixture F;
	if (!TestTrue(TEXT("isolated world"), F.IsValid()))
	{
		return false;
	}

	UVBTutorialComponent* Tutorial = NewObject<UVBTutorialComponent>(F.Player);
	F.Player->AddInstanceComponent(Tutorial);
	Tutorial->RegisterComponent();
	UActorComponent* Base = Tutorial;
	if (!Base->HasBeenInitialized())
	{
		Base->InitializeComponent();
	}

	F.Player->DispatchBeginPlay();
	if (!TestTrue(TEXT("BeginPlay returns through RefreshConditions -> Evaluate -> BindSources"), Tutorial->HasBegunPlay()))
	{
		return false;
	}

	TestTrue(TEXT("listens to the story present at BeginPlay"), F.Story->OnStoryStateChanged.IsBoundToObject(Tutorial));
	Tutorial->RefreshConditions();
	// 중복 구독은 묶음 자체의 시험(StoryBinding.Lifecycle)이 센다.
	TestTrue(TEXT("a later evaluation keeps the story subscription"), F.Story->OnStoryStateChanged.IsBoundToObject(Tutorial));

	AGameStateBase* Replacement = F.World->SpawnActor<AGameStateBase>();
	UVBStoryStateComponent* NewStory = NewObject<UVBStoryStateComponent>(Replacement);
	Replacement->AddInstanceComponent(NewStory);
	NewStory->RegisterComponent();
	F.World->SetGameState(Replacement);
	TestTrue(TEXT("replaced GameState: listens to the new story"), NewStory->OnStoryStateChanged.IsBoundToObject(Tutorial));
	TestFalse(TEXT("replaced GameState: the old story is released"), F.Story->OnStoryStateChanged.IsBoundToObject(Tutorial));

	// 객체는 살려 둔 채 끝낸다 - 파괴하면 약참조가 비어 놓은 것과 구별이 안 된다.
	F.Player->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	TestFalse(TEXT("EndPlay releases the current story"), NewStory->OnStoryStateChanged.IsBoundToObject(Tutorial));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialControlledMovementTest, "VB.Unit.Tutorial.Movement.Acceptance",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTutorialControlledMovementTest::RunTest(const FString& Parameters)
{
	const FVector Intent(300, 0, 0);
	TestTrue(TEXT("forward controlled displacement counts"), VBTutorial::IsControlledMovement(FVector(5, 0, 0), Intent, false));
	TestTrue(TEXT("backward and strafe displacement counts"), VBTutorial::IsControlledMovement(FVector(-2, 3, 0), Intent, false));
	TestFalse(TEXT("input into a wall does not count"), VBTutorial::IsControlledMovement(FVector::ZeroVector, Intent, false));
	TestFalse(TEXT("passive displacement does not count"), VBTutorial::IsControlledMovement(FVector(5, 0, 0), FVector::ZeroVector, false));
	TestFalse(TEXT("vertical falling does not count"), VBTutorial::IsControlledMovement(FVector(0, 0, -5), Intent, false));
	TestFalse(TEXT("blocked cinematic input does not count"), VBTutorial::IsControlledMovement(FVector(5, 0, 0), Intent, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialMovementAdvanceTest, "VB.Unit.Tutorial.Movement.AdvancesOnce",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBTutorialMovementAdvanceTest::RunTest(const FString& Parameters)
{
	FTutorialFixture F;
	if (!F.IsValid() || !TutorialTagsReady(*this))
	{
		return false;
	}

	UVBTutorialDefinition* Definition = MakeDefinition();
	FVBQuestObjective_Event Goal;
	Goal.EventTag = VBTutorial::MovementEventTag();
	Definition->Steps[0].Completion = FInstancedStruct::Make(Goal);
	FVBTutorialProgress Progress;
	TestFalse(TEXT("unstarted tutorial has no movement observer"), VBTutorial::NeedsMovementObservation(*Definition, Progress));
	TestTrue(TEXT("start movement tutorial"), VBTutorial::Start(*Definition, Progress));
	TestTrue(TEXT("movement step requests the observer"), VBTutorial::NeedsMovementObservation(*Definition, Progress));
	FVBGameEvent Event;
	Event.EventTag = VBTutorial::MovementEventTag();
	Event.Instigator = F.OtherPlayer;
	TestFalse(TEXT("another player cannot finish movement"), VBTutorial::ApplyEvent(*Definition, Event, F.Context(), Progress));
	Event.Instigator = F.Player;
	TestTrue(TEXT("one owned movement immediately advances"), VBTutorial::ApplyEvent(*Definition, Event, F.Context(), Progress));
	TestEqual(TEXT("next step is shown"), Progress.CurrentStepId, TutorialTag(StepSecond));
	TestFalse(TEXT("next step releases movement observation"), VBTutorial::NeedsMovementObservation(*Definition, Progress));
	TestFalse(TEXT("held movement cannot advance the next unrelated step"), VBTutorial::ApplyEvent(*Definition, Event, F.Context(), Progress));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBTutorialMovementAssetTest, "VB.Unit.Tutorial.Movement.Authored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FVBTutorialMovementAssetTest::RunTest(const FString& Parameters)
{
	const UVBTutorialDefinition* Definition = LoadObject<UVBTutorialDefinition>(nullptr,
		TEXT("/Game/VB/Narrative/Tutorial/DA_Tutorial_Prologue_Basics.DA_Tutorial_Prologue_Basics"));
	if (!TestNotNull(TEXT("shipped basic tutorial"), Definition))
	{
		return false;
	}

	const int32 Index = Definition->FindStepIndex(TutorialTag(TEXT("Tutorial.Step.Prologue.Move")));
	if (!TestTrue(TEXT("movement step exists"), Definition->Steps.IsValidIndex(Index)))
	{
		return false;
	}

	const FVBQuestObjective_Event* Goal = Definition->Steps[Index].Completion.GetPtr<FVBQuestObjective_Event>();
	if (!TestNotNull(TEXT("movement waits for action instead of doorway fact"), Goal))
	{
		return false;
	}

	TestEqual(TEXT("movement event is authored"), Goal->EventTag, FGameplayTag(VBTutorial::MovementEventTag()));
	TestEqual(TEXT("one movement is enough"), Goal->RequiredCount, 1);
	return true;
}
#endif

}
#endif
