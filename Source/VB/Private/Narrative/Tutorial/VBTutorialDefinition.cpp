// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Tutorial/VBTutorialDefinition.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Narrative/Cinematic/VBCinematicRules.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestRules.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	bool IsConditionUsable(const FInstancedStruct& Condition)
	{
		const UScriptStruct* Type = Condition.GetScriptStruct();
		if (!Type || Type == FVBCondition::StaticStruct() || !Type->IsChildOf(FVBCondition::StaticStruct()))
		{
			return false;
		}

		if (Type == FVBCondition_Fact::StaticStruct())
		{
			return Condition.Get<FVBCondition_Fact>().Fact.IsValid();
		}

		if (Type == FVBCondition_Counter::StaticStruct())
		{
			const FVBCondition_Counter& Counter = Condition.Get<FVBCondition_Counter>();
			return Counter.Counter.IsValid() && (Counter.Compare == EVBRuleCompare::AtLeast
				|| Counter.Compare == EVBRuleCompare::AtMost || Counter.Compare == EVBRuleCompare::Equal);
		}

		if (Type == FVBCondition_QuestState::StaticStruct())
		{
			const FVBCondition_QuestState& Quest = Condition.Get<FVBCondition_QuestState>();
			return Quest.Quest != nullptr && Quest.Stage >= -1 && (Quest.Status == EVBQuestStatus::NotStarted
				|| Quest.Status == EVBQuestStatus::Active || Quest.Status == EVBQuestStatus::Completed);
		}

		if (Type == FVBCondition_CinematicCompleted::StaticStruct())
		{
			return Condition.Get<FVBCondition_CinematicCompleted>().Definition != nullptr;
		}

		return true;
	}

	bool AreConditionsUsable(const TArray<FInstancedStruct>& Conditions)
	{
		for (const FInstancedStruct& Condition : Conditions)
		{
			if (!IsConditionUsable(Condition))
			{
				return false;
			}
		}

		return true;
	}

	bool IsObjectiveUsable(const FInstancedStruct& Completion)
	{
		const FVBQuestObjective* Objective = Completion.GetPtr<FVBQuestObjective>();
		if (!Objective || Completion.GetScriptStruct() == FVBQuestObjective::StaticStruct()
			|| Objective->bOptional || Objective->MarkerTag.IsValid())
		{
			return false;
		}

		if (const FVBQuestObjective_Event* Event = Completion.GetPtr<FVBQuestObjective_Event>())
		{
			return Event->EventTag.IsValid() && Event->RequiredCount > 0;
		}

		if (const FVBQuestObjective_Condition* Condition = Completion.GetPtr<FVBQuestObjective_Condition>())
		{
			return !Condition->Conditions.IsEmpty() && AreConditionsUsable(Condition->Conditions);
		}

		return Objective->GetRequiredCount() > 0;
	}
}

FPrimaryAssetType UVBTutorialDefinition::GetTutorialAssetType()
{
	static const FPrimaryAssetType Type(TEXT("VBTutorial"));
	return Type;
}

FPrimaryAssetId UVBTutorialDefinition::MakeTutorialAssetId(FGameplayTag InTutorialId)
{
	return FPrimaryAssetId(GetTutorialAssetType(), InTutorialId.GetTagName());
}

FPrimaryAssetId UVBTutorialDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(GetTutorialAssetType(), TutorialId.IsValid() ? TutorialId.GetTagName() : GetFName());
}

bool UVBTutorialDefinition::IsUsable() const
{
	if (!TutorialId.MatchesTag(VBGameplayTags::Tutorial) || Steps.IsEmpty() || !AreConditionsUsable(ActivationConditions)
		|| (!ActivationEvent.IsValid() && !ActivationTargetQuery.IsEmpty()))
	{
		return false;
	}

	TSet<FGameplayTag> StepIds;
	for (const FVBTutorialStep& Step : Steps)
	{
		if (!Step.StepId.MatchesTag(VBGameplayTags::Tutorial) || StepIds.Contains(Step.StepId) || !IsObjectiveUsable(Step.Completion))
		{
			return false;
		}

		StepIds.Add(Step.StepId);
	}

	return true;
}

int32 UVBTutorialDefinition::FindStepIndex(FGameplayTag StepId) const
{
	return StepId.IsValid() ? Steps.IndexOfByPredicate([StepId](const FVBTutorialStep& Step)
	{
		return Step.StepId == StepId;
	}) : INDEX_NONE;
}

bool UVBTutorialDefinition::HasRefreshSignal(const UScriptStruct* ConditionType)
{
	// 파생 조건은 부모의 신호가 덮지 않는 상태를 더 읽을 수 있다.
	return ConditionType == FVBCondition_Fact::StaticStruct()
		|| ConditionType == FVBCondition_Counter::StaticStruct()
		|| ConditionType == FVBCondition_QuestState::StaticStruct()
		|| ConditionType == FVBCondition_CinematicCompleted::StaticStruct();
}

#if WITH_EDITOR
namespace
{
	// 오류를 남기고 검사 결과를 실패로 바꾼다.
	void Fail(FDataValidationContext& Context, EDataValidationResult& Result, const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	}
}

EDataValidationResult UVBTutorialDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	ValidateIdentity(Context, Result);
	ValidateConditions(ActivationConditions, INVTEXT("Activation"), Context, Result);
	if (!ActivationEvent.IsValid() && !ActivationTargetQuery.IsEmpty())
	{
		Fail(Context, Result,
			INVTEXT("ActivationTargetQuery requires an ActivationEvent; a condition-only activation cannot consume a target query."));
	}

	TSet<FGameplayTag> StepIds;
	for (int32 Index = 0; Index < Steps.Num(); ++Index)
	{
		ValidateStep(Steps[Index], Index, StepIds, Context, Result);
	}

	if (!IsUsable() && Result != EDataValidationResult::Invalid)
	{
		Fail(Context, Result, INVTEXT("Tutorial structure is unusable at runtime."));
	}

	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}

void UVBTutorialDefinition::ValidateIdentity(FDataValidationContext& Context, EDataValidationResult& Result) const
{
	if (!TutorialId.MatchesTag(VBGameplayTags::Tutorial))
	{
		Fail(Context, Result, INVTEXT("TutorialId must be a valid Tutorial tag."));
	}

	if (Title.IsEmpty() || Summary.IsEmpty())
	{
		Fail(Context, Result, INVTEXT("Tutorial Title and Summary must not be empty."));
	}

	if (Steps.IsEmpty())
	{
		Fail(Context, Result, INVTEXT("A tutorial must contain at least one step."));
	}
}

void UVBTutorialDefinition::ValidateConditions(const TArray<FInstancedStruct>& Conditions, const FText& Where,
	FDataValidationContext& Context, EDataValidationResult& Result) const
{
	if (!VBRules::ValidateStructArray(Conditions, FVBCondition::StaticStruct(), Where, Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	for (int32 Index = 0; Index < Conditions.Num(); ++Index)
	{
		const UScriptStruct* Type = Conditions[Index].GetScriptStruct();
		if (Type == FVBCondition::StaticStruct())
		{
			Fail(Context, Result, FText::Format(INVTEXT("{0}: condition {1} must not use the base condition."), Where, Index));
		}
		else if (Type && Type->IsChildOf(FVBCondition::StaticStruct()))
		{
			if (!IsConditionUsable(Conditions[Index]))
			{
				Fail(Context, Result, FText::Format(INVTEXT("{0}: condition {1} ({2}) has invalid required fields."),
					Where, Index, FText::FromName(Type->GetFName())));
			}
			else if (!HasRefreshSignal(Type))
			{
				Context.AddWarning(FText::Format(INVTEXT("{0}: condition {1} ({2}) has no dedicated refresh signal; its own state change does not trigger evaluation - it is checked only when another signal fires (game events, fact/quest/cinematic changes, save application, BeginPlay)."),
					Where, Index, FText::FromName(Type->GetFName())));
			}
		}
	}
}

void UVBTutorialDefinition::ValidateStep(const FVBTutorialStep& Step, int32 Index, TSet<FGameplayTag>& StepIds,
	FDataValidationContext& Context, EDataValidationResult& Result) const
{
	const FText Where = FText::Format(INVTEXT("Step {0}"), Index);
	if (!Step.StepId.MatchesTag(VBGameplayTags::Tutorial) || StepIds.Contains(Step.StepId))
	{
		Fail(Context, Result, FText::Format(INVTEXT("{0}: StepId must be a valid Tutorial tag and unique within the tutorial."), Where));
	}

	StepIds.Add(Step.StepId);
	if (Step.Title.IsEmpty() || Step.Body.IsEmpty())
	{
		Fail(Context, Result, FText::Format(INVTEXT("{0}: Title and Body must not be empty."), Where));
	}

	for (const TPair<ECommonInputType, FText>& DeviceBody : Step.DeviceBodies)
	{
		if (DeviceBody.Key == ECommonInputType::Count || DeviceBody.Value.IsEmptyOrWhitespace())
		{
			Fail(Context, Result, FText::Format(
				INVTEXT("{0}: DeviceBodies entry {1} needs a real device and text - remove the entry to fall back to Body."),
				Where, UEnum::GetDisplayValueAsText(DeviceBody.Key)));
		}
	}

	ValidateStepCompletion(Step, Where, Context, Result);
}

void UVBTutorialDefinition::ValidateStepCompletion(const FVBTutorialStep& Step, const FText& Where,
	FDataValidationContext& Context, EDataValidationResult& Result) const
{
	const FVBQuestObjective* Objective = Step.Completion.GetPtr<FVBQuestObjective>();
	if (!Objective || Step.Completion.GetScriptStruct() == FVBQuestObjective::StaticStruct())
	{
		Fail(Context, Result, FText::Format(INVTEXT("{0}: Completion must contain a non-base quest objective."), Where));
		return;
	}

	if (Objective->bOptional || Objective->MarkerTag.IsValid())
	{
		Fail(Context, Result, FText::Format(INVTEXT("{0}: optional objectives and quest markers are not supported."), Where));
	}

	if (Objective->IsStateBased() && !Step.Completion.GetPtr<FVBQuestObjective_Condition>())
	{
		Context.AddWarning(FText::Format(INVTEXT("{0}: state-based completion ({1}) has no dedicated refresh signal; use a condition objective or provide a refresh integration."),
			Where, FText::FromName(Step.Completion.GetScriptStruct()->GetFName())));
	}

	if (const FVBQuestObjective_Event* Event = Step.Completion.GetPtr<FVBQuestObjective_Event>())
	{
		if (const FString Problem = Event->ValidateSetup(); !Problem.IsEmpty())
		{
			Fail(Context, Result, FText::Format(INVTEXT("{0}: {1}"), Where, FText::FromString(Problem)));
		}

		if (Event->RequiredCount <= 0)
		{
			Fail(Context, Result, FText::Format(INVTEXT("{0}: event completion needs a positive RequiredCount."), Where));
		}
	}
	else if (const FVBQuestObjective_Condition* Condition = Step.Completion.GetPtr<FVBQuestObjective_Condition>())
	{
		if (Condition->Conditions.IsEmpty())
		{
			Fail(Context, Result, FText::Format(INVTEXT("{0}: condition completion must contain at least one condition."), Where));
		}

		ValidateConditions(Condition->Conditions, Where, Context, Result);
	}
	else if (Objective->GetRequiredCount() <= 0)
	{
		Fail(Context, Result, FText::Format(INVTEXT("{0}: Completion requires a positive count."), Where));
	}
}
#endif
