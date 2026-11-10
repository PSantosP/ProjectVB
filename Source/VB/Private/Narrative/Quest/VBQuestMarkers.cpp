// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Quest/VBQuestMarkers.h"

#include "Narrative/Quest/VBQuestDefinition.h"

namespace VBQuestMarkers
{
	void CollectActiveMarkerTags(const UVBQuestDefinition* Quest, int32 StageIndex, TConstArrayView<int32> Progress,
		FGameplayTagContainer& Out)
	{
		if (!Quest || !Quest->Stages.IsValidIndex(StageIndex))
		{
			return;
		}

		const FVBQuestStage& Stage = Quest->Stages[StageIndex];
		for (int32 Index = 0; Index < Stage.Objectives.Num(); ++Index)
		{
			const FVBQuestObjective* Objective = Stage.Objectives[Index].GetPtr<FVBQuestObjective>();
			if (!Objective || !Objective->MarkerTag.IsValid())
			{
				continue;
			}

			const int32 Done = Progress.IsValidIndex(Index) ? Progress[Index] : 0;
			if (Done < Objective->GetRequiredCount())
			{
				Out.AddTag(Objective->MarkerTag);
			}
		}
	}
}
