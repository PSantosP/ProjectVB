// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Dialogue/VBDialogueAsset.h"

#include "Narrative/Dialogue/VBSpeakerDefinition.h"
#include "Narrative/Story/VBRuleTypes.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBDialogueAsset"

FName UVBDialogueAsset::ChooseEntry(const FVBRuleContext& Context) const
{
	for (const FVBDialogueEntryPoint& Entry : EntryPoints)
	{
		if (VBRules::EvaluateAll(Entry.Conditions, Context))
		{
			return Entry.StartNodeId;
		}
	}

	return NAME_None;
}

const FVBDialogueNode* UVBDialogueAsset::FindNode(FName NodeId) const
{
	if (NodeId.IsNone())
	{
		return nullptr;
	}

	return Nodes.FindByPredicate([NodeId](const FVBDialogueNode& Node) { return Node.NodeId == NodeId; });
}

const UVBSpeakerDefinition* UVBDialogueAsset::FindSpeaker(FGameplayTag SpeakerId) const
{
	for (const UVBSpeakerDefinition* Speaker : Speakers)
	{
		if (Speaker && Speaker->SpeakerId == SpeakerId)
		{
			return Speaker;
		}
	}

	return nullptr;
}

#if WITH_EDITOR
namespace
{
	void FailDialogueValidation(FDataValidationContext& Context, EDataValidationResult& Result, const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	}

	void ForEachDialogueNext(const FVBDialogueNode& Node, TFunctionRef<void(FName)> Visit)
	{
		if (Node.Choices.IsEmpty())
		{
			Visit(Node.NextNodeId);
			return;
		}

		for (const FVBDialogueChoice& Choice : Node.Choices)
		{
			Visit(Choice.NextNodeId);
		}
	}

	void CollectDialogueNodeIds(const UVBDialogueAsset& Dialogue, FDataValidationContext& Context,
		EDataValidationResult& Result, TSet<FName>& NodeIds)
	{
		if (Dialogue.EntryPoints.IsEmpty())
		{
			FailDialogueValidation(Context, Result, LOCTEXT("NoEntry", "진입점이 없다 - 이 대화는 시작할 수 없다."));
		}

		for (int32 Index = 0; Index < Dialogue.Speakers.Num(); ++Index)
		{
			if (!Dialogue.Speakers[Index])
			{
				FailDialogueValidation(Context, Result, FText::Format(LOCTEXT("NullSpeaker", "화자 {0} 번이 비었다."), Index));
			}
		}

		for (const FVBDialogueNode& Node : Dialogue.Nodes)
		{
			if (Node.NodeId.IsNone())
			{
				FailDialogueValidation(Context, Result, LOCTEXT("NoNodeId", "NodeId 가 빈 노드가 있다 - 어디서도 가리킬 수 없다."));
			}
			else if (NodeIds.Contains(Node.NodeId))
			{
				FailDialogueValidation(Context, Result,
					FText::Format(LOCTEXT("DupNode", "노드 {0} 이 두 번 있다 - 연결이 앞의 것만 찾는다."), FText::FromName(Node.NodeId)));
			}

			NodeIds.Add(Node.NodeId);
		}

	}

	void ValidateDialogueLinks(const UVBDialogueAsset& Dialogue, const TSet<FName>& NodeIds,
		FDataValidationContext& Context, EDataValidationResult& Result)
	{
		auto CheckLink = [&](FName Target, const FText& Where)
		{
			if (!Target.IsNone() && !NodeIds.Contains(Target))
			{
				FailDialogueValidation(Context, Result,
					FText::Format(LOCTEXT("BadLink", "{0}: 없는 노드 {1} 를 가리킨다."), Where, FText::FromName(Target)));
			}
		};

		for (int32 Index = 0; Index < Dialogue.EntryPoints.Num(); ++Index)
		{
			const FText Where = FText::Format(LOCTEXT("EntryWhere", "진입점 {0}"), Index);
			if (Dialogue.EntryPoints[Index].StartNodeId.IsNone())
			{
				FailDialogueValidation(Context, Result, FText::Format(LOCTEXT("EmptyEntry", "{0}: 시작 노드가 비었다."), Where));
			}

			CheckLink(Dialogue.EntryPoints[Index].StartNodeId, Where);
			if (!VBRules::ValidateStructArray(Dialogue.EntryPoints[Index].Conditions, FVBCondition::StaticStruct(), Where, Context))
			{
				Result = EDataValidationResult::Invalid;
			}
		}

		for (const FVBDialogueNode& Node : Dialogue.Nodes)
		{
			const FText Where = FText::Format(LOCTEXT("NodeWhere", "노드 {0}"), FText::FromName(Node.NodeId));
			if (Node.Speaker.IsValid() && !Dialogue.FindSpeaker(Node.Speaker))
			{
				FailDialogueValidation(Context, Result,
					FText::Format(LOCTEXT("UnknownSpeaker", "{0}: 화자 {1} 가 Speakers 목록에 없다 - 이름이 비어 보인다."), Where,
						FText::FromName(Node.Speaker.GetTagName())));
			}

			if (!Node.Choices.IsEmpty() && !Node.NextNodeId.IsNone())
			{
				Context.AddWarning(FText::Format(LOCTEXT("IgnoredNext", "{0}: 선택지가 있어 NextNodeId({1})는 쓰이지 않는다."), Where,
					FText::FromName(Node.NextNodeId)));
			}

			CheckLink(Node.NextNodeId, Where);
			if (!VBRules::ValidateStructArray(Node.OnEnterEffects, FVBEffect::StaticStruct(), Where, Context))
			{
				Result = EDataValidationResult::Invalid;
			}

			for (int32 ChoiceIndex = 0; ChoiceIndex < Node.Choices.Num(); ++ChoiceIndex)
			{
				const FVBDialogueChoice& Choice = Node.Choices[ChoiceIndex];
				const FText ChoiceWhere = FText::Format(LOCTEXT("ChoiceWhere", "{0} 선택지 {1}"), Where, ChoiceIndex);
				CheckLink(Choice.NextNodeId, ChoiceWhere);
				if (!VBRules::ValidateStructArray(Choice.Conditions, FVBCondition::StaticStruct(), ChoiceWhere, Context)
					|| !VBRules::ValidateStructArray(Choice.Effects, FVBEffect::StaticStruct(), ChoiceWhere, Context))
				{
					Result = EDataValidationResult::Invalid;
				}
			}
		}

	}

	TSet<FName> FindReachableDialogueNodes(const UVBDialogueAsset& Dialogue, FDataValidationContext& Context)
	{
		TSet<FName> Reached;
		TArray<FName> Frontier;
		for (const FVBDialogueEntryPoint& Entry : Dialogue.EntryPoints)
		{
			Frontier.Add(Entry.StartNodeId);
		}

		while (!Frontier.IsEmpty())
		{
			const FName Current = Frontier.Pop(EAllowShrinking::No);
			if (Current.IsNone() || Reached.Contains(Current))
			{
				continue;
			}

			Reached.Add(Current);
			if (const FVBDialogueNode* Node = Dialogue.FindNode(Current))
			{
				ForEachDialogueNext(*Node, [&Frontier](FName Next) { Frontier.Add(Next); });
			}
		}

		for (const FVBDialogueNode& Node : Dialogue.Nodes)
		{
			if (!Node.NodeId.IsNone() && !Reached.Contains(Node.NodeId))
			{
				Context.AddWarning(FText::Format(LOCTEXT("Unreached", "노드 {0} 에는 어느 진입점에서도 닿지 않는다."), FText::FromName(Node.NodeId)));
			}
		}

		return Reached;
	}

	void ValidateDialogueEndPaths(const UVBDialogueAsset& Dialogue, const TSet<FName>& Reached,
		FDataValidationContext& Context, EDataValidationResult& Result)
	{
		TSet<FName> CanEnd;
		for (bool bGrew = true; bGrew;)
		{
			bGrew = false;
			for (const FVBDialogueNode& Node : Dialogue.Nodes)
			{
				if (Node.NodeId.IsNone() || CanEnd.Contains(Node.NodeId))
				{
					continue;
				}

				bool bEnds = false;
				ForEachDialogueNext(Node, [&bEnds, &CanEnd](FName Next) { bEnds |= Next.IsNone() || CanEnd.Contains(Next); });
				if (bEnds)
				{
					CanEnd.Add(Node.NodeId);
					bGrew = true;
				}
			}
		}

		for (const FVBDialogueNode& Node : Dialogue.Nodes)
		{
			if (Reached.Contains(Node.NodeId) && !CanEnd.Contains(Node.NodeId))
			{
				FailDialogueValidation(Context, Result,
					FText::Format(LOCTEXT("NoEnd", "노드 {0} 에서는 끝으로 가는 길이 없다 - 고리에 갇힌다."), FText::FromName(Node.NodeId)));
			}
		}
	}
}

EDataValidationResult UVBDialogueAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	TSet<FName> NodeIds;
	CollectDialogueNodeIds(*this, Context, Result, NodeIds);
	ValidateDialogueLinks(*this, NodeIds, Context, Result);
	const TSet<FName> Reached = FindReachableDialogueNodes(*this, Context);
	ValidateDialogueEndPaths(*this, Reached, Context, Result);
	return CombineDataValidationResults(Result, EDataValidationResult::Valid);
}
#endif

#undef LOCTEXT_NAMESPACE
