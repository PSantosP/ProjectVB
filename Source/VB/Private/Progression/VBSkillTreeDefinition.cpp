// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Progression/VBSkillTreeDefinition.h"

#include "Abilities/GameplayAbility.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif
const FVBSkillNode* UVBSkillTreeDefinition::FindNode(const FGameplayTag& NodeId) const
{
	return NodeId.IsValid() ? Nodes.FindByPredicate([&NodeId](const FVBSkillNode& Node) { return Node.NodeId == NodeId; }) : nullptr;
}

TArray<FText> UVBSkillTreeDefinition::FindProblems(const UVBSkillTreeDefinition& Tree)
{
	TArray<FText> Problems;
	TSet<FGameplayTag> Seen;
	for (const FVBSkillNode& Node : Tree.Nodes)
	{
		const FText Name = FText::FromString(Node.NodeId.ToString());
		if (!Node.NodeId.IsValid())
		{
			Problems.Add(INVTEXT("NodeId \uac00 \ube48 \uce78\uc774 \uc788\ub2e4 - \uc800\uc7a5\u00b7\uc120\ud589 \uc870\uac74\uc774 \ubd80\ub97c \uc218 \uc5c6\ub2e4."));
			continue;
		}

		if (Seen.Contains(Node.NodeId))
		{
			Problems.Add(FText::Format(INVTEXT("{0}: \uac19\uc740 NodeId \uac00 \ub458\uc774\ub2e4."), Name));
		}

		Seen.Add(Node.NodeId);
		if (Node.Cost < 1)
		{
			Problems.Add(FText::Format(INVTEXT("{0}: Cost \ub294 1 \uc774\uc0c1\uc774\uc5b4\uc57c \ud55c\ub2e4(\ud3ec\uc778\ud2b8\ub97c \uc548 \uc4f0\ub294 \uce78\uc740 \ub0a8\uc740 \ud3ec\uc778\ud2b8 \uc148\uc744 \ud750\ub9b0\ub2e4)."),
				Name));
		}

		if (Node.RequiredLevel < 1)
		{
			Problems.Add(FText::Format(INVTEXT("{0}: RequiredLevel \uc740 1 \uc774\uc0c1\uc774\uc5b4\uc57c \ud55c\ub2e4."), Name));
		}

		if (Node.Stats.IsEmpty() && !Node.GrantedAbility)
		{
			Problems.Add(FText::Format(INVTEXT("{0}: \ub2a5\ub825\uce58\ub3c4 \ub2a5\ub825\ub3c4 \uc5c6\ub2e4 - \uc5f4\uc5b4\ub3c4 \uc544\ubb34 \uc77c\uc774 \uc5c6\ub2e4."),
				Name));
		}

		for (const FGameplayTag& Prerequisite : Node.Prerequisites)
		{
			if (!Tree.FindNode(Prerequisite))
			{
				Problems.Add(FText::Format(INVTEXT("{0}: \uc120\ud589 {1} \uc774 \uc774 \ud2b8\ub9ac\uc5d0 \uc5c6\ub2e4 - \uc601\uc601 \ubabb \uc5f0\ub2e4."),
					Name, FText::FromString(Prerequisite.ToString())));
			}
		}
	}

	// 선행 고리: 칸마다 선행을 따라가 자기에게 돌아오면 고리다(칸 수가 작아 깊이 우선 한 번씩이면 충분하다).
	enum class EVisit : uint8 { None, Active, Done };
	TMap<FGameplayTag, EVisit> Visit;
	TFunction<bool(const FVBSkillNode&)> HasCycle = [&](const FVBSkillNode& Node) -> bool
	{
		EVisit& State = Visit.FindOrAdd(Node.NodeId, EVisit::None);
		if (State == EVisit::Active)
		{
			return true;
		}

		if (State == EVisit::Done)
		{
			return false;
		}

		State = EVisit::Active;
		for (const FGameplayTag& Prerequisite : Node.Prerequisites)
		{
			const FVBSkillNode* Next = Tree.FindNode(Prerequisite);
			if (Next && HasCycle(*Next))
			{
				return true;
			}
		}

		Visit.FindOrAdd(Node.NodeId) = EVisit::Done;
		return false;
	};
	for (const FVBSkillNode& Node : Tree.Nodes)
	{
		if (Node.NodeId.IsValid() && HasCycle(Node))
		{
			Problems.Add(FText::Format(INVTEXT("{0}: \uc120\ud589 \uc870\uac74\uc774 \uace0\ub9ac\ub97c \uc774\ub8ec\ub2e4 - \uace0\ub9ac \uc548\uc758 \uce78\uc740 \uc601\uc601 \ubabb \uc5f0\ub2e4."),
				FText::FromString(Node.NodeId.ToString())));
			break;
		}
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBSkillTreeDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	for (const FText& Problem : FindProblems(*this))
	{
		Context.AddError(Problem);
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif
