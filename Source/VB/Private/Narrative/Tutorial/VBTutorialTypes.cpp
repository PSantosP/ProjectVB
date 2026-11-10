// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Tutorial/VBTutorialTypes.h"

#include "NativeGameplayTags.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Tutorial/VBTutorialDefinition.h"

namespace
{
	UE_DEFINE_GAMEPLAY_TAG_STATIC(Event_Tutorial_Moved, "Event.Tutorial.Moved");

	bool HasContext(const FVBRuleContext& Context)
	{
		return Context.World && Context.Instigator && Context.Story;
	}

	bool BelongsToPlayer(const FVBGameEvent& Event, const FVBRuleContext& Context)
	{
		return Context.Instigator && (!Event.Instigator || Event.Instigator == Context.Instigator);
	}

	void AdvanceStep(const UVBTutorialDefinition& Definition, int32 CurrentIndex, FVBTutorialProgress& Progress)
	{
		Progress.EventCount = 0;
		if (Definition.Steps.IsValidIndex(CurrentIndex + 1))
		{
			Progress.CurrentStepId = Definition.Steps[CurrentIndex + 1].StepId;
			Progress.UnlockedSteps.AddUnique(Progress.CurrentStepId);
		}
		else
		{
			Progress.bCompleted = true;
		}
	}
}

FGameplayTag VBTutorial::MovementEventTag()
{
	return Event_Tutorial_Moved;
}

bool VBTutorial::IsControlledMovement(const FVector& Displacement, const FVector& Acceleration, bool bInputBlocked)
{
	return !bInputBlocked && !Displacement.ContainsNaN() && !Acceleration.ContainsNaN()
		&& Displacement.SizeSquared2D() > UE_SMALL_NUMBER && Acceleration.SizeSquared2D() > UE_SMALL_NUMBER;
}

bool VBTutorial::NeedsMovementObservation(const UVBTutorialDefinition& Definition, const FVBTutorialProgress& Progress)
{
	const FVBTutorialStep* Step = GetHint(Definition, Progress);
	const FVBQuestObjective_Event* Goal = Step ? Step->Completion.GetPtr<FVBQuestObjective_Event>() : nullptr;
	return Goal && Goal->EventTag == VBTutorial::MovementEventTag();
}

bool VBTutorial::CanActivate(const UVBTutorialDefinition& Definition, const FVBGameEvent* Event,
	const FVBRuleContext& Context)
{
	if (!Definition.IsUsable() || !HasContext(Context))
	{
		return false;
	}

	if (Definition.ActivationEvent.IsValid())
	{
		if (!Event || !BelongsToPlayer(*Event, Context))
		{
			return false;
		}

		FVBQuestObjective_Event Matcher;
		Matcher.EventTag = Definition.ActivationEvent;
		Matcher.TargetQuery = Definition.ActivationTargetQuery;
		if (Matcher.CountEvent(*Event) == 0)
		{
			return false;
		}
	}
	else if (Definition.ActivationConditions.IsEmpty())
	{
		return false;
	}

	return VBRules::EvaluateAll(Definition.ActivationConditions, Context);
}

bool VBTutorial::Start(const UVBTutorialDefinition& Definition, FVBTutorialProgress& Progress)
{
	if (!Definition.IsUsable() || Progress.TutorialId.IsValid())
	{
		return false;
	}

	Progress = FVBTutorialProgress();
	Progress.TutorialId = Definition.TutorialId;
	Progress.CurrentStepId = Definition.Steps[0].StepId;
	Progress.UnlockedSteps.Add(Progress.CurrentStepId);
	return true;
}

bool VBTutorial::IsProgressCompatible(const UVBTutorialDefinition& Definition, const FVBTutorialProgress& Progress)
{
	if (!Definition.IsUsable() || Progress.TutorialId != Definition.TutorialId || Progress.EventCount < 0)
	{
		return false;
	}

	const int32 Index = Definition.FindStepIndex(Progress.CurrentStepId);
	if (Index == INDEX_NONE || !Progress.UnlockedSteps.Contains(Progress.CurrentStepId))
	{
		return false;
	}

	if (Progress.UnlockedSteps.Num() > Definition.Steps.Num())
	{
		return false;
	}

	TSet<FGameplayTag> Seen;
	for (FGameplayTag StepId : Progress.UnlockedSteps)
	{
		if (Definition.FindStepIndex(StepId) == INDEX_NONE || Seen.Contains(StepId))
		{
			return false;
		}

		Seen.Add(StepId);
	}

	const FVBQuestObjective* Goal = Definition.Steps[Index].Completion.GetPtr<FVBQuestObjective>();
	if (Progress.bCompleted)
	{
		return Progress.EventCount == 0;
	}

	return Goal->IsStateBased() ? Progress.EventCount == 0 : Progress.EventCount < Goal->GetRequiredCount();
}

bool VBTutorial::ApplyEvent(const UVBTutorialDefinition& Definition, const FVBGameEvent& Event,
	const FVBRuleContext& Context, FVBTutorialProgress& Progress)
{
	if (Progress.bCompleted || !HasContext(Context) || !BelongsToPlayer(Event, Context)
		|| !IsProgressCompatible(Definition, Progress)) { return false; }
	const int32 Index = Definition.FindStepIndex(Progress.CurrentStepId);
	const FVBQuestObjective* Goal = Definition.Steps[Index].Completion.GetPtr<FVBQuestObjective>();
	if (Goal->IsStateBased())
	{
		return RefreshConditions(Definition, Context, Progress);
	}

	const int32 Added = Goal->CountEvent(Event);
	if (Added <= 0)
	{
		return RefreshConditions(Definition, Context, Progress);
	}

	const int32 Required = Goal->GetRequiredCount();
	Progress.EventCount += FMath::Min(Required - Progress.EventCount, Added);
	if (Progress.EventCount == Required)
	{
		AdvanceStep(Definition, Index, Progress);
		// 사건을 다음 단계에 다시 넣지 않는다. 다음 단계는 현재 상태로만 넘어간다
		RefreshConditions(Definition, Context, Progress);
	}

	return true;
}

bool VBTutorial::RefreshConditions(const UVBTutorialDefinition& Definition, const FVBRuleContext& Context,
	FVBTutorialProgress& Progress)
{
	if (Progress.bCompleted || !HasContext(Context) || !IsProgressCompatible(Definition, Progress))
	{
		return false;
	}

	bool bChanged = false;
	for (int32 Remaining = Definition.Steps.Num(); Remaining > 0 && !Progress.bCompleted; --Remaining)
	{
		const int32 Index = Definition.FindStepIndex(Progress.CurrentStepId);
		const FVBQuestObjective* Goal = Definition.Steps[Index].Completion.GetPtr<FVBQuestObjective>();
		if (!Goal->IsStateBased() || !Goal->Evaluate(Context))
		{
			break;
		}

		AdvanceStep(Definition, Index, Progress);
		bChanged = true;
	}

	return bChanged;
}

const FText& VBTutorial::BodyFor(const FVBTutorialStep& Step, TOptional<ECommonInputType> InputType)
{
	const FText* DeviceBody = InputType ? Step.DeviceBodies.Find(*InputType) : nullptr;
	return DeviceBody ? *DeviceBody : Step.Body;
}

const FVBTutorialStep* VBTutorial::GetHint(const UVBTutorialDefinition& Definition, const FVBTutorialProgress& Progress)
{
	return !Progress.bCompleted && IsProgressCompatible(Definition, Progress)
		? &Definition.Steps[Definition.FindStepIndex(Progress.CurrentStepId)] : nullptr;
}

const UVBTutorialDefinition* VBTutorial::SelectHint(const TArray<const UVBTutorialDefinition*>& Definitions,
	const TArray<FVBTutorialProgress>& Progress)
{
	const UVBTutorialDefinition* Best = nullptr;
	for (const UVBTutorialDefinition* Definition : Definitions)
	{
		if (!Definition)
		{
			continue;
		}

		const FVBTutorialProgress* Entry = Progress.FindByPredicate([Definition](const FVBTutorialProgress& Value)
		{
			return Value.TutorialId == Definition->TutorialId;
		});
		if (!Entry || !GetHint(*Definition, *Entry))
		{
			continue;
		}

		if (!Best || Definition->HintPriority > Best->HintPriority
			|| (Definition->HintPriority == Best->HintPriority && (Definition->SortOrder < Best->SortOrder
				|| (Definition->SortOrder == Best->SortOrder
					&& Definition->TutorialId.GetTagName().LexicalLess(Best->TutorialId.GetTagName())))))
		{
			Best = Definition;
		}
	}

	return Best;
}

TArray<int32> VBTutorial::BuildReviewStepIndices(const UVBTutorialDefinition& Definition,
	const FVBTutorialProgress& Progress)
{
	TArray<int32> Indices;
	if (!IsProgressCompatible(Definition, Progress))
	{
		return Indices;
	}

	for (int32 Index = 0; Index < Definition.Steps.Num(); ++Index)
	{
		if (Progress.UnlockedSteps.Contains(Definition.Steps[Index].StepId))
		{
			Indices.Add(Index);
		}
	}

	return Indices;
}
