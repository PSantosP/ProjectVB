// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Quest/VBQuestDefinition.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Narrative/Dialogue/VBDialogueAsset.h"

#include "Narrative/Story/VBGameEventSubsystem.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBQuestDefinition"

int32 FVBQuestObjective_Event::CountEvent(const FVBGameEvent& Event) const
{
	if (!EventTag.IsValid() || !Event.EventTag.MatchesTag(EventTag))
	{
		return 0;
	}

	if (!TargetQuery.IsEmpty() && !TargetQuery.Matches(Event.TargetTags))
	{
		return 0;
	}

	return FMath::Max(1, Event.Count);
}

FString FVBQuestObjective_Event::ValidateSetup() const
{
	if (!EventTag.IsValid())
	{
		return TEXT("EventTag is empty - nothing is counted");
	}
	// 상호작용 완료는 EventTags 가 있는 대상만 낸다 - 빈 질의는 '아무 대상' 으로 읽히지만 EventTags 없는 대상은 영영 세지 않는다.
	if (EventTag == VBGameplayTags::Event_Interaction_Completed.GetTag() && TargetQuery.IsEmpty())
	{
		return TEXT("Event.Interaction.Completed needs a TargetQuery on the targets' EventTags (Interaction.*) - untagged targets send no event");
	}

	return FString();
}

bool FVBQuestObjective_Condition::Evaluate(const FVBRuleContext& Context) const
{
	// 빈 배열은 VBRules 에서 참이다 - 조건 없는 조건 목표는 저작 실수라 검증기가 오류로 잡는다(IsDataValid).
	return VBRules::EvaluateAll(Conditions, Context);
}

FPrimaryAssetType UVBQuestDefinition::GetQuestAssetType()
{
	// 함수 안 정적 변수 - FName 은 모듈 정적 초기화 때 아직 쓸 수 없을 수 있다(UVBItemDefinition 과 같은 이유).
	static const FPrimaryAssetType Type(TEXT("VBQuest"));
	return Type;
}

FPrimaryAssetId UVBQuestDefinition::MakeQuestAssetId(FGameplayTag InQuestId)
{
	return FPrimaryAssetId(GetQuestAssetType(), InQuestId.GetTagName());
}

FPrimaryAssetId UVBQuestDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(GetQuestAssetType(), QuestId.IsValid() ? QuestId.GetTagName() : GetFName());
}

#if WITH_EDITOR
namespace
{
	// 이미 찬 목표의 확인 저작 검사. 확인할 길이 없는 저작은 단계를 영영 멈추게 하므로 저장 전에 잡는다.
	template <typename FailFn, typename WarnFn>
	void ValidateAlreadyMetAcknowledgement(const FVBQuestObjective& Objective, const FText& Where, FailFn&& Fail, WarnFn&& Warn)
	{
		const bool bHasDialogue = Objective.AlreadyMetDialogue != nullptr;
		if (bHasDialogue != Objective.AlreadyMetAckTag.IsValid())
		{
			Fail(FText::Format(LOCTEXT("AckHalf", "{0}: 확인 대화와 확인 태그는 함께 채운다 - 하나만 있으면 확인 없이 넘긴다."), Where));
			return;
		}

		if (!bHasDialogue)
		{
			return;
		}

		if (Objective.bOptional || !Objective.IsStateBased())
		{
			// 오류는 아니다 - 쓰이지 않을 뿐이다. 선택 목표는 단계를 붙잡지 않고, 사건 목표는 단계가 시작되는 순간에 찰 수 없다.
			Warn(FText::Format(LOCTEXT("AckUnused", "{0}: 확인 대화는 필수 «조건» 목표에서만 쓰인다 - 여기서는 무시된다."), Where));
		}

		bool bAckNodeFound = false;
		for (const FVBDialogueNode& Node : Objective.AlreadyMetDialogue->Nodes)
		{
			bAckNodeFound |= Node.ReachedTag == Objective.AlreadyMetAckTag;
		}

		if (!bAckNodeFound)
		{
			Fail(FText::Format(LOCTEXT("AckNoNode", "{0}: 확인 대화에 ReachedTag 가 확인 태그인 노드가 없다 - 확인이 영영 안 온다."), Where));
		}

		for (const FVBDialogueEntryPoint& Entry : Objective.AlreadyMetDialogue->EntryPoints)
		{
			const FVBDialogueNode* Start = Objective.AlreadyMetDialogue->FindNode(Entry.StartNodeId);
			if (Start && Start->ReachedTag == Objective.AlreadyMetAckTag)
			{
				Fail(FText::Format(LOCTEXT("AckOnStart", "{0}: 확인 노드가 시작 노드다 - 플레이어가 읽기도 전에 확인으로 친다."), Where));
			}
		}
	}
}

EDataValidationResult UVBQuestDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Fail = [&Result, &Context](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};

	if (!QuestId.IsValid())
	{
		Fail(LOCTEXT("NoQuestId", "QuestId 가 비었다 - 저장·조건·대화가 이 퀘스트를 부를 수 없다."));
	}

	if (Stages.IsEmpty())
	{
		Fail(LOCTEXT("NoStages", "단계가 없다 - 시작하자마자 끝난다."));
	}

	for (int32 StageIndex = 0; StageIndex < Stages.Num(); ++StageIndex)
	{
		const FVBQuestStage& Stage = Stages[StageIndex];
		const FText Where = FText::Format(LOCTEXT("StageWhere", "단계 {0}"), StageIndex);
		if (!VBRules::ValidateStructArray(Stage.Objectives, FVBQuestObjective::StaticStruct(), Where, Context)
			|| !VBRules::ValidateStructArray(Stage.OnCompleteEffects, FVBEffect::StaticStruct(), Where, Context))
		{
			Result = EDataValidationResult::Invalid;
			continue;
		}

		int32 Required = 0;
		for (int32 ObjectiveIndex = 0; ObjectiveIndex < Stage.Objectives.Num(); ++ObjectiveIndex)
		{
			const FVBQuestObjective& Objective = Stage.Objectives[ObjectiveIndex].Get<FVBQuestObjective>();
			Required += Objective.bOptional ? 0 : 1;
			const FText ObjectiveWhere = FText::Format(LOCTEXT("ObjectiveWhere", "{0} 목표 {1}"), Where, ObjectiveIndex);
			ValidateAlreadyMetAcknowledgement(Objective, ObjectiveWhere, Fail,
				[&Context](const FText& Message) { Context.AddWarning(Message); });
			if (const FVBQuestObjective_Event* EventObjective = Stage.Objectives[ObjectiveIndex].GetPtr<FVBQuestObjective_Event>())
			{
				if (const FString Problem = EventObjective->ValidateSetup(); !Problem.IsEmpty())
				{
					Fail(FText::Format(LOCTEXT("BadEventObjective", "{0}: {1}"), ObjectiveWhere, FText::FromString(Problem)));
				}
			}

			if (const FVBQuestObjective_Condition* ConditionObjective =
				Stage.Objectives[ObjectiveIndex].GetPtr<FVBQuestObjective_Condition>())
			{
				if (ConditionObjective->Conditions.IsEmpty())
				{
					Fail(FText::Format(LOCTEXT("NoConditions", "{0}: 조건이 없다 - 단계가 시작하자마자 찬다."), ObjectiveWhere));
				}
				else if (!VBRules::ValidateStructArray(ConditionObjective->Conditions, FVBCondition::StaticStruct(),
					ObjectiveWhere, Context))
				{
					Result = EDataValidationResult::Invalid;
				}
			}
		}

		if (Required == 0)
		{
			Fail(FText::Format(LOCTEXT("NoRequired", "{0}: 필수 목표가 없다 - 단계가 시작하자마자 넘어간다."), Where));
		}
	}

	if (!VBRules::ValidateStructArray(Rewards, FVBEffect::StaticStruct(), LOCTEXT("RewardsWhere", "보상"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}
	// 부모(UObject)는 NotValidated 를 돌려준다. 여기까지 오류가 없으면 '봤고 멀쩡하다' 로 올린다.
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
